# Repository Layout

Where things are. The repository tests check that every path in the first
column exists.

| Path | Contents |
| --- | --- |
| `README.md` | What GAGP is, results, build and quick start |
| `README.zh-TW.md` | The same in Traditional Chinese |
| `VERSION.md` | Releases and compatibility |
| `AGENTS.md` | Rules for coding agents and contributors |
| `.agents/skills/gagp-navigation/SKILL.md` | Where to find the code, contract and tests of a feature |
| `examples/` | Complete runnable tasks |
| `benchmarks/` | The `asgp30` benchmark suite and its runner, `run.py` |
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
| `cpp/src/evolution/grammar/` | Grammar loading, compilation, generation, membership and variation |
| `cpp/src/evolution/repro/backend.cpp` | Host reproduction of native AST populations |
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
