
int main() {
  if(cudaSetDevice(0)!=cudaSuccess || cudaDeviceSetLimit(cudaLimitStackSize,16384)!=cudaSuccess)return 2;
  std::int64_t* cells=nullptr;Value* globals=nullptr;Result* output=nullptr;Op* operations=nullptr;
  if(cudaMalloc(reinterpret_cast<void**>(&cells),std::size_t(Lanes)*Arena*sizeof(std::int64_t))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&globals),std::size_t(Lanes)*Global*sizeof(Value))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&output),Lanes*sizeof(Result))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&operations),256*sizeof(Op))!=cudaSuccess)return 3;
  std::vector<Value> input;
  for(int lane=0;lane<Lanes;++lane)for(int j=0;j<Global;++j)input.push_back(Value::from_int(sample(lane,1000+j)));
  if(cudaMemcpy(globals,input.data(),input.size()*sizeof(Value),cudaMemcpyHostToDevice)!=cudaSuccess)return 4;
  int checked=0,initial_overflow[3]={},capacity64_append_overflow[3]={};
  std::vector<Result> first(Lanes),retry(Lanes);
  for(int mode=0;mode<3;++mode)for(int kind=0;kind<6;++kind)
    for(int n:{0,1,2,3,7,8,31,32,63,64}) {
      if(kind>=2 && kind<=4 && n!=3)continue;
      const auto ops=sequence(kind,n);
      if(cudaMemcpy(operations,ops.data(),ops.size()*sizeof(Op),cudaMemcpyHostToDevice)!=cudaSuccess)return 5;
      for(int capacity:{0,1,2,3,4,7,8,16,64,Arena})for(unsigned limit:{0u,8u,65536u}) {
        if(cudaMemset(cells,0xA5,std::size_t(Lanes)*Arena*sizeof(std::int64_t))!=cudaSuccess)return 6;
        exercise<true><<<Lanes/32,32>>>(operations,ops.size(),capacity,limit,globals,cells,output,Lanes,mode);
        if(cudaGetLastError()!=cudaSuccess || cudaDeviceSynchronize()!=cudaSuccess ||
           cudaMemcpy(first.data(),output,Lanes*sizeof(Result),cudaMemcpyDeviceToHost)!=cudaSuccess)return 7;
        bool any=false;for(const auto& value:first)any|=value.overflow!=0;
        if(any) {
          exercise<true><<<Lanes/32,32>>>(operations,ops.size(),Arena,limit,globals,cells,output,Lanes,mode);
          if(cudaGetLastError()!=cudaSuccess || cudaDeviceSynchronize()!=cudaSuccess ||
             cudaMemcpy(retry.data(),output,Lanes*sizeof(Result),cudaMemcpyDeviceToHost)!=cudaSuccess)return 8;
        }
        for(int lane=0;lane<Lanes;++lane) {
          const auto& observed=first[lane].overflow?retry[lane]:first[lane];
          const auto semantic=reference(ops,lane,Arena,limit,true);
          ++checked;initial_overflow[mode]+=first[lane].overflow!=0;
          if(first[lane].physical>capacity || !equal(observed,semantic)) {
            std::cerr<<"FAIL mode="<<mode<<" kind="<<kind<<" n="<<n<<" capacity="<<capacity
                     <<" limit="<<limit<<" lane="<<lane<<" physical="<<first[lane].physical
                     <<" hash="<<observed.hash<<"/"<<semantic.hash<<" ok="<<observed.ok<<"/"<<semantic.ok<<"\n";
            return 9;
          }
          if((mode<2 || limit==0) && !equal(first[lane],reference(ops,lane,capacity,limit,true)))return 10;
          if(kind==0 && n==64 && capacity==64 && limit==65536) {
            capacity64_append_overflow[mode]+=first[lane].overflow!=0;
            if(mode==2 && (!first[lane].ok || first[lane].reserved!=2143 ||
                first[lane].logical!=2143 || first[lane].physical!=64))return 11;
          }
        }
      }
    }
  if(capacity64_append_overflow[0]!=Lanes || capacity64_append_overflow[1]!=Lanes ||
     capacity64_append_overflow[2]!=0 || initial_overflow[2]>=initial_overflow[1])return 12;
  std::cout<<"PASS semantic_replay_traces="<<checked<<"; independent CPU vector truth after replay; physical cursor always within capacity; unlimited-budget decline; logical/error/immutable-prefix semantics preserved\n";
  std::cout<<"POSITIVE initial_overflows="<<initial_overflow[0]<<","<<initial_overflow[1]<<","<<initial_overflow[2]
           <<"; n64/cap64 append baseline+adjacent=128overflows each, packed=0, physical64/reserved2143; NOT a search-speed claim\n";
  cudaFree(cells);cudaFree(globals);cudaFree(output);cudaFree(operations);return 0;
}
