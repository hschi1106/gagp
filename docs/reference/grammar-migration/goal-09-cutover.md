# Goal 09 Production Cutover

Release 2.0.0 has one production architecture: compiled
`grammar-definition-v2` definitions materialize `ast-prefix-v2`, which lowers
to v2 bytecode and executes through the shared CPU/GPU runtime contracts.

The cutover reserves AST numeric kinds 53–70 and opcode values 25–27. Normal
production decoding, compilation, execution, generation, and reproduction have
no MapList, FilterList, LinearRec, ASGP, DP-pattern, or phase-specific metadata
dispatch. Compatibility packages express supported old search intent with
general templates, traversal, and bounded-region plans.

Public v2 formats are `ast-prefix-v2`, `bytecode-json-v2`,
`bytecode-fixture-v2`, `grammar-definition-v2`, `grammar-generated-v2`, and
`grammar-population-v2`. Offline migrated programs use
`grammar-materialized-v2`. `fitness-cases` is unchanged.

The offline migration target reads supported release-1 grammar configs and
materialized ASTs. `constrained-intent-v1` config conversion is deterministic
but explicitly lossy for the release-1 search space. Exact `ast-prefix` and
complete `grammar-generated-v1` routes emit `grammar-materialized-v2` with
target semantic version `gagp-native-2.0.0`, an empty v2 AST constant table,
and one detached lossless constant/payload pool. `grammar-population-v1` must
be split and its complete members migrated independently. Old bytecode is
rejected with instructions to migrate its source AST. Seed-only populations
must be materialized with the frozen old build before AST migration; v2
regeneration is not exact release-1 replay.

The registered migration contract tests provide concrete cutover evidence:
`gagp_test_migrate_artifact_cli` checks deterministic conversion of all six
maintained presets, filename independence, strict diagnostics, exact detached
scalar/list payloads and signed 64-bit integers, semantic-version enforcement,
isolated execution without package files, specialized Map/Filter lowering, and
the required bytecode/seed/population rejections. It also checks atomic output
replacement, aliased-path rejection, destination preservation on failure, and
temporary-file cleanup. The registered
`gagp_test_legacy_package_equivalence` suite compares migrated release-1 forms
with the general compatibility packages. Runtime-independent Goal 09 guards
check v2 presets, reserved numeric holes, absence of specialized production
dispatch and retired modules, production linkage isolation, and native test
registration.

Final cutover validation passed the complete Debug native suite (106/106), the
CPU ASan/UBSan suite (88/88), all operational tool tests, and all 23 repository
checks. The explicit CUDA gate passed four tests without skips: VM smoke,
fitness parity, evolution parity, and exact compatibility-package CPU/GPU
results. The optional Release build with benchmarks and experiments enabled
also compiled, and its migration/package boundary tests passed. Package
equivalence remains grounded in the frozen release-1 evidence captured through
Goal 08; no second production release-1 interpreter remains in this tree.

The six checked release-1 preset files convert deterministically and without
filename inference to their checked v2 outputs. This is not exact search-space
preservation: the source files did not record constant domains, execution fuel,
case schema, or a fixed typed assignment environment, and the v2 schema cannot
express the dynamic assignment behavior. The named constrained profile is the
recorded unavoidable discrepancy. Exact behavior preservation is available for
materialized release-1 programs through the AST/member routes described above.

## Cutover performance diagnostic

The external artifact
`/home/hschi1106/gagp-artifacts/grammar-migration/goal-09-cutover` records one
warm-up and three measured trials per public mode on one fixed 1,024-member
`grammar-population-v2` and 1,024 cases. Its manifest SHA-256 is
`e50360d4ed3f843624ec8c4495031d3d279d06ff6782436bc350498e236774c1`;
all 53 listed files were rehashed successfully. This is a diagnostic for the
cutover, not the Goal 11 statistical acceptance run.

Median phase times in milliseconds were:

| Mode | Evaluation | Reproduction | Generation total | Process wall |
| --- | ---: | ---: | ---: | ---: |
| CPU | 1272.510 | 189.605 | 1463.708 | 32604.552 |
| GPU evaluation | 131.826 | 185.007 | 318.639 | 31692.073 |
| GPU reproduction | 131.981 | 1832.091 | 1950.900 | 33193.682 |
| GPU reproduction with overlap | 131.868 | 1717.804 | 1852.217 | 33165.446 |

GPU evaluation's call/kernel medians were 119.851/102.755 ms. GPU reproduction
without overlap spent 1412.046 ms in host preprocessing, 140.228 ms packing,
28.035 ms uploading, 7.193 ms in kernels, and 161.206 ms decoding. The observed
regression is dominated by host metadata preparation rather than transfer or
device execution. Process wall time was about 32 seconds while reported
evolution totals were 0.8–2.4 seconds because exact replay of every population
member occurs before initialization timing begins. Both costs are mandatory
Goal 11 repair and measurement targets. The diagnostic ran while other GPU load
was visible, so its absolute values are not acceptance evidence.
