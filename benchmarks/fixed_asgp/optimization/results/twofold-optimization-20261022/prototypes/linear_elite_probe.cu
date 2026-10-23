#include "linear_elite.cuh"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

void checked(cudaError_t result) {if(result!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(result));}
__global__ void winners(const double* values,const int* offsets,const int* sizes,int* output) {
  const auto result=gagp::evo::repro::linear_elite_index(values+offsets[blockIdx.x],sizes[blockIdx.x]);
  if(threadIdx.x==0)output[blockIdx.x]=result;
}
int main() try {
  checked(cudaSetDevice(0));std::mt19937_64 random(0x718a852);
  std::vector<double> values;std::vector<int> offsets,sizes,expected;
  for(int count:{1,2,3,7,31,32,33,255,256,257,1023,1024,16383,16384})for(int kind=0;kind<7;++kind) {
    offsets.push_back(values.size());sizes.push_back(count);int best=0;
    for(int i=0;i<count;++i) {
      double value=kind==0?-std::numeric_limits<double>::infinity():kind==1?std::numeric_limits<double>::infinity():
          kind==2?(i%2?-0.:0.):kind==3?double(i):kind==4?-double(i):kind==5?double(random()%13):
          (i%3==0?std::numeric_limits<double>::max():i%3==1?-std::numeric_limits<double>::max():0.);
      values.push_back(value);
      if(i && value>values[offsets.back()+best])best=i;
    }
    expected.push_back(best);
  }
  double* input=nullptr;int *starts=nullptr,*counts=nullptr,*output=nullptr;
  checked(cudaMalloc(&input,values.size()*sizeof(double)));
  checked(cudaMalloc(&starts,offsets.size()*sizeof(int)));checked(cudaMalloc(&counts,sizes.size()*sizeof(int)));
  checked(cudaMalloc(&output,expected.size()*sizeof(int)));
  checked(cudaMemcpy(input,values.data(),values.size()*sizeof(double),cudaMemcpyHostToDevice));
  checked(cudaMemcpy(starts,offsets.data(),offsets.size()*sizeof(int),cudaMemcpyHostToDevice));
  checked(cudaMemcpy(counts,sizes.data(),sizes.size()*sizeof(int),cudaMemcpyHostToDevice));
  winners<<<expected.size(),256>>>(input,starts,counts,output);checked(cudaGetLastError());
  std::vector<int> actual(expected.size());checked(cudaMemcpy(actual.data(),output,actual.size()*sizeof(int),cudaMemcpyDeviceToHost));
  for(std::size_t i=0;i<actual.size();++i)if(actual[i]!=expected[i])throw std::runtime_error("GPU elite ordering differs from CPU oracle at case"+std::to_string(i));
  checked(cudaFree(output));checked(cudaFree(counts));checked(cudaFree(starts));checked(cudaFree(input));
  std::cout<<"GPU linear elite oracle PASS cases="<<actual.size()<<" max_population=16384\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
