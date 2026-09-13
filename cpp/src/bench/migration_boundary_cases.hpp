// Included only with the immutable runtime/test_asgp_semantics.cpp helpers.
// These additional inputs observe production behavior; they do not define a new
// evaluator or assume CPU/GPU agreement beyond the existing implementation.
int run_extra_boundary_cases() {
  using namespace gagp;
  const auto observe = [](const BytecodeProgram& program) {
    return gagp::execute_bytecode_cpu(program, {}, 20000);
  };
  const std::vector<Value> result_types{
      Value::from_int(7), Value::from_float(1.25), Value::from_bool(true), Value::from_char('q'),
      payload::make_string_value("result"), payload::make_int_list_value({Value::from_int(7)}),
      payload::make_float_list_value({Value::from_float(1.25)}),
      payload::make_string_list_value({payload::make_string_value("result")})};
  const std::vector<Value> sources{
      payload::make_string_value("abc"),
      payload::make_int_list_value({Value::from_int(1), Value::from_int(2), Value::from_int(3)}),
      payload::make_float_list_value({Value::from_float(1.0), Value::from_float(2.0), Value::from_float(3.0)}),
      payload::make_string_list_value({payload::make_string_value("a"), payload::make_string_value("b"),
                                        payload::make_string_value("c")})};
  for (const auto& source : sources) {
    for (const auto& result : result_types) {
      auto solve = phase({result}, {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)}, 3,
                          {{10, 0}, {11, 1}, {12, 2}});
      auto combine = phase({}, {ins_a(Opcode::Load, 0), ins(Opcode::Return)}, 2, {{14, 0}, {15, 1}});
      const auto program = dc_program(source, dc_segment(solve, dc_divide(Value::from_int(1)), combine));
      (void)observe(program);
    }
  }
  // Frame stack boundaries: split=1 produces a right spine; split=n-1 a left spine.
  for (int count : {0, 1, 63, 64, 65, 66}) {
    std::vector<Value> elements(static_cast<std::size_t>(count), Value::from_int(1));
    auto solve = phase({}, {ins_a(Opcode::Load, 1), ins(Opcode::Return)}, 3, {{10, 0}, {11, 1}, {12, 2}});
    for (int split : {-999, 999}) {
      (void)observe(dc_program(payload::make_int_list_value(elements),
          dc_segment(solve, dc_divide(Value::from_int(split)), dc_combine(BuiltinId::Abs))));
    }
  }
  // DP1D frame and memo boundaries are probed separately: offsets 2/3 visit
  // more than 128 states without requiring a 128-deep dependency stack.
  for (int state : {126, 127, 128, 129, 200}) {
    for (const auto& offsets : {std::vector<int>{1}, std::vector<int>{2, 3}, std::vector<int>{1, 1}}) {
      auto segment = dp1_segment(offsets.size() == 2, Value::from_int(0));
      segment.hi = 256;
      segment.dep_offsets = offsets;
      (void)observe(dp1_program(Value::from_int(state), segment));
    }
  }
  for (const auto& cell : {std::pair<int, int>{10, 10}, {11, 11}, {12, 12}, {0, 126}, {0, 127}, {0, 128}}) {
    auto segment = dp2_segment(Value::from_int(0));
    segment.i_hi = 128; segment.j_hi = 128;
    (void)observe(dp2_program(Value::from_int(cell.first), Value::from_int(cell.second), segment));
  }
  // Every dependency direction/pattern and every result type has a small,
  // terminating observation. Transition returns the first dependency unchanged.
  for (int direction : {-1, 1}) {
    for (int arity : {1, 2, 3}) {
      for (const auto& value : result_types) {
        auto segment = dp1_segment(false, value);
        segment.lo = 0; segment.hi = 4; segment.base_state = direction < 0 ? 0 : 4;
        segment.dep_kind = direction;
        segment.dep_offsets.clear(); segment.transition_dep_names.clear();
        for (int i = 0; i < arity; ++i) {
          segment.dep_offsets.push_back(i + 1); segment.transition_dep_names.push_back(22 + i);
        }
        segment.solve = phase({value}, {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)}, 1, {{20, 0}});
        segment.transition = phase({}, {ins_a(Opcode::Load, 1), ins(Opcode::Return)}, arity + 1, {{21, 0}});
        for (int i = 0; i < arity; ++i) segment.transition.binder_locals[22 + i] = i + 1;
        (void)observe(dp1_program(Value::from_int(2), segment));
      }
    }
  }
  for (int pattern = 0; pattern < 6; ++pattern) {
    for (const auto& value : result_types) {
      auto segment = dp2_segment(value);
      segment.i_hi = 4; segment.j_hi = 4;
      segment.base_i = segment.base_j = pattern % 2 ? 4 : 0;
      segment.dep_kind = pattern;
      const int arity = pattern < 2 ? 2 : pattern < 4 ? 1 : 3;
      segment.transition_dep_names.resize(static_cast<std::size_t>(arity));
      segment.solve = phase({value}, {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)}, 2, {{30, 0}, {31, 1}});
      segment.transition = phase({}, {ins_a(Opcode::Load, 2), ins(Opcode::Return)}, arity + 2, {{32, 0}, {33, 1}});
      for (int i = 0; i < arity; ++i) {
        segment.transition_dep_names[static_cast<std::size_t>(i)] = 34 + i;
        segment.transition.binder_locals[34 + i] = i + 2;
      }
      (void)observe(dp2_program(Value::from_int(2), Value::from_int(2), segment));
    }
  }
  // Boundary must precede the base test even if an invalid base lies outside bounds.
  auto outside1 = dp1_segment(false, Value::from_int(99));
  outside1.base_state = -1;
  (void)observe(dp1_program(Value::from_int(-1), outside1));
  auto outside2 = dp2_segment(Value::from_int(99));
  outside2.base_i = -1;
  (void)observe(dp2_program(Value::from_int(-1), Value::from_int(0), outside2));
  const auto named_observe = [&](const char* name, const BytecodeProgram& program) {
    gagp::migration::observation_name = name;
    (void)observe(program);
    gagp::migration::observation_name.clear();
  };
  // Distinct competing errors expose traversal order directly. No error is
  // inferred from fitness (both errors receive the same fitness penalty).
  const auto division_error = [](int locals,
      std::initializer_list<std::pair<const int, int>> bindings) {
    return phase({Value::from_int(1), Value::from_int(0)},
        {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1), ins(Opcode::Div),
         ins(Opcode::Return)}, locals, bindings);
  };
  const auto hidden_error = [](int locals,
      std::initializer_list<std::pair<const int, int>> bindings) {
    return phase({}, {ins_a(Opcode::Load, locals), ins(Opcode::Return)}, locals + 1, bindings);
  };
  const Value pair = payload::make_int_list_value({Value::from_int(1), Value::from_int(2)});
  const auto bad_solve = division_error(3, {{10, 0}, {11, 1}, {12, 2}});
  const auto bad_divide = hidden_error(1, {{13, 0}});
  named_observe("dc.source-before-divide-and-solve", dc_program(Value::from_int(1),
      dc_segment(bad_solve, bad_divide, dc_combine(BuiltinId::Abs))));
  named_observe("dc.divide-before-solve", dc_program(pair,
      dc_segment(bad_solve, bad_divide, dc_combine(BuiltinId::Abs))));
  auto ordered_solve = phase({Value::from_int(0), Value::from_int(1)},
      {ins_a(Opcode::Load, 2), ins_a(Opcode::PushConst, 0), ins(Opcode::Eq),
       ins_a(Opcode::JmpIfFalse, 8), ins_a(Opcode::PushConst, 1),
       ins_a(Opcode::PushConst, 0), ins(Opcode::Div), ins(Opcode::Return),
       ins_a(Opcode::Load, 3), ins(Opcode::Return)},
      4, {{10, 0}, {11, 1}, {12, 2}});
  named_observe("dc.left-before-right", dc_program(pair,
      dc_segment(ordered_solve, dc_divide(Value::from_int(1)), dc_combine(BuiltinId::Abs))));
  named_observe("dc.solve-before-combine", dc_program(pair,
      dc_segment(bad_solve, dc_divide(Value::from_int(1)), hidden_error(2, {{14, 0}, {15, 1}}))));
  for (int target = 0; target < 3; ++target) {
    auto segment = dc_segment(dc_index_solve(), dc_divide(Value::from_int(1)), dc_combine(BuiltinId::Abs));
    if (target == 0) segment.solve = hidden_error(3, {{10, 0}, {11, 1}, {12, 2}});
    if (target == 1) segment.divide = hidden_error(1, {{13, 0}});
    if (target == 2) segment.combine = hidden_error(2, {{14, 0}, {15, 1}});
    named_observe(target == 0 ? "scope.dc.solve" : target == 1 ? "scope.dc.divide" : "scope.dc.combine",
                  dc_program(pair, segment));
  }
  for (bool solve : {true, false}) {
    auto one = dp1_segment(false, Value::from_int(0));
    if (solve) one.solve = hidden_error(1, {{20, 0}});
    else one.transition = hidden_error(2, {{21, 0}, {22, 1}});
    named_observe(solve ? "scope.dp1.solve" : "scope.dp1.transition",
                  dp1_program(Value::from_int(solve ? 0 : 1), one));
    auto two = dp2_segment(Value::from_int(0));
    if (solve) two.solve = hidden_error(2, {{30, 0}, {31, 1}});
    else two.transition = hidden_error(5, {{32, 0}, {33, 1}, {34, 2}, {35, 3}, {36, 4}});
    named_observe(solve ? "scope.dp2.solve" : "scope.dp2.transition",
                  dp2_program(Value::from_int(solve ? 0 : 1), Value::from_int(solve ? 0 : 1), two));
  }
  // Reference Mixed scratch capacities: 512 string bytes, 128 list values,
  // and 32 entries per payload kind (constants_gpu.hpp at b049183).
  const auto concat_program = [](Value a, Value b) {
    BytecodeProgram program;
    program.consts = {a, b};
    program.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
        ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2), ins(Opcode::Return)};
    return program;
  };
  for (int length : {511, 512, 513}) {
    const std::string name = "payload.string-bytes." + std::to_string(length);
    named_observe(name.c_str(), concat_program(payload::make_string_value(std::string(length - 1, 'x')),
                                              payload::make_string_value("y")));
  }
  for (int length : {127, 128, 129}) {
    for (int type = 0; type < 3; ++type) {
      const Value element = type == 0 ? Value::from_int(7) : type == 1 ? Value::from_float(1.25)
                                                                 : payload::make_string_value("element");
      const auto list = [&](int count) {
        std::vector<Value> values(static_cast<std::size_t>(count), element);
        return type == 0 ? payload::make_int_list_value(values) : type == 1 ? payload::make_float_list_value(values)
                                                                         : payload::make_string_list_value(values);
      };
      const std::string name = "payload.list-values.type" + std::to_string(type) + "." + std::to_string(length);
      named_observe(name.c_str(), concat_program(list(length - 1), list(1)));
    }
  }
  for (int entries : {31, 32, 33}) {
    BytecodeProgram program;
    program.n_locals = 1;
    program.consts.push_back(payload::make_string_value("_"));
    for (int entry = 0; entry < entries; ++entry) {
      program.consts.push_back(payload::make_string_value(std::string(1, static_cast<char>('!' + entry))));
      program.code.insert(program.code.end(), {ins_a(Opcode::PushConst, entry + 1), ins_a(Opcode::PushConst, 0),
          ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2), ins_a(Opcode::Store, 0)});
    }
    program.code.insert(program.code.end(), {ins_a(Opcode::Load, 0), ins(Opcode::Return)});
    const std::string name = "payload.string-entries." + std::to_string(entries);
    named_observe(name.c_str(), program);
  }
  for (bool reversed : {false, true}) {
    auto segment = dp1_segment(true, Value::from_int(0));
    segment.base_state = 1;
    segment.dep_offsets = reversed ? std::vector<int>{2, 1} : std::vector<int>{1, 2};
    segment.solve = division_error(1, {{20, 0}});
    segment.transition = hidden_error(3, {{21, 0}, {22, 1}, {23, 2}});
    named_observe(reversed ? "dp1.first-error.offsets-2-1" : "dp1.first-error.offsets-1-2",
                  dp1_program(Value::from_int(2), segment));
  }
  for (bool first_is_base : {true, false}) {
    auto segment = dp2_segment(Value::from_int(0));
    segment.dep_kind = 0;
    segment.base_i = first_is_base ? 0 : 1;
    segment.base_j = first_is_base ? 1 : 0;
    segment.transition_dep_names.resize(2);
    segment.solve = division_error(2, {{30, 0}, {31, 1}});
    segment.transition = hidden_error(4, {{32, 0}, {33, 1}, {34, 2}, {35, 3}});
    named_observe(first_is_base ? "dp2.first-error.first-dependency-base" : "dp2.first-error.second-dependency-base",
                  dp2_program(Value::from_int(1), Value::from_int(1), segment));
  }
  // Two values per entry keep all 33 outputs below the 128-value limit,
  // isolating the separate 32-entry table capacity for every typed-list kind.
  for (int type = 0; type < 3; ++type) {
    const auto singleton = [&](int value) {
      if (type == 0) return payload::make_int_list_value({Value::from_int(value)});
      if (type == 1) return payload::make_float_list_value({Value::from_float(value + 0.25)});
      return payload::make_string_list_value({payload::make_string_value(std::to_string(value))});
    };
    for (int entries : {31, 32, 33}) {
      BytecodeProgram program;
      program.n_locals = 1;
      program.consts.push_back(singleton(-1));
      for (int entry = 0; entry < entries; ++entry) {
        program.consts.push_back(singleton(entry));
        program.code.insert(program.code.end(), {ins_a(Opcode::PushConst, entry + 1), ins_a(Opcode::PushConst, 0),
            ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2), ins_a(Opcode::Store, 0)});
      }
      program.code.insert(program.code.end(), {ins_a(Opcode::Load, 0), ins(Opcode::Return)});
      const std::string name = "payload.list-entries.type" + std::to_string(type) + "." + std::to_string(entries);
      named_observe(name.c_str(), program);
    }
  }
  return 0;
}
