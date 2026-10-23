"""Interleave private compact IntList cells within each physical CUDA block."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[6]


def apply(dry_run=False):
    changed = {}

    def edit(path, old, new, count=1):
        text = changed.get(path, (ROOT / path).read_text())
        assert text.count(old) == count, (path, old[:110], text.count(old))
        changed[path] = text.replace(old, new)

    path = 'cpp/include/gagp/runtime/gpu/region_types_gpu.hpp'
    edit(path, '  bool compact_intlist_cells = false;\n',
         '  bool compact_intlist_cells = false;\n  bool private_cell_soa = false;\n  int private_cell_stride = 1;\n')
    path = 'cpp/src/runtime/gpu/device/builtins_device.cuh'
    edit(path, '  static constexpr const std::int64_t* intlist_cells = nullptr;',
         '  static constexpr const std::int64_t* intlist_cells = nullptr;\n  static constexpr int private_cell_stride = 1;')
    edit(path, '  std::int64_t* intlist_cells = nullptr;',
         '  std::int64_t* intlist_cells = nullptr;\n  int private_cell_stride = 1;')
    edit(path, '  int size=0;\n  template<bool Compact>',
         '  int size=0;\n  int stride=1;\n  template<bool Compact>')
    edit(path, 'if constexpr (Compact)return integers[index];',
         'if constexpr (Compact)return integers[static_cast<std::size_t>(index)*stride];')
    edit(path, '    span.integers=state.intlist_cells?state.intlist_cells+offset:nullptr;',
         '    span.stride=state.private_cell_stride;\n    span.integers=state.intlist_cells?state.intlist_cells+static_cast<std::size_t>(offset)*span.stride:nullptr;')
    edit(path, '    bool cooperative=false) {\n#if defined(__CUDA_ARCH__)',
         '    bool cooperative=false,int target_stride=1) {\n#if defined(__CUDA_ARCH__)')
    edit(path, '  if(cooperative) {\n    // Opportunistic membership only:',
         '  if(cooperative && target_stride==1 && source.stride==1) {\n    // Opportunistic membership only:')
    edit(path, 'if(source.integers)for(int i=0;i<source.size;++i)target[i]=source.read<true>(i);\n  else for(int i=0;i<source.size;++i)target[i]=source.read<false>(i);',
         'if(source.integers)for(int i=0;i<source.size;++i)target[static_cast<std::size_t>(i)*target_stride]=source.read<true>(i);\n  else for(int i=0;i<source.size;++i)target[static_cast<std::size_t>(i)*target_stride]=source.read<false>(i);')
    edit(path, 'if(st.compact_intlist_cells)st.intlist_cells[offset]=args[0].i;',
         'if(st.compact_intlist_cells)st.intlist_cells[static_cast<std::size_t>(offset)*st.private_cell_stride]=args[0].i;')
    edit(path, '        d_copy_private_intlist(st.intlist_cells+offset,a,st.private_warp_copy);\n        d_copy_private_intlist(st.intlist_cells+offset+n,b,st.private_warp_copy);',
         '        d_copy_private_intlist(st.intlist_cells+static_cast<std::size_t>(offset)*st.private_cell_stride,a,st.private_warp_copy,st.private_cell_stride);\n        d_copy_private_intlist(st.intlist_cells+static_cast<std::size_t>(offset+n)*st.private_cell_stride,b,st.private_warp_copy,st.private_cell_stride);')
    path = 'cpp/src/runtime/gpu/device/kernels.cuh'
    edit(path, '    if(workspace.payload_intlist_cells)workspace.payload_intlist_cells += slot * workspace.payload_value_capacity;',
         '''    if(workspace.payload_intlist_cells) {
      // Same number of cells and capacity per lane. Consecutive lanes own
      // different columns, so element i is coalesced across the physical block.
      // Program scheduling/replay never changes a block's scratch ownership.
      if(workspace.private_cell_soa) {
        workspace.payload_intlist_cells += static_cast<std::size_t>(blockIdx.x)*blockDim.x*
            workspace.payload_value_capacity+tid;
        workspace.private_cell_stride=blockDim.x;
      } else workspace.payload_intlist_cells += slot * workspace.payload_value_capacity;
    }''')
    path = 'cpp/src/runtime/gpu/device/execute_bytecode_device.cuh'
    edit(path, '    payload_state.intlist_cells=workspace.payload_intlist_cells;',
         '    payload_state.intlist_cells=workspace.payload_intlist_cells;\n    payload_state.private_cell_stride=workspace.private_cell_stride;')
    path = 'cpp/src/runtime/gpu/fitness_gpu.cu'
    edit(path, '  region_workspace.compact_intlist_cells=private_intlists && request_compact;',
         '''  region_workspace.compact_intlist_cells=private_intlists && request_compact;
  const char* cell_soa_setting=std::getenv("GAGP_GPU_PRIVATE_CELL_SOA");
  const bool request_cell_soa=cell_soa_setting && std::string(cell_soa_setting)=="1";
  region_workspace.private_cell_soa=request_cell_soa && region_workspace.compact_intlist_cells;''')
    edit(path, '    if(request_compact)out.execution_profile+=region_workspace.compact_intlist_cells?',
         '    if(request_cell_soa)out.execution_profile+=region_workspace.private_cell_soa?\n        "-private-cell-soa":"-private-cell-soa-declined";\n    if(request_compact)out.execution_profile+=region_workspace.compact_intlist_cells?')
    path = 'cpp/tests/gpu/private_intlist_offset_checks.cuh'
    edit(path, 'Value* source, bool compact, int* failure) {',
         'Value* source, bool compact, int cell_stride, int* failure) {')
    edit(path, '  *failure=0;\n  DArenaPayloadState st;',
         '''  const int lane=threadIdx.x;
  failure+=lane;*failure=0;
  cells+=static_cast<std::size_t>(lane)*65538;
  source+=static_cast<std::size_t>(lane)*65537;
  int_cells+=cell_stride==1?static_cast<std::size_t>(lane)*65538:lane;
  DArenaPayloadState st;
  st.private_cell_stride=cell_stride;''')
    edit(path, '  DIndexedArenaPayloadState local;local.private_intlists=true;local.compact_intlist_cells=compact;',
         '  DIndexedArenaPayloadState local;local.private_intlists=true;local.compact_intlist_cells=compact;\n  local.private_cell_stride=cell_stride;local.private_warp_copy=true;')
    edit(path, 'span.integers!=int_cells+saved:', 'span.integers!=int_cells+static_cast<std::size_t>(saved)*cell_stride:')
    edit(path, '65538*sizeof(Value)', '65538*32*sizeof(Value)')
    edit(path, '65537*sizeof(Value)', '65537*32*sizeof(Value)')
    edit(path, '65538*sizeof(std::int64_t)', '65538*32*sizeof(std::int64_t)')
    edit(path, 'int *device_failure=nullptr,failure=-1;', 'int *device_failure=nullptr,failure=-1;std::vector<int> failures(32,-1);')
    edit(path, 'reinterpret_cast<void**>(&device_failure),sizeof(int)', 'reinterpret_cast<void**>(&device_failure),32*sizeof(int)')
    edit(path, '''  for(bool compact:{false,true}) {
    inspect_private_intlist_offsets<<<1,1>>>(cells,int_cells,source,compact,device_failure);
    ok=cudaGetLastError()==cudaSuccess && cudaDeviceSynchronize()==cudaSuccess &&
        cudaMemcpy(&failure,device_failure,sizeof(int),cudaMemcpyDeviceToHost)==cudaSuccess && failure==0;
    if(!ok)break;
  }''', '''  for(int width:{1,32})for(bool compact:{false,true})for(int stride:{1,32}) {
    if(!ok || (stride!=1 && (!compact || width!=32)))continue;
    inspect_private_intlist_offsets<<<1,width>>>(cells,int_cells,source,compact,stride,device_failure);
    ok=cudaGetLastError()==cudaSuccess && cudaDeviceSynchronize()==cudaSuccess &&
        cudaMemcpy(failures.data(),device_failure,width*sizeof(int),cudaMemcpyDeviceToHost)==cudaSuccess;
    for(int lane=0;ok && lane<width;++lane)if(failures[lane]){failure=failures[lane];ok=false;}
    if(!ok)break;
    failure=0;
  }''')
    path = 'cpp/tests/gpu/test_gpu_phase_population.cu'
    edit(path, 'int main(){try {\n test_simple_pattern_refresh();',
         '''int main(){try {
 // Exercise actual private window/staged/reused-workspace paths and complete
 // CPU/origin/fuel/evolution guards with the new interleaved layout enabled.
 setenv("GAGP_GPU_PRIVATE_CELL_SOA","1",1);
 test_simple_pattern_refresh();''')
    if not dry_run:
        for path, text in changed.items():
            (ROOT / path).write_text(text)
    return sorted(changed)


if __name__ == '__main__':
    print(apply(dry_run=True))
