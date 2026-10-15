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

GPU fitness's `extended-payload` profile has per-case limits of 4096 list values,
256 list entries, 8192 string bytes and 256 string entries. Intermediate
allocations count toward these limits. Capacity exhaustion is a Value error
with the ordinary error penalty, rather than a synthetic successful result.
Configured CPU task metrics use the same ordered 32-lane-group reduction as
CUDA when `reduction_lanes` equals the GPU block size. Each accumulation uses
48-bit mantissa rounding; evolution ranking additionally canonicalizes to 40
bits. Raw evaluator scores and stored ranked fitness therefore have different
precision. Audit comparisons must compare the same representation, and record
raw errors and wrong-case counts separately. A numeric tolerance alone does not
establish value-level execution parity, especially for adjacent 64-bit integers
whose conversion to double can erase a real difference. Use exact metric or
raw-value oracles for those regressions.
