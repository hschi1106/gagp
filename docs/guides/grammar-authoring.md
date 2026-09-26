# Grammar Authoring

Restricted evolution uses compiler-verified replacement classes, fixed donor
interfaces and template skeletons. The complete
[restricted example](../../configs/grammar/examples/restricted_variation.json)
demonstrates all three policies:

- `Left` and `Right` have the same accepted language and share the `integer`
  crossover group. `inspect` reports their equal `replacement_class`; changing
  one rule's accepted operators/domain separates them even if the group is kept.
- Fresh donors see the declared input `n` and each rule's declared lexical scope.
  They do not inherit extra ordinary names from their destination. If an ordinary
  local is intentionally required, declare e.g.
  `"mutation_locals":[{"name":"x","type":"Int"}]` on the selected rule;
  `x` must also be a grammar local and available at the actual destination.
- `Offset` fixes its add skeleton and exposes the `value` hole plus an explicitly
  `mutable` constant. Repeated holes stay atomic (at most 64 occurrences), and
  the whole template can still be replaced through `Main`.

See the [normative restrictions and migration rules](../../spec/grammar_definition.md#restricted-template-variation).
CPU donors remain fresh; GPU preparation may share a bounded pool across eight
equivalent destinations, refreshing every preparation window. This changes
sampling correlations and seed trajectories. Replay identity is now
`gagp-native-2.0.0-restricted-1`; regenerate seed-only artifacts under this version.

Build the native commands and install the thin Python command wrapper from the
repository root:

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug
cmake --build cpp/build -j
python3 -m venv .venv-tools
.venv-tools/bin/pip install -e tools
```

`gagp-tools` does not interpret a grammar. `init` copies a checked example;
`validate`, `inspect`, and `resolve` invoke `cpp/build/gagp_grammar_cli`; and
`migrate` invokes the offline native migration adapter. Use `/tmp` for the
workflows below and `logs/` for results that must be retained.

## Authoring commands

```bash
.venv-tools/bin/gagp-tools grammar init \
  --example scalar --out /tmp/my-grammar.json
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition /tmp/my-grammar.json
.venv-tools/bin/gagp-tools grammar inspect \
  --grammar-definition /tmp/my-grammar.json
.venv-tools/bin/gagp-tools grammar resolve \
  --grammar-definition /tmp/my-grammar.json \
  --out /tmp/my-grammar.resolved.json
.venv-tools/bin/gagp-tools grammar migrate \
  --input configs/grammar/migration/v1/scalar.json \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --conversion-profile constrained-intent-v1 \
  --out /tmp/scalar.migrated-v2.json
```

`init` accepts `scalar`, `sequence`, `template`, or `memo` and refuses to
overwrite its output. `validate` checks resolution, compilation, productivity,
types, scopes, limits, and whether the entry is executable. `inspect` emits the
compiled IDs, contexts, scopes, costs, counts, and version identity. `resolve`
writes canonical, import-resolved `grammar-definition-v2` JSON and refuses to
overwrite the root definition or any imported source file.

The deterministic identity tuple reported by validation/inspection is:

```text
definition_version = grammar-definition-v2
catalog_version = gagp-primitives-v3
normalization_version = 1
semantic_version = gagp-native-2.0.0-restricted-1
generator_version = typed-derivation-v2
rng_version = splitmix64-rejection-v1
```

The tuple plus the canonical resolved-content `grammar_hash`, seed, generation
request, and exact case schema identifies same-version generation. Moving or
renaming a root or imported package without changing resolved content preserves
the hash; changing resolved content changes it. A JSON edit is loaded on the
next command and needs no C++ rebuild or Python reinstall.

## Workflow 1: scalar grammar

Start with `Authoring.Scalar`, whose entry and input are `Int`. It initially
chooses input `n` with weight 3, constant zero with weight 1, or recursive
`add(Int,Int)->Int` with weight 1, within 9 nodes, depth 5, and fuel 32.

```bash
.venv-tools/bin/gagp-tools grammar init \
  --example scalar --out /tmp/scalar.json
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition /tmp/scalar.json
.venv-tools/bin/gagp-tools grammar inspect \
  --grammar-definition /tmp/scalar.json
cpp/build/gagp_generate_cli \
  --grammar-definition /tmp/scalar.json \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --population-size 16 --seed 7 \
  --out-json /tmp/scalar.population-v2.json
cpp/build/gagp_generate_cli \
  --replay-json /tmp/scalar.population-v2.json \
  --grammar-definition /tmp/scalar.json \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --out-json /tmp/scalar.replayed.population-v2.json
cmp /tmp/scalar.population-v2.json /tmp/scalar.replayed.population-v2.json
cpp/build/gagp_evolve_cli \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --grammar-definition /tmp/scalar.json \
  --engine cpu --repro-backend cpu --repro-overlap off \
  --population-size 16 --generations 2 \
  --out-json /tmp/scalar.cpu.run.json
```

For a concrete customization, change the `sum` production signature from
`add(Int,Int)->Int` to `mul(Int,Int)->Int`, change its weight from 1 to 3, and
change `input` from 3 to 1. Keep both recursive arguments as
`{"ref":"Authoring.Scalar"}` and keep the result type `Int`; then rerun
validate and inspect before generation.

## Workflow 2: typed sequence grammar

Numeric terminals may use `{"type":"Float","range":[-5,5]}` or an Int range
with decimal-string endpoints. Float interval membership includes both endpoints;
generation and constant resampling use `[minimum,maximum)`, with a singleton
returning its endpoint exactly. Both endpoints must be finite. This also works
as the element domain of a `FloatList` sequence. A range controls resampling,
not additive perturbation of the previous value.

To round sampled Floats to three decimal places, add
`"quantization_scale":1000` to the Float range domain. Sampling then rounds
the continuous draw, preserving half-width endpoint cells and negative zero.
Endpoints must lie on the grid; scaled endpoint magnitudes must not exceed
`2^50`. The same option works for FloatList element domains and on-device
scalar constant mutation.

Set `"mutation":"keep"` on a constant domain to preserve its value when the
constant mutation branch selects it. It remains a selectable candidate; this is
an accepted unchanged result, not a request for subtree mutation. For Bool domains
containing both values, `"mutation":"flip"` complements the current value.
Omission or `"resample"` retains ordinary domain sampling. These policies do not
change generation or membership and cannot be set on sequence elements or nested
`sample_from` domains.

For a numeric range, use `"mutation":{"kind":"add","range":[-1,1]}`
to perturb the current Float value; Int delta endpoints use decimal strings,
such as `["-2","2"]`. Keep membership broad enough to admit evolved values and
use `sample_from` for narrow construction. Addition never reapplies construction
quantization. A sum outside membership (or overflowing its type) retains the old
value without retry or fallback. Quantized outer membership is not supported for
addition. Float mutation may also specify `"gpu_grid_steps":65535`: GPU uses
65536 equally weighted closed-interval grid indices, while CPU retains continuous
sampling. Omit it for continuous deltas on both backends.

Use `sample_from` on a numeric range when construction should draw from a smaller
domain than membership allows. For example:

```json
{"type":"Float","range":[-100,100],"sample_from":{
  "type":"Float","range":[-8,8],"quantization_scale":1000
}}
```

This constructs three-decimal values between -8 and 8, but also admits values
such as 42.125. The sampler must have the same type, remain within the outer
domain and must not contain another `sample_from`. Current constant resampling
also uses this sampler; this option alone does not perform additive mutation.

Constant domains accept exactly one of `values`, an inclusive `Int` `range`, or
`sequence`. A sequence describes lengths and an exact element domain without
enumerating every possible payload:

```json
{"constant":{"type":"StringList","sequence":{
  "length":[0,5],
  "element":{"type":"String","sequence":{
    "length":[0,8],"element":{"type":"Char","values":["a","b","c"]}
  }}
}}}
```

`String` elements must be `Char`; `IntList`, `FloatList`, and `StringList`
elements must be `Int`, `Float`, and `String`, respectively. Lists cannot nest.
String length counts Unicode scalars, including NUL, rather than UTF-8 bytes.
Lengths are inclusive integers from 0 through 65536. Compilation bounds a
sample's expansion by 16 MiB, charging eight bytes per scalar/sequence and the
decoded bytes of finite String elements. This is a construction bound, not a
runtime payload limit or a measurement of allocator overhead.

Sampling first chooses a length uniformly, then independently samples each
element from its declared domain. Duplicate finite values retain their declared
sampling weight. Generation, constant mutation, subtree donors and membership
share these domains. Fixed template skeletons still require singleton `values`;
put a variable sequence domain in an evolvable hole. Saved program constants
remain concrete singleton-domain payloads, and existing finite/range seeds keep
their sampling sequence.

`Authoring.Sequence` takes and returns `IntList`. Its alternatives are input
(weight 2), `reverse(IntList)->IntList` (1), and
`concat(IntList,IntList)->IntList` with the original and reversed input (1).
Changing `concat_with_reverse` from weight 1 to weight 3 changes sampling only;
it does not change its operator or type.

```bash
.venv-tools/bin/gagp-tools grammar init \
  --example sequence --out /tmp/sequence.json
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition /tmp/sequence.json
cpp/build/gagp_generate_cli \
  --grammar-definition /tmp/sequence.json \
  --cases configs/grammar/examples/authoring/sequence.cases.json \
  --population-size 16 --seed 9 \
  --out-json /tmp/sequence.population-v2.json
cpp/build/gagp_evolve_cli \
  --cases configs/grammar/examples/authoring/sequence.cases.json \
  --grammar-definition /tmp/sequence.json \
  --engine gpu --repro-backend gpu --repro-overlap on \
  --population-size 16 --generations 2 \
  --out-json /tmp/sequence.gpu.run.json
```

Fixture tags convert to grammar types exactly as follows:

| Fitness-case tag | Grammar type |
| --- | --- |
| `int` | `Int` |
| `float` | `Float` |
| `bool` | `Bool` |
| `char` | `Char` |
| `string` | `String` |
| `int_list` | `IntList` |
| `float_list` | `FloatList` |
| `string_list` | `StringList` |

Checked single-type roots and paired cases for every row live under
`configs/grammar/examples/types/`. Lists are homogeneous and typed; there is no
public generic list, numeric-list union, nested list, or implicit numeric/list
conversion. The current fitness-case codec accepts `char` only as a one-byte,
length-1 JSON string (the checked example uses `q`); grammar constant domains
have their own Char parsing rules. Input names, ordered input declarations, and
the return type must also match the cases exactly.

## Workflow 3: templates, recursion, and memoization

The small template example shows a shared hole: `Authoring.Twice.value` has
type `Int`, scope `[]`, and occurs twice in `add(Int,Int)->Int`. Start it with:

```bash
.venv-tools/bin/gagp-tools grammar init \
  --example template --out /tmp/template.json
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition /tmp/template.json
cpp/build/gagp_generate_cli \
  --grammar-definition /tmp/template.json \
  --cases configs/grammar/examples/authoring/template.cases.json \
  --population-size 8 --seed 3 \
  --out-json /tmp/template.population-v2.json
```

For an imported divide-and-conquer package, copy the root and package while
preserving their relative directory layout. Customize the `combine` hole through
`Example.RestrictedCombine.Combine`; do not edit the imported package. Its
required scope is exactly `left:Int, right:Int`. Change the existing
`maximum` alternative to ID `sum`, signature `add(Int,Int)->Int`, arguments
`left` and `right`, and weight 3. Add a second `maximum` alternative with
signature `max(Int,Int)->Int`, the same arguments and scope, and weight 1.
Validate and exercise the derived definition with:

```bash
mkdir -p /tmp/gagp-derived/examples /tmp/gagp-derived/packages
cp configs/grammar/examples/restricted_combine.json \
  /tmp/gagp-derived/examples/derived.json
cp configs/grammar/packages/dc_intlist_int.json \
  /tmp/gagp-derived/packages/dc_intlist_int.json
.venv-tools/bin/gagp-tools grammar resolve \
  --grammar-definition /tmp/gagp-derived/examples/derived.json \
  --out /tmp/gagp-derived.before.json
mv /tmp/gagp-derived/packages/dc_intlist_int.json \
  /tmp/gagp-derived/packages/renamed-dc.json
sed -i 's#../packages/dc_intlist_int.json#../packages/renamed-dc.json#' \
  /tmp/gagp-derived/examples/derived.json
.venv-tools/bin/gagp-tools grammar resolve \
  --grammar-definition /tmp/gagp-derived/examples/derived.json \
  --out /tmp/gagp-derived.after-rename.json
cmp /tmp/gagp-derived.before.json /tmp/gagp-derived.after-rename.json
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition /tmp/gagp-derived/examples/derived.json \
  > /tmp/gagp-derived.before-edit.validation.json
```

The successful `cmp` proves that a package filename/import-path rename does not
change resolved content or its grammar hash. Make the `sum`/`maximum` edits
described above, then confirm that the content edit changes the reported hash:

```bash
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition /tmp/gagp-derived/examples/derived.json \
  > /tmp/gagp-derived.after-edit.validation.json
grep -o '"grammar_hash":"[^"]*"' \
  /tmp/gagp-derived.before-edit.validation.json \
  /tmp/gagp-derived.after-edit.validation.json
.venv-tools/bin/gagp-tools grammar inspect \
  --grammar-definition /tmp/gagp-derived/examples/derived.json
cpp/build/gagp_generate_cli \
  --grammar-definition /tmp/gagp-derived/examples/derived.json \
  --cases configs/grammar/examples/restricted_combine.cases.json \
  --population-size 16 --seed 11 \
  --out-json /tmp/derived.population-v2.json
```

The memo example is a complete bounded two-coordinate recurrence with
`row:Int`, `column:Int`, rank `(0,+1),(1,+1)`, decreasing requests
`(-1,+2)` and `(0,-1)`, and limits of 128 frames and 128 cells. Inspect it
before altering dependencies: every request must still strictly decrease at
its first nonzero ranked coordinate, remain overflow-safe for the declared
domain, and fit the frame/cell limits.

```bash
.venv-tools/bin/gagp-tools grammar init \
  --example memo --out /tmp/memo.json
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition /tmp/memo.json
.venv-tools/bin/gagp-tools grammar inspect \
  --grammar-definition /tmp/memo.json
cpp/build/gagp_generate_cli \
  --grammar-definition /tmp/memo.json \
  --cases configs/grammar/examples/authoring/memo.cases.json \
  --population-size 8 --seed 5 \
  --out-json /tmp/memo.population-v2.json
```

## Bounds and comparison limits

`search_limits.max_nodes` and `search_limits.max_depth` bound construction,
generation, membership, and variation. `execution_limits.fuel` bounds program
execution. Structured regions additionally own explicit frame/cell/entry-fuel
bounds. Search bounds do not increase execution fuel, and execution bounds do
not enlarge the authoring search space. The production runner accepts only
`--grammar-definition`; there is no `--grammar-config` alias.

Use these exact backend combinations for smoke or comparative runs; the
[benchmarking guide](benchmarking.md) gives complete commands that reuse one
population across all four:

| Mode | `--engine` | `--repro-backend` | `--repro-overlap` |
| --- | --- | --- | --- |
| CPU | `cpu` | `cpu` | `off` |
| GPU evaluation, CPU reproduction | `gpu` | `cpu` | `off` |
| GPU evaluation and synchronous GPU reproduction | `gpu` | `gpu` | `off` |
| GPU evaluation and overlapped GPU reproduction | `gpu` | `gpu` | `on` |

Release-1 `grammar-config` migration preserves only representable ordinary
enable/disable intent under an explicit schema and profile. It cannot recover
exact literal domains, typed scopes/holes, structured dependencies, package
choices, or seed replay. Materialized AST/member migration preserves values and
execution behavior within its documented boundary, but cannot recreate the old
search process. Cross-version results are oracle/context comparisons, not exact
generator replay. Research comparisons must also hold cases, resolved grammar
hash, limits, seed budget, backend policy, and hardware constant; CPU speedup is
relative to this repository's sequential CPU backend.

The normative fields and validation rules are in
[grammar_definition.md](../../spec/grammar_definition.md); the release-1
conversion boundary is in [grammar_config.md](../../spec/grammar_config.md).
The checked tree and package roles are indexed in
[configs/grammar/README.md](../../configs/grammar/README.md).


Production generation stages can preserve different initialization and subtree
mutation rules while keeping one membership contract. Add
`"generation_stages": ["initial"]` or `["mutation"]` to an alternative; omit the
field to enable both. `[]` makes an alternative available only to membership and
reconstruction. For example, two alternatives on the same String nonterminal
can use different sequence length bounds for initial programs and mutation
donors. All referenced productions and template holes follow the selected stage.
If the stage has no budget-feasible derivation, generation fails rather than
using another stage's alternatives. Crossover still uses the complete grammar.
This setting does not alter constant perturbation or runtime execution.

Set `"variation":false` on a nonterminal to omit that occurrence from subtree
replacement candidates. For example, a Program root can keep its outer structure
while referenced expression nonterminals remain evolvable. The default is true.
This does not freeze descendants or change constant mutation; use their own
nonterminal and constant-domain policies for those controls. CPU and GPU use the
same filtered site analysis.

Alternatives can declare `"crossover_group":"coordinate-int"` to explicitly
partition exchanges across different nonterminals or template holes. The compiler
must also prove equal replacement classes; a label cannot assert interchangeability.
Exact type and scope still have to match, and child validation remains active. Different groups
separate structural roles even within one nonterminal. Omit the field to retain
the default compiler-verified replacement contract. Grouping does not change
which subtree mutation donors are generated.

Add `"crossover_scope":"closed"` to a grouped alternative when complete
subtrees should exchange even if their declarations list different unused outer
bindings. The runtime first proves that the actual subtree has no external
lexical references. A capturing subtree keeps exact scope matching. This is
useful for moving a complete nested traversal whose bindings are all internal;
it does not permit moving a body that depends on an enclosing traversal element.
Mutation uses the rule's declared lexical interface and `mutation_locals`.

Use `"mutation_entry":"OtherRule"` when a node needs one construction
distribution inside a newly generated structure and another when selected for
subtree mutation. For example, a coordinate rule can sample the fixed value 2
while admitting the full Int range, and redirect standalone mutation to a rule
sampling integers from -8 to 8. Both rules must declare the same exact type,
category and ordered scope. The redirect applies once at the selected site;
referenced children still construct normally. It does not widen membership or
crossover compatibility, and an inadmissible replacement is rejected. Set
`variation:false` on helper rules when they should not add replacement sites.

`configs/grammar/packages/source_literals.json` supplies reusable
`Source.Literal.Int`, `Float`, `Bool`, `Char`, `String`, `IntList`, `FloatList`
and `StringList` expression nonterminals (each uses the `Source.Literal.` prefix).
Import it and reference the desired nonterminal to use the frozen release-1
ordinary literal policy. Initial and mutation construction are separate:
lists allow lengths 0–5 and 0–4 respectively, nested StringList strings allow
0–8 and 0–5, and Char uses letters/digits/space initially versus letters for
donors. Standalone strings use 0–8 lowercase letters in both stages.
Int/Float sampling uses the original [-8,8] numeric domains and Float quantizer;
membership remains wide enough for additive perturbation. Bool flips, while
Char and sequence constants retain their value when selected for constant
mutation. This package does not define operator weights, binder scopes,
structured skeleton literals, or the special [-6,6] index/slice parameters.

For resource correspondence studies, a materialized expression can declare
`"resource_charge":{"nodes":0,"depth":0,"resets_depth":false}` to represent
administrative expansion without construction cost. The C++
`project_derivation_resources` interface derives costs from verified grammar
membership; explicit structural nodes can use `resets_depth:true` to model
expression-depth accounting. Physical `search_limits` always still apply.
Native callers can additionally set `EvolutionConfig.initial_resource_budget`
and `offspring_resource_budget` to enforce projected admission at initialization
and CPU/GPU reproduction. These optional budgets are not CLI options. Generation
uses bounded retries, so exhaustion does not prove that a grammar is infeasible.
Admission reconstructs membership instead of trusting attached derivation metadata.

The LinearRec, DC, DP1D and DP2D compatibility packages annotate each source
operation with unit cost and its expansion-only helper nodes with zero cost.
Their source operands retain their own costs. DP coordinate inputs count as
source operands; boundary constants stored as source metadata do not. A standalone
expression still has the native four-node program envelope; its full projected
depth therefore includes that envelope. A source expression-depth comparison must
use the expression subtree, or an explicitly authored structural depth reset.
These package annotations alone do not reproduce a complete legacy search profile.
The legacy grammar-config converter also annotates explicit program, block and
statement nodes with one node, zero depth, and a depth reset. Whole-program
projected depth then measures source expression depth, without subtracting an
assumed fixed envelope from programs containing branches or multiple statements.

The C++ `joint_resource_frontier` query can check structural feasibility against
physical and projected limits together. An empty result means no admitted cost
combination; a capacity exception means the query did not finish. Neither result
substitutes for native/contextual validation or enables larger production limits.

To preserve a program that updates an input variable, use `input_name` instead
of `name` on its assignment control. For example, an `assign(Float)->Statement`
with `input_name: "x"` writes the declared Float input slot; subsequent
`{"input":"x"}` expressions read that updated value. The declaration type stays
fixed, and the original fitness case is unchanged.

Use alternative `variation: "unbound"` to exclude choices containing authored
bound references from subtree operators while retaining their independent
descendant choices. Fixed template implementation references do not exclude
a site. Alternative `variation: false` disables only that alternative's site.
