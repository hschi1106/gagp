#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/budget.hpp"
#include "gagp/evolution/fuel_events.hpp"
#include "gagp/serialization/region_plan_json.hpp"

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
std::uint32_t nonnegative(const Json& value, std::uint32_t maximum,
                          const char* name) {
  if (value.kind != Kind::Number || !std::isfinite(value.number_v) ||
      value.number_v < 0 || value.number_v > maximum ||
      std::floor(value.number_v) != value.number_v)
    throw std::invalid_argument(std::string(name) +
                                " must be a nonnegative integer within capacity");
  return static_cast<std::uint32_t>(value.number_v);
}

std::vector<FuelCharge> fuel_charges(const Json& expression, NodeKind owner) {
  const auto found = expression.object_v.find("fuel_events");
  if (found == expression.object_v.end()) return {};
  const Json& profile = found->second;
  if (profile.kind != Kind::Object || profile.object_v.empty())
    throw std::invalid_argument("fuel_events must be a nonempty object");
  std::vector<FuelCharge> charges;
  charges.reserve(profile.object_v.size());
  for (const auto& item : profile.object_v) {
    FuelEvent event;
    if (!parse_fuel_event(item.first, &event))
      throw std::invalid_argument("unknown fuel event: " + item.first);
    const auto cost = nonnegative(item.second,
        static_cast<std::uint32_t>(std::numeric_limits<int>::max()),
        "fuel event cost");
    if (!supports_fuel_event(owner, event))
      throw std::invalid_argument("fuel event is not supported by the owning node kind: " +
                                  item.first);
    charges.push_back({event, cost});
  }
  std::sort(charges.begin(), charges.end(), [](const FuelCharge& left,
                                               const FuelCharge& right) {
    return static_cast<int>(left.event) < static_cast<int>(right.event);
  });
  return charges;
}
RType region_type(ValueTag type) {
  switch (type) {
    case ValueTag::Int: return RType::Int;
    case ValueTag::Float: return RType::Float;
    case ValueTag::Bool: return RType::Bool;
    case ValueTag::Char: return RType::Char;
    case ValueTag::String: return RType::String;
    case ValueTag::IntList: return RType::IntList;
    case ValueTag::FloatList: return RType::FloatList;
    case ValueTag::StringList: return RType::StringList;
    default: throw std::invalid_argument("bounded region requires an exact public type");
  }
}
RegionSlotBank region_bank(const std::string& name) {
  if (name == "state") return RegionSlotBank::State;
  if (name == "parameter") return RegionSlotBank::Parameter;
  if (name == "prepared") return RegionSlotBank::Prepared;
  if (name == "result") return RegionSlotBank::Result;
  if (name == "measure") return RegionSlotBank::Measure;
  throw std::invalid_argument("unknown bounded region slot bank: " + name);
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
      keys(rule, {"id", "type", "scope", "alternatives", "category", "variation", "mutation_entry", "mutation_locals"}, "nonterminal");
      CompiledNonterminal nt;
      nt.id = static_cast<std::uint32_t>(out_.nonterminals_.size());
      nt.stable_id = field(rule, "id"); nt.type = parse_type(field(rule, "type"));
      nt.category = category(rule);
      const auto variation = rule.object_v.find("variation");
      if (variation != rule.object_v.end()) {
        if (variation->second.kind != Kind::Bool)
          throw std::invalid_argument("nonterminal variation must be Boolean");
        nt.variation_enabled = variation->second.bool_v;
      }
      nt.scope = scope(require_object_field(rule, "scope"));
      nt.context = intern_context(nt.scope);
      if (rule.object_v.count("mutation_locals")) {
        nt.mutation_locals = scope(rule.object_v.at("mutation_locals"));
        for (const auto& required : nt.mutation_locals) {
          const auto index = binding(out_.locals_, required.name);
          if (out_.locals_[index].type != required.type)
            throw std::invalid_argument("mutation_locals requires a declared local of the exact type");
        }
      }
      ids_.emplace(nt.stable_id, nt.id); out_.nonterminals_.push_back(std::move(nt));
    }
    for (std::size_t i = 0; i < rules.size(); ++i) {
      if (!rules[i].object_v.count("mutation_entry")) continue;
      auto& nt = out_.nonterminals_[i];
      nt.mutation_entry = reference(field(rules[i], "mutation_entry"));
      const auto& donor = out_.nonterminals_[nt.mutation_entry];
      if (donor.type != nt.type || donor.category != nt.category || donor.context != nt.context)
        throw std::invalid_argument("mutation_entry requires identical type, category and ordered scope");
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
        keys(alternative, {"id", "weight", "expression", "generation_stages", "crossover_group", "crossover_scope", "variation"}, "alternative");
        const auto& weight = require_object_field(alternative, "weight");
        if (weight.kind != Kind::Number || !std::isfinite(weight.number_v) || weight.number_v <= 0)
          throw std::invalid_argument("production weight must be positive and finite");
        CompiledProduction production;
        production.id = static_cast<std::uint32_t>(out_.productions_.size());
        production.stable_id = nt.stable_id + "/" + field(alternative, "id");
        production.nonterminal = nt.id; production.weight = weight.number_v;
        if (alternative.object_v.count("crossover_group"))
          production.crossover_group = field(alternative, "crossover_group");
        const auto variation = alternative.object_v.find("variation");
        if (variation != alternative.object_v.end()) {
          production.variation_enabled = variation->second.kind != Json::Kind::Bool || variation->second.bool_v;
          production.unbound_variation = variation->second.kind == Json::Kind::String;
        }
        production.closed_crossover = alternative.object_v.count("crossover_scope") &&
            field(alternative, "crossover_scope") == "closed";
        production.generation_mask = production_generation_mask(alternative);
        production.expression = expression(require_object_field(alternative, "expression"), nt.scope, 0);
        if (out_.expressions_[production.expression].type != nt.type || out_.expressions_[production.expression].category != nt.category)
          throw std::invalid_argument("production result type mismatch: " + production.stable_id);
        nt.productions.push_back(production.id);
        if (nt.category == NodeCategory::Expression) out_.by_type_[type_index(nt.type)].push_back(production.id);
        out_.by_category_[category_index(nt.category)].push_back(production.id);
        out_.productions_.push_back(std::move(production));
      }
    }
    compile_replacement_classes(rules);
    validate_bounded_phase_closure();
    costs();
    index_contexts();
    return std::move(out_);
  }

 private:
  // A deliberately conservative syntactic language certificate. References
  // use proved recursive classes, templates retain their checked hole linkage,
  // and fuel/resource annotations remain part of the contract. Sampling and
  // constant perturbation policies do not change membership. This is not a
  // general grammar-equivalence solver or an author-supplied assertion.
  static void membership_domain(Json& value) {
    value.object_v.erase("sample_from");
    value.object_v.erase("mutation");
    if (value.object_v.count("elements")) membership_domain(value.object_v.at("elements"));
  }
  static void membership_expression(Json& value) {
    if (value.kind == Kind::Object) {
      if (value.object_v.count("constant")) membership_domain(value.object_v.at("constant"));
      for (auto& item : value.object_v) membership_expression(item.second);
    } else if (value.kind == Kind::Array) {
      for (auto& item : value.array_v) membership_expression(item);
    }
  }
  void rewrite_references(Json& value, const std::vector<std::uint32_t>& classes) const {
    if (value.kind == Kind::Object) {
      const auto ref = value.object_v.find("ref");
      if (ref != value.object_v.end() && ref->second.kind == Kind::String)
        ref->second.string_v = "@" + std::to_string(classes.at(reference(ref->second.string_v)));
      for (auto& item : value.object_v) rewrite_references(item.second, classes);
    } else if (value.kind == Kind::Array) {
      for (auto& item : value.array_v) rewrite_references(item, classes);
    }
  }
  void compile_replacement_classes(const std::vector<Json>& rules) {
    std::vector<std::vector<Json>> expressions(rules.size());
    for (std::size_t i = 0; i < rules.size(); ++i)
      for (const auto& alternative : array(require_object_field(rules[i], "alternatives"))) {
        auto expression = require_object_field(alternative, "expression");
        membership_expression(expression);
        expressions[i].push_back(std::move(expression));
      }
    // Monotone partition refinement proves recursive rule bisimulation. It
    // ignores rule names and alternative order/weights, but preserves every
    // expression field, ordered binding interface and shared-hole relation.
    std::vector<std::uint32_t> classes(rules.size(), 0);
    std::size_t previous_count = 1;
    for (;;) {
      std::map<std::string, std::uint32_t> partitions;
      auto next = classes;
      for (std::size_t i = 0; i < rules.size(); ++i) {
        std::set<std::string> language;
        for (auto expression : expressions[i]) {
          rewrite_references(expression, classes);
          language.insert(canonical_json(expression));
        }
        const auto& nt = out_.nonterminals_[i];
        std::string key = std::to_string(classes[i]) + ":" + std::to_string(int(nt.type)) + ":" +
            std::to_string(int(nt.category)) + ":" + canonical_json(require_object_field(rules[i], "scope"));
        for (const auto& expression : language)
          key += std::to_string(expression.size()) + ":" + expression;
        next[i] = partitions.emplace(key, static_cast<std::uint32_t>(partitions.size())).first->second;
      }
      classes = std::move(next);
      if (partitions.size() == previous_count) break;
      previous_count = partitions.size();
    }
    std::map<std::string, std::uint32_t> closed;
    for (std::size_t i = 0; i < rules.size(); ++i)
      for (std::size_t j = 0; j < expressions[i].size(); ++j) {
        auto expression = expressions[i][j];
        rewrite_references(expression, classes);
        auto& production = out_.productions_[out_.nonterminals_[i].productions[j]];
        production.replacement_class = classes[i];
        production.closed_replacement_class = closed.emplace(canonical_json(expression),
            static_cast<std::uint32_t>(closed.size())).first->second;
      }
  }
  void validate_bounded_phase_closure() const {
    const auto validate_root = [&](std::uint32_t root) {
      std::vector<std::uint32_t> pending = {root};
      std::vector<bool> expressions(out_.expressions_.size(), false);
      std::vector<bool> nonterminals(out_.nonterminals_.size(), false);
      while (!pending.empty()) {
        const std::uint32_t id = pending.back();
        pending.pop_back();
        if (expressions.at(id)) continue;
        expressions[id] = true;
        const CompiledExpression& expression = out_.expressions_[id];
        if (expression.kind == ExpressionKind::Input ||
            expression.kind == ExpressionKind::Local) {
          throw std::invalid_argument(
              "bounded region phase must use an explicit parameter binding");
        }
        if (expression.kind == ExpressionKind::Structured) {
          throw std::invalid_argument(
              "bounded region phase cannot contain a structured expression");
        }
        if (expression.kind == ExpressionKind::Hole &&
            expression.children.empty()) {
          throw std::invalid_argument(
              "bounded region phase contains an unresolved template hole");
        }
        if (expression.kind == ExpressionKind::Reference) {
          if (nonterminals.at(expression.target)) continue;
          nonterminals[expression.target] = true;
          for (std::uint32_t production :
               out_.nonterminals_[expression.target].productions) {
            pending.push_back(out_.productions_[production].expression);
          }
        }
        pending.insert(pending.end(), expression.children.begin(),
                       expression.children.end());
      }
    };
    std::vector<bool> reachable(out_.expressions_.size(), false);
    std::vector<std::uint32_t> pending;
    for (const CompiledProduction& production : out_.productions_)
      pending.push_back(production.expression);
    while (!pending.empty()) {
      const std::uint32_t id = pending.back();
      pending.pop_back();
      if (reachable.at(id)) continue;
      reachable[id] = true;
      const CompiledExpression& expression = out_.expressions_[id];
      pending.insert(pending.end(), expression.children.begin(),
                     expression.children.end());
      if (expression.kind == ExpressionKind::Reference) {
        for (std::uint32_t production :
             out_.nonterminals_.at(expression.target).productions) {
          pending.push_back(out_.productions_.at(production).expression);
        }
      }
    }
    for (std::size_t id = 0; id < out_.expressions_.size(); ++id) {
      if (!reachable[id]) continue;
      const CompiledExpression& expression = out_.expressions_[id];
      if (expression.kind != ExpressionKind::Structured) continue;
      const StructuredContract& contract = out_.structured_.at(expression.target);
      if (contract.family != StructuredFamily::BoundedRegion) continue;
      for (const CompiledRegionPhase& phase : expression.phases) {
        if (phase.argument >= expression.children.size())
          throw std::logic_error("compiled bounded phase argument is out of range");
        validate_root(expression.children[phase.argument]);
      }
    }
  }

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
      if (!count || count > 64)
        throw std::invalid_argument("each template hole must occur 1..64 times; split independent holes explicitly");
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
    } else if (family == "bounded") {
      keys(value, {"family", "plan"}, "bounded region contract");
      try {
        contract = bounded_contract(serialization::decode_region_plan(
            require_object_field(value, "plan")));
      } catch (const std::exception& error) {
        throw std::invalid_argument(std::string("invalid bounded region plan: ") +
                                    error.what());
      }
      contract.key = "bounded:" + canonical_json(
          serialization::encode_region_plan(*contract.plan));
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
      keys(value, {"constant", "fuel_events", "resource_charge", "mutable"}, "constant expression");
      if (value.object_v.count("mutable")) {
        if (!context || !fixed_region || value.object_v.at("mutable").kind != Kind::Bool)
          throw std::invalid_argument("mutable is only supported on a fixed template constant");
        if (value.object_v.at("mutable").bool_v) node.fixed = false;
      }
      node.kind = ExpressionKind::Constant;
      node.target = static_cast<std::uint32_t>(out_.constants_.size());
      out_.constants_.push_back(parse_constant_domain(value.object_v.at("constant")));
      out_.constant_encodings_.emplace_back();
      const auto& domain_json = value.object_v.at("constant");
      if (!out_.constants_.back().integer_range && !out_.constants_.back().float_range && !out_.constants_.back().elements) {
        Json singleton = domain_json;
        // Variation policy belongs to grammar identity, never value identity.
        singleton.object_v.erase("mutation");
        singleton.object_v.at("values").array_v.clear();
        for (const auto& item : domain_json.object_v.at("values").array_v) {
          singleton.object_v.at("values").array_v = {item};
          out_.constant_encodings_.back().insert(canonical_json(singleton));
        }
      }
      node.type = out_.constants_.back().type;
      node.fuel_charges = fuel_charges(value, NodeKind::CONST);
      if (node.fixed && (out_.constants_.back().integer_range || out_.constants_.back().float_range || out_.constants_.back().elements || out_.constants_.back().values.size() != 1))
        throw std::invalid_argument("fixed template constant requires exactly one value");
    } else if (value.object_v.count("ref")) {
      if (context && fixed_region) throw std::invalid_argument("evolvable template reference must occupy a declared hole");
      keys(value, {"ref"}, "nonterminal reference");
      node.kind = ExpressionKind::Reference; node.target = reference(field(value, "ref"));
      const auto& target = out_.nonterminals_[node.target]; node.type = target.type;
      node.category = target.category;
      node.scope_mapping = map_scope(target.scope, environment);
    } else if (value.object_v.count("local")) {
      keys(value, {"local", "fuel_events", "resource_charge"}, "local expression");
      node.kind = ExpressionKind::Local; node.target = binding(out_.locals_, field(value, "local"));
      node.type = out_.locals_[node.target].type;
      node.fuel_charges = fuel_charges(value, NodeKind::VAR);
    } else if (value.object_v.count("control")) {
      keys(value, {"control", "type", "args", "name", "input_name", "fuel_events", "resource_charge"}, "control expression");
      const auto& signature = PrimitiveCatalog::standard().resolve_control(field(value, "control"));
      node.kind = ExpressionKind::Control; node.target = signature.id; node.category = signature.result;
      node.type = parse_type(field(value, "type"));
      node.fuel_charges = fuel_charges(value, signature.lowering_node);
      if (signature.requires_name) {
        if (value.object_v.count("name") + value.object_v.count("input_name") != 1)
          throw std::invalid_argument("control requires exactly one name or input_name target");
        node.target_input = value.object_v.count("input_name") != 0;
        const auto& targets = node.target_input ? out_.inputs_ : out_.locals_;
        node.local = binding(targets, field(value, node.target_input ? "input_name" : "name"));
        if (targets[node.local].type != signature.arguments.front().value_type)
          throw std::invalid_argument("control target local type mismatch");
      } else if (value.object_v.count("name") || value.object_v.count("input_name"))
        throw std::invalid_argument("control does not accept a target name");
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
      keys(value, {name, "fuel_events", "resource_charge"}, "binding expression");
      const auto& bindings = input ? out_.inputs_ : environment;
      node.kind = input ? ExpressionKind::Input : ExpressionKind::Bound;
      node.target = binding(bindings, field(value, name)); node.type = bindings[node.target].type;
      node.fuel_charges = fuel_charges(value, input ? NodeKind::VAR : NodeKind::REGION_VAR);
    } else {
      PrimitiveSignature signature;
      bool bounded = false;
      if (value.object_v.count("structured") &&
          field(value.object_v.at("structured"), "family") == "bounded") {
        keys(value, {"structured", "captures", "phases", "args", "fuel_events", "resource_charge"},
             "bounded structured expression");
        node.kind = ExpressionKind::Structured;
        node.target = structured(value.object_v.at("structured"));
        const auto& contract = out_.structured_[node.target];
        const RegionPlan& plan = *contract.plan;
        signature.arguments = contract.arguments;
        signature.result = contract.result;
        bounded = true;
        node.fuel_charges = fuel_charges(value, NodeKind::BOUNDED_REGION);

        const auto& captures = array(require_object_field(value, "captures"));
        if (captures.size() != plan.parameter_types.size())
          throw std::invalid_argument("bounded region capture count must match plan parameters");
        std::set<std::pair<int, std::uint32_t>> unique_captures;
        for (std::size_t i = 0; i < captures.size(); ++i) {
          const Json& capture = captures[i];
          if (capture.kind != Kind::Object || capture.object_v.size() != 1)
            throw std::invalid_argument("bounded region capture must contain exactly one reference");
          CompiledRegionCapture compiled;
          RType actual = RType::Invalid;
          if (capture.object_v.count("input")) {
            keys(capture, {"input"}, "bounded region capture");
            compiled.kind = CompiledCaptureKind::Input;
            compiled.target = binding(out_.inputs_, field(capture, "input"));
            actual = out_.inputs_[compiled.target].type;
          } else if (capture.object_v.count("local")) {
            keys(capture, {"local"}, "bounded region capture");
            compiled.kind = CompiledCaptureKind::Local;
            compiled.target = binding(out_.locals_, field(capture, "local"));
            actual = out_.locals_[compiled.target].type;
          } else if (capture.object_v.count("bound")) {
            keys(capture, {"bound"}, "bounded region capture");
            compiled.kind = CompiledCaptureKind::Bound;
            compiled.target = binding(environment, field(capture, "bound"));
            actual = environment[compiled.target].type;
          } else {
            throw std::invalid_argument("unknown bounded region capture reference");
          }
          if (actual != region_type(plan.parameter_types[i]))
            throw std::invalid_argument("bounded region capture type mismatch");
          const auto identity = std::make_pair(static_cast<int>(compiled.kind),
                                               compiled.target);
          if (!unique_captures.insert(identity).second)
            throw std::invalid_argument("duplicate bounded region capture");
          node.captures.push_back(compiled);
        }

        const std::size_t phase_count = bounded_region_arity(plan) -
            plan.state_types.size() - plan.bound_operand_count;
        const auto& phases = array(require_object_field(value, "phases"));
        if (phases.size() != phase_count)
          throw std::invalid_argument("bounded region must declare every phase");
        for (std::size_t ordinal = 0; ordinal < phases.size(); ++ordinal) {
          const Json& phase = phases[ordinal];
          keys(phase, {"argument", "bindings"}, "bounded region phase");
          const std::uint32_t expected_argument = static_cast<std::uint32_t>(
              plan.state_types.size() + plan.bound_operand_count + ordinal);
          const std::uint32_t argument = nonnegative(
              require_object_field(phase, "argument"),
              static_cast<std::uint32_t>(kBoundedRegionArityCapacity - 1),
              "bounded region phase argument");
          if (argument != expected_argument)
            throw std::invalid_argument("bounded region phase argument is not canonical");
          const auto& bindings = array(require_object_field(phase, "bindings"));
          if (bindings.size() > kBoundedRegionPhaseBindingCapacity)
            throw std::invalid_argument("bounded region phase binding capacity exceeded");
          Scope phase_scope;
          CompiledRegionPhase compiled_phase;
          compiled_phase.argument = argument;
          std::set<std::pair<int, std::uint32_t>> sources;
          for (const Json& item : bindings) {
            keys(item, {"bank", "slot", "name"}, "bounded region phase binding");
            RegionValueSlot source;
            source.bank = region_bank(field(item, "bank"));
            source.slot = nonnegative(require_object_field(item, "slot"),
                                      std::numeric_limits<std::uint32_t>::max(),
                                      "bounded region binding slot");
            const auto identity = std::make_pair(static_cast<int>(source.bank),
                                                 source.slot);
            if (!sources.insert(identity).second)
              throw std::invalid_argument("duplicate bounded region phase source");
            const RegionPhaseKind phase_kind =
                bounded_region_phase_kind(plan, ordinal);
            const std::uint32_t preparation =
                bounded_region_preparation_ordinal(plan, ordinal);
            RType type;
            try {
              type = region_type(region_slot_type(plan, phase_kind, source,
                                                  preparation));
            } catch (const std::exception& error) {
              throw std::invalid_argument(
                  std::string("invalid bounded region phase source: ") +
                  error.what());
            }
            add_binding(phase_scope, field(item, "name"), type);
            compiled_phase.sources.push_back(source);
          }
          node.regions.push_back({argument, std::move(phase_scope)});
          node.phases.push_back(std::move(compiled_phase));
        }
      } else if (value.object_v.count("structured")) {
        keys(value, {"structured", "args", "bind"}, "structured expression");
        node.kind = ExpressionKind::Structured; node.target = structured(value.object_v.at("structured"));
        const auto& contract = out_.structured_[node.target];
        signature.arguments = contract.arguments; signature.regions = contract.regions; signature.result = contract.result;
      } else {
        keys(value, {"signature", "args", "bind", "fuel_events", "resource_charge"}, "primitive expression");
        signature = PrimitiveCatalog::standard().resolve(field(value, "signature"));
        if (value.object_v.count("resource_charge") && !signature.lowering_node)
          throw std::invalid_argument("resource_charge requires a concrete materialized owner");
        node.kind = ExpressionKind::Primitive; node.target = signature.id;
        if (value.object_v.count("fuel_events")) {
          if (!signature.lowering_node)
            throw std::invalid_argument("fuel_events requires a concrete materialized owner");
          node.fuel_charges = fuel_charges(value, *signature.lowering_node);
        }
      }
      if (signature.operation == "constant" || signature.operation == "input" || signature.operation == "bound")
        throw std::invalid_argument("leaf primitive requires a domain or binding expression");
      node.type = signature.result;
      const auto& arguments = array(require_object_field(value, "args"));
      if (arguments.size() != signature.arguments.size()) throw std::invalid_argument("primitive argument count mismatch");
      std::map<std::uint32_t, Scope> local;
      if (!bounded && value.object_v.count("bind")) {
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
      if (!bounded && local.size() != signature.regions.size()) throw std::invalid_argument("primitive requires explicit region binding names");
      if (bounded) {
        for (const RegionSlot& region : node.regions)
          local.emplace(region.argument, region.bindings);
      }
      for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto found = local.find(static_cast<std::uint32_t>(i));
        const auto child = expression(arguments[i], found == local.end() ? environment : found->second, depth + 1, context, fixed_region);
        if (out_.expressions_[child].type != signature.arguments[i] || out_.expressions_[child].category != NodeCategory::Expression)
          throw std::invalid_argument("primitive argument type mismatch");
        node.children.push_back(child);
      }
    }
    if (value.object_v.count("resource_charge")) {
      node.resource_charge = parse_resource_charge(value.object_v.at("resource_charge"));
    }
    return append(std::move(node));
  }
  std::uint32_t cost(std::uint32_t id, std::uint32_t depth) const {
    return minimum_expression_nodes(out_, id, depth);
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
    if (std::any_of(out_.productions_.begin(), out_.productions_.end(),
        [](const auto& production) { return production.generation_mask != 3; })) {
      for (auto stage : {GenerationStage::Initial, GenerationStage::Mutation}) {
        const auto index = static_cast<std::size_t>(stage);
        for (auto& nt : out_.nonterminals_)
          nt.generation_minimum_nodes[index].assign(depths + 1, kNoGrammarId);
        for (auto& production : out_.productions_)
          production.generation_minimum_nodes[index].assign(depths + 1, kNoGrammarId);
        for (std::uint32_t depth = 1; depth <= depths; ++depth) {
          bool changed;
          do {
            changed = false;
            for (auto& production : out_.productions_) {
              if (!production.generates(stage)) continue;
              const auto nodes = minimum_expression_nodes(out_, production.expression, depth, {},
                  [&](std::uint32_t nt, std::uint32_t d) {
                    return out_.nonterminals_[nt].generation_minimum_nodes[index][d];
                  });
              production.generation_minimum_nodes[index][depth] = nodes;
              auto& target = out_.nonterminals_[production.nonterminal].generation_minimum_nodes[index][depth];
              if (nodes < target) { target = nodes; changed = true; }
            }
          } while (changed);
        }
      }
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
bool CompiledGrammar::generates_payload(std::uint32_t nonterminal) const {
  if (nonterminal >= nonterminals_.size()) throw std::invalid_argument("unknown construction nonterminal");
  std::lock_guard<std::mutex> lock(executable_cache_->mutex);
  const auto cached = executable_cache_->payload_roots.find(nonterminal);
  if (cached != executable_cache_->payload_roots.end()) return cached->second;
  std::vector<std::uint32_t> pending;
  std::vector<bool> seen_nt(nonterminals_.size()), seen_expression(expressions_.size());
  const auto enqueue = [&](std::uint32_t nt) {
    if (seen_nt[nt]) return;
    seen_nt[nt] = true;
    for (auto production : nonterminals_[nt].productions)
      pending.push_back(productions_[production].expression);
  };
  enqueue(nonterminal);
  bool payload = false;
  while (!pending.empty() && !payload) {
    const auto id = pending.back(); pending.pop_back();
    if (seen_expression[id]) continue;
    seen_expression[id] = true;
    const auto& expression = expressions_[id];
    if (expression.kind == ExpressionKind::Constant) {
      const auto type = constants_[expression.target].type;
      payload = type == RType::String || type == RType::IntList ||
          type == RType::FloatList || type == RType::StringList;
    }
    if (expression.kind == ExpressionKind::Reference) enqueue(expression.target);
    pending.insert(pending.end(), expression.children.begin(), expression.children.end());
  }
  executable_cache_->payload_roots.emplace(nonterminal, payload);
  return payload;
}

void CompiledGrammar::require_executable() const { require_executable(entry_); }
void CompiledGrammar::require_executable(std::uint32_t nonterminal) const {
  if (nonterminal >= nonterminals_.size()) throw std::invalid_argument("unknown executable nonterminal");
  std::lock_guard<std::mutex> lock(executable_cache_->mutex);
  if (executable_cache_->verified_roots.count(nonterminal)) return;
  std::vector<std::uint32_t> pending;
  std::vector<bool> nonterminals(nonterminals_.size(), false), expressions(expressions_.size(), false);
  const auto enqueue = [&](std::uint32_t nt) {
    if (nonterminals[nt]) return;
    nonterminals[nt] = true;
    for (auto production : nonterminals_[nt].productions) pending.push_back(productions_[production].expression);
  };
  enqueue(nonterminal);
  while (!pending.empty()) {
    const auto id = pending.back(); pending.pop_back();
    if (expressions[id]) continue;
    expressions[id] = true;
    const auto& expression = expressions_[id];
    if (expression.kind == ExpressionKind::Primitive) PrimitiveCatalog::standard().require_executable(expression.target);
    if (expression.kind == ExpressionKind::Structured) require_structured_execution(structured_[expression.target]);
    if (expression.kind == ExpressionKind::Reference) enqueue(expression.target);
    pending.insert(pending.end(), expression.children.begin(), expression.children.end());
  }
  executable_cache_->verified_roots.insert(nonterminal);
}

}  // namespace gagp::evo::grammar
