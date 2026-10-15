# Fitness

This document defines release 2.0.0 scoring rules used by evolution and benchmark
entrypoints.

Current preserves the old scoring shape, removes public `None`, removes public
`NumList`, adds public `Char`, and scores direct list tags exactly.

See also:

- [grammar.md](./grammar.md)
- [fitness_cases.md](./fitness_cases.md)
- [bytecode_format.md](./bytecode_format.md)

## Case Classification

A case is classified by the runtime type of `expected`.

### Numeric Case

A case is numeric when `expected` is `Int` or `Float`.

`Bool` is not numeric.
`Char` is not numeric.

### Exact-Match Case

A case is exact-match when `expected` is one of:

- `Bool`
- `Char`
- `String`
- `IntList`
- `FloatList`
- `StringList`

`IntList` and `FloatList` are different runtime types.
`Char` and `String` are different runtime types.

## Adjustable Parameter

`penalty` is a non-negative scalar parameter.

```text
penalty >= 0
```

Default runtime value:

```text
penalty = 1.0
```

The penalty is applied when evaluation cannot produce a directly comparable
result, including runtime errors and type mismatches.

## Case-Level Scoring

### Numeric Case

For numeric expected values:

```text
if actual is Int or Float:
    score_case(actual, expected) = -min(abs(actual - expected), penalty)
else:
    score_case(actual, expected) = -penalty
```

Properties:

```text
score_case = 0    iff actual = expected and actual is numeric
score_case < 0    otherwise
```

### Exact-Match Case

For exact-match expected values:

```text
if runtime_type(actual) = runtime_type(expected):
    score_case(actual, expected) = 1 if actual == expected else 0
else:
    score_case(actual, expected) = -penalty
```

Runtime type equality is exact:

- `Char("a")` and `String("a")` are a type mismatch.
- `IntList([1])` and `FloatList([1.0])` are a type mismatch.

## Runtime Errors

Runtime errors always contribute:

```text
-penalty
```

This includes:

- `NameError`
- `TypeError`
- `ZeroDiv`
- `ValueError`
- `Timeout`
- deterministic bounded-region frame, memo, or payload overflow errors

The first runtime error for a case terminates that case execution.

## Total Program Fitness

Program fitness is the sum over all cases:

```text
fitness(program) = sum(score_case(actual_i, expected_i))
```

For a mixed benchmark:

```text
fitness(program) =
    sum_numeric(score_numeric_case(actual_i, expected_i))
  + sum_exact(score_exact_match_case(actual_i, expected_i))
```

Value range:

```text
fitness(program) in [-penalty * N_cases, N_exact]
```

where:

- `N_cases` is the total number of cases
- `N_exact` is the number of exact-match cases

## Solved Criteria

### Pure Numeric Benchmark

A pure numeric benchmark is solved iff total fitness is exactly:

```text
0
```

### Pure Exact-Match Benchmark

A pure exact-match benchmark with `N` cases is solved iff total fitness is:

```text
N
```

### Mixed Benchmark

A mixed benchmark with `N_exact` exact-match cases is solved iff total fitness
is:

```text
N_exact
```

Reason:

- every numeric exact hit contributes `0`
- every exact-match hit contributes `1`
- any mismatch, runtime error, or type mismatch prevents the target score

## PSB Regression Interpretation

PSB regression reports must record:

- train solved flag
- test solved flag when a test fixture is available
- best train fitness
- best test fitness when available
- best-fitness history
- first solved generation when available
- runtime error, timeout, and type error rates when available

For `compat`, compare these values against the baseline and gates recorded in
the relevant benchmark manifest.

### CPU payload allocation boundary

CPU fitness evaluation permits at most 8 MiB of cumulative generated payload
data and registry keys per case. Exceeding this limit is a runtime Value error
and receives the ordinary error penalty. Temporary payloads are released after
that case is scored. This resource bound is independent of instruction fuel;
backend-specific resource limits need not be equal.
## Opt-in task metrics and elitism

`FitnessPolicy` defaults to `Legacy`, preserving the scores above. Native CPU
and GPU fitness accept the same optional policy; evolution carries it in
`EvolutionConfig::fitness_policy`. The task metrics maximize negative error:

- `Absolute`: numeric absolute difference, capped by `abs(penalty)`.
- `Exact`: zero for a matching value, minus one otherwise.
- `Levenshtein`: insertion/deletion/substitution distance for typed lists or
  string bytes, capped by `abs(penalty)`. List tags must match.
- `TupleSummary`: an `IntList` encodes `[predicate, summary1, summary2]`, with
  exactly `summary_count + 1` entries. Error is predicate mismatch plus
  `summary_weight * min(abs(actual-expected), summary_cap)/summary_cap` for
  each summary. `summary_count` is 0 through 2; the cap must be positive.

`Discounted` proximity replaces the cap for Absolute and Levenshtein by
`error/(1+error)`. Runtime errors and incomparable values receive the metric's
ceiling: one for Exact or discounted proximity, `abs(penalty)` for capped
proximity, and `1 + summary_count * summary_weight` for TupleSummary.
Sequence answers above 1024 elements are rejected at session/evaluation setup.
TupleSummary answers must have a resident IntList payload with exactly
`summary_count + 1` Int fields; malformed answers are rejected at setup. A
malformed actual tuple contributes the metric ceiling and one wrong case.
Payloads are resolved before invocation-owned GPU storage expires; absent
payloads receive the ceiling. No distance is inferred from transport hashes.

`EvolutionConfig::elite_count` defaults to zero and must be less than population
size. Positive values preserve that many highest-ranked previous members in the
next generation, after variation. The native GPU phase owner copies those trees
on device before compilation. This keeps population size and evaluation count
unchanged. This policy alone does not align random streams or tournament rules
with external implementations.

Configured task metrics rank by total error, then wrong-case count, then native
AST node count. TupleSummary counts only predicate mismatches as wrong, even
when summaries contribute error. Native AST size is not necessarily the source
ASGP logical phase size: lowering can expand an authored operation. Remaining
ties use the existing program key (native phase owner: source index). Legacy
metrics retain their previous tie rules. GPU reproduction consumes ordinal
rank scores, preserving these comparisons without changing reported fitness.

GPU fitness's `extended-payload` profile has per-case limits of 8192 list values,
1024 list entries, 8192 string bytes and 1024 string entries. Intermediate
allocations count toward these physical scratch limits. Physical exhaustion
triggers bounded GPU replay of affected programs with larger global payload
arenas. Replay preserves the original logical lane grouping and canonical
reduction order, replaces preliminary scores and case counters, and contributes
to measured evaluation time. Logical policy errors are not replayed. If physical
capacity cannot be supplied within the replay workspace bound, the fitness call
fails explicitly; an unresolved physical failure is never a scored case error.
The storage policy and replay counters are documented in
[the payload design](../docs/design/payload.md).
Configured CPU task metrics use the same ordered 32-lane-group reduction as
CUDA when `reduction_lanes` equals the GPU block size. Each accumulation uses
48-bit mantissa rounding; evolution ranking additionally canonicalizes to 40
bits. Raw evaluator scores and stored ranked fitness therefore have different
precision. Audit comparisons must compare the same representation, and record
raw errors and wrong-case counts separately. A numeric tolerance alone does not
establish value-level execution parity, especially for adjacent 64-bit integers
whose conversion to double can erase a real difference. Use exact metric or
raw-value oracles for those regressions.

### Case-order optimization

Verified integer-list view executors may reorder cases by input length for
`Legacy`, `Exact`, or `Absolute` with capped proximity when every possible
case score is integral and the bounded total is below 2^40. The view type-flow
proof excludes floating-point outputs; expected values must be Int or Bool.
Errors count with the same policy ceiling. Nonintegral error ceilings, discounted absolute proximity, and tuple-summary
scores retain the original reduction order. Exact scoring always has ceiling one. This changes scheduling only, not case coverage, scores, or wrong counts.
`FitnessPolicy::list_allocation_limit` optionally supplies a logical list-cell
arena limit (zero leaves the existing backend policy unchanged). Each case starts
with the sum of its effective typed-list input lengths: only slots in
`[0, program.n_locals)` count, and duplicate bindings use the last value, as in
the VM local loader. A binding outside this range is ignored even when it names
a list. `Singleton` consumes one cell;
`Concat` of two nonempty lists consumes their combined length on every execution,
including repeated identical results. An empty-list concat returns the other
operand without allocation, and slicing consumes no logical cells. The next
allocation that exceeds the limit produces the ordinary error penalty. Physical
storage exhaustion remains a separate backend constraint; a GPU result marks
physical payload exhaustion separately for bounded replay without treating a
logical limit error as recoverable. This policy does not change fuel or frame
limits. The aligned fixed-ASGP list-output benchmark selects the source arena
limit of 65536 cells; source character lists use native IntList cells too.

The explicit experimental `GAGP_GPU_PRIVATE_INTLIST_OFFSETS=1` profile scores
resident IntList outputs by content for Exact and Levenshtein, including exact
wrong-case counts. Its generated local list identity is collision-independent,
so it intentionally differs from canonical transport-registry aliasing under a
hash collision, and from token inequality caused by the historical saturated
host versus masked device length encoding at 65536 elements. Admission excludes container equality and IntList memo keys;
unsupported populations/policies report a declined profile and execute normally.
It accepts external payloads through 65536 elements and generated private
payloads through 65537 elements: clamped length 65535 plus length one charges
65536 while copying 65537 elements. Private content scoring retains that full
backing; numeric list observations retain the existing encoded-length rules.
It preserves the logical allocation policy, fuel, complete case coverage and
physical-overflow replay above. See `docs/design/payload.md` for its private
descriptor lifetime and per-generation capability requirements. The additional
`GAGP_GPU_COMPACT_INTLIST_CELLS=1` storage opt-in requires an admitted private
profile and preserves its scores, wrong counts and resource charges exactly;
only local physical cells change from boxed Values to signed 64-bit integers.
