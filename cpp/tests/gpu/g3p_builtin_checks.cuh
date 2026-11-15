#pragma once

#include <functional>

#include "gagp/runtime/cpu/builtins_cpu.hpp"

// CPU/GPU parity for the G3P builtins (insert, remove_at, set_at, index_mod,
// list contains, range, strip family, startswith, endswith, capitalize), run
// through FitnessSessionGpu in every payload and profile family. Included by
// test_extended_payload_gpu.cu, which provides the instruction and region helpers.

namespace g3p_builtin_checks {

using gagp::eval_fitness_cpu;

struct Probe {
  std::string label;
  BuiltinId id;
  std::vector<Value> args;
};

Value I(std::int64_t v) { return Value::from_int(v); }
Value F(double v) { return Value::from_float(v); }
Value S(const std::string& s) { return payload::make_string_value(s); }
Value IL(const std::vector<std::int64_t>& v) {
  std::vector<Value> out;
  for (const auto x : v) out.push_back(I(x));
  return payload::make_int_list_value(out);
}
Value FL(const std::vector<double>& v) {
  std::vector<Value> out;
  for (const auto x : v) out.push_back(F(x));
  return payload::make_float_list_value(out);
}
Value SL(const std::vector<std::string>& v) {
  std::vector<Value> out;
  for (const auto& x : v) out.push_back(S(x));
  return payload::make_string_list_value(out);
}

// result == expected, where `expected` is the CPU builtin result. A runtime error
// on either engine scores -1 under the Exact metric.
struct Built {
  BytecodeProgram program;
  bool errors = false;
};

Built compare_program(const std::vector<Instr>& body, std::vector<Value> consts, const Value& expected,
                      bool errors) {
  Built built;
  built.errors = errors;
  built.program.consts = std::move(consts);
  built.program.code = body;
  const int slot = static_cast<int>(built.program.consts.size());
  built.program.consts.push_back(expected);
  built.program.code.push_back(ins_a(Opcode::PushConst, slot));
  built.program.code.push_back(ins(Opcode::Eq));
  built.program.code.push_back(ins(Opcode::Return));
  return built;
}

Built probe_program(const Probe& probe) {
  const auto cpu = gagp::builtin_call(probe.id, probe.args);
  std::vector<Instr> body;
  for (std::size_t i = 0; i < probe.args.size(); ++i) body.push_back(ins_a(Opcode::PushConst, static_cast<int>(i)));
  body.push_back(ins_ab(Opcode::CallBuiltin, static_cast<int>(probe.id), static_cast<int>(probe.args.size())));
  return compare_program(body, probe.args, cpu.is_error ? I(0) : cpu.value, cpu.is_error);
}

std::vector<Probe> probes() {
  const std::int64_t lo = INT64_MIN, hi = INT64_MAX;
  std::vector<Probe> out;
  const auto add = [&](const std::string& label, BuiltinId id, std::vector<Value> args) {
    out.push_back({label, id, std::move(args)});
  };
  const std::string white = " \t\n\r\f\v";
  for (const std::int64_t index : {std::int64_t(0), std::int64_t(1), std::int64_t(3), std::int64_t(4),
                                   std::int64_t(-1), std::int64_t(-3), std::int64_t(-4), std::int64_t(-100), lo, hi,
                                   lo + 1, hi - 1}) {
    const std::string at = "@" + std::to_string(index);
    add("insert int" + at, BuiltinId::Insert, {IL({1, 2, 3}), I(index), I(9)});
    add("insert float" + at, BuiltinId::Insert, {FL({1.5, 2.5, 3.5}), I(index), F(-0.0)});
    add("insert string" + at, BuiltinId::Insert, {SL({"a", "bb", ""}), I(index), S("zz")});
    add("insert empty" + at, BuiltinId::Insert, {IL({}), I(index), I(5)});
    add("remove_at int" + at, BuiltinId::RemoveAt, {IL({10, 20, 30}), I(index)});
    add("remove_at float" + at, BuiltinId::RemoveAt, {FL({1.5, 2.5, 3.5}), I(index)});
    add("remove_at string" + at, BuiltinId::RemoveAt, {SL({"a", "bb", "c"}), I(index)});
    add("remove_at empty" + at, BuiltinId::RemoveAt, {IL({}), I(index)});
    add("remove_at single" + at, BuiltinId::RemoveAt, {SL({"x"}), I(index)});
    add("set_at int" + at, BuiltinId::SetAt, {IL({10, 20, 30}), I(index), I(99)});
    add("set_at float" + at, BuiltinId::SetAt, {FL({1.5, 2.5, 3.5}), I(index), F(7.25)});
    add("set_at string" + at, BuiltinId::SetAt, {SL({"a", "bb", "c"}), I(index), S("")});
    add("set_at empty" + at, BuiltinId::SetAt, {FL({}), I(index), F(1.0)});
    add("index_mod int" + at, BuiltinId::IndexMod, {IL({10, 20, 30}), I(index)});
    add("index_mod float" + at, BuiltinId::IndexMod, {FL({1.5, 2.5, 3.5}), I(index)});
    add("index_mod string list" + at, BuiltinId::IndexMod, {SL({"a", "bb", "c"}), I(index)});
    add("index_mod string" + at, BuiltinId::IndexMod, {S("abc"), I(index)});
    add("index_mod empty int" + at, BuiltinId::IndexMod, {IL({}), I(index)});
    add("index_mod empty float" + at, BuiltinId::IndexMod, {FL({}), I(index)});
    add("index_mod empty strings" + at, BuiltinId::IndexMod, {SL({}), I(index)});
    add("index_mod empty text" + at, BuiltinId::IndexMod, {S(""), I(index)});
  }
  add("contains int hit", BuiltinId::Contains, {IL({1, 2, 3}), I(3)});
  add("contains int miss", BuiltinId::Contains, {IL({1, 2, 3}), I(4)});
  add("contains empty", BuiltinId::Contains, {IL({}), I(0)});
  add("contains float zero", BuiltinId::Contains, {FL({0.0}), F(-0.0)});
  add("contains float miss", BuiltinId::Contains, {FL({1.5, 2.5}), F(3.5)});
  add("contains string list hit", BuiltinId::Contains, {SL({"a", "bc"}), S("bc")});
  add("contains string list miss", BuiltinId::Contains, {SL({"a", "bc"}), S("b")});
  add("contains string unchanged", BuiltinId::Contains, {S("hello"), S("ell")});
  for (const auto& bounds : std::vector<std::pair<std::int64_t, std::int64_t>>{
           {0, 5}, {-2, 2}, {5, 0}, {3, 3}, {lo, lo + 3}, {hi - 2, hi}, {hi, lo}, {hi, hi}, {lo, lo},
           {lo, hi}, {-1, hi}, {lo, 0}, {0, 65536}, {0, 1000}, {-500, 500}}) {
    add("range " + std::to_string(bounds.first) + ".." + std::to_string(bounds.second), BuiltinId::Range,
        {I(bounds.first), I(bounds.second)});
  }
  const std::vector<std::string> texts{"", white, "a", " a ", white + "ab c" + white, "xxabcyx", "\xa0z\xa0",
                                       std::string("\0 a\0", 4), "hELLO wORLD", "1ABC", "\xe9\xc9", "aaa"};
  for (const auto& text : texts) {
    const std::string at = "(" + std::to_string(text.size()) + ":" + (text.empty() ? "" : std::to_string(int(text[0]))) + ")";
    add("strip" + at, BuiltinId::Strip, {S(text)});
    add("lstrip" + at, BuiltinId::LStrip, {S(text)});
    add("rstrip" + at, BuiltinId::RStrip, {S(text)});
    add("capitalize" + at, BuiltinId::Capitalize, {S(text)});
    for (const std::string chars : {std::string(), std::string("xy"), std::string("a"), std::string("a "), white}) {
      add("strip_chars" + at + std::to_string(chars.size()), BuiltinId::StripChars, {S(text), S(chars)});
      add("lstrip_chars" + at + std::to_string(chars.size()), BuiltinId::LStripChars, {S(text), S(chars)});
      add("rstrip_chars" + at + std::to_string(chars.size()), BuiltinId::RStripChars, {S(text), S(chars)});
      add("startswith" + at + std::to_string(chars.size()), BuiltinId::StartsWith, {S(text), S(chars)});
      add("endswith" + at + std::to_string(chars.size()), BuiltinId::EndsWith, {S(text), S(chars)});
    }
  }
  // Type errors and arity errors fail identically on both engines.
  add("insert wrong element", BuiltinId::Insert, {IL({1}), I(0), F(1.0)});
  add("insert string into int list", BuiltinId::Insert, {IL({1}), I(0), S("a")});
  add("insert non-list", BuiltinId::Insert, {I(1), I(0), I(1)});
  add("insert float index", BuiltinId::Insert, {IL({1}), F(0.0), I(1)});
  add("insert arity", BuiltinId::Insert, {IL({1}), I(0)});
  add("remove_at float index", BuiltinId::RemoveAt, {IL({1}), F(0.0)});
  add("remove_at string", BuiltinId::RemoveAt, {S("ab"), I(0)});
  add("remove_at arity", BuiltinId::RemoveAt, {IL({1})});
  add("set_at wrong element", BuiltinId::SetAt, {IL({1}), I(0), F(1.0)});
  add("set_at empty wrong element", BuiltinId::SetAt, {IL({}), I(0), F(1.0)});
  add("set_at arity", BuiltinId::SetAt, {IL({1}), I(0)});
  add("index_mod int", BuiltinId::IndexMod, {I(3), I(0)});
  add("index_mod float index", BuiltinId::IndexMod, {IL({1}), F(0.0)});
  add("contains wrong element", BuiltinId::Contains, {IL({1}), F(1.0)});
  add("contains int in string list", BuiltinId::Contains, {SL({"a"}), I(1)});
  add("range float", BuiltinId::Range, {F(0.0), I(3)});
  add("range arity", BuiltinId::Range, {I(0)});
  add("strip int", BuiltinId::Strip, {I(1)});
  add("strip chars int", BuiltinId::StripChars, {S("a"), I(1)});
  add("lstrip chars arity", BuiltinId::LStripChars, {S("a")});
  add("startswith int", BuiltinId::StartsWith, {S("a"), I(1)});
  add("endswith int", BuiltinId::EndsWith, {I(1), S("a")});
  add("capitalize int", BuiltinId::Capitalize, {I(1)});
  return out;
}

// Runs one population against one shared case set and compares every program's
// fitness, wrong and error counts with the CPU reference.
bool compare_population(const std::vector<BytecodeProgram>& programs, const std::vector<CaseBindings>& cases,
                        const std::vector<Value>& answers, const FitnessPolicy& policy,
                        const std::string& label, FitnessEvalResult* out = nullptr,
                        std::vector<CpuFitnessCaseCounts>* cpu_counts = nullptr,
                        std::vector<double>* cpu_fitness = nullptr, int blocksize = 33) {
  std::vector<unsigned> wrong;
  std::vector<CpuFitnessCaseCounts> counts;
  const auto cpu = eval_fitness_cpu(programs, cases, answers, 1000, 1000.0, blocksize, policy, &wrong, &counts);
  FitnessSessionGpu session;
  if (!check(session.init(cases, answers, 1000, blocksize, 1000.0, policy).ok, label + ": session init")) return false;
  const auto gpu = session.eval_programs(programs, true);
  if (!check(gpu.ok, label + ": gpu run failed")) return false;
  if (!check(gpu.fitness == cpu, label + ": gpu fitness differs from the CPU reference")) {
    for (std::size_t i = 0; i < cpu.size() && i < gpu.fitness.size(); ++i)
      if (cpu[i] != gpu.fitness[i]) std::cerr << "  program " << i << " cpu=" << cpu[i] << " gpu=" << gpu.fitness[i] << '\n';
    return false;
  }
  if (policy.metric != FitnessMetric::Legacy && !check(gpu.wrong == wrong, label + ": wrong counts differ")) return false;
  for (std::size_t i = 0; i < programs.size(); ++i) {
    if (!check(gpu.case_counts[i][0] == static_cast<unsigned>(cases.size()) && gpu.case_counts[i][1] == counts[i].errors &&
               gpu.case_counts[i][2] == counts[i].timeouts, label + ": case/error/timeout counts differ")) return false;
  }
  if (out) *out = gpu;
  if (cpu_counts) *cpu_counts = counts;
  if (cpu_fitness) *cpu_fitness = cpu;
  return true;
}

bool test_signature_parity() {
  const auto all = probes();
  std::vector<BytecodeProgram> programs;
  std::vector<bool> errors;
  std::vector<std::string> labels;
  for (const auto& probe : all) {
    auto built = probe_program(probe);
    programs.push_back(built.program);
    errors.push_back(built.errors);
    labels.push_back(probe.label);
  }
  FitnessPolicy policy{FitnessMetric::Exact};
  std::vector<double> cpu;
  std::vector<CpuFitnessCaseCounts> counts;
  FitnessEvalResult gpu;
  if (!compare_population(programs, {{}}, {Value::from_bool(true)}, policy, "g3p signature parity", &gpu, &counts,
                          &cpu)) return false;
  for (std::size_t i = 0; i < programs.size(); ++i) {
    if (!check(cpu[i] == (errors[i] ? -1.0 : 0.0) && (counts[i].errors == (errors[i] ? 1U : 0U)),
               "g3p CPU result for " + labels[i] + " is not the builtin's own result")) return false;
  }
  std::cout << "  g3p signature parity: " << programs.size() << " programs, profile " << gpu.execution_profile << '\n';
  return true;
}

// Inputs reach builtins through bound case locals. Consumer-only programs run in
// the ordinary (non-extended) payload flavors, producers in the extended one.
bool test_bound_inputs() {
  std::vector<CaseBindings> cases;
  std::vector<std::vector<Value>> selectors;
  const std::vector<std::vector<std::int64_t>> lists{{}, {4}, {1, 2, 3}, {7, 7, 9, -4, 0}};
  const std::vector<std::string> texts{"", "a", "  padded text\t", "xxhixx", "Hello"};
  const std::vector<std::int64_t> indexes{0, 1, 5, -1, -7, INT64_MIN, INT64_MAX};
  for (std::size_t i = 0; i < 28; ++i) {
    cases.push_back({{0, IL(lists[i % lists.size()])}, {1, I(indexes[i % indexes.size()])},
                     {2, S(texts[i % texts.size()])}, {3, S(i % 3 == 0 ? "" : i % 3 == 1 ? "x" : "He")},
                     {4, I(static_cast<std::int64_t>(i % 5))}});
  }
  struct Shape { std::string label; std::vector<Instr> code; std::function<Value(const CaseBindings&)> expected; };
  const auto local = [](const CaseBindings& bindings, int slot) {
    for (const auto& binding : bindings) if (binding.idx == slot) return binding.value;
    return Value::invalid();
  };
  const auto builtin_result = [](BuiltinId id, std::vector<Value> args) {
    const auto result = gagp::builtin_call(id, args);
    return result.is_error ? Value::invalid() : result.value;
  };
  const auto length_of = [&](Value value) {
    return value.tag == ValueTag::Invalid ? Value::invalid() : builtin_result(BuiltinId::Len, {value});
  };
  const std::vector<Shape> shapes{
      {"index_mod(list, i)", {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins_ab(Opcode::CallBuiltin, int(BuiltinId::IndexMod), 2), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return builtin_result(BuiltinId::IndexMod, {local(b, 0), local(b, 1)}); }},
      {"len(insert(list, i, 5))", {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins_a(Opcode::Load, 4), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Insert), 3), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Len), 1), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return length_of(builtin_result(BuiltinId::Insert, {local(b, 0), local(b, 1), local(b, 4)})); }},
      {"set_at(list, i, k)", {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins_a(Opcode::Load, 4), ins_ab(Opcode::CallBuiltin, int(BuiltinId::SetAt), 3), ins_a(Opcode::Load, 1), ins_ab(Opcode::CallBuiltin, int(BuiltinId::IndexMod), 2), ins(Opcode::Return)},
       [&](const CaseBindings& b) {
         const auto edited = builtin_result(BuiltinId::SetAt, {local(b, 0), local(b, 1), local(b, 4)});
         return edited.tag == ValueTag::Invalid ? edited : builtin_result(BuiltinId::IndexMod, {edited, local(b, 1)});
       }},
      {"remove_at(list, i) elements", {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins_ab(Opcode::CallBuiltin, int(BuiltinId::RemoveAt), 2), ins_a(Opcode::Load, 4), ins_ab(Opcode::CallBuiltin, int(BuiltinId::IndexMod), 2), ins(Opcode::Return)},
       [&](const CaseBindings& b) {
         const auto edited = builtin_result(BuiltinId::RemoveAt, {local(b, 0), local(b, 1)});
         return edited.tag == ValueTag::Invalid ? edited : builtin_result(BuiltinId::IndexMod, {edited, local(b, 4)});
       }},
      {"contains(list, k)", {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 4), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Contains), 2), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return builtin_result(BuiltinId::Contains, {local(b, 0), local(b, 4)}); }},
      {"len(range(0, k))", {ins_a(Opcode::PushConst, 0), ins_a(Opcode::Load, 4), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Range), 2), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Len), 1), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return length_of(builtin_result(BuiltinId::Range, {I(0), local(b, 4)})); }},
      {"strip(text)", {ins_a(Opcode::Load, 2), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Strip), 1), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Len), 1), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return length_of(builtin_result(BuiltinId::Strip, {local(b, 2)})); }},
      {"rstrip(text, chars)", {ins_a(Opcode::Load, 2), ins_a(Opcode::Load, 3), ins_ab(Opcode::CallBuiltin, int(BuiltinId::RStripChars), 2), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Len), 1), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return length_of(builtin_result(BuiltinId::RStripChars, {local(b, 2), local(b, 3)})); }},
      {"capitalize(text) == text", {ins_a(Opcode::Load, 2), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Capitalize), 1), ins_a(Opcode::Load, 2), ins(Opcode::Eq), ins(Opcode::Return)},
       [&](const CaseBindings& b) {
         const auto capital = builtin_result(BuiltinId::Capitalize, {local(b, 2)});
         return Value::from_bool(capital.i == local(b, 2).i);
       }},
      {"startswith(text, prefix)", {ins_a(Opcode::Load, 2), ins_a(Opcode::Load, 3), ins_ab(Opcode::CallBuiltin, int(BuiltinId::StartsWith), 2), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return builtin_result(BuiltinId::StartsWith, {local(b, 2), local(b, 3)}); }},
      {"endswith(text, suffix)", {ins_a(Opcode::Load, 2), ins_a(Opcode::Load, 3), ins_ab(Opcode::CallBuiltin, int(BuiltinId::EndsWith), 2), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return builtin_result(BuiltinId::EndsWith, {local(b, 2), local(b, 3)}); }},
      {"index_mod(text, i)", {ins_a(Opcode::Load, 2), ins_a(Opcode::Load, 1), ins_ab(Opcode::CallBuiltin, int(BuiltinId::IndexMod), 2), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Len), 1), ins(Opcode::Return)},
       [&](const CaseBindings& b) { return length_of(builtin_result(BuiltinId::IndexMod, {local(b, 2), local(b, 1)})); }},
  };
  for (const auto& shape : shapes) {
    BytecodeProgram program;
    program.n_locals = 5;
    program.consts = {I(0)};
    program.code = shape.code;
    std::vector<Value> answers;
    bool any_error = false;
    for (const auto& bindings : cases) {
      auto expected = shape.expected(bindings);
      any_error |= expected.tag == ValueTag::Invalid;
      answers.push_back(expected.tag == ValueTag::Invalid ? I(-12345) : expected);
    }
    std::vector<CpuFitnessCaseCounts> counts;
    FitnessEvalResult gpu;
    const FitnessPolicy policy{FitnessMetric::Exact};
    if (!compare_population({program}, cases, answers, policy, "g3p bound input " + shape.label, &gpu, &counts)) return false;
    // A case whose CPU builtin errors must error on both engines; all others match their answer.
    unsigned errored = 0;
    for (const auto& answer : answers) errored += answer.tag == ValueTag::Int && answer.i == -12345 ? 1U : 0U;
    if (!check(any_error == (errored != 0) && counts[0].errors == errored,
               "g3p bound input expectation " + shape.label)) return false;
  }
  return true;
}

// More than the per-lane arena holds: ExtendedMixed reports physical overflow and
// the program is replayed in the larger arena, with the identical result.
bool test_arena_overflow_replay() {
  const auto big_list = [](int length) {
    std::vector<std::int64_t> values;
    for (int i = 0; i < length; ++i) values.push_back(i % 97);
    return IL(values);
  };
  std::vector<BytecodeProgram> programs;
  std::vector<std::string> labels;
  const auto add_compare = [&](const std::string& label, const std::vector<Instr>& body, std::vector<Value> consts,
                               const Value& expected) {
    programs.push_back(compare_program(body, std::move(consts), expected, false).program);
    labels.push_back(label);
  };
  const auto int_const = [&](int slot) { return ins_a(Opcode::PushConst, slot); };
  const auto call = [&](BuiltinId id, int argc) { return ins_ab(Opcode::CallBuiltin, int(id), argc); };
  const auto cpu_result = [&](BuiltinId id, std::vector<Value> args) {
    const auto result = gagp::builtin_call(id, args);
    return result.value;
  };
  {  // 6000 + 6001 list cells.
    const auto range = cpu_result(BuiltinId::Range, {I(0), I(6000)});
    const auto inserted = cpu_result(BuiltinId::Insert, {range, I(3), I(-1)});
    add_compare("range then insert", {int_const(0), int_const(1), call(BuiltinId::Range, 2), int_const(2), int_const(3),
                call(BuiltinId::Insert, 3)}, {I(0), I(6000), I(3), I(-1)}, inserted);
    const auto removed = cpu_result(BuiltinId::RemoveAt, {inserted, I(-2)});
    add_compare("range insert remove_at", {int_const(0), int_const(1), call(BuiltinId::Range, 2), int_const(2), int_const(3),
                call(BuiltinId::Insert, 3), int_const(4), call(BuiltinId::RemoveAt, 2)},
                {I(0), I(6000), I(3), I(-1), I(-2)}, removed);
    const auto replaced = cpu_result(BuiltinId::SetAt, {range, I(5999), I(42)});
    add_compare("range then set_at then range again", {int_const(0), int_const(1), call(BuiltinId::Range, 2), int_const(2), int_const(3),
                call(BuiltinId::SetAt, 3)}, {I(0), I(6000), I(5999), I(42)}, replaced);
  }
  {  // Two long edits of a bound constant overflow the 8192 cells.
    const auto source = big_list(5000);
    const auto first = cpu_result(BuiltinId::Insert, {source, I(7), I(1)});
    const auto second = cpu_result(BuiltinId::SetAt, {first, I(-1), I(2)});
    add_compare("insert then set_at on a constant", {int_const(0), int_const(1), int_const(2), call(BuiltinId::Insert, 3),
                int_const(3), int_const(4), call(BuiltinId::SetAt, 3)}, {source, I(7), I(1), I(-1), I(2)}, second);
  }
  {  // String bytes: two 5000-byte copies exceed the 8192 string bytes.
    const std::string text(5000, 'q');
    const auto once = cpu_result(BuiltinId::Capitalize, {S(text)});
    const auto twice = cpu_result(BuiltinId::Strip, {once});
    add_compare("capitalize then strip", {int_const(0), call(BuiltinId::Capitalize, 1), call(BuiltinId::Strip, 1)}, {S(text)}, twice);
    const auto trimmed = cpu_result(BuiltinId::StripChars, {S(text + "ab"), S("b")});
    const auto pick = cpu_result(BuiltinId::IndexMod, {trimmed, I(4999)});
    add_compare("strip chars then index_mod", {int_const(0), int_const(1), call(BuiltinId::StripChars, 2), int_const(2),
                call(BuiltinId::IndexMod, 2)}, {S(text + "ab"), S("b"), I(4999)}, pick);
  }
  FitnessPolicy policy{FitnessMetric::Exact};
  FitnessEvalResult gpu;
  if (!compare_population(programs, {{}}, {Value::from_bool(true)}, policy, "g3p arena overflow replay", &gpu)) return false;
  if (!check(gpu.payload_replay_programs > 0 && gpu.execution_profile.find("-gpu-arena-replay") != std::string::npos,
             "g3p arena overflow did not go through the replay path")) return false;
  return true;
}

// The logical list allocation limit applies to range exactly as it does to
// singleton and concat; huge bounds fail before anything is allocated.
bool test_allocation_limits() {
  const auto call = [&](BuiltinId id, int argc) { return ins_ab(Opcode::CallBuiltin, int(id), argc); };
  const auto push = [&](int slot) { return ins_a(Opcode::PushConst, slot); };
  struct Case { std::string label; std::vector<Instr> code; std::vector<Value> consts; };
  std::vector<Case> cases;
  const auto length_of_range = [&](const std::string& label, std::int64_t a, std::int64_t b) {
    cases.push_back({label, {push(0), push(1), call(BuiltinId::Range, 2), call(BuiltinId::Len, 1), ins(Opcode::Return)}, {I(a), I(b)}});
  };
  length_of_range("range 0..10", 0, 10);
  length_of_range("range 0..11", 0, 11);
  length_of_range("range reversed", 10, -3);
  length_of_range("range huge", INT64_MIN, INT64_MAX);
  length_of_range("range over the cap", 0, 70000);
  cases.push_back({"two ranges", {push(0), push(1), call(BuiltinId::Range, 2), push(0), push(1), call(BuiltinId::Range, 2),
                   call(BuiltinId::Concat, 2), call(BuiltinId::Len, 1), ins(Opcode::Return)}, {I(0), I(5)}});
  cases.push_back({"range then singleton", {push(0), push(1), call(BuiltinId::Range, 2), push(0), call(BuiltinId::Singleton, 1),
                   call(BuiltinId::Concat, 2), call(BuiltinId::Len, 1), ins(Opcode::Return)}, {I(0), I(9)}});
  cases.push_back({"range then insert", {push(0), push(1), call(BuiltinId::Range, 2), push(0), push(0), call(BuiltinId::Insert, 3),
                   call(BuiltinId::Len, 1), ins(Opcode::Return)}, {I(0), I(10)}});
  cases.push_back({"empty range is free", {push(0), push(0), call(BuiltinId::Range, 2), push(0), push(1), call(BuiltinId::Range, 2),
                   call(BuiltinId::Concat, 2), call(BuiltinId::Len, 1), ins(Opcode::Return)}, {I(3), I(10)}});
  for (const unsigned limit : {10u, 11u, 64u, 65536u}) {
    for (const auto metric : {FitnessMetric::Exact}) {
      std::vector<BytecodeProgram> programs;
      for (const auto& c : cases) {
        BytecodeProgram program;
        program.consts = c.consts;
        program.code = c.code;
        program.instruction_fuel.assign(program.code.size(), 1);
        programs.push_back(program);
      }
      FitnessPolicy policy{metric};
      policy.list_allocation_limit = limit;
      std::vector<CpuFitnessCaseCounts> counts;
      FitnessEvalResult gpu;
      if (!compare_population(programs, {{}}, {I(10)}, policy, "g3p allocation limit " + std::to_string(limit), &gpu, &counts)) return false;
      if (!check(counts[0].errors == 0U && counts[1].errors == (limit >= 11 ? 0U : 1U) &&
                 counts[2].errors == 0U && counts[3].errors == 1U && counts[4].errors == 1U,
                 "g3p allocation limit expectation for range at limit " + std::to_string(limit))) return false;
    }
  }
  return true;
}

// Unresolved contents: list edits publish the same deterministic hash and length
// on both engines.
bool test_unresolved_payload() {
  const auto list = IL({1, 2, 3});
  const auto empty = IL({});
  const auto text = S("  unresolved  ");
  payload::clear();
  std::vector<BytecodeProgram> programs;
  std::vector<Probe> list_probes{
      {"insert", BuiltinId::Insert, {list, I(1), I(7)}}, {"insert negative", BuiltinId::Insert, {list, I(-9), I(7)}},
      {"remove_at", BuiltinId::RemoveAt, {list, I(5)}}, {"remove_at empty", BuiltinId::RemoveAt, {empty, I(5)}},
      {"set_at", BuiltinId::SetAt, {list, I(-1), I(8)}}, {"set_at empty", BuiltinId::SetAt, {empty, I(0), I(8)}},
      {"index_mod empty", BuiltinId::IndexMod, {empty, I(3)}}};
  for (const auto& probe : list_probes) {
    const auto cpu = gagp::builtin_call(probe.id, probe.args);
    if (!check(!cpu.is_error, "g3p unresolved CPU " + probe.label)) return false;
    std::vector<Instr> body;
    for (std::size_t i = 0; i < probe.args.size(); ++i) body.push_back(ins_a(Opcode::PushConst, int(i)));
    body.push_back(ins_ab(Opcode::CallBuiltin, int(probe.id), int(probe.args.size())));
    programs.push_back(compare_program(body, probe.args, cpu.value, false).program);
  }
  // Operations that need contents fail, and the hash-only string results report
  // their length through len().
  for (const auto id : {BuiltinId::StartsWith, BuiltinId::EndsWith}) {
    BytecodeProgram p;
    p.consts = {text, text};
    p.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1), ins_ab(Opcode::CallBuiltin, int(id), 2), ins(Opcode::Return)};
    programs.push_back(p);
  }
  {
    BytecodeProgram p;
    p.consts = {list, I(1)};
    p.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1), ins_ab(Opcode::CallBuiltin, int(BuiltinId::Contains), 2), ins(Opcode::Return)};
    programs.push_back(p);
  }
  for (const auto id : {BuiltinId::Strip, BuiltinId::LStrip, BuiltinId::RStrip, BuiltinId::Capitalize}) {
    BytecodeProgram p;
    p.consts = {text};
    p.code = {ins_a(Opcode::PushConst, 0), ins_ab(Opcode::CallBuiltin, int(id), 1), ins(Opcode::Return)};
    programs.push_back(p);
  }
  FitnessPolicy policy{FitnessMetric::Exact};
  std::vector<CpuFitnessCaseCounts> counts;
  if (!compare_population(programs, {{}}, {Value::from_bool(true)}, policy, "g3p unresolved payload", nullptr, &counts)) return false;
  return check(counts[list_probes.size()].errors == 1U && counts[list_probes.size() + 1].errors == 1U &&
               counts[list_probes.size() + 2].errors == 1U, "g3p unresolved affix/contains must be runtime errors");
}

// A population holding a new builtin must leave every specialized profile that
// cannot execute it. The control population shows the profile is otherwise active.
bool test_profile_families() {
  const auto call = [&](BuiltinId id, int argc) { return ins_ab(Opcode::CallBuiltin, int(id), argc); };
  const auto push = [&](int slot) { return ins_a(Opcode::PushConst, slot); };
  const auto has = [](const FitnessEvalResult& r, const std::string& what) {
    return r.execution_profile.find(what) != std::string::npos;
  };
  const auto active = [&](const FitnessEvalResult& r, const std::string& what) {
    return has(r, what) && !has(r, what + "-declined");
  };
  // Private IntList arenas, private typed phases and the private adjacent concat.
  {
    std::vector<CaseBindings> cases;
    std::vector<Value> answers;
    for (int i = 0; i < 64; ++i) {
      const auto input = IL({i, i + 1, i + 2});
      cases.push_back({{0, input}});
      answers.push_back(input);
    }
    BytecodeProgram pass;
    pass.n_locals = 1;
    pass.code = {ins_a(Opcode::Load, 0), ins(Opcode::Return)};
    BytecodeProgram sliced;
    sliced.n_locals = 1;
    sliced.consts = {I(0), I(2), I(5)};
    sliced.code = {ins_a(Opcode::Load, 0), push(0), push(1), call(BuiltinId::Slice, 3), push(2), call(BuiltinId::Singleton, 1),
                   call(BuiltinId::Concat, 2), ins(Opcode::Return)};
    BytecodeProgram indexed;
    indexed.n_locals = 1;
    indexed.consts = {I(1)};
    indexed.code = {ins_a(Opcode::Load, 0), push(0), call(BuiltinId::Index, 2), call(BuiltinId::Singleton, 1), ins(Opcode::Return)};
    BytecodeProgram inserted;
    inserted.n_locals = 1;
    inserted.consts = {I(1), I(-1)};
    inserted.code = {ins_a(Opcode::Load, 0), push(0), push(1), call(BuiltinId::Insert, 3), ins(Opcode::Return)};
    BytecodeProgram edited;
    edited.n_locals = 1;
    edited.consts = {I(-1), I(77)};
    edited.code = {ins_a(Opcode::Load, 0), push(0), push(1), call(BuiltinId::SetAt, 3), push(0), call(BuiltinId::RemoveAt, 2), ins(Opcode::Return)};
    BytecodeProgram ranged;
    ranged.consts = {I(0), I(3)};
    ranged.code = {push(0), push(1), call(BuiltinId::Range, 2), ins(Opcode::Return)};
    BytecodeProgram picked;
    picked.n_locals = 1;
    picked.consts = {I(INT64_MIN)};
    picked.code = {ins_a(Opcode::Load, 0), push(0), call(BuiltinId::IndexMod, 2), call(BuiltinId::Singleton, 1), ins(Opcode::Return)};
    BytecodeProgram membership;
    membership.n_locals = 1;
    membership.consts = {I(2)};
    membership.code = {ins_a(Opcode::Load, 0), push(0), call(BuiltinId::Contains, 2), ins(Opcode::Return)};
    for (const auto metric : {FitnessMetric::Exact}) {
      FitnessPolicy policy{metric};
      policy.list_allocation_limit = 65536;
      FitnessEvalResult control;
      if (!compare_population({pass, sliced, indexed}, cases, answers, policy, "g3p private control", &control)) return false;
      if (!check(active(control, "-private-intlist-offset-v1"), "g3p control must use private IntList arenas")) return false;
      for (const auto* program : {&inserted, &edited, &ranged, &picked, &membership}) {
        FitnessEvalResult mixed;
        if (!compare_population({pass, sliced, indexed, *program}, cases, answers, policy, "g3p private mixed", &mixed)) return false;
        if (!check(!active(mixed, "-private-intlist-offset-v1") && !active(mixed, "-private-typed-phases") &&
                   !active(mixed, "-private-adjacent-concat") && !active(mixed, "-compact-intlist-cells") &&
                   !has(mixed, "int-list-views-typed") && !active(mixed, "-private-bound-phases"),
                   "g3p new builtin stayed in a private IntList profile: " + mixed.execution_profile)) return false;
      }
    }
  }
  // Private tuple scoring: the proof carries a bound for every allocation.
  {
    FitnessPolicy policy{FitnessMetric::TupleSummary};
    policy.summary_count = 1;
    policy.list_allocation_limit = 65536;
    const auto answer = IL({3, 4});
    BytecodeProgram tuple;
    tuple.consts = {I(3), I(4)};
    tuple.code = {push(0), call(BuiltinId::Singleton, 1), push(1), call(BuiltinId::Singleton, 1), call(BuiltinId::Concat, 2), ins(Opcode::Return)};
    BytecodeProgram direct;
    direct.consts = {answer};
    direct.code = {push(0), ins(Opcode::Return)};
    FitnessEvalResult control;
    if (!compare_population({tuple, direct}, {{}}, {answer}, policy, "g3p tuple control", &control)) return false;
    if (!check(active(control, "-private-tuple-score"), "g3p tuple control must use private tuple scoring")) return false;
    BytecodeProgram inserted;
    inserted.consts = {answer, I(0), I(5)};
    inserted.code = {push(0), push(1), push(2), call(BuiltinId::Insert, 3), push(1), call(BuiltinId::RemoveAt, 2), ins(Opcode::Return)};
    BytecodeProgram ranged;
    ranged.consts = {I(3), I(5)};
    ranged.code = {push(0), push(1), call(BuiltinId::Range, 2), ins(Opcode::Return)};
    for (const auto* program : {&inserted, &ranged}) {
      FitnessEvalResult mixed;
      if (!compare_population({tuple, direct, *program}, {{}}, {answer}, policy, "g3p tuple mixed", &mixed)) return false;
      if (!check(has(mixed, "-private-tuple-score-declined"), "g3p new builtin kept private tuple scoring")) return false;
    }
  }
  // Typed views, view phases, shape result cache and the other region profiles:
  // a population of regions the view profile accepts, plus one with a new builtin.
  {
    const auto region_program = [&](RegionPhase body, ValueTag result_type) {
      auto program = invocation(unary_segment(result_type, std::move(body), constant_phase(nominal_value(result_type))), 0);
      program.instruction_fuel.assign(program.code.size(), 1);
      return program;
    };
    const auto scalar_phase = [&](std::vector<Value> constants, std::vector<Instr> code) {
      return phase(std::move(constants), std::move(code));
    };
    const auto eligible = region_program(scalar_phase({I(3)}, {push(0), ins(Opcode::Return)}), ValueTag::Int);
    const auto listed = region_program(scalar_phase({I(9), I(0), I(5)}, {push(0), push(1), push(2), call(BuiltinId::Clip, 3), ins(Opcode::Return)}), ValueTag::Int);
    const auto ranged = region_program(scalar_phase({I(0), I(4), I(7)},
        {push(0), push(1), call(BuiltinId::Range, 2), push(2), call(BuiltinId::IndexMod, 2), ins(Opcode::Return)}), ValueTag::Int);
    const auto stripped = region_program(scalar_phase({S("  ab  "), S("x")},
        {push(0), call(BuiltinId::Strip, 1), call(BuiltinId::Len, 1), ins(Opcode::Return)}), ValueTag::Int);
    const auto listy = region_program(scalar_phase({I(0), I(4)},
        {push(0), push(1), call(BuiltinId::Range, 2), push(0), push(1), call(BuiltinId::Insert, 3), call(BuiltinId::Len, 1), ins(Opcode::Return)}), ValueTag::Int);
    const auto membership = region_program(scalar_phase({I(0), I(4), I(3)},
        {push(0), push(1), call(BuiltinId::Range, 2), push(2), call(BuiltinId::Contains, 2), ins(Opcode::Return)}), ValueTag::Bool);
    for (const auto metric : {FitnessMetric::Legacy, FitnessMetric::Exact}) {
      FitnessPolicy policy{metric};
      const auto answer = I(3);
      FitnessEvalResult control;
      if (!compare_population({eligible, listed}, {{}, {}, {}}, {answer, answer, answer}, policy, "g3p region control", &control)) return false;
      if (!check(has(control, "int-list-views-typed"),
                 "g3p region control must use the typed view profile: " + control.execution_profile)) return false;
      for (const auto* program : {&ranged, &stripped, &listy, &membership}) {
        FitnessEvalResult mixed;
        if (!compare_population({eligible, listed, *program}, {{}, {}, {}}, {answer, answer, answer}, policy, "g3p region mixed", &mixed)) return false;
        if (!check(!has(mixed, "int-list-views-typed") && !active(mixed, "-private-typed-phases") &&
                   !active(mixed, "-trivial-phases") && !active(mixed, "-private-bound-phases") &&
                   !active(mixed, "-shape-result-cache") && !active(mixed, "-integer-case-reuse"),
                   "g3p new builtin stayed in a view profile: " + mixed.execution_profile)) return false;
      }
    }
  }
  return true;
}

// Concatenation, window aliases and the concat cache share the arena with the
// edits: results must keep their identity when edits read concat results.
bool test_concat_interaction() {
  const auto call = [&](BuiltinId id, int argc) { return ins_ab(Opcode::CallBuiltin, int(id), argc); };
  const auto push = [&](int slot) { return ins_a(Opcode::PushConst, slot); };
  const auto left = IL({1, 2, 3}), right = IL({4, 5});
  std::vector<BytecodeProgram> programs;
  const auto cpu_apply = [&](BuiltinId id, std::vector<Value> args) { return gagp::builtin_call(id, args).value; };
  const auto joined = cpu_apply(BuiltinId::Concat, {left, right});
  const auto edited = cpu_apply(BuiltinId::Insert, {joined, I(2), I(99)});
  programs.push_back(compare_program({push(0), push(1), call(BuiltinId::Concat, 2), push(2), push(3), call(BuiltinId::Insert, 3)},
                                     {left, right, I(2), I(99)}, edited, false).program);
  const auto again = cpu_apply(BuiltinId::Concat, {edited, edited});
  programs.push_back(compare_program({push(0), push(1), call(BuiltinId::Concat, 2), push(2), push(3), call(BuiltinId::Insert, 3), push(4), call(BuiltinId::Concat, 2)},
                                     {left, right, I(2), I(99), edited}, again, false).program);
  const auto window = cpu_apply(BuiltinId::Slice, {joined, I(1), I(-1)});
  const auto windowed = cpu_apply(BuiltinId::SetAt, {window, I(-5), I(0)});
  programs.push_back(compare_program({push(0), push(1), call(BuiltinId::Concat, 2), push(2), push(3), call(BuiltinId::Slice, 3), push(4), push(5), call(BuiltinId::SetAt, 3)},
                                     {left, right, I(1), I(-1), I(-5), I(0)}, windowed, false).program);
  const auto picked = cpu_apply(BuiltinId::IndexMod, {window, I(7)});
  programs.push_back(compare_program({push(0), push(1), call(BuiltinId::Concat, 2), push(2), push(3), call(BuiltinId::Slice, 3), push(4), call(BuiltinId::IndexMod, 2)},
                                     {left, right, I(1), I(-1), I(7)}, picked, false).program);
  const auto removed = cpu_apply(BuiltinId::RemoveAt, {cpu_apply(BuiltinId::Prepend, {joined, I(0)}), I(3)});
  programs.push_back(compare_program({push(0), push(1), call(BuiltinId::Concat, 2), push(2), call(BuiltinId::Prepend, 2), push(3), call(BuiltinId::RemoveAt, 2)},
                                     {left, right, I(0), I(3)}, removed, false).program);
  const auto text = cpu_apply(BuiltinId::Concat, {S(" ab"), S("cd ")});
  const auto stripped = cpu_apply(BuiltinId::Strip, {text});
  programs.push_back(compare_program({push(0), push(1), call(BuiltinId::Concat, 2), call(BuiltinId::Strip, 1)}, {S(" ab"), S("cd "), I(0)}, stripped, false).program);
  const auto capital = cpu_apply(BuiltinId::Capitalize, {cpu_apply(BuiltinId::Reverse, {S("dcba")})});
  programs.push_back(compare_program({push(0), call(BuiltinId::Reverse, 1), call(BuiltinId::Capitalize, 1)}, {S("dcba")}, capital, false).program);
  FitnessPolicy policy{FitnessMetric::Exact};
  FitnessEvalResult gpu;
  if (!compare_population(programs, {{}}, {Value::from_bool(true)}, policy, "g3p concat interaction", &gpu)) return false;
  return check(gpu.fitness == std::vector<double>(programs.size(), 0.0), "g3p concat interaction scores");
}

bool run() {
  return test_signature_parity() && test_bound_inputs() && test_arena_overflow_replay() && test_allocation_limits() &&
         test_unresolved_payload() && test_profile_families() && test_concat_interaction();
}

}  // namespace g3p_builtin_checks
