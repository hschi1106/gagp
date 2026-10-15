#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <vector>
#include "eval/dc_scheme.h"
#include "lang/grammar.h"

using namespace asgp;
Expr node(NodeKind kind, Type type, int op=0, int64_t value=0,
          std::vector<Expr> children={}) {
  Node n{}; n.kind=kind; n.type=type; n.op=op; n.value=value;
  Expr out{n};
  for(const auto& child:children)out.insert(out.end(),child.begin(),child.end());
  out[0].size=out.size(); return out;
}
int main() {
  const auto list=[](int slot){return node(NodeKind::kResult,Type::kIntList,slot);};
  auto solve=node(NodeKind::kInputList,Type::kIntList);
  auto divide=node(NodeKind::kIntOp,Type::kInt,1,0,
      {node(NodeKind::kInputSize,Type::kInt),node(NodeKind::kIntConst,Type::kInt,0,1)});
  auto combine=node(NodeKind::kConcat,Type::kIntList,0,0,{list(0),
      node(NodeKind::kSingleton,Type::kIntList,0,0,{
          node(NodeKind::kIntOp,Type::kInt,0,0,{
              node(NodeKind::kHead,Type::kInt,1,0,{list(0)}),
              node(NodeKind::kHead,Type::kInt,0,0,{list(1)})})})});
  GrammarConfig config; config.carrier=Type::kIntList;
  auto grammar=SchemeGrammar::Build(config);
  int heads=0,singletons=0;
  for(size_t i=0;i<grammar[2].size();++i) {
    const auto& p=grammar[2][i];
    heads+=p.kind==NodeKind::kHead;
    singletons+=p.kind==NodeKind::kSingleton;
  }
  assert(heads==2 && singletons==1);
  // Non-list carrier grammars retain their original available productions.
  config.carrier=Type::kInt; grammar=SchemeGrammar::Build(config);
  for(size_t i=0;i<grammar[2].size();++i)assert(grammar[2][i].kind!=NodeKind::kHead);
  ListArena arena; EvalLimits limits;
  DcProgram program{&solve,&divide,&combine,Type::kIntList,1};
  for(int length=1;length<=64;++length) {
    std::vector<int64_t> input;
    for(int i=0;i<length;++i)input.push_back((i*13+length*7)%31-15);
    auto result=RunDc(program,{input.data(),uint32_t(input.size()),nullptr},limits,arena);
    assert(result.ok() && result.value.list.len==input.size());
    int64_t expected=0;
    for(size_t i=0;i<input.size();++i) {
      expected+=input[i]; assert(arena.At(result.value.list)[i]==expected);
    }
  }
  assert(RunDc(program,{nullptr,0,nullptr},limits,arena).error==EvalError::kEmptyList);
  // General last semantics, independent of the cumulative witness.
  for(int op:{0,1}) {
    auto access=node(NodeKind::kHead,Type::kInt,op,0,{node(NodeKind::kInputList,Type::kIntList)});
    int64_t values[]={7,-3,11}; arena.Reset(values,3);
    EvalState state{};state.arena=&arena;state.nodes=access.data();state.len=3;
    assert(EvalInt(state,0)==(op ? 11 : 7) && state.ok());
    arena.Reset(nullptr,0);state.error=EvalError::kNone;state.len=0;
    EvalInt(state,0);assert(state.error==EvalError::kEmptyList);
  }
}
