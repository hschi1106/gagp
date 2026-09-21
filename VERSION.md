# Version

Current release: 2.0.0

GAGP—GPU-Accelerated Genetic Programming for Program Synthesis—defines the
current grammar, AST, bytecode, builtin, fitness, fixture, and compiled grammar
contracts in `spec/`.

Release 2.0.0 makes compiled grammar definitions the sole production authoring,
generation, variation, and evolution architecture. The old `grammar-config`
format and release-1 ASTs are accepted only by offline migration tooling.
Release-1 bytecode and seed-only populations are rejected because they do not
contain enough source/type or materialized-program information for an exact
conversion.

| Contract | Release 1 | Release 2 | Compatibility rule |
| --- | --- | --- | --- |
| Native AST | `ast-prefix` | `ast-prefix-v2` | Migrate the materialized release-1 AST offline. Numeric node kinds 53–70 remain unassigned in v2. |
| Bytecode request | `bytecode-json` | `bytecode-json-v2` | Recompile a migrated source AST; release-1 bytecode is rejected. |
| Bytecode fixture | `bytecode-fixture` | `bytecode-fixture-v2` | Regenerate from v2 source/bytecode; release-1 fixtures are rejected. |
| Grammar definition | transitional `grammar-definition-v1` | `grammar-definition-v2` | Re-resolve or migrate the source definition; the production CLI accepts v2 only. |
| Generated program | `grammar-generated-v1` | `grammar-generated-v2` | A complete v1 member migrates directly as a materialized program; do not claim changed RNG regeneration is exact replay. |
| Generated population | `grammar-population-v1` | `grammar-population-v2` | Replay is same-version only. Extract and migrate every complete materialized member offline. |
| Migrated materialization | none | `grammar-materialized-v2` | Offline migration output accepted for AST evaluation and population conversion. |
| Search configuration | `grammar-config` | compiled `grammar-definition-v2` | Use the explicit constrained intent profile, or migrate materialized members for exact behavior. |
| Seed-only population | `population-seeds` | no v2 equivalent | Materialize every seed with the frozen release-1 build, then migrate the ASTs. |
| Fitness cases | `fitness-cases` | `fitness-cases` | Unchanged. |

The v2 opcode layout assigns 0–24 and 28; numeric values 25–27 are permanent
holes formerly used by specialized region opcodes. The v2 AST layout assigns
the general nodes through 52 and from 71 onward; 53–70 are permanent holes for
removed `MapList`, `FilterList`, `LinearRec`, ASGP, and dependency-pattern
nodes. Normal decoders never reinterpret either hole range.

Migration preserves materialized values, constants, payload contents, source
limits, execution fuel, and program behavior where the release-1 artifact
contains them. Legacy decoding is isolated in migration targets and is not
linked into normal production dispatch. Package and historical documentation
may name removed constructs as migration inputs; they are not native nodes,
opcodes, metadata arrays, or runtime modes in release 2.

`grammar-materialized-v2` records `gagp-native-2.0.0` and stores constants in a
separate lossless singleton-domain array while its `ast-prefix-v2` shape keeps
an empty constant table. Decoding requires the matching semantic version and
exactly one detached pool.
