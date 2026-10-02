# Native 1T optimization checkpoint — 2026-10-02

Active objective: preserve CPU / GPU-e / GPU-er / GPU-er-o architecture, use
CPU 1T + GPU, target fastest native GPU >30× frozen ASGP-1T on Sum/House;
Median remains fixed-overhead workload. Do not resume retired CPU fragment route.

## Current best and active experiment

Best validated GPU source: `5508a60` (Q+R immutable crossover handoff), with
locally extracted jemalloc 5.2.1-4ubuntu1. Use `round2.py` FLAGS and process env:
```
LD_PRELOAD=$PWD/logs/optimization/native-allocator/extracted/usr/lib/x86_64-linux-gnu/libjemalloc.so.2
MALLOC_CONF=background_thread:false,narenas:1
```
No system installation; allocator confirms no background workers and one arena.
Previous N (`d72b5f4`) nine-repeat medians Sum279.706 / House295.679 / Median288.718ms;
same-round allocator controls329.388 /348.488 /339.451ms.
Frozen headline6.583x Sum /4.336x House, not30x. All3072 snapshots,3 seeds x4/32
generation results and final ASTs match. Four focused native/safety tests pass.
All120 tests pass with N and the O window-view candidate; scaling still pending. Use `round2.py` flags including `GAGP_OWNED_PREPARATION=1`.
Archive/repeats/commands `logs/optimization/native-owned-validation/`;
[validation](results/native-owned-validation.json).
Dependency hashes at `logs/optimization/native-allocator/dependency.json`.
Earlier allocator and selected-site results remain in their historical manifests.

K selects logical sites before materializing contracts and uses owned scalar
parent continuations. Full fallback for multiple root requests, projected
budgets, registry constants, staged payloads or explicit multiple workers.
GPU operators/RNG/cases/admission semantics unchanged. Two GPU copybacks/AST
rebuilds remain. J's execution-only admission is incorporated as K's internal
step; its standalone flag/entry is removed (small isolated benefit).

Rejected L: exact CPU list-slice cache.64K entries: Sum17.441→22.109s,
13.09M hits/23.47M misses,358 capacity clears, zero mutable-payload invalidations.
524K diagnostic:35.24M hits/1.32M misses (96.4%), but snapshot process still17.88s
versus ~18.5s reference with additional memory. High hit rate gives too little
benefit to retain this mutable-registry memo policy. All3 tasks' CPU generation
fields and3072 CPU/GPU snapshot records match; safety tests passed. Feature,
flag, exclusive API/stats/tests removed; raw binaries/patch/counters retained in
`logs/optimization/native-cpu-slice/`. Short registered-list Slice bounds fix and
its negative test remain. Next CPU direction needs cheaper view execution, not
another cache capacity sweep.
Retained M: allocator reduces all three GPU generation times15-18%; all nine
32-generation searches preserve non-time results/exports. Full120-test pass.
New Median NVTX diagnostic (373ms profiled vs334ms unprofiled):13,915 runtime
identities92.5ms; membership Matcher21.5ms self, witness22.0ms self;
2 decode passes128.4ms inclusive; selected analysis63.4ms inclusive.
Raw timeline/attribution: `logs/optimization/native-selected-profile/`.
Retained N (`d72b5f4`, `GAGP_OWNED_PREPARATION=1`): an owned immutable scalar population can share its admission
witness/VerifiedAst/identity through native preprocess/pack, avoiding repeated
serialization and proof copies while preserving public input checks. Keep GPU
operators, RNG, membership and both copybacks unchanged for this experiment.
Quick paired full generation: Sum325.464->284.576 /House350.142->297.930 /
Median330.887->289.507ms. Four focused tests pass (including owner/input mutation,
request/domain/payload/index/lifetime checks). Raw `logs/optimization/native-owned-preparation/`;
9-repeat, fixed snapshot and3-seed4/32-gen validation `native-owned-validation/`.
Formal9-repeat and3-seed4/32-gen comparisons pass; retained.
Candidate O (`GAGP_CPU_REGION_VIEWS=1`): invocation-local IntList views
for statically proved region phases, eliminating intermediate Slice registry
hash/copy work; original CPU VM/region control flow and numeric/fuel remain.
5 focused parity/region/session tests and full120 suite pass. Initial P1024 CPU
Sum16942.169->9241.978ms, House7806.760->7848.474, Median712.673->751.051.
All CPU generation non-time fields and3072 CPU/GPU snapshots match.
Coordinate views had no supported performance benefit; implementation is now
restricted by `SequenceWindows` structure (not task name). Formal9 CPU repeats,
full-case snapshots and5-mode quick controls running in
`logs/optimization/native-cpu-window-validation/`. Initial all-region prototype
binary/results retained in `logs/optimization/native-cpu-views/`.
No CPU workers, no view escape into root code; memo list states unsupported.
Temporary slice registry contents are intentionally not materialized in this
profile (same scalar-content aim as GPU views); no claimed speedup yet.

## Current fair baseline

Source parent `9ce96a9`; thread-policy checkpoint follows. `GAGP_HOST_THREADS=1`
is now the default; `2..20` is optional host parallelism, excluded from fair
headlines. One-thread overlap runs preparation on the calling thread after GPU
launch, before synchronization. Existing native fast-profile flags below remain.

Exploratory P1024/full1024 baseline (one process, 1 warmup + 3 repeats):
Sum GPU-overlap 724.686 ms, House 853.414 ms, Median 792.708 ms.
These supersede historical multiworker times **for the 1T contract**; they do not
claim a performance improvement. Raw five-mode measurements and binary:
`logs/optimization/native-1t-20261002/`; compact manifest:
[1T baseline](results/native-1t-baseline.json).

Validation: complete Release build; all 120 native tests passed across full run
and focused reruns after adapting two worker-count expectations. Explicit 4-worker
resource/cache/parity tests also passed; tool tests 5, repository tests 24 passed.
A clone trace plus pthread_create interposer/backtrace smoke records three CUDA
driver thread creations, no GAGP host workers. Logs are alongside the baseline.

Candidate A (`GAGP_REUSE_ADMISSION_COMPILE=1`): retain admission's existing
lowering in its opaque native certificate, scalar constants only. Exploratory
Sum640.959 / House736.418 / Median699.902 ms; all non-timing fields equal to 1T
baseline. Compile drops to 12.8–13.9 ms. Tests reject stale AST/constants/input
layout/fuel and decline registry-dependent constants. Needs formal and changing
search validation before promotion. Raw `logs/optimization/native-admission-compile`.

NVTX diagnosis of candidate A, Median: site analysis self178.6 ms, runtime
identity105.9 ms/11024 calls, bytecode+region verification96.7 ms; full profiled
722.7 ms. Nested ranges are not additive. Raw source-only instrumentation and
restored sources at `logs/optimization/native-1t-host-profile/`.

Retained candidate C: 1T decode uses ordered admission in the owning run context;
no discarded worker caches or speculative child warming. Formal 3 processes ×
(1 warmup + 3 repeats), interleaved with 1T controls: Sum552.771 / House637.124 /
Median590.220 ms, reductions22.2% /25.1% /21.1%. All 3072 snapshot program records
match (full cases, GPU counts and CPU fitness); all nine 3-seed×4-generation
search comparisons match non-timing fields and exported ASTs. Quality limits:
short runs only, no convergence/long-run assertion. Raw commands/binaries/repeats:
`logs/optimization/native-checkpoint-validation/`; summary
[validation](results/native-1t-reuse-validation.json).

Rejected B: grammar-wide local-resource eligibility cache. Sum657.988 /
House752.135 /Median697.685 ms versus candidate A640.959 /736.418 /699.902;
no reliable gain. The cache change is reverted, raw data in
`logs/optimization/native-static-charges/`.

Rejected D: compact-before-admission. No performance gain (571.8/656.3/604.5 ms)
and a new negative test caught unused duplicate-name validation being hidden
by compaction. The implementation, flag and proposed spec change are reverted;
the acceptance tests remain and pass on the retained implementation. Raw rejected
patch/timings in `logs/optimization/native-compact-admission/`; no maintained entry.

Candidate E: analysis-local site contract reuse (`GAGP_REUSE_SITE_CONTRACTS=1`).
Same verified scope/nonterminal/template/group/closure reuses the exact key;
repeated-hole intersections retain full construction. Quick Sum505.950 /
House595.256 /Median542.493 ms, unchanged non-timing results. Differential
contract fixtures plus reproduction/bounded variation pass. Formal repeats and
changing-search checks pending. Raw `logs/optimization/native-site-contracts/`.
Detailed Median NVTX: 141886 contract constructions (~94.7 ms self), runtime
identity90.2 ms, root/region bytecode verification91.8 ms. Instrumented timings
are diagnosis only. Next: shared immutable verifier locals between instructions,
copy on Store/merge; preserve traversal and admission/error semantics.

Candidate F: verifier local environments share ownership between unchanged
instruction states; Store and control-flow merge detach before mutation. CFG
order, validation rules and diagnostics unchanged. Quick Sum455.695 /
House498.343 /Median484.025 ms after E; all non-timing fields match. Four focused
tests pass; independent old-source differential checks match all 100000 ordinary
bytecode and 20000 bounded-region results including exact diagnostics and resource
summaries. Full Release build passes. Raw `logs/optimization/native-verifier-locals/`.
E+F formal validation passed; archived in
`logs/optimization/native-contract-verifier-validation/`. Superseded by I then K.

Rejected G: retaining full sites in native certificates. Fixed generation slowed
~30–47 ms, and 32-generation searches slowed3–4% with RSS+120–140 MB. All short
and32-generation fields/exports matched, but copying/retaining sites lost.
Implementation/flag/exclusive tests removed; raw rejected patch and evidence:
`logs/optimization/native-certified-sites/`. Best runtime unchanged.
Rejected H: retain the run analysis context (also tested a smaller2P capacity).
All short and32-generation results/ASTs matched. After correctly charging final
cache destruction, fixed-gen changes were only ~1–3%, and32-generation process
wall Sum18.78→19.90s, House17.69→17.32s, Median20.53→19.63s. Mixed/small benefit
and Sum regression do not justify this extra lifetime policy. Implementation,
flags, exclusive eligibility/ownership tests and guide removed. Raw corrected
patch/results `logs/optimization/native-run-analysis-accounted/`; earlier timing
notices remain in `native-run-analysis` and `native-run-analysis-2p`.

Timing audit adds `evolve_wall_ms` outside the actual evolve call. Historical
`evolve_call_ms` is an internal timer excluding function-local destructors.
Keep both and original generation boundaries. Fixed benchmark smoke covers it.

Latest best-version NVTX: Median profiled563.5 ms, identity89.4, site build92.7,
key construction9.3, root/region verifier39.8 ms. Separate scopes are nested.
Raw `logs/optimization/native-refreshed-profile/`. Process-local SIGPROF PC samples
(no workers/system settings) in `logs/optimization/native-cpu-pc-profile/` show
allocation/free pressure and repeated linear descriptor lookups; CPU Sum also
spends substantial samples hashing/registering list slices. Full-process samples
include setup, not a precise per-generation attribution. Descriptor indexing (I) was subsequently validated:415.657/430.558/437.677 ms,
all full snapshots and short searches match. Raw `native-node-index-validation`.

Current best runtime `ed19fde`, archived binary/flags/commands in
`logs/optimization/native-contract-verifier-validation/manifest.json`.
Enable retained flags plus `GAGP_HOST_THREADS=1 GAGP_REUSE_ADMISSION_COMPILE=1
GAGP_REUSE_SITE_CONTRACTS=1`; round2 runner supplies these.
Formal nine-repeat Sum465.603 /House505.974 /Median499.036 ms versus same-round
controls562.207 /650.451 /590.818. Every snapshot/non-timing search field and
exported AST matches across 3 seeds ×4 generations. All120 native tests pass
across full run and repository rerun (admission contract's freeze hash updated).
[Raw-repeat summary](results/native-contract-verifier-validation.json).
Next: reprofile, then retain certified site analysis across actual generations.
Do not merely persist the compatibility registry: its numeric IDs are local and
its 65536-key bound must not become a long-run cumulative admission restriction.
CPU perf sampling remains blocked by perf_event_paranoid=4; optional request
pending. No system settings changed.

---

Worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`, branch
`opt/gpu-generation-20261001`. The maintained mainline is GPU evaluation plus
GPU reproduction. CPU controls remain part of the five-mode fixed benchmark.

## Removed route

CPU fragment evolution/reproduction, its phase-bank predecessor, exclusive
owners/executable certificates, owned packing and capacity buckets, probe modes,
runner options and exclusive tests have been deleted. They are not isolated
alternatives, fallback reproduction backends, or future recommended work.

Historical measurements remain unchanged; their status is recorded in the
[archive notice](results/README.md). The old CPU-fragment timings are withdrawn
from the main target assessment. Original worktree modifications, frozen inputs,
archived binaries and historical logs remain protected. No branch reset or push.

## Retained native settings

Presence enables these flags (including a value of `0`):
```
GAGP_CUDA_DEVICE=0
GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1
GAGP_COMPACT_FRAMES=1 GAGP_CONSTANT_PHASE=1 GAGP_POPULATION_HANDOFF=1
GAGP_DIRECT_PHASE=1 GAGP_DIRECT_ROOT=1 GAGP_WINDOW_EXECUTOR=1
GAGP_UNBOXED_WINDOW_FRAMES=1 GAGP_BOUND_ADD_PHASE=1
```
Direct phase/root, window execution, unboxed frames, bound Add, case sorting,
compact workspaces and native generation-owned analysis handoff remain. The
native bytecode verifier, grammar admission/certificates, donor preparation and
GPU reproduction/decode paths retain their safety contracts and tests.

Most recent nine-repeat native medians at P1024/full1024 cases, before cleanup:
Sum205.448, House207.503, Median178.636 ms. These are historical native values,
not new cleanup measurements; 40× is not achieved. See native rows in
[round2-final.json](results/round2-final.json). Historical control binary
`logs/optimization/final_bench` and frozen inputs must not be overwritten.

## Reproduce maintained paths

Build the existing Release CUDA configuration:
```
cmake --build cpp/build/release -j10
```
The runner supplies the native flags above. Use a new output directory each time:
```
python3 benchmarks/fixed_asgp/optimization/round2.py formal NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench --baseline logs/optimization/final_bench
python3 benchmarks/fixed_asgp/optimization/round2.py quality NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench --generations 32
python3 benchmarks/fixed_asgp/optimization/round2.py scaling NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench
```
Scaling retains `asgp_1t`, `gagp_cpu`, `gpu_eval`, `gpu_repro`, `gpu_overlap` at
P1024/2048/4096/8192, using the existing frozen P8192 prefixes and all 1024 cases.
`snapshot` and `search` are native diagnostic modes. Never rerun prepare against
original inputs. GPU runs are serial; profile only for diagnosis, not headlines.

## Cleanup validation

Logs and dependency/protection audit:
`logs/optimization/cpu-fragment-removal-20261002/`.
Full Release build and 119 native tests passed. Tools: 89 tests (4 optional skips);
repository: 24 tests passed. Three tasks × five modes × (one warmup + three measured runs)
at P32/full 1024 cases matched every non-timing field in the pre-cleanup archived
native binary. This is smoke/regression evidence, not a new performance baseline.
Removed modes reject with `unknown mode`; no deleted symbols remain in native
libraries and no removed entrypoints remain in maintained source/guides/runners.
All original-file hashes match. [Validation record](results/cpu-fragment-removal-20261002.json).
The cleaned binary is archived at
`logs/optimization/cpu-fragment-removal-20261002/bench`.
No new architecture or optimization experiment is part of this cleanup.

Next high-value GPU question: can native GPU offspring be lowered directly into
existing proven phase bytecode, without CPU AST reconstruction between passes?
Start with a bounded prefix-phase lowering prototype and compare it against the
existing compiler (code, constants, fuel, outputs). This is necessary groundwork,
not a speedup claim or another reproduction backend. Grammar membership must
remain a distinct proof; type-correct phase lowering alone does not establish it.

P prototype: device prefix-phase lowering is now runnable in
`cpp/build/release/gagp_test_gpu_phase_compile [PREPARED_JSON GRAMMAR_JSON]`.
512 generated expressions (including branch/fuel cases) +8 negative cases pass;
all14336 phases from3 frozen P1024 populations match CPU compiler instructions,
constants and fuel. CUDA12.6 memcheck:0 errors. System `/usr/bin/compute-sanitizer`
misses its injection library; use `/usr/local/cuda-12.6/bin/compute-sanitizer`.
Warm batch medians ~0.62/0.89/0.55ms (Sum/House/Median); cold launch~2.9-3.2ms,
H2D~0.36-0.40ms,D2H~2.0-2.5ms. Host diagnostic setup~212-230ms includes JSON,
independent admission, reference CPU compilation and normalization; not a fair
end-to-end replacement measurement.40 registers,2816-byte indexed stack,0 spills.
[Raw manifest](results/native-gpu-phase-compile.json). Isolated test-only path;
no grammar trust, operators, evaluator or generation timing changed. This is
useful groundwork, not an adopted generation speedup.

O CPU window candidate follow-up: O2 lazy allocation and O3 separate VM
instantiations did not remove generic regressions. O4 moved private view handling
to scoped builtins; O5 delayed conversion but slowed recursive execution. O6
restricted eager setup to proven constant-false window predicates; O7 outlined
ownership/cleanup from the ordinary VM. O7 nine repeats: Sum16877.975->9520.944ms,
House7789.829->7982.784ms (+2.5%), Median707.697->714.291ms (+0.9%). GPU controls
remain ~280/292/284ms. This CPU candidate is NOT promoted; artifacts, all nine
raw repeats and source experiments are under `native-cpu-window-final-check/`
and preceding `native-cpu-window-*` directories. Current source also packs shared
CPU case bindings once per evaluator call (focused parity passed; separate timing
pending). No application workers were added.

P2 GPU donor construction: current opt-in `GAGP_GPU_DONORS=1` extends owned
selected-site native GPU mutation. Production/scope/domain tables build lazily
inside timed preparation; GPU makes fresh donors, native GPU applies mutation,
final canonical child admission remains. Unsupported jobs use native donors;
new donor RNG/budget-allocation trajectory is explicit, not canonical replay.
All4096 phase donors from3 frozen grammars independently admit/lower; default
and scope-permutation fixtures plus native deferred-preparation regression pass.
Current prototype source is not promoted and still needs full quality/scaling.

INVALID experiment `logs/optimization/native-gpu-donors-integration/`: a misplaced
deferred-batch loop produced no donors, so296 mutations fell back. The203/214/206ms
numbers were retracted; INVALID.txt preserves the reason. Accounting invariant
and actual native preparation test now prevent this failure. Corrected quick run
`native-gpu-donors-corrected/`: Sum280.360->288.195, House293.288->292.745,
Median292.184->281.072ms;1092/1064/1112 GPU donors,92/120/72 native fallback donors,
zero offspring fallback, generation or admission rejection. Not a stable overall
improvement. P2b packs lexical scope maps into24 bits (8 slots), reducing compiler
local stack8624->3168 bytes, zero spills; active A/B and selective batch trace:
`logs/optimization/native-gpu-donors-compact/`.

Refreshed N NVTX diagnostic: `native-owned-profile/`,323.803ms profiled versus
~289ms unprofiled Median.7771 identities49.172ms;2 decode passes129.812ms inclusive;
1320 admissions116.728ms inclusive;Matcher21.642ms self,Witness22.223ms self;
compile39.889ms inclusive, selected analysis41.973ms inclusive. Do not subtract
these annotated/overlapped times from formal totals.

### P2c checkpoint — GPU grammar donors, compact output

- Native GPU selection/crossover/mutation remain; opt-in `GAGP_GPU_DONORS=1`
  constructs supported fresh donors on GPU, then uses existing native splicing
  and canonical final admission. No CPU workers. Unsupported requests explicitly
  use the native donor preparation. New production/budget RNG trajectory.
- Nine unprofiled repeats against N, interleaved 3 processes × warm + 3:
  Sum 278.274330 → 262.496018 ms; House 292.987050 → 270.175429 ms;
  Median 283.401071 → 263.846364 ms. Raw/manifest/summary:
  `logs/optimization/native-gpu-donors-validation/`.
- Full 123-test suite passed. Three seeds × four generations: complete 1,048,576
  pairs/generation, zero timeouts/unscored, top-16 CPU fitness exact. New trajectories
  retain diversity but Sum grows larger programs and total search is slower;
  **not promoted**. 32-generation comparison underway in `native-gpu-donors-long/`.
- Compact output removes unused-capacity D2H/host allocation, not evaluated work.
  Earlier `native-gpu-donors-integration/` timings are INVALID (deferred batch was
  misplaced; mutation fallback); accounting invariant and 128-donor regression
  now exercise actual construction. Never cite that directory as a speedup.
- Next hypothesis: compile_population serializes each admitted AST for the cache
  key and again to validate its executable; one private combined lookup should
  eliminate the duplicate while retaining mutation/input/fuel validation.

### Q — single evaluation identity

- `GAGP_SINGLE_EVAL_IDENTITY=1` computes the current runtime identity once and
  returns the matching sealed executable in the same private lookup. External
  AST mutation, input order and fuel still invalidate reuse. No RNG/operator change.
- Focused membership/reproduction/parity tests pass; all non-time search fields
  and exports match for three tasks × four generations. Raw quick and nine-repeat
  results: `logs/optimization/native-single-identity{,-formal}/`.
- Next: preserve ownership between the already-admitted GPU crossover output
  and mutation preparation; no admission deletion at external boundaries.

### R — immutable crossover-to-mutation handoff

- `GAGP_OWNED_CROSSOVER_HANDOFF=1`, with Q, removes repeated certification/copy
  of the private, just-admitted crossover population. Both GPU operator passes,
  AST decodes and final canonical admission remain. No RNG/language change.
- Nine repeats vs N: Sum281.756493→269.700719, House297.769050→278.160918,
  Median288.717111→268.244850 ms. All three seeds ×32 generations match every
  non-time search field and final exported AST, including top16 CPU reevaluation.
  Search wall improves about5–8%; raw `native-owned-handoff-validation/`.
- Four focused tests pass, including repeated-hole/capture, wrong source/context,
  invalid output and destroyed-parent lifetime. Full123 suite passes; all3072
  complete-case CPU/GPU snapshot rows match N.
- Updated host/CUDA diagnostic underway in `native-owned-handoff-profile/`.
- P2 GPU donor32-generation tests did not justify promotion: most whole searches
  slower with larger programs despite fixed-parent gain. Keep it off the best
  flags; it is a GPU construction experiment, not a headline performance claim.

Q+R activation: N flags plus `GAGP_SINGLE_EVAL_IDENTITY=1` and
`GAGP_OWNED_CROSSOVER_HANDOFF=1`; keep GPU_DONORS and CPU_REGION_VIEWS unset.
Profile302.222ms (instrumented Median, not formal268.245ms): runtime identities
5736calls/34.001ms; decode2calls/128.005ms inclusive; matcher21.762ms self;
witness22.347ms self; compile16.341ms self; constant streams5.209ms.
Median fitness kernel2.113ms on final profiled generation. Remaining host proof
work dominates. Full123 suite and3072 snapshot rows pass. Scaling pending.

### S — incremental canonical crossover matcher prototype

- `GAGP_TRANSPORT_CROSSOVER_PROOF=1` uses immutable parents plus actual copied
  node/value correspondence to seed unchanged/donor subtree decisions. Affected
  ancestors are removed and re-matched in declaration order. Native verification,
  witness/resource construction and lowering remain; no RNG/language change.
- Zero-node alias roots, noninjective scope maps, coupled occurrence sites and
  inner-binder donors decline to ordinary admission. First disjoint-root profile
  admitted0 benchmark children (`native-proof-transport/`), not an improvement.
- Actual incremental matcher handles982/984/998 crossover children. Initial
  table/AST copies cancelled savings; moving owned temporaries gives quick
  paired271.820→262.111 /275.211→267.552 /266.850→263.308ms. Search4-generation
  times mixed; **not promoted**, formal/long comparison pending. Raw
  `native-incremental-matcher{,-move}/`.
- Focused4 tests pass; added512-child canonical witness check includes ambiguous
  ancestor alternatives and explicit alias fallback. Final ASTs remain exact.
- Remaining eval candidate: bounded one-coordinate memo lookup currently scans
  all occupied cells. Test direct indexing with a register validity bitmap,
  keeping complete state, lazy execution, fuel/base/capacity order unchanged.

S decision: **rejected from active runtime**, reproducible source checkpoint
`ed0448a`; patch/binary/raw in `native-incremental-matcher-validation/`. Nine
paired medians R→S: Sum261.695204→261.260983, House274.157456→270.559995,
Median272.448157→268.385679ms. Larger quick gains did not repeat. Sum gain0.2%,
House/Median1–2% with overlapping ranges; insufficient for added proof transport
complexity. Dedicated512-child ambiguity/alias witness tests passed, and the
prototype remains useful evidence, not the recommended path. Removed its flag,
private admission path, exclusive test and active guide/navigation entries.
R remains best; T dense coordinate memo now awaiting serial build/evaluation.

### T — dense coordinate memo: rejected

- One-Int-state memo domain <=128 slots; direct indexing plus per-invocation
  validity bitmap, with original fuel/base/capacity order. Three focused tests
  pass, including raw values/errors/fuel and boundary checks; all1024 House
  full-case CPU/GPU snapshot rows exact.
- Two paired orders give House272.33→278.59 and275.00→284.09ms; flag-off
  candidate272.46/276.44ms. Sum/Median changes are order-dependent noise.
  No end-to-end benefit: removed active code/flag/exclusive tests and guide.
  Patch, binary, raw JSON retained in `logs/optimization/native-dense-memo/`;
  tracked summary `results/native-dense-memo.json`.
- R remains best. Next hypothesis: table compaction after successful admission
  need not repeat matching, witness building and lowering when only unused
  scalar constants are removed and names/scopes are unchanged. Validate first,
  then transform owned data and bind its proof before publication.

### U — post-admission constant compaction: rejected

- Minimal owned path retains proof after full original-input validation, only
  remapping scalar constant indices; changed names use full readmission.
  Focused membership/reproduction tests pass including malformed unused entries,
  canonical witness comparison and stale public certificate rejection.
- Quick medians R→U:262.10→264.00 /268.41→274.96 /263.29→267.04ms.
  Diagnostic5280/5280 admissions have already compact constant/name tables:
  native GPU copyback already eliminates this work. No gain; source/flag and
  exclusive tests removed. Existing shared compaction safety tests retained.
- Corrected attribution: R Median1320 matcher/lowering calls under actual
  offspring decode; other1184 calls belong to donor preparation. This is not
  evidence of duplicate full offspring admission.
- Raw `native-admitted-compaction/`. Diagnostic runner overwrote its own quick
  archived binary (quick original SHA remains in manifest); diagnostic source
  patch retained. Quick timing is rejected evidence, not a reproducible best.

### V1 — GPU expression subtree variation foundation

- Device-owned prefix trees carry distinct nonterminal sites (not type-only
  tags). GPU crossover copies site metadata; new GPU mutation donors annotate
  construction once. No reannotation of existing offspring in operators.
  Independent diagnostic reconstruction checks every transported site separately.
- Prototype profile: pure Int/Bool/IntList expressions, scalar Int/Bool constants,
  identity scope maps, default variation/resource policy, no aliases/inner
  binders/templates/repeated holes inside phases. Unsupported combinations
  explicitly decline. This is NOT CPU fragment reproduction, a finite phase bank,
  a native benchmark mode, or a complete-generation performance claim.
- 1024 phases ×32 operator rounds for all16 three-task expression nonterminals;
 5120 independently admitted exports per root, first/final1024 GPU bytecodes
  exactly match CPU instructions/constants/fuel. Fresh mutation uses destination
  remaining node/depth budget:32768/32768 admitted per root. Crossover capacity
  rejections explicit. Distinct audited/final genotypes recorded separately.
- Cross+fresh-mutation+lowering warm CUDA-event medians0.95–2.43ms per1024 phases
  on these profiles. No selection fitness/evaluation/setup/export included, so
  not a generation speedup. Raw `native-gpu-phase-variation/{sum,house,median}-v3.log`.
- Scalar recursive fixture: memory sanitizer0 errors, node/index/NT/scope/capacity
  guards, exact transported metadata checked. Finite-language scope fixture
  exposed an overly broad diversity assertion; assertion retained for recursive
  built-in fixture, finite languages report actual uniqueness and validate all
  members. Scope fixture supports BA/Root, declines permutation AB and Float.
- Next: whole-program static skeleton admission, frozen import/export and resource
  accounting, then native GPU evaluator integration and actual selection/search.
  R remains validated best; this foundation is not promoted.

V2 cold layout adapter: `gpu/phase_layout.hpp` independently admits inputs,
extracts phase-aligned independent holes, and exports ASTs without carried proof.
All3072 frozen parents and the full1024-member R seed2 Median32-generation final
population roundtrip with exactly equal compiled instructions/constants/fuel,
bindings and plans. A separate two-coordinate memo grammar fixture also passes;
coupled/inner-lexical fixture explicitly declines. Raw `layout-*.log` alongsideV1.
This proves representability (including native later Median), not search quality.
Next implementation: run-owned double-buffered GPU phase population, completed
fitness-driven tournament/crossover/mutation, GPU lowering, timed code copyback
and native evaluator integration. No CPU reproduction. Best still R.
