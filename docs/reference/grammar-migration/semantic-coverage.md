# Semantic baseline coverage ledger

All artifact paths below are relative to
`/home/hschi1106/gagp-artifacts/grammar-migration/`. The reference revision is
`b04918307eb69ec0f08c6bf5b03a0fae9399dbbf`. This ledger records measured coverage;
it does not declare Goal 01 complete or change normative semantics.

| Requirement | Inspected evidence | Coverage and limits |
| --- | --- | --- |
| Map/Filter list order, empty typed lists | `native-oracle-04/capture-0.jsonl`, execution ordinals 1, 3, 5, 7 | Materialized values and CPU fuel boundaries; corresponding direct GPU capture agrees. |
| Source evaluated once | Same capture, compilation/execution pairs ending at 9 and 11; immutable `test_sources_are_lowered_once` tests | Native tests assert lowered source occurrence and execution result. |
| LinearRec empty, singleton, right-to-left step order | Same capture, ordinals 13, 15, 17 | Results 42, 75, and 30621, plus exact fuel probes. |
| LinearRec typed-list sources | `native-oracle-04/capture-2.jsonl`, compilation ordinals 3013, 3032, 3034 and their subsequent executions | IntList, FloatList and StringList sources compile through the reference compiler and execute with direct CPU/GPU result capture. |
| Nested binder shadowing and ordinary-local isolation | Same capture, ordinals 19 and 21; `test_binder_capture_and_ordinary_local_isolation` | Nested same-name binders produce `[11,12]`; same-name ordinary local remains separate and produces `[101]`. |
| DC first-error order | `boundary-oracle-order-02/capture-0.jsonl`, ordinals 186–189 | Source type error precedes divide/solve; divide NameError precedes solve; left solve ZeroDivisionError precedes right NameError; solve precedes combine. |
| Unbound locals in structured phases | Same capture, ordinals 190–196 | NameError in each DC solve/divide/combine and DP1/DP2 solve/transition phase. These probes complement the lexical AST tests above. |
| DC source/result types; DP dependency directions, arities, and patterns | `boundary-oracle-r64/capture-0.jsonl` and generated inputs in archived `migration_boundary_cases.hpp` | Four sequence source types crossed with eight DC result types; DP1 two directions × three arities × eight result types; DP2 six patterns × eight result types. |
| DC frame and DP frame/memo limits | Same boundary capture and `gpu-boundary-results-01/` | Sizes around 64 frames and 128 states; duplicate dependencies and branching memo workloads. CPU/GPU differences remain frozen separately. |
| Boundary-before-base behavior | Last two observations of original boundary capture | Out-of-bounds state equals the configured base; boundary result wins. |
| String byte capacity | `boundary-oracle-order-02/` and `gpu-boundary-order-02/`, ordinals 197–199 | 511 and 512 bytes agree exactly. At 513 bytes GPU returns an internal fallback token while CPU materializes the string. |
| Typed-list value capacity | Same captures, ordinals 200–208 | Each list kind agrees at 127 and 128 values. At 129 values GPU retains the typed-list tag with a deterministic compact token and no materialized payload. |
| String entry capacity | Same captures, ordinals 209–211 | 31 and 32 entries agree exactly. At 33 entries GPU retains the exact String token but its bytes are absent from scratch. |
| Numeric edges and error codes | `native-oracle-04/capture-2.jsonl` and `gpu-native-results-01/` | Existing parity cases captured as individual executions. Integer conversion overflow has a recorded direct-value discrepancy that equal fitness had hidden. |

DP dependency first-error order is captured in `boundary-oracle-order-03/` and
`gpu-boundary-order-03/`, ordinals 212–215. Reversing DP1 offsets changes
ZeroDivisionError to NameError; placing the DP2 base on its first versus second
dependency also changes the first error. CPU/GPU errors and fuel thresholds agree.
Ordinals 216–224 isolate the list entry limit with two values per output: all three
list types agree at 31 and 32 entries; the 33rd GPU entry retains the exact token
but lacks its materialized payload.

The 39 named order/visibility/payload probes are checked into
`cpp/tests/fixtures/migration/reference-boundary-oracle.jsonl`, with independent
CPU and GPU expectations and source/raw hashes in `boundary-oracle-provenance.json`.
The focused CPU replay and GPU tests passed (`boundary-fixture-tests.log`). Their
expected GPU results are observations, not replacements for normative CPU results.

The coverage review maps supported nested binders and ordinary-local isolation to
their execution artifacts, and unsupported nested ASGP phases to explicit verifier
rejections below. Invalid-input evidence combines runtime type/value/name errors,
AST binding/type rejection, and bytecode structure/resource rejection. The recorded
CPU/GPU discrepancies remain part of the reference behavior for later migration
comparison; they do not establish normative parity where the implementations differ.

`verifier-oracle-02/` captures 11 AST rejections from the unchanged ASGP and binder
verifier tests, whose original assertions all pass. They cover undefined ordinary
locals and binders, duplicate binders, type mismatches, and the `nested_asgp`
rejection. Nested ASGP source forms in ASGP phase bodies are unsupported by the
reference, consistent with the bytecode verifier's `InvalidPrivateOpcode` check
for nested ASGP phase calls. Ordinary nested Map binders remain supported as
recorded above. This is an explicit old-language capability boundary, not a
missing old speedup measurement to fabricate.

The initial verifier capture failed because an intentionally opaque list constant
could not be serialized as a materialized population. Its log remains in
`verifier-oracle-01/`. Diagnostic rejection capture now allows opaque constants;
ordinary population snapshots still require decoded payloads. The snapshot test
checks both behaviors. Standalone replay now verifies all 11 rejected ASTs,
including exact snapshot roundtrip, rejection code, node and message. The new
`gagp_test_migration_ast_rejections` test also corrupts an expected rejection and
requires replay failure. The cases and provenance are checked into
`reference-ast-rejections.jsonl` and `ast-rejections-provenance.json`.

The complete native execution corpus was replayed again after this addition:
3,078 execution rows and its two AST rejection rows pass. Results are preserved in
`native-oracle-04-replay-final-{0,1,2}.json`. The execution corpus contains 2,855
successes, 133 TypeError, 72 Timeout, seven ValueError, two ZeroDivisionError, and
nine NameError results. One execution deliberately uses a verifier-invalid constant;
the replay requires its explicit runtime-negative marker. Compilation records
are not counted as runtime or AST-rejection checks. Replay now reconstructs all
95 compilation records' AST, payload and bytecode snapshots exactly
(`native-oracle-04-replay-compilation-{0,1,2}.json`). This is snapshot preservation,
not a compiler-equivalence check: legacy records do not retain every compiler call
option. A materialized compilation record is included in the compact replay fixture
and its provenance; all original 18 execution observations remain unchanged.

`verifier-oracle-03/capture-2.jsonl` freezes 22 bytecode verifier rejections, including
invalid opcodes/operands, constants, locals and mappings; jumps and stack flow;
builtin IDs and arities; private/segment/binder metadata; and a resource limit.
Each stores all six `BytecodeVerifyOptions` fields and the exact diagnostic
instruction, path and message. The checked-in `reference-bytecode-rejections.jsonl`
and provenance replay successfully. The test changes an expected rejection and
separately removes the triggering resource limit; both changes must fail replay.
The unchanged original verifier tests also run their generated-program acceptance
checks during capture.
