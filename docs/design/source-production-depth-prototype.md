# Source production depth prototype

This opt-in search profile limits each ASGP-derived phase to seven source
productions on any root-to-leaf path. It does not change grammar membership,
runtime fuel, evaluation, production identities, nonterminal compatibility, or
operator sampling. Physical node/depth limits remain additional constraints.
A known solution outside the search cap remains a valid runtime/grammar witness;
in particular Scrabble's depth-16 witness is not evidence of cap-seven admission.

Each materialized source production root charges depth one. Lowering helpers
charge zero, except fixed Bool leaves in the IF lowering of binary AND/OR charge
one. This normalization preserves exact source height: each actual child has
height at least one, so adding a fixed leaf contributes
`1 + max(height(a), height(b), 1) = 1 + max(height(a), height(b))`.
It also makes the shared lowering of `AND(x,true)` and `OR(x,false)` have identical
per-occurrence charges. Without this normalization the existing conservative
resource-invariance certificate rejects those otherwise equal-height derivations.
Physical node charges remain one. The fixed scaffold has depth-zero charges and
the bounded region resets incoming depth; implicit outer program envelopes are
still charged normally and fit within seven.

Admission must certify canonical resource invariance and compare each frozen
parent's native projected phase depth with its original source phase height.
Every frozen parent must fit; no filtering or resampling is allowed. The search
budget is distinct from grammar membership and must be supplied consistently to
native CPU and GPU reproduction. Unsupported profiles fail explicitly.

GPU generation retains physical and projected remaining depth independently.
Its minimum-node feasibility table is indexed by both bounds; transparent nodes
still consume physical capacity. Expression origins supply projected charges,
and existing prefix occurrences supply ancestry. Mutation receives the logical
allowance at the selected site. Crossover/splicing checks the complete resulting
phase against both bounds. No source AST reconstruction, nonterminal layering,
or CPU reproduction is introduced.

The feature remains experimental until frozen-parent equality, host/device
variation parity, offspring canonical admission, and short quality/performance
checks pass. Flags-off behavior must remain unchanged.

The prototype is prepared through
`gagp_tools.experiments.fixed_asgp.source_depth.annotate(document)` and selected
in the fixed-ASGP benchmark by `GAGP_BM_SOURCE_DEPTH=7`. GPU reproduction also
requires `GAGP_GPU_PHASE_POPULATION`; unsupported projected phase admission
throws instead of switching reproduction profiles. The general native phase
owner supports certified zero/one depth charges with a projected bound of 1–15;
the benchmark deliberately exposes only seven. Flags-off defaults are unchanged.

The initial invariance probe accepts 28 source grammars. IsSorted and AllEqual
fail certification for their tuple lowerings and are unsupported by this
prototype; their ordinary profiles remain available. Do not report a cap-seven
all-30 comparison by silently mixing these policies.

Validation: `gagp_test_phase_projected_depth` exercises the shared construction
and splice algorithms on host memory, including physical/logical constraints and
compact/reference task parity. The source annotation tests run with
`PYTHONPATH=tools python3 -m unittest tools.tests.test_fixed_asgp_source_depth`.
Frozen-parent `prepare` with the environment enabled records source/native depth
comparisons for every phase and fails the entire artifact on any mismatch.
These host checks do not substitute for native GPU integration and offspring
admission checks.
