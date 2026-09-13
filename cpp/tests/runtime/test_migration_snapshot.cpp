#include <climits>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "migration_snapshot.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  try {
    using namespace gagp;
    std::vector<Value> values{Value::from_int(LLONG_MAX), Value::from_int(LLONG_MIN),
      Value::from_float(-0.0), Value::from_float(std::numeric_limits<double>::infinity()),
      Value::from_float(std::numeric_limits<double>::quiet_NaN()), Value::from_bool(true),
      Value::from_char(255), payload::make_string_value(std::string("a\0\1\xff", 4)),
      payload::make_int_list_value({Value::from_int(LLONG_MIN), Value::from_int(LLONG_MAX)}),
      payload::make_float_list_value({Value::from_float(-0.0), Value::from_float(0.125)}),
      payload::make_string_list_value({payload::make_string_value(""), payload::make_string_value("\n")})};
    std::vector<std::string> encoded;
    for (const auto& value : values) encoded.push_back(migration::encode_value(value));
    payload::clear();
    for (std::size_t i = 0; i < values.size(); ++i) {
      const auto decoded = migration::decode_value(cli_detail::JsonParser(encoded[i]).parse());
      check(decoded.tag == values[i].tag, "tag changed");
      check(migration::encode_value(decoded) == encoded[i], "bits or decoded payload changed");
    }

    auto ast = cli_detail::decode_ast_json(cli_detail::JsonParser(R"({"version":"ast-prefix",
      "nodes":[{"kind":0,"i0":0,"i1":0},{"kind":2,"i0":0,"i1":0},
      {"kind":6,"i0":0,"i1":0},{"kind":7,"i0":0,"i1":0},{"kind":1,"i0":0,"i1":0}],
      "names":[],"consts":[{"type":"int","value":1}]})").parse());
    ast.consts[0] = Value::from_int(LLONG_MAX);
    evo::ProgramGenome genome{ast, evo::build_genome_meta(ast)};
    const std::string snapshot = migration::encode_population({genome});
    const auto restored = migration::decode_population(cli_detail::JsonParser(snapshot).parse());
    const auto result = execute_bytecode_cpu(evo::compile_for_eval(restored[0]), {}, 100);
    check(!result.is_error && result.value.i == LLONG_MAX, "materialized execution lost integer precision");
    check(migration::encode_population(restored) == snapshot, "population roundtrip changed artifact");
    const auto bytecode = evo::compile_for_eval(restored[0]);
    const auto encoded_bytecode = migration::encode_bytecode(bytecode);
    const auto decoded_bytecode = migration::decode_bytecode(cli_detail::JsonParser(encoded_bytecode).parse());
    check(migration::encode_bytecode(decoded_bytecode) == encoded_bytecode, "bytecode bits changed");
    const auto decoded_result = execute_bytecode_cpu(decoded_bytecode, {}, 100);
    check(!decoded_result.is_error && decoded_result.value.i == LLONG_MAX, "bytecode replay lost integer precision");
    auto opaque = Value::invalid();
    opaque.tag = ValueTag::String;
    opaque.i = 123456789;
    const auto missing = migration::encode_value(opaque, false);
    const auto decoded_missing = migration::decode_value(cli_detail::JsonParser(missing).parse());
    check(migration::encode_value(decoded_missing, false) == missing, "absent payload marker lost");
    bool required_payload = false;
    try { (void)migration::encode_value(decoded_missing); }
    catch (const std::exception&) { required_payload = true; }
    check(required_payload, "materialized population codec accepted an absent payload");
    genome.ast.consts[0] = decoded_missing;
    required_payload = false;
    try { (void)migration::encode_population({genome}); }
    catch (const std::exception&) { required_payload = true; }
    check(required_payload, "ordinary population snapshots must require payloads");
    const auto diagnostic = migration::encode_population({genome}, false);
    check(diagnostic.find("\"materialized\":false") != std::string::npos,
          "diagnostic population lost opaque payload marker");
    bool rejected = false;
    try { migration::decode_population(cli_detail::JsonParser("{\"format_version\":\"bad\",\"programs\":[]}").parse()); }
    catch (const std::exception&) { rejected = true; }
    check(rejected, "invalid snapshot accepted");
    std::cout << "migration snapshot tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
