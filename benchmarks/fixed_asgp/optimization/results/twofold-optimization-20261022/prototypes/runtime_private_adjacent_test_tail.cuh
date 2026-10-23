
bool run() {
  if(cudaDeviceSetLimit(cudaLimitStackSize,16384)!=cudaSuccess)return false;
  std::int64_t* cells=nullptr;Value* globals=nullptr;Result* output=nullptr;Op* operations=nullptr;
  if(cudaMalloc(reinterpret_cast<void**>(&cells),std::size_t(Lanes)*Arena*sizeof(std::int64_t))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&globals),std::size_t(Lanes)*Global*sizeof(Value))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&output),Lanes*sizeof(Result))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&operations),256*sizeof(Op))!=cudaSuccess)return false;
  std::vector<Value> input;
  for(int lane=0;lane<Lanes;++lane)for(int j=0;j<Global;++j)
    input.push_back(Value::from_int(sample(lane,1000+j)));
  if(cudaMemcpy(globals,input.data(),input.size()*sizeof(Value),cudaMemcpyHostToDevice)!=cudaSuccess)return false;
  std::vector<Result> actual(Lanes);
  int traces=0;
  for(int enabled=0;enabled<2;++enabled)for(int kind=0;kind<6;++kind)
    for(int n:{0,1,2,3,7,8,31,32,63,64}) {
      if(kind>=2 && kind<=4 && n!=3)continue;
      const auto ops=sequence(kind,n);
      if(cudaMemcpy(operations,ops.data(),ops.size()*sizeof(Op),cudaMemcpyHostToDevice)!=cudaSuccess)return false;
      for(int capacity:{0,1,2,3,4,7,8,16,64,Arena})for(unsigned limit:{0u,8u,65536u}) {
        if(cudaMemset(cells,0xA5,std::size_t(Lanes)*Arena*sizeof(std::int64_t))!=cudaSuccess)return false;
        exercise<true><<<Lanes/32,32>>>(operations,ops.size(),capacity,limit,globals,cells,output,Lanes,enabled);
        if(cudaGetLastError()!=cudaSuccess || cudaDeviceSynchronize()!=cudaSuccess ||
           cudaMemcpy(actual.data(),output,Lanes*sizeof(Result),cudaMemcpyDeviceToHost)!=cudaSuccess)return false;
        for(int lane=0;lane<Lanes;++lane) {
          ++traces;
          if(!equal(actual[lane],reference(ops,lane,capacity,limit,true)))return false;
        }
      }
    }
  // Positive witness: both append and recursive prepend physically store64
  // cells, retaining all immutable prefixes, while reserving/charging2143.
  // This asserts the mechanism rather than inferring it from timing.
  for(int kind:{0,1})for(int enabled=0;enabled<2;++enabled) {
    const auto ops=sequence(kind,64);
    if(cudaMemcpy(operations,ops.data(),ops.size()*sizeof(Op),cudaMemcpyHostToDevice)!=cudaSuccess)return false;
    exercise<true><<<Lanes/32,32>>>(operations,ops.size(),Arena,65536,globals,cells,output,Lanes,enabled);
    if(cudaGetLastError()!=cudaSuccess || cudaDeviceSynchronize()!=cudaSuccess ||
       cudaMemcpy(actual.data(),output,Lanes*sizeof(Result),cudaMemcpyDeviceToHost)!=cudaSuccess)return false;
    for(int lane=0;lane<Lanes;++lane)if(!equal(actual[lane],reference(ops,lane,Arena,65536,true)) ||
        actual[lane].reserved!=2143 || actual[lane].logical!=2143 ||
        actual[lane].physical!=(enabled?64:2143))return false;
  }
  if(cudaFree(cells)!=cudaSuccess || cudaFree(globals)!=cudaSuccess ||
     cudaFree(output)!=cudaSuccess || cudaFree(operations)!=cudaSuccess)return false;
  std::cout<<"PASS private-adjacent traces="<<traces
           <<"; CPU vectors; exact charge/overflow; physical64/reserved2143 append+prepend; immutable prefixes\n";
  return true;
}
