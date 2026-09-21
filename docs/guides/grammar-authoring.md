# Grammar Authoring

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
semantic_version = gagp-native-2.0.0
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
