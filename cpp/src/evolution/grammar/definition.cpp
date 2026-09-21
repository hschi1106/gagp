#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/evolution/grammar/identity.hpp"
#include "gagp/evolution/grammar/constants.hpp"
#include "gagp/evolution/fuel_events.hpp"
#include "gagp/serialization/region_plan_json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace gagp::evo::grammar {
namespace {
using Json = cli_detail::JsonValue;
using Kind = Json::Kind;
using cli_detail::require_object_field;
using cli_detail::require_string;

Json string(std::string value) { Json out; out.kind = Kind::String; out.string_v = std::move(value); return out; }
Json array() { Json out; out.kind = Kind::Array; return out; }
Json object() { Json out; out.kind = Kind::Object; return out; }

std::uint32_t source_uint32(const Json& value, const char* context) {
  if (value.kind != Kind::Number || !std::isfinite(value.number_v) ||
      value.number_v < 0 ||
      value.number_v > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
      std::floor(value.number_v) != value.number_v) {
    throw std::invalid_argument(std::string(context) +
                                " must be a uint32 integer");
  }
  return static_cast<std::uint32_t>(value.number_v);
}

void source_fuel_events(const Json& expression, NodeKind owner) {
  const auto found = expression.object_v.find("fuel_events");
  if (found == expression.object_v.end()) return;
  const Json& profile = found->second;
  if (profile.kind != Kind::Object || profile.object_v.empty())
    throw std::invalid_argument("fuel_events must be a nonempty object");
  for (const auto& item : profile.object_v) {
    FuelEvent event;
    if (!parse_fuel_event(item.first, &event))
      throw std::invalid_argument("unknown fuel event: " + item.first);
    (void)source_uint32(item.second, "fuel event cost");
    if (item.second.number_v >
        static_cast<double>(std::numeric_limits<int>::max()))
      throw std::invalid_argument("fuel event cost must be within 0..INT_MAX");
    if (!supports_fuel_event(owner, event))
      throw std::invalid_argument("fuel event is not supported by the owning node kind: " +
                                  item.first);
  }
}

RegionSlotBank source_region_bank(const std::string& name) {
  if (name == "state") return RegionSlotBank::State;
  if (name == "parameter") return RegionSlotBank::Parameter;
  if (name == "prepared") return RegionSlotBank::Prepared;
  if (name == "result") return RegionSlotBank::Result;
  if (name == "measure") return RegionSlotBank::Measure;
  throw std::invalid_argument("unknown bounded region slot bank: " + name);
}

void source_binding_identifier(const std::string& name) {
  const auto letter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  if (name.empty() || !letter(name.front()) ||
      !std::all_of(name.begin(), name.end(), [&](char c) {
        return letter(c) || (c >= '0' && c <= '9');
      })) {
    throw std::invalid_argument("invalid bounded region binding name");
  }
}

void keys(const Json& value, std::initializer_list<const char*> allowed, const char* context) {
  if (value.kind != Kind::Object) throw std::invalid_argument(std::string(context) + " must be an object");
  const std::set<std::string> names(allowed.begin(), allowed.end());
  for (const auto& entry : value.object_v)
    if (!names.count(entry.first)) throw std::invalid_argument(std::string(context) + ": unknown key " + entry.first);
}

const std::vector<Json>& elements(const Json& value) {
  if (value.kind != Kind::Array) throw std::invalid_argument("expected grammar array");
  return value.array_v;
}

std::string identifier(const Json& value) {
  const auto id = require_string(require_object_field(value, "id"), "id");
  const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
  if (id.empty() || !letter(id.front()) || !std::all_of(id.begin(), id.end(), [&](char c) {
        return letter(c) || (c >= '0' && c <= '9') || c == '.' || c == '-'; }))
    throw std::invalid_argument("invalid grammar identifier: " + id);
  return id;
}

void sort_ids(Json& value) {
  (void)elements(value);
  std::set<std::string> ids;
  for (const auto& item : value.array_v)
    if (!ids.insert(identifier(item)).second) throw std::invalid_argument("duplicate grammar ID: " + identifier(item));
  std::sort(value.array_v.begin(), value.array_v.end(), [](const Json& a, const Json& b) { return identifier(a) < identifier(b); });
}

// Validate source shape before overrides discard it. Resolution must not hide an
// unknown key or malformed constant in an imported/replaced definition.
void source_scope(const Json& value) {
  std::set<std::string> names;
  for (const auto& binding : elements(value)) {
    keys(binding, {"name", "type"}, "scope binding");
    const auto name = require_string(require_object_field(binding, "name"), "name");
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (name.empty() || !letter(name.front()) || !std::all_of(name.begin(), name.end(), [&](char c) {
          return letter(c) || (c >= '0' && c <= '9'); }) || !names.insert(name).second)
      throw std::invalid_argument("invalid/duplicate source scope name");
    (void)parse_type(require_string(require_object_field(binding, "type"), "type"));
  }
}
void source_category(const Json& value) {
  if (!value.object_v.count("category")) return;
  const auto category = require_string(value.object_v.at("category"), "category");
  if (category != "Expression" && category != "Program" && category != "Block" && category != "Statement")
    throw std::invalid_argument("unknown grammar syntax category: " + category);
}
void source_expression(const Json& value) {
  if (value.kind != Kind::Object) throw std::invalid_argument("expression must be an object");
  if (value.object_v.count("constant")) {
    keys(value, {"constant", "fuel_events"}, "constant expression");
    (void)parse_constant_domain(value.object_v.at("constant"));
    source_fuel_events(value, NodeKind::CONST);
  } else if (value.object_v.count("template")) {
    keys(value, {"template", "holes"}, "template invocation");
    (void)require_string(value.object_v.at("template"), "template");
    const auto& holes = require_object_field(value, "holes");
    if (holes.kind != Kind::Object) throw std::invalid_argument("template arguments must be an object");
    for (const auto& hole : holes.object_v) source_expression(hole.second);
  } else if (value.object_v.count("signature") || value.object_v.count("structured") || value.object_v.count("control")) {
    bool bounded = false;
    if (value.object_v.count("signature")) {
      keys(value, {"signature", "args", "bind", "fuel_events"}, "primitive expression");
      const auto& signature = PrimitiveCatalog::standard().resolve(
          require_string(value.object_v.at("signature"), "signature"));
      if (value.object_v.count("fuel_events")) {
        if (!signature.lowering_node)
          throw std::invalid_argument("fuel_events requires a concrete materialized owner");
        source_fuel_events(value, *signature.lowering_node);
      }
    } else if (value.object_v.count("control")) {
      keys(value, {"control", "type", "args", "name", "fuel_events"}, "control expression");
      const auto& signature = PrimitiveCatalog::standard().resolve_control(
          require_string(value.object_v.at("control"), "control"));
      source_fuel_events(value, signature.lowering_node);
      (void)parse_type(require_string(require_object_field(value, "type"), "type"));
    } else {
      const auto& contract = value.object_v.at("structured");
      const auto family = require_string(require_object_field(contract, "family"), "family");
      if (family == "recur") keys(contract, {"family", "state_types", "result_type", "requests"}, "recursive contract");
      else if (family == "memo") keys(contract, {"family", "dimensions", "result_type", "requests"}, "memoized contract");
      else if (family == "bounded") {
        bounded = true;
        keys(value, {"structured", "captures", "phases", "args", "fuel_events"},
             "bounded structured expression");
        source_fuel_events(value, NodeKind::BOUNDED_REGION);
        keys(contract, {"family", "plan"}, "bounded region contract");
        RegionPlan plan;
        try {
          plan = serialization::decode_region_plan(
              require_object_field(contract, "plan"));
        } catch (const std::exception& error) {
          throw std::invalid_argument(std::string("invalid bounded region plan: ") +
                                      error.what());
        }

        const auto& captures = elements(require_object_field(value, "captures"));
        if (captures.size() != plan.parameter_types.size())
          throw std::invalid_argument(
              "bounded region capture count must match plan parameters");
        std::set<std::pair<std::string, std::string>> capture_ids;
        for (const Json& capture : captures) {
          if (capture.kind != Kind::Object || capture.object_v.size() != 1)
            throw std::invalid_argument(
                "bounded region capture must contain exactly one reference");
          const auto& reference = *capture.object_v.begin();
          if (reference.first != "input" && reference.first != "local" &&
              reference.first != "bound")
            throw std::invalid_argument("unknown bounded region capture reference");
          const std::string name = require_string(reference.second,
                                                  "bounded region capture");
          if (!capture_ids.insert({reference.first, name}).second)
            throw std::invalid_argument("duplicate bounded region capture");
        }

        const std::size_t phase_count = bounded_region_arity(plan) -
            plan.state_types.size() - plan.bound_operand_count;
        const auto& phases = elements(require_object_field(value, "phases"));
        if (phases.size() != phase_count)
          throw std::invalid_argument("bounded region must declare every phase");
        for (std::size_t ordinal = 0; ordinal < phases.size(); ++ordinal) {
          const Json& phase = phases[ordinal];
          keys(phase, {"argument", "bindings"}, "bounded region phase");
          const std::uint32_t argument = source_uint32(
              require_object_field(phase, "argument"),
              "bounded region phase argument");
          const std::uint32_t expected = static_cast<std::uint32_t>(
              plan.state_types.size() + plan.bound_operand_count + ordinal);
          if (argument != expected)
            throw std::invalid_argument(
                "bounded region phase argument is not canonical");
          const auto& bindings = elements(require_object_field(phase, "bindings"));
          if (bindings.size() > kBoundedRegionPhaseBindingCapacity)
            throw std::invalid_argument(
                "bounded region phase binding capacity exceeded");
          std::set<std::pair<int, std::uint32_t>> sources;
          std::set<std::string> names;
          for (const Json& binding : bindings) {
            keys(binding, {"bank", "slot", "name"},
                 "bounded region phase binding");
            RegionValueSlot source;
            source.bank = source_region_bank(require_string(
                require_object_field(binding, "bank"), "bounded region bank"));
            source.slot = source_uint32(require_object_field(binding, "slot"),
                                        "bounded region slot");
            if (!sources.insert({static_cast<int>(source.bank), source.slot}).second)
              throw std::invalid_argument(
                  "duplicate bounded region phase source");
            const std::string name = require_string(
                require_object_field(binding, "name"),
                "bounded region binding name");
            source_binding_identifier(name);
            if (!names.insert(name).second)
              throw std::invalid_argument(
                  "duplicate bounded region phase binding name");
            try {
              (void)region_slot_type(
                  plan, bounded_region_phase_kind(plan, ordinal), source,
                  bounded_region_preparation_ordinal(plan, ordinal));
            } catch (const std::exception& error) {
              throw std::invalid_argument(
                  std::string("invalid bounded region phase source: ") +
                  error.what());
            }
          }
        }
      }
      else throw std::invalid_argument("unknown structured primitive family: " + family);
      if (!bounded)
        keys(value, {"structured", "args", "bind"}, "structured expression");
    }
    for (const auto& arg : elements(require_object_field(value, "args"))) source_expression(arg);
    if (!bounded && value.object_v.count("bind")) {
      const auto& bindings = value.object_v.at("bind");
      if (bindings.kind != Kind::Object) throw std::invalid_argument("bind must be an object");
      for (const auto& region : bindings.object_v)
        for (const auto& name : elements(region.second)) (void)require_string(name, "binding name");
    }
  } else {
    for (const char* field : {"ref", "input", "bound", "local", "hole"}) {
      if (!value.object_v.count(field)) continue;
      if (std::string(field) == "input" || std::string(field) == "local" ||
          std::string(field) == "bound") {
        keys(value, {field, "fuel_events"}, "binding expression");
        source_fuel_events(value, std::string(field) == "bound" ?
            NodeKind::REGION_VAR : NodeKind::VAR);
      } else {
        keys(value, {field}, "reference expression");
      }
      (void)require_string(value.object_v.at(field), field); return;
    }
    throw std::invalid_argument("unknown grammar expression shape");
  }
}
void source_schema(const Json& document) {
  for (const char* field : {"inputs", "locals"})
    if (document.object_v.count(field)) source_scope(document.object_v.at(field));
  if (document.object_v.count("entry")) keys(document.object_v.at("entry"), {"nonterminal", "type", "category"}, "entry");
  if (document.object_v.count("search_limits")) keys(document.object_v.at("search_limits"), {"max_nodes", "max_depth"}, "search_limits");
  if (document.object_v.count("execution_limits")) keys(document.object_v.at("execution_limits"), {"fuel"}, "execution_limits");
  for (const char* category : {"nonterminals", "templates"}) {
    if (!document.object_v.count(category)) continue;
    for (const auto& resource : elements(document.object_v.at(category))) {
      const bool nt = std::string(category) == "nonterminals";
      if (nt) keys(resource, {"id", "operation", "type", "scope", "category", "alternatives"}, "nonterminal");
      else keys(resource, {"id", "operation", "type", "scope", "category", "holes", "body"}, "template");
      const auto operation = resource.object_v.count("operation") ? require_string(resource.object_v.at("operation"), "operation") : "define";
      if (operation == "extend") {
        if (!nt) throw std::invalid_argument("only nonterminal alternatives can be extended");
        keys(resource, {"id", "operation", "alternatives"}, "extension");
      } else {
        source_scope(require_object_field(resource, "scope"));
        (void)parse_type(require_string(require_object_field(resource, "type"), "type"));
        source_category(resource);
      }
      if (nt) {
        for (const auto& alternative : elements(require_object_field(resource, "alternatives"))) {
          keys(alternative, {"id", "weight", "expression"}, "alternative");
          const auto& weight = require_object_field(alternative, "weight");
          if (weight.kind != Kind::Number || !std::isfinite(weight.number_v) || weight.number_v <= 0)
            throw std::invalid_argument("production weight must be positive and finite");
          source_expression(require_object_field(alternative, "expression"));
        }
      } else {
        for (const auto& hole : elements(require_object_field(resource, "holes"))) {
          keys(hole, {"id", "type", "scope", "category"}, "template hole");
          (void)parse_type(require_string(require_object_field(hole, "type"), "type"));
          source_scope(require_object_field(hole, "scope"));
          source_category(hole);
        }
        source_expression(require_object_field(resource, "body"));
      }
    }
  }
}

void quoted(std::ostream& out, const std::string& value) {
  const char* digits = "0123456789abcdef";
  out << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
    else if (c < 0x20) out << "\\u00" << digits[c >> 4] << digits[c & 15];
    else out << static_cast<char>(c);
  }
  out << '"';
}

void emit(std::ostream& out, const Json& value) {
  switch (value.kind) {
    case Kind::Null: out << "null"; break;
    case Kind::Bool: out << (value.bool_v ? "true" : "false"); break;
    case Kind::Number:
      if (!std::isfinite(value.number_v)) throw std::invalid_argument("non-finite grammar number");
      out << std::setprecision(std::numeric_limits<double>::max_digits10) << value.number_v;
      break;
    case Kind::String: quoted(out, value.string_v); break;
    case Kind::Array:
      out << '[';
      for (std::size_t i = 0; i < value.array_v.size(); ++i) { if (i) out << ','; emit(out, value.array_v[i]); }
      out << ']'; break;
    case Kind::Object:
      out << '{';
      { bool first = true;
        for (const auto& entry : value.object_v) {
          if (!first) out << ',';
          first = false; quoted(out, entry.first); out << ':'; emit(out, entry.second);
        }
      }
      out << '}'; break;
  }
}

struct Resource {
  Json value;
  std::set<std::filesystem::path> writers;
};

class Resolver {
 public:
  ResolvedDefinition file(const std::filesystem::path& path) {
    const auto root = std::filesystem::canonical(path);
    const Json document = read(root);
    visit(root, document);
    return finish(document);
  }
  ResolvedDefinition text(const std::string& text) {
    const Json document = parse(text);
    if (document.object_v.count("imports") && !elements(document.object_v.at("imports")).empty())
      throw std::invalid_argument("in-memory grammar cannot resolve file imports");
    visit({}, document);
    return finish(document);
  }

 private:
  static Json parse(const std::string& text) {
    if (text.size() > 16 * 1024 * 1024) throw std::invalid_argument("grammar file exceeds 16 MiB");
    return cli_detail::JsonParser(text, {true, 256}).parse();
  }
  static Json read(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 16 * 1024 * 1024) throw std::invalid_argument("grammar file exceeds 16 MiB");
    std::ifstream in(path);
    if (!in) throw std::invalid_argument("cannot read grammar: " + path.string());
    std::ostringstream contents; contents << in.rdbuf();
    return parse(contents.str());
  }
  std::set<std::filesystem::path> visit(const std::filesystem::path& path, Json document) {
    if (active_.count(path)) throw std::invalid_argument("grammar import cycle: " + path.string());
    if (visited_.count(path)) return visited_.at(path);
    if (visited_.size() + active_.size() >= 256) throw std::invalid_argument("grammar import limit exceeds 256 files");
    keys(document, {"format_version", "imports", "entry", "inputs", "locals", "nonterminals", "templates", "search_limits",
        "execution_limits", "resolved", "source_content", "catalog_version", "normalization_version"}, "grammar");
    if (require_string(require_object_field(document, "format_version"), "format_version") != kDefinitionVersion)
      throw std::invalid_argument("unsupported grammar definition version");
    source_schema(document);
    const bool resolved = document.object_v.count("resolved") != 0;
    if (resolved) {
      const auto& flag = document.object_v.at("resolved");
      if (flag.kind != Kind::Bool || !flag.bool_v || document.object_v.count("imports"))
        throw std::invalid_argument("resolved grammar cannot contain imports");
      if (require_string(require_object_field(document, "catalog_version"), "catalog_version") != kCatalogVersion ||
          require_string(require_object_field(document, "normalization_version"), "normalization_version") != kNormalizationVersion)
        throw std::invalid_argument("incompatible resolved grammar versions");
    } else if (document.object_v.count("source_content") || document.object_v.count("catalog_version") ||
               document.object_v.count("normalization_version"))
      throw std::invalid_argument("resolved metadata requires resolved:true");
    active_.insert(path);
    std::set<std::filesystem::path> dependencies;
    if (document.object_v.count("imports")) {
      for (const auto& imported : elements(document.object_v.at("imports"))) {
        const auto name = require_string(imported, "import path");
        const std::filesystem::path relative(name);
        if (name.empty() || relative.is_absolute() || name.find("://") != std::string::npos ||
            name.find('\0') != std::string::npos)
          throw std::invalid_argument("imports must be local relative file paths");
        const auto child = std::filesystem::canonical(path.parent_path() / relative);
        const auto nested = visit(child, read(child));
        dependencies.insert(nested.begin(), nested.end()); dependencies.insert(child);
      }
    }
    for (const char* category : {"nonterminals", "templates"}) {
      auto found = document.object_v.find(category);
      if (found == document.object_v.end()) continue;
      sort_ids(found->second);
      if (std::string(category) == "nonterminals") {
        for (auto& resource : found->second.array_v)
          if (resource.object_v.count("alternatives")) sort_ids(resource.object_v.at("alternatives"));
      } else {
        for (auto& resource : found->second.array_v)
          if (resource.object_v.count("holes")) sort_ids(resource.object_v.at("holes"));
      }
      for (auto value : found->second.array_v) {
        const auto id = identifier(value);
        const auto op = value.object_v.count("operation") ? require_string(value.object_v.at("operation"), "operation") : "define";
        if (resolved && op != "define") throw std::invalid_argument("resolved grammar contains an override operation");
        value.object_v.erase("operation");
        auto& table = resources_[category];
        auto previous = table.find(id);
        if (op == "define") {
          if (previous != table.end()) throw std::invalid_argument("duplicate/conflicting grammar ID: " + id);
        } else if (op == "replace" || op == "extend") {
          if (previous == table.end()) throw std::invalid_argument("override has no imported target: " + id);
          for (const auto& writer : previous->second.writers)
            if (!dependencies.count(writer)) throw std::invalid_argument("ambiguous sibling override: " + id);
          if (op == "extend") {
            if (std::string(category) != "nonterminals") throw std::invalid_argument("only nonterminal alternatives can be extended");
            keys(value, {"id", "alternatives"}, "extension");
            Json alternatives = require_object_field(previous->second.value, "alternatives");
            const auto& additions = elements(require_object_field(value, "alternatives"));
            alternatives.array_v.insert(alternatives.array_v.end(), additions.begin(), additions.end());
            value = previous->second.value; value.object_v["alternatives"] = std::move(alternatives);
          }
        } else throw std::invalid_argument("unknown grammar override operation: " + op);
        if (std::string(category) == "nonterminals") sort_ids(value.object_v.at("alternatives"));
        table[id] = Resource{std::move(value), {path}};
      }
    }
    if (resolved) {
      for (const auto& content : elements(require_object_field(document, "source_content")))
        sources_.insert(canonical_json(content));
    } else {
      document.object_v.erase("imports");
      sources_.insert(canonical_json(document));
    }
    active_.erase(path); visited_[path] = dependencies;
    return dependencies;
  }
  ResolvedDefinition finish(const Json& root) {
    Json out = object();
    out.object_v["format_version"] = string(kDefinitionVersion);
    out.object_v["catalog_version"] = string(std::string(kCatalogVersion));
    out.object_v["normalization_version"] = string(kNormalizationVersion);
    Json flag; flag.kind = Kind::Bool; flag.bool_v = true; out.object_v["resolved"] = flag;
    for (const char* field : {"entry", "search_limits", "execution_limits"}) out.object_v[field] = require_object_field(root, field);
    out.object_v["inputs"] = root.object_v.count("inputs") ? root.object_v.at("inputs") : array();
    out.object_v["locals"] = root.object_v.count("locals") ? root.object_v.at("locals") : array();
    for (const char* category : {"nonterminals", "templates"}) {
      Json values = array();
      for (const auto& resource : resources_[category]) values.array_v.push_back(resource.second.value);
      out.object_v[category] = std::move(values);
    }
    Json content = array();
    for (const auto& text : sources_) content.array_v.push_back(parse(text));
    out.object_v["source_content"] = std::move(content);
    const auto canonical = canonical_json(out);
    std::vector<std::filesystem::path> source_paths;
    source_paths.reserve(visited_.size());
    for (const auto& visited : visited_)
      if (!visited.first.empty()) source_paths.push_back(visited.first);
    return {std::move(out), canonical, content_sha256(canonical),
            std::move(source_paths)};
  }
  std::set<std::filesystem::path> active_;
  std::map<std::filesystem::path, std::set<std::filesystem::path>> visited_;
  std::map<std::string, std::map<std::string, Resource>> resources_;
  std::set<std::string> sources_;
};
}  // namespace

std::string canonical_json(const cli_detail::JsonValue& value) {
  std::ostringstream out; out.imbue(std::locale::classic()); emit(out, value); return out.str();
}
ResolvedDefinition load_definition(const std::filesystem::path& path) { return Resolver{}.file(path); }
ResolvedDefinition parse_definition(const std::string& text) { return Resolver{}.text(text); }

}  // namespace gagp::evo::grammar
