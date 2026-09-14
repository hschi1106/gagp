#include "gagp/evolution/fuel_events.hpp"

namespace gagp::evo {

const char* fuel_event_name(FuelEvent event) {
  switch (event) {
    case FuelEvent::Operation: return "operation";
    case FuelEvent::Bind: return "bind";
    case FuelEvent::BranchTest: return "branch_test";
    case FuelEvent::BranchMerge: return "branch_merge";
    case FuelEvent::StoreSequence: return "store_sequence";
    case FuelEvent::StoreStart: return "store_start";
    case FuelEvent::StoreBegin: return "store_begin";
    case FuelEvent::StoreEnd: return "store_end";
    case FuelEvent::CheckStart: return "check_start";
    case FuelEvent::CheckBegin: return "check_begin";
    case FuelEvent::CheckEnd: return "check_end";
    case FuelEvent::ObserveSequence: return "observe_sequence";
    case FuelEvent::ClampBegin: return "clamp_begin";
    case FuelEvent::ClampEnd: return "clamp_end";
    case FuelEvent::SetBegin: return "set_begin";
    case FuelEvent::SetEnd: return "set_end";
    case FuelEvent::InitializeState: return "initialize_state";
    case FuelEvent::InitializeCursor: return "initialize_cursor";
    case FuelEvent::TestCursor: return "test_cursor";
    case FuelEvent::ReadElement: return "read_element";
    case FuelEvent::BindElement: return "bind_element";
    case FuelEvent::ComputeIndex: return "compute_index";
    case FuelEvent::UpdateState: return "update_state";
    case FuelEvent::AdvanceCursor: return "advance_cursor";
    case FuelEvent::Repeat: return "repeat";
    case FuelEvent::Result: return "result";
  }
  return "unknown";
}

bool parse_fuel_event(const std::string& name, FuelEvent* out) {
  if (out == nullptr) return false;
#define GAGP_PARSE_FUEL_EVENT(value, spelling) \
  if (name == spelling) {                       \
    *out = FuelEvent::value;                    \
    return true;                                \
  }
  GAGP_PARSE_FUEL_EVENT(Operation, "operation")
  GAGP_PARSE_FUEL_EVENT(Bind, "bind")
  GAGP_PARSE_FUEL_EVENT(BranchTest, "branch_test")
  GAGP_PARSE_FUEL_EVENT(BranchMerge, "branch_merge")
  GAGP_PARSE_FUEL_EVENT(StoreSequence, "store_sequence")
  GAGP_PARSE_FUEL_EVENT(StoreStart, "store_start")
  GAGP_PARSE_FUEL_EVENT(StoreBegin, "store_begin")
  GAGP_PARSE_FUEL_EVENT(StoreEnd, "store_end")
  GAGP_PARSE_FUEL_EVENT(CheckStart, "check_start")
  GAGP_PARSE_FUEL_EVENT(CheckBegin, "check_begin")
  GAGP_PARSE_FUEL_EVENT(CheckEnd, "check_end")
  GAGP_PARSE_FUEL_EVENT(ObserveSequence, "observe_sequence")
  GAGP_PARSE_FUEL_EVENT(ClampBegin, "clamp_begin")
  GAGP_PARSE_FUEL_EVENT(ClampEnd, "clamp_end")
  GAGP_PARSE_FUEL_EVENT(SetBegin, "set_begin")
  GAGP_PARSE_FUEL_EVENT(SetEnd, "set_end")
  GAGP_PARSE_FUEL_EVENT(InitializeState, "initialize_state")
  GAGP_PARSE_FUEL_EVENT(InitializeCursor, "initialize_cursor")
  GAGP_PARSE_FUEL_EVENT(TestCursor, "test_cursor")
  GAGP_PARSE_FUEL_EVENT(ReadElement, "read_element")
  GAGP_PARSE_FUEL_EVENT(BindElement, "bind_element")
  GAGP_PARSE_FUEL_EVENT(ComputeIndex, "compute_index")
  GAGP_PARSE_FUEL_EVENT(UpdateState, "update_state")
  GAGP_PARSE_FUEL_EVENT(AdvanceCursor, "advance_cursor")
  GAGP_PARSE_FUEL_EVENT(Repeat, "repeat")
  GAGP_PARSE_FUEL_EVENT(Result, "result")
#undef GAGP_PARSE_FUEL_EVENT
  return false;
}

namespace {

bool operation_node(NodeKind kind) {
  switch (kind) {
    case NodeKind::CONST:
    case NodeKind::VAR:
    case NodeKind::BOUND_VAR:
    case NodeKind::REGION_VAR:
    case NodeKind::NEG:
    case NodeKind::NOT:
    case NodeKind::CHECK_INT:
    case NodeKind::CHECK_LIST:
    case NodeKind::ADD:
    case NodeKind::SUB:
    case NodeKind::MUL:
    case NodeKind::DIV:
    case NodeKind::MOD:
    case NodeKind::LT:
    case NodeKind::LE:
    case NodeKind::GT:
    case NodeKind::GE:
    case NodeKind::EQ:
    case NodeKind::NE:
    case NodeKind::CALL_ABS:
    case NodeKind::CALL_MIN:
    case NodeKind::CALL_MAX:
    case NodeKind::CALL_CLIP:
    case NodeKind::CALL_IDIV0:
    case NodeKind::CALL_IMOD0:
    case NodeKind::CALL_LEN:
    case NodeKind::CALL_CONCAT:
    case NodeKind::CALL_SLICE:
    case NodeKind::CALL_INDEX:
    case NodeKind::CALL_APPEND:
    case NodeKind::CALL_PREPEND:
    case NodeKind::CALL_REVERSE:
    case NodeKind::CALL_FIND:
    case NodeKind::CALL_CONTAINS:
    case NodeKind::CALL_CHAR_TO_STRING:
    case NodeKind::CALL_STRING_TO_CHAR:
    case NodeKind::CALL_ORD:
    case NodeKind::CALL_CHR:
    case NodeKind::CALL_IS_LETTER:
    case NodeKind::CALL_IS_DIGIT:
    case NodeKind::CALL_IS_SPACE:
    case NodeKind::CALL_IS_VOWEL:
    case NodeKind::CALL_TO_LOWER:
    case NodeKind::CALL_TO_UPPER:
    case NodeKind::CALL_TO_STRING:
    case NodeKind::CALL_SINGLETON:
      return true;
    default:
      return false;
  }
}

bool common_traversal_event(FuelEvent event) {
  switch (event) {
    case FuelEvent::StoreSequence:
    case FuelEvent::StoreStart:
    case FuelEvent::CheckStart:
    case FuelEvent::ObserveSequence:
    case FuelEvent::InitializeState:
    case FuelEvent::InitializeCursor:
    case FuelEvent::TestCursor:
    case FuelEvent::ReadElement:
    case FuelEvent::BindElement:
    case FuelEvent::ComputeIndex:
    case FuelEvent::UpdateState:
    case FuelEvent::AdvanceCursor:
    case FuelEvent::Repeat:
    case FuelEvent::Result:
      return true;
    default:
      return false;
  }
}

}  // namespace

bool supports_fuel_event(NodeKind kind, FuelEvent event) {
  if (event == FuelEvent::Operation) return operation_node(kind);
  if (kind == NodeKind::LET_REGION) return event == FuelEvent::Bind;
  if (kind == NodeKind::IF_EXPR) {
    return event == FuelEvent::BranchTest || event == FuelEvent::BranchMerge;
  }
  if (kind == NodeKind::TRAVERSE) {
    return common_traversal_event(event) ||
           event == FuelEvent::SetBegin || event == FuelEvent::SetEnd;
  }
  if (kind == NodeKind::TRAVERSE_RANGE) {
    return common_traversal_event(event) ||
           event == FuelEvent::StoreBegin || event == FuelEvent::StoreEnd ||
           event == FuelEvent::CheckBegin || event == FuelEvent::CheckEnd ||
           event == FuelEvent::ClampBegin || event == FuelEvent::ClampEnd;
  }
  return false;
}

}  // namespace gagp::evo
