# Goal 08 compatibility-package evidence

Status: the reusable compatibility packages, general `fuel_events` authoring,
repository examples, CPU sanitizer suite, and CUDA Release suite are implemented
and pass their recorded correctness gates. The timing samples remain diagnostic;
this record does not claim statistical performance acceptance.

The artifact root used below is
`/home/hschi1106/gagp-artifacts/grammar-migration`. External artifacts are
retained evidence and are not normative specifications.

## Package surface

The implementation uses ordinary grammar-definition imports and resources. No
parser, compiler, generator, verifier, runtime, or kernel branch selects behavior
from these filenames or resource names.

| Role | Repository resources |
| --- | --- |
| General typed building blocks | `configs/grammar/basic/int.json`, `int_list.json` |
| Reusable templates and pattern alternatives | `configs/grammar/packages/linear_rec_intlist_int.json`, `dc_intlist_int.json`, `dp1d_int.json`, `dp2d_int.json` |
| Runnable compatibility roots and cases | `configs/grammar/compat/*_int.json`, matching `*.cases.json`, and `matrix.json` |
| Structures outside the old flag model | `configs/grammar/examples/restricted_combine.json`, `changed_recursive_split.json`, `acyclic_memo.json`, with matching cases |

LinearRec is a fixed composition of lexical bindings, checks, an empty branch,
last-element initialization, and one reverse ranged traversal. DC is a
nonmemoized sequence-window region with one clamped interior cut and ordered
left/right requests. DP1D contains backward and forward alternatives of arity
one, two, and three; DP2D contains cross, diagonal, and three-neighbor
alternatives in both directions. Their phases use only explicit typed region
slots.

The general expression schema accepts an optional nonempty `fuel_events` object
only on materialized owners. Event names and owner support use the native fuel
event table; costs are integer values from zero through `INT_MAX`. Compiled
profiles are canonical, generation emits physical `NodeFuelSpec` records, and
membership requires exact profile presence and event-to-cost equality. Explicit
zero remains meaningful. An absent profile retains ordinary lowering costs; in a
present partial profile, an unspecified supported event retains the native
profile default of one. Bounded `RegionPlan.entry_fuel` remains a separate
per-frame charge.

## Concrete typed variants

The external validation report contains 72 successful rows:

| Family | Concrete variants | Coverage |
| --- | ---: | --- |
| LinearRec | 24 | Three typed-list sources crossed with eight result types |
| DC | 32 | Four sequence sources crossed with eight result types |
| DP1D | 8 | Eight result types; every root contains all six direction/arity patterns |
| DP2D | 8 | Eight result types; every root contains all six dependency patterns |
| Total | 72 | 72 passed, zero nonzero return codes |

Each row invoked `gagp_generate_cli` for two materializations and recorded its
grammar hash and output SHA-256. All 72 output hashes are distinct. The report is
`goal-08-variants/validation.json`; its SHA-256 is
`016fcfae0c42fbbb8ce6b68e0b25c68a19887b14b323618027b88773828a9503`.
The report records 22.753 seconds of aggregate process time. This is a generation
validation, not a timing benchmark or an all-mode execution oracle.

The repository `gagp_test_grammar_packages` adds a focused contract around the
representative roots. It:

- loads, compiles, exports, reparses, generates, and checks membership for all
  four compatibility roots and all three custom examples;
- renames each imported package file in a temporary but topologically identical
  import tree, then requires identical canonical definitions, hashes, and
  deterministic generated AST cache keys;
- enumerates deterministic seeds until all six DP1D and all six DP2D alternatives
  appear, checking direction, request count and order, exact offsets, and duplicate
  policy;
- checks LinearRec administrative fuel owners and lexical scopes and DC phase
  banks, zero-cost request construction, and separation from `entry_fuel`;
- performs twelve deterministic mutation/crossover rounds over four parents for
  every family, requiring membership and family constraints for every child; and
- checks maximum-only combine, a two-cut three-way decomposition with a changed
  base, and the skew acyclic memo dependencies `(-1,+2)` and `(0,-1)`.

No generated, mutated, or crossed compatibility program may contain a legacy
`LINEAR_REC`, `ASGP_DC`, `ASGP_DP1D`, or `ASGP_DP2D` node. The matrix integrity portion parses the committed manifest, requires the exact
24/32/8/8 family counts and type/pattern sets, rejects duplicate or missing IDs,
and resolves every referenced symbol. It builds a temporary typed root for all 72
entries; every entry generates, passes membership and native verification, compiles
to bytecode, and has the declared result type.

## Direct compatibility differentials

`gagp_test_grammar_package_equivalence` instantiates a structurally matched legacy
node and authored package for each of LinearRec, DC, DP1D, and DP2D across all eight
result tags. It compares scalar bits or decoded payload values and error codes at
every fuel value from zero through three units beyond the first non-timeout boundary,
requiring identical boundaries. It also covers wrong sequence inputs and wrong DP
coordinates. Representative source/pattern choices provide the old-to-package link;
the matrix and DP enumeration tests separately cover every typed export and all six
patterns.

`gagp_test_grammar_package_gpu_results` compiles deterministic generated programs
from the four compatibility roots. For each family it probes a valid recursive input
and a mistyped input below, at, and above the CPU boundary: 24 direct production-GPU
observations. The test requires exact encoded tags and values or matching error codes,
the same first-non-timeout boundary, and expected remaining fuel after success.
Together with the five-mode evolution stress and existing generic lexical/bounded
capacity tests, this connects package generation to GPU values, errors, fuel, depth,
memo, and payload behavior without duplicating the exhaustive substrate fixtures.

## Frozen transition evidence and size changes

The transition reports replay frozen records from `native-oracle-04`. They test
the same general traversal and bounded-region substrate used by the packages,
but they are adapter evidence rather than a claim that every frozen AST was
generated from a repository package root.

`goal-08-linear-transition.json` consumes `native-oracle-04/capture-0.jsonl`.
Its SHA-256 is
`65b7a702c9f6e7b2d27fa64cd0d5641a12417984cd172f33c8bf9621c3fa0fbb`.
All four frozen LinearRec pairs passed 16 archived checkpoint checks and 231
fuel-by-fuel differential checks. Exact first-success fuel remained
16, 37, 83, and 75.

| Compilation / execution ordinal | AST nodes, old → general | Instructions, old → general | One-iteration median ns, old → general |
| --- | ---: | ---: | ---: |
| 12 / 13 | 14 → 43 | 54 → 58 | 610 → 691 |
| 14 / 15 | 16 → 45 | 56 → 60 | 1,160 → 1,400 |
| 16 / 17 | 18 → 47 | 58 → 62 | 2,250 → 2,730 |
| 30 / 31 | 12 → 41 | 52 → 56 | 2,060 → 2,470 |

The composition therefore adds 29 physical AST nodes and four lowered
instructions to each captured LinearRec fixture. The report uses one timed
iteration and seven repetitions, so these medians are diagnostic only.

`goal-08-bounded-transition.json` consumes
`native-oracle-04/capture-2.jsonl`. Its SHA-256 is
`9b40d4cc4ab14391415c915ac3c64a34a06c45a6c32b74cbc3be43ca6e4f8c11`.
The two verified DC fixtures each change from 13 to 18 AST nodes and from 10 to
15 instructions. They passed 168 archived checkpoint checks, 540 direct
differential checks, 144 session checkpoint checks, and 1,080 session
differential checks. Their one-iteration direct medians were 9,011 → 49,181 ns
and 9,200 → 49,411 ns; warm session medians were 9,150 → 10,130 ns and
9,260 → 10,210 ns. These are diagnostic samples, not the formal performance
gate.

Six other bounded-source fixtures are explicitly untranslatable: compilation
ordinals 3037 and 3039 (DC), 3092 and 3094 (DP1D), and 3116 and 3118 (DP2D)
contain undefined phase locals. They retain their frozen source checkpoints but
have no transformed size or differential rows. General package phases require
explicit typed slot bindings, so this report does not claim compatibility for
raw verifier-invalid phase captures. The broader exact-type limitations remain
recorded in [Goal 06 bounded-region evidence](goal-06-bounded-regions.md).

## Commands and audit

The focused package target was configured and run without CUDA:

```bash
cmake -S cpp -B /home/hschi1106/gagp-artifacts/grammar-migration/goal-08-cpu-debug -DCMAKE_BUILD_TYPE=Debug -DGAGP_ENABLE_CUDA=OFF
cmake --build /home/hschi1106/gagp-artifacts/grammar-migration/goal-08-cpu-debug --target gagp_test_grammar_packages -j2
ctest --test-dir /home/hschi1106/gagp-artifacts/grammar-migration/goal-08-cpu-debug -R '^gagp_test_grammar_packages$' --output-on-failure
```

Result: both focused CPU package targets passed. The reports can be independently inspected without rerunning
the generators:

```bash
sha256sum /home/hschi1106/gagp-artifacts/grammar-migration/goal-08-variants/validation.json
jq '{count,passed}' /home/hschi1106/gagp-artifacts/grammar-migration/goal-08-variants/validation.json

/home/hschi1106/gagp-artifacts/grammar-migration/goal-08-cpu-debug/gagp_benchmark_linear_transition \
  /home/hschi1106/gagp-artifacts/grammar-migration/native-oracle-04/capture-0.jsonl \
  /home/hschi1106/gagp-artifacts/grammar-migration/goal-08-linear-transition.json 1
/home/hschi1106/gagp-artifacts/grammar-migration/goal-08-cpu-debug/gagp_benchmark_bounded_transition \
  /home/hschi1106/gagp-artifacts/grammar-migration/native-oracle-04/capture-2.jsonl \
  /home/hschi1106/gagp-artifacts/grammar-migration/goal-08-bounded-transition.json 1
```

The benchmark commands rewrite their output paths. Preserve the retained reports
or select new output names when reproducing them.

## Full validation gates

A fresh CPU Debug build with AddressSanitizer and UndefinedBehaviorSanitizer passed
102/102 native tests in 48.48 seconds. Its log is `goal-08-asan/ctest.log`
(SHA-256 `1c6b54c37481b90d031839ca2920ba3fd4b27a2dd8098cab4ee506c86d252263`).

A fresh CUDA Release build passed 121/121 native tests in 32.66 seconds, including
all 19 GPU-labelled tests. Its log is `goal-08-release/ctest.log` (SHA-256
`85fd37b11b57a82e7409a9c453a1b145fb24b93dcd1a89aa6b8de836cc8f24d6`). The
compiled-evolution stress test exercises each compatibility package through CPU
and GPU evaluation, CPU reproduction, synchronous GPU reproduction, overlapped
GPU reproduction, and CPU evaluation with GPU reproduction. Final populations
retain grammar membership and native validity and have exact CPU/GPU fitness
parity. The bounded-region GPU capacity tests cover fuel, depth, memo, payload,
and host/device limit errors used by the same general substrate.

The first Release run found that the Goal07 capability-dispatch test requested
1,024 threads for an ASGP kernel using 140 registers per thread on RTX 4090. Both
visible devices rejected that physically invalid launch. The test block was reduced
to 256, preserving its 32-program grid-stride and mixed-dispatch coverage; the
direct test and the complete rerun passed.

Operational checks passed 66/66, and repository/spec/documentation checks passed
17/17. `git diff --check` also passed.

## Scope of the evidence

This evidence establishes user-authored package structure, typed variant
generation, deterministic relocation identity, membership and variation,
all supported CPU/GPU evaluation and reproduction modes, sanitizer coverage, and
the recorded frozen transition comparisons. It does not establish the research
algorithms' fitness distributions, selection probabilities, or simplification
behavior. The one-iteration timing samples do not establish formal statistical
performance acceptance.
