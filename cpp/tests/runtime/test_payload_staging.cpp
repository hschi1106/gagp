#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/runtime/cpu/builtins_cpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "../../src/runtime/payload/staging.hpp"

using namespace gagp;
using namespace gagp::payload;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Result { std::unique_ptr<StagedPayloads> transaction; Value strings; };
Result generate(const std::string& text) {
  Result result{std::make_unique<StagedPayloads>(), Value::invalid()};
  StagedPayloads::Scope scope(*result.transaction);
  const auto string = make_string_value(text);
  result.strings = make_string_list_value({string});
  std::vector<Value> values;
  std::string decoded;
  Value indexed;
  check(lookup_list_packed(result.strings.tag, result.strings.i, &values) && values.size() == 1,
        "staged list lookup failed");
  check(lookup_string_packed(values[0].i, &decoded) && decoded == text,
        "staged nested string lookup failed");
  check(lookup_index(result.strings, 0, &indexed) && indexed.i == string.i,
        "staged index lookup failed");
  return result;
}
}
int main() {
  try {
    clear();
    auto one = std::async(std::launch::async, generate, "first");
    auto two = std::async(std::launch::async, generate, "second");
    auto a = one.get(), b = two.get();
    check(stats().string_entries == 0 && stats().list_entries == 0, "staged writes leaked globally");
    check(StagedPayloads::commit_all({a.transaction.get(), b.transaction.get()}), "independent commit failed");
    check(stats().string_entries == 2 && stats().list_entries == 2, "commit lost payloads");
    std::vector<Value> values;
    std::string decoded;
    check(lookup_list(a.strings, &values) && lookup_string(values[0], &decoded) && decoded == "first",
          "committed typed-list closure missing");

    const auto collision = Value::from_string_hash_len(1234567, 1);
    StagedPayloads left, right;
    { StagedPayloads::Scope scope(left); register_string(collision, "a"); }
    { StagedPayloads::Scope scope(right); register_string(collision, "b"); }
    check(!StagedPayloads::commit_all({&left, &right}), "cross-worker collision accepted");
    check(!lookup_string(collision, &decoded), "failed commit partially registered a payload");
    register_string(collision, "a");
    StagedPayloads overwrite;
    { StagedPayloads::Scope scope(overwrite); register_string(collision, "b"); }
    check(!StagedPayloads::commit_all({&overwrite}) && lookup_string(collision, &decoded) && decoded == "a",
          "registry collision changed committed contents");

    StagedPayloads reader;
    { StagedPayloads::Scope scope(reader);
      check(lookup_string(collision, &decoded) && decoded == "a", "base read failed");
      auto update = std::async(std::launch::async, [&] { register_string(collision, "b"); });
      update.get();
      check(lookup_string(collision, &decoded) && decoded == "a",
            "repeated staged read lost its captured value");
    }
    check(!StagedPayloads::commit_all({&reader}), "changed read dependency was ignored");
    const auto absent = Value::from_string_hash_len(9876543, 1);
    StagedPayloads missing, writer;
    { StagedPayloads::Scope scope(missing); check(!lookup_string(absent, &decoded), "absent key unexpectedly exists"); }
    { StagedPayloads::Scope scope(writer); register_string(absent, "x"); }
    check(!StagedPayloads::commit_all({&missing, &writer}) && !lookup_string(absent, &decoded),
          "missing-read conflict changed registry");

    const auto floating = Value::from_float_list_hash_len(1234567, 1);
    register_list(floating, {Value::from_float(0.0)});
    StagedPayloads negative_zero;
    { StagedPayloads::Scope scope(negative_zero); register_list(floating, {Value::from_float(-0.0)}); }
    check(!StagedPayloads::commit_all({&negative_zero}), "float-list conflict lost signed zero");
    const auto nested_string = make_string_value("old");
    const auto nested_list = make_string_list_value({nested_string});
    StagedPayloads reusable;
    check(!reusable.read_snapshot_unchanged(), "unsealed snapshot was reusable");
    {
      StagedPayloads::Scope scope(reusable);
      check(lookup_list(nested_list, &values) && lookup_string(values.front(), &decoded),
            "snapshot did not read the typed-list closure");
      check(!reusable.read_snapshot_unchanged(), "active snapshot was reusable");
    }
    check(reusable.read_snapshot_unchanged() && reusable.read_snapshot_unchanged(),
          "read-only validation consumed its snapshot");
    check(StagedPayloads::commit_all({&reusable}) && reusable.read_snapshot_unchanged(),
          "committed read-only snapshot could not be revalidated");
    register_string(nested_string, "new");
    check(!reusable.read_snapshot_unchanged(), "nested string overwrite was ignored");
    register_string(nested_string, "old");
    check(reusable.read_snapshot_unchanged(), "exact restored closure did not revalidate");
    register_list(nested_list, {});
    check(!reusable.read_snapshot_unchanged(), "outer typed-list overwrite was ignored");
    check(!negative_zero.read_snapshot_unchanged(), "write transaction was treated as read-only");
    StagedPayloads missing_snapshot;
    { StagedPayloads::Scope scope(missing_snapshot); (void)lookup_string(absent, &decoded); }
    check(missing_snapshot.read_snapshot_unchanged(), "absent read did not revalidate");
    register_string(absent, "x");
    check(!missing_snapshot.read_snapshot_unchanged(), "absent-to-present change was ignored");
    StagedPayloads enclosing;
    { StagedPayloads::Scope scope(enclosing);
      check(!reusable.read_snapshot_unchanged(), "snapshot reuse ignored an enclosing view");
    }

    // Evaluation scopes retain inputs but discard generated list/string closures.
    const auto input = make_int_list_value({Value::from_int(7)});
    const auto expected = Value::from_int(2);
    const auto before = stats();
    BytecodeProgram concat;
    concat.consts = {input};
    concat.code = {{Opcode::PushConst, 0, 0, true, false},
                   {Opcode::PushConst, 0, 0, true, false},
                   {Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2, true, true},
                   {Opcode::CallBuiltin, static_cast<int>(BuiltinId::Len), 1, true, true},
                   {Opcode::Return, 0, 0, false, false}};
    const auto score = eval_fitness_cpu({concat}, std::vector<CaseBindings>(100),
                                      std::vector<Value>(100, expected), 100, 1000.0);
    check(score.size() == 1 && score[0] == 0.0, "temporary payload score changed");
    check(stats().list_entries == before.list_entries &&
          stats().list_value_count == before.list_value_count,
          "fitness evaluation retained case temporaries");
    BytecodeProgram amplified;
    amplified.n_locals = 1;
    amplified.consts = {input};
    amplified.code = {{Opcode::PushConst, 0, 0, true, false},
                      {Opcode::Store, 0, 0, true, false}};
    for (int i = 0; i < 25; ++i) {
      amplified.code.push_back({Opcode::Load, 0, 0, true, false});
      amplified.code.push_back({Opcode::Load, 0, 0, true, false});
      amplified.code.push_back({Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2, true, true});
      amplified.code.push_back({Opcode::Store, 0, 0, true, false});
    }
    amplified.code.push_back({Opcode::Load, 0, 0, true, false});
    amplified.code.push_back({Opcode::Return, 0, 0, false, false});
    const auto bounded_score = eval_fitness_cpu({amplified}, std::vector<CaseBindings>(2),
                                               {input, input}, 1000, 1000.0);
    check(bounded_score.size() == 1 && bounded_score[0] == -2000.0,
          "payload exhaustion did not produce the ordinary fitness penalty");
    check(stats().list_value_count == before.list_value_count,
          "resource-limited fitness retained case temporaries");
    bool limited = false;
    { StagedPayloads bounded(4096);
      StagedPayloads::Scope scope(bounded);
      auto value = input;
      try {
        for (int i = 0; i < 30; ++i) {
          auto result = builtin_call(BuiltinId::Concat, {value, value});
          check(!result.is_error, "bounded concat failed before resource limit");
          value = result.value;
        }
      } catch (const PayloadBudgetExceeded&) { limited = true; }
    }
    check(limited, "exponential concat exceeded bounded payload allocation");
    check(stats().list_value_count == before.list_value_count,
          "failed evaluation retained generated payloads");

    StagedPayloads guard;
    { StagedPayloads::Scope scope(guard);
      bool rejected = false;
      try { clear(); } catch (const std::logic_error&) { rejected = true; }
      check(rejected, "worker cleared the global registry");
    }
    std::cout << "payload staging isolation, ordered conflicts and typed closure passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
