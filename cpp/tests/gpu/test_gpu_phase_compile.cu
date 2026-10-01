#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <vector>
#include "gagp/cli/commands.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/fuel_events.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/device/phase_compile.cuh"

namespace {
using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::repro;
using Clock=std::chrono::steady_clock;
void require(bool value,const std::string& message) { if(!value) throw std::runtime_error(message); }
void cuda_check(cudaError_t result) { if(result!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(result)); }
double ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
template<class T> struct Device {
  T* data=nullptr; std::size_t count;
  explicit Device(std::size_t n):count(n) { cuda_check(cudaMalloc(reinterpret_cast<void**>(&data),sizeof(T)*std::max<std::size_t>(1,n))); }
  ~Device(){ cudaFree(data); }
  Device(const Device&)=delete;
  void upload(const std::vector<T>& values) { require(values.size()==count,"upload size"); if(count) cuda_check(cudaMemcpy(data,values.data(),sizeof(T)*count,cudaMemcpyHostToDevice)); }
  std::vector<T> download() { std::vector<T> out(count); if(count) cuda_check(cudaMemcpy(out.data(),data,sizeof(T)*count,cudaMemcpyDeviceToHost)); return out; }
};
struct Fixture {
  std::vector<PhaseTreeNode> nodes;
  std::vector<Value> constants;
  std::vector<ValueTag> locals;
  PhaseProgram reference;
  ValueTag result=ValueTag::Int;
  PhaseCompileStatus status=PhaseCompileStatus::Ok;
  int code_limit=-1,constant_limit=-1;
};
struct Row { int node_offset,node_count,const_offset,const_count,local_offset,local_count,code_offset,code_capacity,out_const_offset,const_capacity; ValueTag expected; };
__global__ void compile_many(const Row* rows,int count,const PhaseTreeNode* nodes,
    const Value* constants,const ValueTag* locals,gpu_detail::DInstr* code,Value* out_consts,PhaseCompileResult* results) {
  const int index=blockIdx.x*blockDim.x+threadIdx.x; if(index>=count)return;
  const auto row=rows[index];
  results[index]=compile_phase_tree(nodes+row.node_offset,row.node_count,constants+row.const_offset,row.const_count,
      locals+row.local_offset,row.local_count,row.expected,code+row.code_offset,row.code_capacity,
      out_consts+row.out_const_offset,row.const_capacity);
}
unsigned charge(const AstProgram& ast,std::size_t index,FuelEvent event) {
  for(const auto& spec:ast.fuel_specs) if(spec.node_index==index)
    for(const auto& cost:spec.charges) if(cost.event==event)return cost.cost;
  return 1;
}
Opcode arithmetic(NodeKind kind) {
  switch(kind) {
    case NodeKind::ADD:return Opcode::Add; case NodeKind::SUB:return Opcode::Sub;
    case NodeKind::MUL:return Opcode::Mul; case NodeKind::MOD:return Opcode::Mod;
    case NodeKind::NEG:return Opcode::Neg; case NodeKind::NOT:return Opcode::Not;
    case NodeKind::LT:return Opcode::Lt; case NodeKind::LE:return Opcode::Le;
    case NodeKind::GT:return Opcode::Gt; case NodeKind::GE:return Opcode::Ge;
    case NodeKind::EQ:return Opcode::Eq; case NodeKind::NE:return Opcode::Ne;
    case NodeKind::CHECK_INT:return Opcode::CheckInt; case NodeKind::CHECK_LIST:return Opcode::CheckList;
    default:throw std::invalid_argument("unsupported phase tree node");
  }
}
Fixture normalize(const AstProgram& ast,std::size_t begin,std::size_t end,
    const std::map<int,int>& bindings,std::vector<ValueTag> locals,ValueTag result,PhaseProgram reference) {
  Fixture out;out.constants=ast.consts;out.locals=std::move(locals);out.result=result;out.reference=std::move(reference);
  for(auto i=begin;i<end;++i) {
    const auto& node=ast.nodes[i]; const auto& descriptor=node_descriptor(node.kind);
    PhaseTreeNode value;value.arity=descriptor.prefix_arity;value.fuel=charge(ast,i,FuelEvent::Operation);
    if(node.kind==NodeKind::CONST) { value.kind=PhaseTreeKind::Constant;value.operand=node.i0; }
    else if(node.kind==NodeKind::REGION_VAR || node.kind==NodeKind::VAR) {
      value.kind=PhaseTreeKind::Local;value.operand=bindings.at(node.i0);
    } else if(node.kind==NodeKind::IF_EXPR) {
      value.kind=PhaseTreeKind::If;value.fuel=charge(ast,i,FuelEvent::BranchTest);value.merge_fuel=charge(ast,i,FuelEvent::BranchMerge);
    } else if(node.kind==NodeKind::AND || node.kind==NodeKind::OR) {
      value.kind=node.kind==NodeKind::AND?PhaseTreeKind::And:PhaseTreeKind::Or;
    } else {
      value.kind=PhaseTreeKind::Operation;
      if(descriptor.is_builtin()) {value.opcode=Opcode::CallBuiltin;value.operand=descriptor.builtin_id;value.arity=descriptor.builtin_arity;}
      else value.opcode=arithmetic(node.kind);
    }
    out.nodes.push_back(value);
  }
  return out;
}
void random_expr(AstProgram& ast,std::mt19937& random,bool boolean,int depth) {
  const int pick=depth==0 ? random()%2 : random()%8;
  if(pick==0) { int index=ast.consts.size();ast.consts.push_back(boolean?Value::from_bool(random()%2):Value::from_int(int(random()%15)-7));ast.nodes.push_back({NodeKind::CONST,index});return; }
  if(pick==1) {ast.nodes.push_back({NodeKind::VAR,boolean?1:0});return;}
  if(boolean) {
    if(pick==2) {ast.nodes.push_back({NodeKind::NOT});random_expr(ast,random,true,depth-1);}
    else if(pick==3 || pick==4) {ast.nodes.push_back({pick==3?NodeKind::AND:NodeKind::OR});random_expr(ast,random,true,depth-1);random_expr(ast,random,true,depth-1);}
    else {ast.nodes.push_back({pick==5?NodeKind::LT:pick==6?NodeKind::EQ:NodeKind::NE});random_expr(ast,random,false,depth-1);random_expr(ast,random,false,depth-1);}
  } else if(pick==2) {
    ast.nodes.push_back({NodeKind::IF_EXPR});random_expr(ast,random,true,depth-1);random_expr(ast,random,false,depth-1);random_expr(ast,random,false,depth-1);
  } else {
    const NodeKind kinds[]{NodeKind::ADD,NodeKind::SUB,NodeKind::MUL,NodeKind::CALL_IDIV0,NodeKind::CALL_IMOD0};
    ast.nodes.push_back({kinds[pick-3]});random_expr(ast,random,false,depth-1);random_expr(ast,random,false,depth-1);
  }
}
std::vector<Fixture> generated() {
  std::vector<Fixture> out;std::mt19937 random(4192);
  for(int i=0;i<512;++i) {
    AstProgram ast;ast.names={"x","b"};ast.nodes={{NodeKind::PROGRAM},{NodeKind::BLOCK_CONS},{NodeKind::RETURN}};
    const bool boolean=i%2;random_expr(ast,random,boolean,i%6);const auto end=ast.nodes.size();ast.nodes.push_back({NodeKind::BLOCK_NIL});
    for(std::size_t j=3;j<end;++j) {
      if(ast.nodes[j].kind==NodeKind::IF_EXPR)ast.fuel_specs.push_back({j,{{FuelEvent::BranchTest,static_cast<std::uint32_t>(random()%4)},{FuelEvent::BranchMerge,static_cast<std::uint32_t>(random()%4)}}});
      else if(supports_fuel_event(ast.nodes[j].kind,FuelEvent::Operation)) ast.fuel_specs.push_back({j,{{FuelEvent::Operation,static_cast<std::uint32_t>(random()%4)}}});
    }
    const auto verified=verify_ast(ast,{{"x",RType::Int},{"b",RType::Bool}});require(verified.ok,"generated fixture "+std::to_string(i)+" type check: "+verified.diagnostic.message);
    ProgramGenome genome;genome.ast=ast;auto compiled=compile_for_eval(genome,verified.verified,{"x","b"});
    require(compiled.code.back().op==Opcode::Return,"reference return");compiled.code.pop_back();compiled.instruction_fuel.pop_back();
    PhaseProgram phase;phase.code=std::move(compiled.code);phase.consts=std::move(compiled.consts);phase.n_locals=compiled.n_locals;phase.instruction_fuel=std::move(compiled.instruction_fuel);
    out.push_back(normalize(ast,3,end,{{0,0},{1,1}},{ValueTag::Int,ValueTag::Bool},boolean?ValueTag::Bool:ValueTag::Int,std::move(phase)));
  }
  Fixture leaf;leaf.nodes={{PhaseTreeKind::Constant,Opcode::PushConst,0,0,1,1}};leaf.constants={Value::from_int(1)};leaf.reference.consts=leaf.constants;leaf.reference.code={{Opcode::PushConst,0,0,true,false}};
  auto invalid=leaf;invalid.nodes[0].operand=1;invalid.status=PhaseCompileStatus::Invalid;out.push_back(invalid);
  invalid=leaf;invalid.nodes.push_back(invalid.nodes[0]);invalid.status=PhaseCompileStatus::Invalid;out.push_back(invalid);
  invalid=leaf;invalid.constants[0]=Value::from_float(1);invalid.status=PhaseCompileStatus::Unsupported;out.push_back(invalid);
  invalid=leaf;invalid.nodes.insert(invalid.nodes.begin(),65,{PhaseTreeKind::Operation,Opcode::Neg,0,1,1,1});invalid.status=PhaseCompileStatus::Capacity;out.push_back(invalid);
  invalid=leaf;invalid.nodes.insert(invalid.nodes.begin(),{PhaseTreeKind::Operation,Opcode::Add,0,2,1,1});invalid.nodes.push_back(leaf.nodes[0]);invalid.code_limit=1;invalid.status=PhaseCompileStatus::Capacity;out.push_back(invalid);
  invalid.constant_limit=1;invalid.code_limit=-1;out.push_back(invalid);
  invalid=leaf;invalid.nodes[0]={PhaseTreeKind::Local,Opcode::Load,2,0,1,1};invalid.locals={ValueTag::Int};invalid.status=PhaseCompileStatus::Invalid;out.push_back(invalid);
  invalid=leaf;invalid.nodes[0].fuel=0xffffffffU;invalid.status=PhaseCompileStatus::Invalid;out.push_back(invalid);
  return out;
}
const RegionPhase& phase_at(const BoundedRegionSegment& segment,std::size_t ordinal) {
  auto kind=bounded_region_phase_kind(segment.plan,ordinal);
  switch(kind) {
    case RegionPhaseKind::BasePredicate:return segment.base_predicate;
    case RegionPhaseKind::BaseBody:return segment.base_body;
    case RegionPhaseKind::Combine:return segment.combine;
    case RegionPhaseKind::Boundary:return *segment.boundary;
    case RegionPhaseKind::Preparation:return segment.preparations.at(bounded_region_preparation_ordinal(segment.plan,ordinal));
    case RegionPhaseKind::Request: {
      const auto first=2+segment.preparations.size();
      return segment.request_expressions.at(ordinal-first);
    }
  }
  throw std::logic_error("phase kind");
}
std::vector<Fixture> frozen(const char* prepared,const char* grammar_path) {
  std::ifstream file(prepared);require(bool(file),"missing prepared input");
  const std::string text((std::istreambuf_iterator<char>(file)),{});
  const auto json=cli_detail::JsonParser(text,{true,512}).parse();
  const auto grammar=grammar::compile_grammar(grammar::load_definition(grammar_path));
  std::vector<Fixture> result;
  for(const auto& raw:json.object_v.at("programs").array_v) {
    ProgramGenome genome;genome.ast=cli_detail::decode_ast_json(raw);
    // External inputs get independent grammar admission, not a type-only claim.
    grammar::require_membership(grammar,genome);
    const auto shape=verify_ast_structure(genome.ast);require(shape.ok,"frozen shape");
    const auto compiled=compile_for_eval(genome);
    require(genome.ast.bounded_region_specs.size()==1 && compiled.bounded_region_segments.size()==1,"prototype requires one region");
    const auto& spec=genome.ast.bounded_region_specs[0];const auto& segment=compiled.bounded_region_segments[0];
    std::size_t cursor=spec.node_index+1;
    for(std::size_t i=0;i<spec.plan.state_types.size()+spec.plan.bound_operand_count;++i)cursor=shape.verified.subtree_end.at(cursor);
    for(std::size_t i=0;i<spec.phases.size();++i) {
      const auto end=shape.verified.subtree_end.at(cursor);const auto& phase=phase_at(segment,i);
      std::map<int,int> bindings;std::vector<ValueTag> locals;
      for(std::size_t j=0;j<spec.phases[i].bindings.size();++j) {
        const auto& binding=spec.phases[i].bindings[j];bindings.emplace(binding.binder_id,int(j));
        locals.push_back(region_slot_type(spec.plan,bounded_region_phase_kind(spec.plan,i),binding.source,bounded_region_preparation_ordinal(spec.plan,i)));
      }
      result.push_back(normalize(genome.ast,cursor,end,bindings,std::move(locals),bounded_region_phase_type(spec.plan,i),phase.program));cursor=end;
    }
  }
  return result;
}
void run(const std::vector<Fixture>& fixtures) {
  const auto start=Clock::now();std::vector<Row> rows;std::vector<PhaseTreeNode> nodes;std::vector<Value> constants;std::vector<ValueTag> locals;
  int code_count=0,out_const_count=0;
  require(!fixtures.empty() && fixtures.size()<=65536,"prototype batch capacity");
  for(const auto& f:fixtures) {
    require(f.nodes.size()<=4096 && nodes.size()+f.nodes.size()<=1000000,"prototype node capacity");
    const int code_capacity=f.code_limit<0?int(f.nodes.size()*6+1):f.code_limit;
    const int const_capacity=f.constant_limit<0?int(f.nodes.size()+1):f.constant_limit;
    rows.push_back({int(nodes.size()),int(f.nodes.size()),int(constants.size()),int(f.constants.size()),int(locals.size()),int(f.locals.size()),code_count,code_capacity,out_const_count,const_capacity,f.result});
    nodes.insert(nodes.end(),f.nodes.begin(),f.nodes.end());constants.insert(constants.end(),f.constants.begin(),f.constants.end());locals.insert(locals.end(),f.locals.begin(),f.locals.end());
    code_count+=code_capacity;out_const_count+=const_capacity;
  }
  const auto flatten_ms=ms(start);const auto alloc=Clock::now();
  Device<Row> dr(rows.size());Device<PhaseTreeNode> dn(nodes.size());Device<Value> dc(constants.size()),dout(out_const_count);Device<ValueTag> dl(locals.size());
  Device<gpu_detail::DInstr> dcode(code_count);Device<PhaseCompileResult> dresult(rows.size());
  const auto alloc_ms=ms(alloc);auto begin=Clock::now();dr.upload(rows);dn.upload(nodes);dc.upload(constants);dl.upload(locals);const auto h2d_ms=ms(begin);
  auto launch=[&]{compile_many<<<(rows.size()+63)/64,64>>>(dr.data,rows.size(),dn.data,dc.data,dl.data,dcode.data,dout.data,dresult.data);cuda_check(cudaGetLastError());};
  begin=Clock::now();launch();cuda_check(cudaDeviceSynchronize());const auto cold_ms=ms(begin);
  cudaEvent_t a,b;cuda_check(cudaEventCreate(&a));cuda_check(cudaEventCreate(&b));std::vector<float> samples;
  for(int repeat=0;repeat<10;++repeat) {cuda_check(cudaEventRecord(a));launch();cuda_check(cudaEventRecord(b));cuda_check(cudaEventSynchronize(b));float elapsed;cuda_check(cudaEventElapsedTime(&elapsed,a,b));samples.push_back(elapsed);}
  cudaEventDestroy(a);cudaEventDestroy(b);
  begin=Clock::now();const auto results=dresult.download();const auto code=dcode.download();const auto values=dout.download();const auto d2h_ms=ms(begin);
  for(std::size_t i=0;i<fixtures.size();++i) {
    const auto& f=fixtures[i];const auto r=results[i];const auto row=rows[i];
    require(r.status==f.status,"status mismatch fixture "+std::to_string(i)+" got "+std::to_string(int(r.status)));
    if(f.status!=PhaseCompileStatus::Ok)continue;
    require(r.code_count==int(f.reference.code.size()) && r.constant_count==int(f.reference.consts.size()),"lowering shape mismatch fixture "+std::to_string(i));
    for(int j=0;j<r.code_count;++j) {
      const auto& x=code[row.code_offset+j];const auto& y=f.reference.code[j];
      const unsigned cost=f.reference.instruction_fuel.empty()?1:f.reference.instruction_fuel[j];
      require(x.op==int(y.op) && x.flags==unsigned((y.has_a?1:0)|(y.has_b?2:0)) && x.a==y.a && x.b==y.b && x.fuel==cost,"instruction/fuel mismatch fixture "+std::to_string(i)+" ip "+std::to_string(j));
    }
    for(int j=0;j<r.constant_count;++j) {
      const auto& x=values[row.out_const_offset+j];const auto& y=f.reference.consts[j];
      require(x.tag==y.tag && (x.tag==ValueTag::Bool?x.b==y.b:x.i==y.i),"constant mismatch");
    }
  }
  std::cout<<"{\"phases\":"<<fixtures.size()<<",\"nodes\":"<<nodes.size()<<",\"flatten_ms\":"<<flatten_ms<<",\"allocation_ms\":"<<alloc_ms<<",\"h2d_ms\":"<<h2d_ms<<",\"cold_launch_ms\":"<<cold_ms<<",\"d2h_ms\":"<<d2h_ms<<",\"warm_kernel_ms\":[";
  for(std::size_t i=0;i<samples.size();++i)std::cout<<(i?",":"")<<samples[i];std::cout<<"],\"all_compiler_outputs_match\":true}\n";
}
} // namespace
int main(int argc,char** argv) {
  try {
    require(argc==1 || argc==3,"usage: test_gpu_phase_compile [prepared.json grammar.json]");
    const auto start=Clock::now();const auto fixtures=argc==3?frozen(argv[1],argv[2]):generated();const auto prepare_ms=ms(start);
    FitnessSessionGpu session;const auto init=session.init({{}},{Value::from_int(0)},1,1,1.0);require(init.ok,"CUDA initialization");
    std::cout<<"{\"host_admission_normalization_reference_compile_ms\":"<<prepare_ms<<",\"cuda_session_init_ms\":"<<init.timing.total_ms<<",\"prototype_only\":true}\n";
    run(fixtures);
  } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}return 0;
}
