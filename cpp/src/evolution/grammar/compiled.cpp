#include "gagp/evolution/grammar/compiled.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace gagp::evo::grammar {
namespace {
using Json = cli_detail::JsonValue;
using Kind = Json::Kind;
using Scope = std::vector<RegionBinding>;
using cli_detail::require_object_field;
using cli_detail::require_string;

void keys(const Json& value, std::initializer_list<const char*> allowed, const char* context) {
  if (value.kind != Kind::Object) throw std::invalid_argument(std::string(context) + " must be an object");
  const std::set<std::string> names(allowed.begin(), allowed.end());
  for (const auto& entry : value.object_v)
    if (!names.count(entry.first)) throw std::invalid_argument(std::string(context) + ": unknown key " + entry.first);
}
const std::vector<Json>& array(const Json& value) {
  if (value.kind != Kind::Array) throw std::invalid_argument("expected grammar array");
  return value.array_v;
}
std::string field(const Json& value, const char* name) { return require_string(require_object_field(value, name), name); }
std::uint32_t positive(const Json& value, std::uint32_t maximum, const char* name) {
  if (value.kind != Kind::Number || !std::isfinite(value.number_v) || value.number_v < 1 ||
      value.number_v > maximum || std::floor(value.number_v) != value.number_v)
    throw std::invalid_argument(std::string(name) + " must be a positive integer within capacity");
  return static_cast<std::uint32_t>(value.number_v);
}
std::size_t type_index(RType type) {
  const auto& types = value_types();
  const auto found = std::find(types.begin(), types.end(), type);
  if (found == types.end()) throw std::invalid_argument("index requires exact value type");
  return static_cast<std::size_t>(found - types.begin());
}
std::size_t category_index(NodeCategory category) {
  switch (category) {
    case NodeCategory::Program: return 0;
    case NodeCategory::Block: return 1;
    case NodeCategory::Statement: return 2;
    case NodeCategory::Expression: return 3;
    default: throw std::invalid_argument("unsupported grammar syntax category");
  }
}
NodeCategory category(const Json& value) {
  if (!value.object_v.count("category")) return NodeCategory::Expression;
  const auto name = field(value, "category");
  if (name == "Program") return NodeCategory::Program;
  if (name == "Block") return NodeCategory::Block;
  if (name == "Statement") return NodeCategory::Statement;
  if (name == "Expression") return NodeCategory::Expression;
  throw std::invalid_argument("unknown grammar syntax category: " + name);
}
void add_binding(Scope& scope, std::string name, RType type) {
  const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
  if (name.empty() || !letter(name.front()) || !std::all_of(name.begin(), name.end(), [&](char c) {
        return letter(c) || (c >= '0' && c <= '9'); })) throw std::invalid_argument("invalid lexical binding name");
  for (const auto& binding : scope)
    if (binding.name == name) throw std::invalid_argument("duplicate/shadowed lexical binding: " + name);
  scope.push_back({std::move(name), type});
}
Scope scope(const Json& value) {
  Scope result;
  for (const auto& item : array(value)) {
    keys(item, {"name", "type"}, "scope binding");
    add_binding(result, field(item, "name"), parse_type(field(item, "type")));
  }
  return result;
}
std::uint32_t binding(const Scope& scope, const std::string& name) {
  for (std::uint32_t i = 0; i < scope.size(); ++i) if (scope[i].name == name) return i;
  throw std::invalid_argument("binding is not visible: " + name);
}
}  // namespace

class GrammarCompiler {
 public:
  CompiledGrammar compile(const ResolvedDefinition& definition) {
    // Re-resolve the supplied model instead of trusting mutable cached strings/hash.
    const auto normalized = parse_definition(canonical_json(definition.document));
    out_.canonical_ = normalized.canonical; out_.content_hash_ = normalized.content_hash;
    const auto& doc = normalized.document;
    const auto& search = require_object_field(doc, "search_limits");
    keys(search, {"max_nodes", "max_depth"}, "search_limits");
    out_.search_limits_.max_nodes = positive(require_object_field(search, "max_nodes"), 65536, "max_nodes");
    out_.search_limits_.max_depth = positive(require_object_field(search, "max_depth"), 256, "max_depth");
    const auto& execution = require_object_field(doc, "execution_limits");
    keys(execution, {"fuel"}, "execution_limits");
    out_.execution_limits_.fuel = positive(require_object_field(execution, "fuel"), 0x7fffffff, "fuel");
    out_.inputs_ = scope(require_object_field(doc, "inputs"));
    out_.locals_ = scope(require_object_field(doc, "locals"));
    for (const auto& local : out_.locals_)
      for (const auto& input : out_.inputs_)
        if (local.name == input.name) throw std::invalid_argument("local declaration conflicts with input name");
    const auto& rules = array(require_object_field(doc, "nonterminals"));
    if (rules.empty() || rules.size() > 4096) throw std::invalid_argument("grammar requires 1..4096 nonterminals");
    for (const auto& rule : rules) {
      keys(rule, {"id", "type", "scope", "alternatives", "category"}, "nonterminal");
      CompiledNonterminal nt;
      nt.id = static_cast<std::uint32_t>(out_.nonterminals_.size());
      nt.stable_id = field(rule, "id"); nt.type = parse_type(field(rule, "type"));
      nt.category = category(rule);
      nt.scope = scope(require_object_field(rule, "scope"));
      nt.context = intern_context(nt.scope);
      ids_.emplace(nt.stable_id, nt.id); out_.nonterminals_.push_back(std::move(nt));
    }
    declare_templates(array(require_object_field(doc, "templates")));
    for (std::uint32_t id = 0; id < out_.templates_.size(); ++id)
      out_.templates_[id].body = instantiate(id, nullptr, out_.templates_[id].scope, 0);
    const auto& entry = require_object_field(doc, "entry");
    keys(entry, {"nonterminal", "type", "category"}, "entry");
    out_.entry_ = reference(field(entry, "nonterminal"));
    const auto& entry_nt = out_.nonterminals_[out_.entry_];
    if (entry_nt.type != parse_type(field(entry, "type"))) throw std::invalid_argument("entry result type mismatch");
    if (entry_nt.category != category(entry) ||
        (entry_nt.category != NodeCategory::Expression && entry_nt.category != NodeCategory::Program))
      throw std::invalid_argument("entry requires matching Expression or Program category");
    if (!entry_nt.scope.empty()) throw std::invalid_argument("entry nonterminal requires unavailable lexical bindings");
    for (std::size_t i = 0; i < rules.size(); ++i) {
      const auto& alternatives = array(require_object_field(rules[i], "alternatives"));
      if (alternatives.empty()) throw std::invalid_argument("nonterminal has no alternatives");
      auto& nt = out_.nonterminals_[i];
      for (const auto& alternative : alternatives) {
        if (out_.productions_.size() >= 65536) throw std::invalid_argument("grammar production capacity exceeded");
        keys(alternative, {"id", "weight", "expression"}, "alternative");
        const auto& weight = require_object_field(alternative, "weight");
        if (weight.kind != Kind::Number || !std::isfinite(weight.number_v) || weight.number_v <= 0)
          throw std::invalid_argument("production weight must be positive and finite");
        CompiledProduction production;
        production.id = static_cast<std::uint32_t>(out_.productions_.size());
        production.stable_id = nt.stable_id + "/" + field(alternative, "id");
        production.nonterminal = nt.id; production.weight = weight.number_v;
        production.expression = expression(require_object_field(alternative, "expression"), nt.scope, 0);
        if (out_.expressions_[production.expression].type != nt.type || out_.expressions_[production.expression].category != nt.category)
          throw std::invalid_argument("production result type mismatch: " + production.stable_id);
        nt.productions.push_back(production.id);
        if (nt.category == NodeCategory::Expression) out_.by_type_[type_index(nt.type)].push_back(production.id);
        out_.by_category_[category_index(nt.category)].push_back(production.id);
        out_.productions_.push_back(std::move(production));
      }
    }
    costs();
    index_contexts();
    return std::move(out_);
  }

 private:
  std::uint32_t intern_context(const Scope& scope) {
    for (const auto& context : out_.contexts_) {
      if (context.scope.size() != scope.size()) continue;
      bool same = true;
      for (std::size_t i = 0; i < scope.size(); ++i)
        same &= context.scope[i].name == scope[i].name && context.scope[i].type == scope[i].type;
      if (same) return context.id;
    }
    if (out_.contexts_.size() >= 4096) throw std::invalid_argument("grammar context capacity exceeded");
    const auto id = static_cast<std::uint32_t>(out_.contexts_.size());
    CompiledContext context; context.id = id; context.scope = scope;
    out_.contexts_.push_back(std::move(context)); return id;
  }
  void index_contexts() {
    std::size_t entries = 0;
    for (auto& context : out_.contexts_) {
      for (const auto& nt : out_.nonterminals_) {
        ContextNonterminal candidate; candidate.nonterminal = nt.id;
        bool compatible = true;
        for (const auto& required : nt.scope) {
          const auto found = std::find_if(context.scope.begin(), context.scope.end(), [&](const auto& available) {
            return required.name == available.name && required.type == available.type;
          });
          if (found == context.scope.end()) { compatible = false; break; }
          candidate.scope_mapping.push_back(static_cast<std::uint32_t>(found - context.scope.begin()));
        }
        if (!compatible) continue;
        entries += 1 + nt.productions.size() * (nt.category == NodeCategory::Expression ? 2 : 1) + candidate.scope_mapping.size();
        if (entries > 4 * 1024 * 1024) throw std::invalid_argument("grammar context index capacity exceeded");
        context.nonterminals.push_back(std::move(candidate));
        auto& productions = context.productions_by_category[category_index(nt.category)];
        productions.insert(productions.end(), nt.productions.begin(), nt.productions.end());
        if (nt.category == NodeCategory::Expression) {
          auto& typed = context.productions_by_type[type_index(nt.type)];
          typed.insert(typed.end(), nt.productions.begin(), nt.productions.end());
        }
      }
    }
  }
  struct TemplateContext {
    std::uint32_t id;
    const Json* arguments;
    std::vector<unsigned>* uses;
    const TemplateContext* argument_context;
  };
  void declare_templates(const std::vector<Json>& definitions) {
    if (definitions.size() > 4096) throw std::invalid_argument("grammar template capacity exceeded");
    for (const auto& definition : definitions) {
      keys(definition, {"id", "type", "scope", "holes", "body", "category"}, "template");
      CompiledTemplate value;
      value.id = static_cast<std::uint32_t>(out_.templates_.size());
      value.stable_id = field(definition, "id"); value.type = parse_type(field(definition, "type"));
      value.category = category(definition);
      value.scope = scope(require_object_field(definition, "scope"));
      for (const auto& hole : array(require_object_field(definition, "holes"))) {
        keys(hole, {"id", "type", "scope", "category"}, "template hole");
        value.holes.push_back({static_cast<std::uint32_t>(value.holes.size()), field(hole, "id"),
            parse_type(field(hole, "type")), scope(require_object_field(hole, "scope"))});
        value.holes.back().category = category(hole);
      }
      template_ids_.emplace(value.stable_id, value.id);
      template_bodies_.push_back(&require_object_field(definition, "body"));
      out_.templates_.push_back(std::move(value));
    }
  }
  std::vector<std::uint32_t> map_scope(const Scope& required, const Scope& environment) const {
    std::vector<std::uint32_t> result;
    for (const auto& item : required) {
      const auto index = binding(environment, item.name);
      if (environment[index].type != item.type) throw std::invalid_argument("reference scope type mismatch");
      result.push_back(index);
    }
    return result;
  }
  std::uint32_t instantiate(std::uint32_t id, const Json* arguments, const Scope& environment, unsigned depth,
      const TemplateContext* argument_context = nullptr) {
    if (depth >= 256 || !active_templates_.insert(id).second)
      throw std::invalid_argument("template expansion cycle or depth capacity exceeded");
    const auto& definition = out_.templates_[id];
    if (arguments) {
      if (arguments->kind != Kind::Object || arguments->object_v.size() != definition.holes.size())
        throw std::invalid_argument("template arguments must fill every declared hole");
      for (const auto& hole : definition.holes)
        if (!arguments->object_v.count(hole.stable_id)) throw std::invalid_argument("missing template hole: " + hole.stable_id);
    }
    CompiledExpression node;
    node.kind = ExpressionKind::Template; node.target = id; node.type = definition.type;
    node.category = definition.category;
    node.template_id = id; node.fixed = true;
    node.context = intern_context(environment);
    node.scope_mapping = map_scope(definition.scope, environment);
    std::vector<unsigned> uses(definition.holes.size(), 0);
    const TemplateContext context{id, arguments, &uses, argument_context};
    const auto body = expression(*template_bodies_[id], definition.scope, depth + 1, &context);
    if (out_.expressions_[body].type != definition.type || out_.expressions_[body].category != definition.category)
      throw std::invalid_argument("template body result type mismatch");
    for (auto count : uses)
      if (!count) throw std::invalid_argument("each template hole must occur in its body");
    node.children.push_back(body);
    active_templates_.erase(id);
    return append(std::move(node));
  }
  std::uint32_t append(CompiledExpression node) {
    if (out_.expressions_.size() >= 65536) throw std::invalid_argument("grammar expression capacity exceeded");
    const auto id = static_cast<std::uint32_t>(out_.expressions_.size());
    out_.expressions_.push_back(std::move(node)); return id;
  }
  std::uint32_t reference(const std::string& id) const {
    const auto found = ids_.find(id);
    if (found == ids_.end()) throw std::invalid_argument("unknown nonterminal: " + id);
    return found->second;
  }
  std::uint32_t structured(const Json& value) {
    const auto family = field(value, "family");
    StructuredContract contract;
    if (family == "recur") {
      keys(value, {"family", "state_types", "result_type", "requests"}, "recursive contract");
      std::vector<RType> state;
      for (const auto& type : array(require_object_field(value, "state_types")))
        state.push_back(parse_type(require_string(type, "state type")));
      contract = recursive_contract(state, parse_type(field(value, "result_type")),
          positive(require_object_field(value, "requests"), kStructuredRequestCapacity, "requests"));
    } else if (family == "memo") {
      keys(value, {"family", "dimensions", "result_type", "requests"}, "memoized contract");
      contract = memoized_contract(positive(require_object_field(value, "dimensions"), kStructuredStateCapacity, "dimensions"),
          parse_type(field(value, "result_type")),
          positive(require_object_field(value, "requests"), kStructuredRequestCapacity, "requests"));
    } else throw std::invalid_argument("unknown structured primitive family: " + family);
    for (std::uint32_t id = 0; id < out_.structured_.size(); ++id)
      if (out_.structured_[id].key == contract.key) return id;
    const auto id = static_cast<std::uint32_t>(out_.structured_.size());
    out_.structured_.push_back(std::move(contract)); return id;
  }
  std::uint32_t expression(const Json& value, const Scope& environment, unsigned depth,
      const TemplateContext* context = nullptr, bool fixed_region = true) {
    if (depth >= 256 || out_.expressions_.size() >= 65536)
      throw std::invalid_argument("grammar expression capacity exceeded");
    if (value.kind != Kind::Object) throw std::invalid_argument("expression must be an object");
    CompiledExpression node;
    node.context = intern_context(environment);
    if (context && fixed_region) { node.template_id = context->id; node.fixed = true; }
    if (value.object_v.count("template")) {
      keys(value, {"template", "holes"}, "template invocation");
      const auto found = template_ids_.find(field(value, "template"));
      if (found == template_ids_.end()) throw std::invalid_argument("unknown template");
      return instantiate(found->second, &require_object_field(value, "holes"), environment, depth + 1, context);
    } else if (value.object_v.count("hole")) {
      keys(value, {"hole"}, "template hole expression");
      if (!context) throw std::invalid_argument("hole expression outside template body");
      const auto& holes = out_.templates_[context->id].holes;
      const auto name = field(value, "hole");
      const auto found = std::find_if(holes.begin(), holes.end(), [&](const auto& hole) { return hole.stable_id == name; });
      if (found == holes.end()) throw std::invalid_argument("unknown template hole: " + name);
      ++context->uses->at(found->id);
      node.kind = ExpressionKind::Hole; node.target = found->id; node.type = found->type; node.fixed = false;
      node.category = found->category;
      node.template_id = context->id;
      node.scope_mapping = map_scope(found->scope, environment);
      if (context->arguments) {
        const auto child = expression(context->arguments->object_v.at(name), found->scope, depth + 1,
            context->argument_context, false);
        if (out_.expressions_[child].type != found->type || out_.expressions_[child].category != found->category)
          throw std::invalid_argument("template hole result type mismatch");
        node.children.push_back(child);
      }
    } else if (value.object_v.count("constant")) {
      keys(value, {"constant"}, "constant expression");
      node.kind = ExpressionKind::Constant;
      node.target = static_cast<std::uint32_t>(out_.constants_.size());
      out_.constants_.push_back(parse_constant_domain(value.object_v.at("constant")));
      node.type = out_.constants_.back().type;
      if (context && fixed_region && (out_.constants_.back().integer_range || out_.constants_.back().values.size() != 1))
        throw std::invalid_argument("fixed template constant requires exactly one value");
    } else if (value.object_v.count("ref")) {
      if (context && fixed_region) throw std::invalid_argument("evolvable template reference must occupy a declared hole");
      keys(value, {"ref"}, "nonterminal reference");
      node.kind = ExpressionKind::Reference; node.target = reference(field(value, "ref"));
      const auto& target = out_.nonterminals_[node.target]; node.type = target.type;
      node.category = target.category;
      node.scope_mapping = map_scope(target.scope, environment);
    } else if (value.object_v.count("local")) {
      keys(value, {"local"}, "local expression");
      node.kind = ExpressionKind::Local; node.target = binding(out_.locals_, field(value, "local"));
      node.type = out_.locals_[node.target].type;
    } else if (value.object_v.count("control")) {
      keys(value, {"control", "type", "args", "name"}, "control expression");
      const auto& signature = PrimitiveCatalog::standard().resolve_control(field(value, "control"));
      node.kind = ExpressionKind::Control; node.target = signature.id; node.category = signature.result;
      node.type = parse_type(field(value, "type"));
      if (signature.requires_name) {
        node.local = binding(out_.locals_, field(value, "name"));
        if (out_.locals_[node.local].type != signature.arguments.front().value_type)
          throw std::invalid_argument("control target local type mismatch");
      } else if (value.object_v.count("name")) throw std::invalid_argument("control does not accept a target name");
      if (signature.lowering_node == NodeKind::RETURN && signature.arguments.front().value_type != node.type)
        throw std::invalid_argument("return type disagrees with enclosing result contract");
      const auto& arguments = array(require_object_field(value, "args"));
      if (arguments.size() != signature.arguments.size()) throw std::invalid_argument("control argument count mismatch");
      for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto child = expression(arguments[i], environment, depth + 1, context, fixed_region);
        const auto& compiled = out_.expressions_[child];
        const auto& required = signature.arguments[i];
        const auto expected_type = required.category == NodeCategory::Expression ? required.value_type : node.type;
        if (compiled.category != required.category || compiled.type != expected_type)
          throw std::invalid_argument("control argument category or type mismatch");
        node.children.push_back(child);
      }
    } else if (value.object_v.count("bound") || value.object_v.count("input")) {
      const bool input = value.object_v.count("input") != 0;
      const char* name = input ? "input" : "bound";
      keys(value, {name}, "binding expression");
      const auto& bindings = input ? out_.inputs_ : environment;
      node.kind = input ? ExpressionKind::Input : ExpressionKind::Bound;
      node.target = binding(bindings, field(value, name)); node.type = bindings[node.target].type;
    } else {
      PrimitiveSignature signature;
      if (value.object_v.count("structured")) {
        keys(value, {"structured", "args", "bind"}, "structured expression");
        node.kind = ExpressionKind::Structured; node.target = structured(value.object_v.at("structured"));
        const auto& contract = out_.structured_[node.target];
        signature.arguments = contract.arguments; signature.regions = contract.regions; signature.result = contract.result;
      } else {
        keys(value, {"signature", "args", "bind"}, "primitive expression");
        signature = PrimitiveCatalog::standard().resolve(field(value, "signature"));
        node.kind = ExpressionKind::Primitive; node.target = signature.id;
      }
      if (signature.operation == "constant" || signature.operation == "input" || signature.operation == "bound")
        throw std::invalid_argument("leaf primitive requires a domain or binding expression");
      node.type = signature.result;
      const auto& arguments = array(require_object_field(value, "args"));
      if (arguments.size() != signature.arguments.size()) throw std::invalid_argument("primitive argument count mismatch");
      std::map<std::uint32_t, Scope> local;
      if (value.object_v.count("bind")) {
        const auto& bindings = value.object_v.at("bind");
        if (bindings.kind != Kind::Object) throw std::invalid_argument("bind must be an object keyed by region argument");
        for (const auto& item : bindings.object_v) {
          const auto region = std::find_if(signature.regions.begin(), signature.regions.end(), [&](const auto& slot) {
            return std::to_string(slot.argument) == item.first;
          });
          if (region == signature.regions.end()) throw std::invalid_argument("binding names target a non-region argument");
          const auto& names = array(item.second);
          if (names.size() != region->bindings.size()) throw std::invalid_argument("region binding arity mismatch");
          Scope body = environment; RegionSlot renamed{region->argument, {}};
          for (std::size_t i = 0; i < names.size(); ++i) {
            const auto name = require_string(names[i], "region binding name");
            add_binding(body, name, region->bindings[i].type);
            renamed.bindings.push_back({name, region->bindings[i].type});
          }
          local.emplace(region->argument, std::move(body)); node.regions.push_back(std::move(renamed));
        }
      }
      if (local.size() != signature.regions.size()) throw std::invalid_argument("primitive requires explicit region binding names");
      for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto found = local.find(static_cast<std::uint32_t>(i));
        const auto child = expression(arguments[i], found == local.end() ? environment : found->second, depth + 1, context, fixed_region);
        if (out_.expressions_[child].type != signature.arguments[i] || out_.expressions_[child].category != NodeCategory::Expression)
          throw std::invalid_argument("primitive argument type mismatch");
        node.children.push_back(child);
      }
    }
    return append(std::move(node));
  }
  std::uint32_t cost(std::uint32_t id, std::uint32_t depth) const {
    const auto& node = out_.expressions_[id];
    if (node.kind == ExpressionKind::Reference) return out_.nonterminals_[node.target].minimum_nodes_by_depth[depth];
    if (node.kind == ExpressionKind::Template || node.kind == ExpressionKind::Hole)
      return node.children.empty() ? kNoGrammarId : cost(node.children.front(), depth);
    if (!depth) return kNoGrammarId;
    std::uint64_t result = 1;
    for (auto child : node.children) {
      const auto nodes = cost(child, depth - 1);
      if (nodes == kNoGrammarId) return kNoGrammarId;
      result += nodes;
      if (result > out_.search_limits_.max_nodes) return kNoGrammarId;
    }
    return static_cast<std::uint32_t>(result);
  }
  void costs() {
    const auto depths = out_.search_limits_.max_depth;
    for (auto& nt : out_.nonterminals_) nt.minimum_nodes_by_depth.assign(depths + 1, kNoGrammarId);
    for (auto& production : out_.productions_) production.minimum_nodes_by_depth.assign(depths + 1, kNoGrammarId);
    for (std::uint32_t depth = 1; depth <= depths; ++depth) {
      bool changed;
      do {
        changed = false;
        for (auto& production : out_.productions_) {
          const auto nodes = cost(production.expression, depth);
          production.minimum_nodes_by_depth[depth] = nodes;
          auto& target = out_.nonterminals_[production.nonterminal].minimum_nodes_by_depth[depth];
          if (nodes < target) { target = nodes; changed = true; }
        }
      } while (changed);
    }
    for (auto& nt : out_.nonterminals_) {
      for (std::uint32_t depth = 1; depth <= depths; ++depth) {
        const auto nodes = nt.minimum_nodes_by_depth[depth];
        if (nodes != kNoGrammarId && nt.minimum_depth == kNoGrammarId) nt.minimum_depth = depth;
        nt.minimum_nodes = std::min(nt.minimum_nodes, nodes);
      }
      if (nt.minimum_nodes == kNoGrammarId)
        throw std::invalid_argument("unproductive nonterminal or no derivation within search budget: " + nt.stable_id);
    }
  }
  CompiledGrammar out_;
  std::map<std::string, std::uint32_t> ids_;
  std::map<std::string, std::uint32_t> template_ids_;
  std::vector<const Json*> template_bodies_;
  std::set<std::uint32_t> active_templates_;
};

CompiledGrammar compile_grammar(const ResolvedDefinition& definition) { return GrammarCompiler{}.compile(definition); }
const std::vector<std::uint32_t>& CompiledGrammar::productions_for_type(RType type) const { return by_type_[type_index(type)]; }
const std::vector<std::uint32_t>& CompiledGrammar::productions_for_category(NodeCategory category) const {
  return by_category_[category_index(category)];
}
void CompiledGrammar::require_executable() const {
  for (const auto& expression : expressions_) {
    if (expression.kind == ExpressionKind::Primitive) PrimitiveCatalog::standard().require_executable(expression.target);
    if (expression.kind == ExpressionKind::Structured) require_structured_execution(structured_[expression.target]);
  }
}

}  // namespace gagp::evo::grammar
