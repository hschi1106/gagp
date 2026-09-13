#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/identity.hpp"

using namespace gagp::evo::grammar;
using gagp::cli_detail::JsonParser;

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template <class Action> void rejects(Action action, const std::string& reason) {
  try { action(); }
  catch (const std::exception& error) {
    if (std::string(error.what()).find(reason) != std::string::npos) return;
    throw std::runtime_error("unexpected diagnostic: " + std::string(error.what()));
  }
  throw std::runtime_error("expected rejection: " + reason);
}
struct Temp {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("gagp-definition-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Temp() { std::filesystem::create_directories(path); }
  ~Temp() { std::error_code error; std::filesystem::remove_all(path, error); }
  void write(const std::string& name, const std::string& content) const {
    const auto target = path / name; std::filesystem::create_directories(target.parent_path());
    std::ofstream out(target); out << content;
    if (!out) throw std::runtime_error("failed to write grammar fixture");
  }
};
const std::string base = R"({"format_version":"grammar-definition-v1","nonterminals":[
  {"id":"Expr","type":"Int","scope":[],"alternatives":[
    {"id":"zero","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}}]}]})";
const std::string settings = R"("entry":{"nonterminal":"Expr","type":"Int"},
  "search_limits":{"max_nodes":10,"max_depth":5},"execution_limits":{"fuel":100})";
std::string root(const std::string& imports, const std::string& rules = "[]") {
  return "{\"format_version\":\"grammar-definition-v1\",\"imports\":" + imports +
      ",\"nonterminals\":" + rules + "," + settings + "}";
}
std::string replacement(const std::string& id) {
  return "[{\"id\":\"Expr\",\"operation\":\"replace\",\"type\":\"Int\",\"scope\":[],\"alternatives\":[{\"id\":\"" +
      id + "\",\"weight\":1,\"expression\":{\"constant\":{\"type\":\"Int\",\"values\":[\"1\"]}}}]}]";
}
}  // namespace

int main() {
  try {
    check(content_sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 empty vector");
    check(content_sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 short vector");
    check(content_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 two-block vector");
    check(content_sha256(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 streaming vector");
    rejects([] { JsonParser(R"({"x":1,"x":2})", {true, 64}).parse(); }, "duplicate");
    rejects([] { JsonParser(R"({"\u0078":1,"x":2})", {true, 64}).parse(); }, "duplicate");
    check(JsonParser(R"({"x":1,"x":2})").parse().object_v.at("x").number_v == 1,
          "legacy parser default changed");
    const auto unicode = JsonParser(R"("\ud83d\ude00\u0001")", {true, 64}).parse();
    check(unicode.string_v == std::string("\xf0\x9f\x98\x80\x01", 5), "Unicode escape decoding changed");
    check(JsonParser(canonical_json(unicode), {true, 64}).parse().string_v == unicode.string_v,
          "canonical strings must roundtrip control bytes");
    rejects([] { JsonParser(R"("\ud800")", {true, 64}).parse(); }, "surrogate");
    rejects([] { JsonParser(std::string("\"\xc0\x80\"", 4), {true, 64}).parse(); }, "UTF-8");
    rejects([] { JsonParser(std::string("\"\xed\xa0\x80\"", 5), {true, 64}).parse(); }, "UTF-8");
    rejects([] { JsonParser("\v0", {true, 64}).parse(); }, "token");
    rejects([] { JsonParser("1e-9999", {true, 64}).parse(); }, "out of range");
    rejects([] { JsonParser("1e9999", {true, 64}).parse(); }, "out of range");
    check(JsonParser("1.25", {true, 64}).parse().number_v == 1.25, "strict numeric conversion");
    rejects([] { JsonParser("[[[0]]]", {true, 3}).parse(); }, "nesting");
    rejects([] { parse_definition(R"({"format_version":"grammar-definition-v2"})"); }, "version");
    rejects([] { parse_definition(R"({"format_version":"grammar-definition-v1","hook":"execute"})"); }, "unknown key");
    rejects([] { parse_definition(root("[\"file.json\"]")); }, "in-memory");

    Temp first, second;
    first.write("base.json", base);
    const std::string extension = R"([{"id":"Expr","operation":"extend","alternatives":[
      {"id":"one","weight":2,"expression":{"constant":{"type":"Int","values":["1"]}}}]}])";
    first.write("root.json", root("[\"base.json\"]", extension));
    const auto resolved = load_definition(first.path / "root.json");
    const auto& rules = resolved.document.object_v.at("nonterminals").array_v;
    check(rules.size() == 1 && rules.front().object_v.at("alternatives").array_v.size() == 2, "extend lost alternatives");
    check(resolved.canonical.find(first.path.string()) == std::string::npos, "absolute path leaked into identity");
    check(parse_definition(resolved.canonical).canonical == resolved.canonical, "resolved export is not idempotent");
    check(parse_definition(resolved.canonical).content_hash == resolved.content_hash, "resolved identity changed");
    second.write("lib/renamed.json", base);
    second.write("root.json", root("[\"lib/renamed.json\"]", extension));
    check(load_definition(second.path / "root.json").canonical == resolved.canonical, "relocation changed canonical definition");
    check(load_definition(second.path / "root.json").content_hash == resolved.content_hash, "relocation changed hash");
    std::string changed = base;
    changed.replace(changed.find("[\"0\"]"), 5, "[\"9\"]");
    second.write("lib/renamed.json", changed);
    check(load_definition(second.path / "root.json").canonical != resolved.canonical, "transitive content change was lost");
    check(load_definition(second.path / "root.json").content_hash != resolved.content_hash, "transitive hash change was lost");

    first.write("root.json", root("[\"base.json\"]", replacement("new_leaf")));
    check(load_definition(first.path / "root.json").document.object_v.at("nonterminals").array_v.front()
          .object_v.at("alternatives").array_v.size() == 1, "replace retained old alternatives");
    auto malformed_source = JsonParser(base).parse();
    malformed_source.object_v.at("nonterminals").array_v[0].object_v["hook"] = JsonParser("true").parse();
    first.write("base.json", canonical_json(malformed_source));
    rejects([&] { load_definition(first.path / "root.json"); }, "unknown key");
    malformed_source = JsonParser(base).parse();
    malformed_source.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0]
        .object_v.at("expression").object_v.at("constant").object_v.at("values") = JsonParser("[1]").parse();
    first.write("base.json", canonical_json(malformed_source));
    rejects([&] { load_definition(first.path / "root.json"); }, "decimal string");
    first.write("base.json", base);
    first.write("root.json", root("[\"base.json\"]",
        canonical_json(JsonParser(base).parse().object_v.at("nonterminals"))));
    rejects([&] { load_definition(first.path / "root.json"); }, "duplicate/conflicting");
    first.write("left.json", root("[\"base.json\"]"));
    first.write("right.json", root("[\"base.json\"]"));
    first.write("root.json", root("[\"left.json\",\"right.json\"]"));
    const auto diamond = load_definition(first.path / "root.json").canonical;
    first.write("root.json", root("[\"right.json\",\"left.json\"]"));
    check(load_definition(first.path / "root.json").canonical == diamond, "diamond import order changed resolution");
    first.write("left.json", root("[\"base.json\"]", replacement("left_leaf")));
    first.write("right.json", root("[\"base.json\"]", replacement("right_leaf")));
    rejects([&] { load_definition(first.path / "root.json"); }, "ambiguous sibling");
    first.write("cycle-a.json", root("[\"cycle-b.json\"]"));
    first.write("cycle-b.json", root("[\"cycle-a.json\"]"));
    rejects([&] { load_definition(first.path / "cycle-a.json"); }, "cycle");
    first.write("root.json", root("[\"https://example.invalid/grammar.json\"]"));
    rejects([&] { load_definition(first.path / "root.json"); }, "local relative");
    std::cout << "grammar import and canonical export checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
