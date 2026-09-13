#include <iostream>
#include <stdexcept>

#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/evolution/grammar/structured.hpp"
#include "gagp/evolution/ast_verify.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Action> void rejects(Action action) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid catalog request was accepted");
}
Value literal(RType type) {
  switch (type) {
    case RType::Int: return Value::from_int(1);
    case RType::Float: return Value::from_float(1.0);
    case RType::Bool: return Value::from_bool(true);
    case RType::Char: return Value::from_char('a');
    case RType::String: return Value::from_string_hash_len(1, 0);
    case RType::IntList: return Value::from_int_list_hash_len(1, 0);
    case RType::FloatList: return Value::from_float_list_hash_len(1, 0);
    case RType::StringList: return Value::from_string_list_hash_len(1, 0);
    default: throw std::runtime_error("unexpected type");
  }
}
}  // namespace

int main() {
  try {
    const auto& catalog = PrimitiveCatalog::standard();
    check(catalog.control_signatures().size() == 21, "control overload catalog incomplete");
    for (const auto& signature : catalog.control_signatures()) {
      check(&catalog.resolve_control(signature.key) == &signature, "control lookup lost identity");
      const auto& native = node_descriptor(signature.lowering_node);
      check(native.category == signature.result, "control category disagrees with native descriptor");
      check(native.prefix_arity == static_cast<int>(signature.arguments.size()), "control arity disagrees with native descriptor");
      if (signature.id)
        check(catalog.control_signatures()[signature.id - 1].key < signature.key, "control IDs not canonical");
    }
    check(catalog.resolve_control("for_range(Int,Block)->Statement").requires_name,
          "ForRange lost index-name contract");
    rejects([&] { catalog.resolve_control("for_range(Float,Block)->Statement"); });
    rejects([&] { catalog.resolve_control("return(Any)->Statement"); });
    const auto recursive = recursive_contract({RType::String, RType::Int}, RType::StringList, 3);
    check(recursive.arguments.size() == 11 && recursive.base_predicate == 2 &&
          recursive.base_body == 3 && recursive.combine_body == 10, "recursive static region layout");
    check(recursive.regions.back().bindings.size() == 5 &&
          recursive.regions.back().bindings.back().type == RType::StringList, "recursive result binding types");
    for (std::size_t i = 0; i + 1 < recursive.regions.size(); ++i)
      check(recursive.regions[i].bindings.size() == 2, "request result leaked into earlier phase");
    rejects([&] { require_structured_execution(recursive); });
    for (auto type : value_types()) {
      for (std::uint32_t dimensions = 1; dimensions <= 4; ++dimensions) {
        const auto memo = memoized_contract(dimensions, type, 5);
        check(memo.arguments.size() == 2 * dimensions + 4 && memo.regions.size() == 4,
              "memoized coordinate layout depends on specialized dimension");
        check(memo.regions.back().bindings.size() == dimensions + 5 &&
              memo.regions.back().bindings.back().type == type, "memoized result tag erased");
        rejects([&] { require_structured_execution(memo); });
      }
    }
    rejects([] { recursive_contract({}, RType::Int, 1); });
    rejects([] { recursive_contract({RType::Any}, RType::Int, 1); });
    rejects([] { recursive_contract({RType::Int}, RType::Int, 0); });
    rejects([] { memoized_contract(5, RType::Int, 1); });
    rejects([] { memoized_contract(1, RType::Int, 9); });
    check(catalog.resolve("index", {RType::String, RType::Int}, RType::Char).executable(), "String index missing");
    check(catalog.resolve("index", {RType::StringList, RType::Int}, RType::String).executable(), "StringList index missing");
    rejects([&] { catalog.resolve("index", {RType::String, RType::Int}, RType::Int); });
    rejects([&] { catalog.resolve("add", {RType::Int, RType::Float}, RType::Float); });
    rejects([&] { catalog.resolve("div", {RType::Int, RType::Int}, RType::Int); });
    rejects([&] { catalog.resolve("append", {RType::String, RType::Char}, RType::String); });
    rejects([&] { catalog.resolve("asgp_dc", {RType::IntList}, RType::Int); });
    rejects([&] { catalog.resolve("linear_rec", {RType::IntList}, RType::Int); });
    rejects([] { parse_type("Any"); });
    rejects([] { parse_type("CharList"); });
    const auto& traversal = catalog.resolve("traverse", {RType::String, RType::Int, RType::Int, RType::Int}, RType::Int);
    check(traversal.regions.size() == 1 && traversal.regions[0].argument == 3, "traversal body slot changed");
    check(traversal.regions[0].bindings[0].type == RType::Char, "String traversal must bind Char");
    rejects([&] { catalog.require_executable(traversal.id); });
    std::size_t checked = 0;
    for (const auto& signature : catalog.signatures()) {
      check(&catalog.at(signature.id) == &signature, "numeric catalog index changed");
      check(&catalog.resolve(signature.operation, signature.arguments, signature.result) == &signature,
            "exact overload lookup is inconsistent");
      if (signature.id) check(catalog.at(signature.id - 1).key < signature.key, "IDs are not canonical");
      if (!signature.executable() || signature.operation == "bound") continue;
      AstProgram ast;
      ast.nodes = {{NodeKind::PROGRAM, 0, 0}, {NodeKind::BLOCK_CONS, 0, 0},
                   {NodeKind::RETURN, 0, 0}, {*signature.lowering_node, 0, 0}};
      std::vector<InputSpec> inputs;
      if (signature.operation == "constant") ast.consts.push_back(literal(signature.result));
      if (signature.operation == "input") {
        ast.names = {"x"};
        inputs = {{"x", signature.result}};
      }
      for (auto argument : signature.arguments) {
        ast.nodes.push_back({NodeKind::CONST, static_cast<int>(ast.consts.size()), 0});
        ast.consts.push_back(literal(argument));
      }
      ast.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
      const auto result = verify_ast(ast, inputs);
      if (!result || result.verified.return_type != signature.result)
        throw std::runtime_error("catalog disagrees with native type verifier: " + signature.key);
      ++checked;
    }
    std::cout << checked << " exact signatures agree with native AST typing\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
