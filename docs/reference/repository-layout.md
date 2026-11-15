# Repository Layout

Where things are. The repository tests check that every path in the first
column exists.

| Path | Contents |
| --- | --- |
| `README.md` | What GAGP is, the four claims with the commands that reproduce them, the limits, build and quick start |
| `README.zh-TW.md` | The same in Traditional Chinese |
| `VERSION.md` | Releases and compatibility |
| `AGENTS.md` | Rules for coding agents and contributors |
| `.agents/skills/gagp-navigation/SKILL.md` | Where to find the code, contract and tests of a feature |
| `examples/` | Complete runnable tasks |
| `benchmarks/` | The `asgp30` and `psb1_g3p` benchmark suites and their runner |
| `benchmarks/README.md` | The benchmark guide, the runner options and the recorded results |
| `benchmarks/run.py` | Runs a suite on either engine and writes the report |
| `benchmarks/test_run.py` | Tests of the runner on the `asgp30` suite |
| `benchmarks/test_run_psb1_g3p.py` | Tests of the runner on the `psb1_g3p` suite |
| `benchmarks/asgp30/` | The 30 ASGP tasks |
| `benchmarks/asgp30/asgp_paper.json` | The published ASGP success counts |
| `benchmarks/psb1_g3p/` | Six PSB1 tasks with G3P grammars |
| `benchmarks/psb1_g3p/literature.json` | The published G3P success counts |
| `benchmarks/psb1_g3p/README.md` | The `psb1_g3p` guide: data, settings, differences from G3P |
| `benchmarks/psb1_g3p/build.py` | Generator of the `psb1_g3p` grammars, tasks and case files |
| `configs/search/default.json` | The default search configuration |
| `configs/grammar/` | Grammar packages and example grammars |
| `configs/grammar_definitions/` | Grammars used by the bounded-region tests |
| `data/fixtures/` | Case fixtures used by tests |
| `spec/` | Normative formats and semantics |
| `docs/` | Guides, design notes and references ([index](../README.md)) |
| `cpp/CMakeLists.txt` | Every native target and test |
| `cpp/include/gagp/` | Public headers, grouped like `cpp/src` |
| `cpp/src/cli/` | The `gagp` program: commands, task and search files, JSON codecs |
| `cpp/src/evolution/` | ASTs, verification, compilation, population initialization, the evolution loop |
| `cpp/src/evolution/grammar/` | Grammar loading, compilation, generation and membership |
| `cpp/src/evolution/repro/gpu/` | The resident engine: GPU and CPU-reference phase populations |
| `cpp/src/runtime/` | Bytecode verification and bounded-region plans |
| `cpp/src/runtime/cpu/` | The CPU interpreter, builtins and fitness |
| `cpp/src/runtime/gpu/` | The CUDA interpreter, GPU fitness and payload arena |
| `cpp/src/runtime/payload/` | The host payload registry for strings and lists |
| `cpp/src/serialization/` | JSON codec of region plans |
| `cpp/tests/` | Native tests, by area (`cli`, `evolution`, `gpu`, `parity`, `runtime`, `fuzz`) |
| `tests/repository/` | Repository and documentation checks |

Build trees (`cpp/build*`), run outputs (`logs/`, `runs/`) and downloaded data
are not versioned.
