#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <vector>
#include "myers64.hpp"

using namespace gagp::myers_prototype;
struct Case {int offset,count,pattern,symbols,length,expected;};

int reference(const std::vector<std::int64_t>& a,const std::vector<std::int64_t>& b) {
  std::vector<unsigned> row(b.size()+1);
  for(unsigned j=0;j<row.size();++j)row[j]=j;
  for(unsigned i=1;i<=a.size();++i) {
    unsigned diagonal=row[0];row[0]=i;
    for(unsigned j=1;j<=b.size();++j) {
      const unsigned previous=row[j];
      row[j]=std::min({row[j]+1,row[j-1]+1,diagonal+unsigned(a[i-1]!=b[j-1])});
      diagonal=previous;
    }
  }
  return row.back();
}

__global__ void bit_kernel(const std::int64_t* text,const Entry* patterns,
                          const Case* cases,int count,int* result) {
  const int i=blockIdx.x*blockDim.x+threadIdx.x;
  if(i<count) {
    const auto c=cases[i];
    result[i]=distance64(Reader{text+c.offset},c.count,patterns+c.pattern,c.symbols,c.length);
  }
}

__global__ void row_kernel(const std::int64_t* text,const std::int64_t* answers,
                          const Case* cases,int count,int* result) {
  const int index=blockIdx.x*blockDim.x+threadIdx.x;
  if(index>=count)return;
  const auto c=cases[index];unsigned row[65];
  for(int j=0;j<=c.length;++j)row[j]=j;
  for(int i=1;i<=c.count;++i) {
    auto diagonal=row[0];row[0]=i;
    for(int j=1;j<=c.length;++j) {
      const auto old=row[j];
      const auto best=row[j]<row[j-1]?row[j]:row[j-1];
      const auto substitution=diagonal+(text[c.offset+i-1]!=answers[index*64+j-1]);
      row[j]=best+1<substitution?best+1:substitution;
      diagonal=old;
    }
  }
  result[index]=row[c.length];
}

void checked(cudaError_t status) {if(status!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(status));}
template<class T> T* upload(const std::vector<T>& values) {
  T* result=nullptr;checked(cudaMalloc(reinterpret_cast<void**>(&result),std::max<std::size_t>(1,values.size())*sizeof(T)));
  if(!values.empty())checked(cudaMemcpy(result,values.data(),values.size()*sizeof(T),cudaMemcpyHostToDevice));
  return result;
}

int main() {try {
  std::vector<std::int64_t> text,answers;
  std::vector<Entry> patterns;
  std::vector<Case> cases;
  auto add=[&](const std::vector<std::int64_t>& a,const std::vector<std::int64_t>& b) {
    if(b.size()>64)throw std::runtime_error("probe pattern exceeds one word");
    std::map<std::int64_t,std::uint64_t> mapping;
    for(unsigned i=0;i<b.size();++i)mapping[b[i]]|=std::uint64_t{1}<<i;
    std::vector<Entry> current;
    for(const auto& item:mapping)current.push_back({item.first,item.second});
    const auto expected=reference(a,b);
    const auto actual=distance64(Reader{a.data()},a.size(),current.data(),current.size(),b.size());
    if(actual!=expected)throw std::runtime_error("host bit-vector differs from independent row recurrence");
    cases.push_back({int(text.size()),int(a.size()),int(patterns.size()),int(current.size()),int(b.size()),expected});
    text.insert(text.end(),a.begin(),a.end());patterns.insert(patterns.end(),current.begin(),current.end());
    answers.insert(answers.end(),b.begin(),b.end());answers.resize(cases.size()*64,0);
  };
  std::vector<std::vector<std::int64_t>> binary;
  for(int length=0;length<=6;++length)for(int bits=0;bits<(1<<length);++bits) {
    std::vector<std::int64_t> value;
    for(int i=0;i<length;++i)value.push_back((bits>>i)&1);
    binary.push_back(std::move(value));
  }
  for(const auto& a:binary)for(const auto& b:binary)add(a,b);
  std::mt19937_64 random(2026102301);
  const std::vector<std::int64_t> special={0,1,-1,INT64_MIN,INT64_MAX,
      (INT64_C(1)<<53)-1,INT64_C(1)<<53,(INT64_C(1)<<53)+1,-(INT64_C(1)<<53)-1};
  for(int i=0;i<6000;++i) {
    const int m=i%65,n=i%9==0?0:i%9==1?1:i%9==2?63:i%9==3?64:i%9==4?65:random()%257;
    std::vector<std::int64_t> a(n),b(m);
    for(auto& value:a)value=i%2?special[random()%special.size()]:static_cast<std::int64_t>(random());
    for(auto& value:b)value=i%2?special[random()%special.size()]:static_cast<std::int64_t>(random());
    if(i%5==0 && n==m)b=a;
    add(a,b);
  }
  for(int m:{0,1,31,32,33,63,64})for(int n:{0,1,63,64,65,127,512,2048}) {
    add(std::vector<std::int64_t>(n,INT64_MIN),std::vector<std::int64_t>(m,INT64_MIN));
    add(std::vector<std::int64_t>(n,INT64_MAX),std::vector<std::int64_t>(m,INT64_MIN));
  }
  add({0,1,2,0},{1,2}); // Global distance2; substring distance0 is wrong here.
  if(distance64(Reader{nullptr},0,nullptr,0,65)!=-1)throw std::runtime_error("unsupported length did not decline");
  checked(cudaSetDevice(0));
  auto* dtext=upload(text);auto* dpatterns=upload(patterns);auto* dcases=upload(cases);auto* danswers=upload(answers);
  std::vector<int> results(cases.size(),-1);int* dresult=upload(results);
  for(int method=0;method<2;++method) {
    if(method==0)bit_kernel<<<(cases.size()+127)/128,128>>>(dtext,dpatterns,dcases,cases.size(),dresult);
    else row_kernel<<<(cases.size()+127)/128,128>>>(dtext,danswers,dcases,cases.size(),dresult);
    checked(cudaGetLastError());checked(cudaDeviceSynchronize());
    checked(cudaMemcpy(results.data(),dresult,results.size()*sizeof(int),cudaMemcpyDeviceToHost));
    for(unsigned i=0;i<cases.size();++i)if(results[i]!=cases[i].expected)throw std::runtime_error("GPU distance mismatch method="+std::to_string(method)+" case="+std::to_string(i));
  }
  cudaEvent_t start,finish;checked(cudaEventCreate(&start));checked(cudaEventCreate(&finish));
  float milliseconds[2]{};
  for(int method=0;method<2;++method) {
    checked(cudaEventRecord(start));
    for(int repeat=0;repeat<20;++repeat) {
      if(method==0)bit_kernel<<<(cases.size()+127)/128,128>>>(dtext,dpatterns,dcases,cases.size(),dresult);
      else row_kernel<<<(cases.size()+127)/128,128>>>(dtext,danswers,dcases,cases.size(),dresult);
      checked(cudaGetLastError());
    }
    checked(cudaEventRecord(finish));checked(cudaEventSynchronize(finish));
    checked(cudaEventElapsedTime(&milliseconds[method],start,finish));
  }
  checked(cudaEventDestroy(start));checked(cudaEventDestroy(finish));
  std::cout<<"DIAGNOSTIC synthetic mixed-length kernels x20: bit_ms="<<milliseconds[0]
      <<" row_ms="<<milliseconds[1]<<" kernel_ratio="<<milliseconds[1]/milliseconds[0]
      <<"; excludes pattern preparation/transfer/VM/search, not task speedup\n";
  checked(cudaFree(dtext));checked(cudaFree(dpatterns));checked(cudaFree(dcases));checked(cudaFree(danswers));checked(cudaFree(dresult));
  std::cout<<"PASS cases="<<cases.size()<<"; exhaustive binary0..6, random fullInt64, global boundary, pattern0..64, text0..2048; host/GPU bit-vector and independent host/GPU row recurrence\n";
  return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
