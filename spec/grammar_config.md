# Legacy Grammar Config Migration

`grammar-config` is the release-1 search-space format. It is not a release-2
production input, runtime mode, or evolution option. Release 2 uses only
compiled [`grammar-definition-v2`](grammar_definition.md) definitions for
generation, membership, variation, and evolution.

## Offline conversion contract

The migration command accepts a release-1 document with
`format_version: "grammar-config"`, an exact `fitness-cases` schema, and the
explicit `constrained-intent-v1` conversion profile. It emits a self-contained
`grammar-definition-v2`. Production CLIs reject `grammar-config` and direct
users to this offline conversion.

Conversion must:

- derive enabled ordinary operations from document contents rather than infer
  behavior from a filename such as `all.json`;
- require exact input and return types and reject `Any` or ambiguous empty-list
  inference;
- preserve structural limits from the source document and make every domain,
  schema, and execution limit in the constrained output explicit;
- map ordinary scalar, control, builtin, and typed-list choices to exact
  compiled productions;
- expand supported structured intent into general lexical/traversal/bounded
  templates;
- reject unknown fields, unsupported combinations, missing limits/domains, and
  ambiguous legacy intent with an actionable diagnostic.

A release-1 `grammar-config` does not contain constant domains, execution fuel,
an exact input/return schema, or the dynamic assignment environment used by the
release-1 generator. The current v2 finite-domain and fixed-local schema cannot
represent that generator's complete String/list literal domains and dynamic
name retyping. Therefore config conversion is deliberately labelled
`constrained-intent-v1`; it is not an exact release-1 program-set or seed
replay claim. The profile uses the exact case schema, canonical finite domains
(`Int[-1,1]`, `Float{0,1}`, both Bool values, `Char{'a'}`, and empty
String/typed-list values), and fuel 1,000,000. Those choices are visible in the
resulting definition. Omission or misspelling of the profile is rejected rather
than silently selecting it. Exact old behavior is preserved by migrating
materialized artifacts.

The converter reads every legacy structured switch so it cannot silently widen
or narrow the source search space. A switch that is enabled is rejected with an
instruction to migrate materialized ASTs or select the corresponding release-2
package: the boolean alone does not encode the typed holes, phase scopes, or
dependency choices needed for an exact compiled definition. Materialized
`MapList`, `FilterList`, `LinearRec`, ASGP, and DP nodes are migrated to ordinary
lexical, traversal, template, and bounded-region structures. No output contains
node, opcode, or catalog aliases for the removed forms.

## Materialized artifacts and seeds

Release-1 `ast-prefix` artifacts may be migrated offline because they contain
a materialized program. The output is `grammar-materialized-v2`, containing
target runtime semantic version `gagp-native-2.0.0`, an `ast-prefix-v2` shape,
exact inputs/return type, search and execution limits, losslessly detached
singleton-domain constants/payloads, and source identity. A plain legacy AST
needs `--cases`, `--fuel`, `--max-nodes`, and `--max-depth` because it does not
embed that contract.

A complete `grammar-generated-v1` member is already materialized and embeds
its schema, search limits, fuel, AST shape, constants, and provenance. It is
migrated without `--cases` or limit overrides. Full signed 64-bit integers,
Unicode characters, embedded-NUL strings, and typed lists use the detached
constant codec. Normal v2 execution reconstructs the pool by content and does
not load the old grammar or package files.

`grammar-population-v1` is not converted as one container. Extract each
complete member and migrate it independently; the command rejects the
container with that instruction.

`population-seeds` cannot guarantee exact replay under the v2 generator and
RNG mapping. A seed-only input must fail with instructions to materialize every
member using the frozen release-1 build and then migrate those materialized
ASTs. Changed regeneration must never be labelled exact replay.

Release-1 bytecode is also rejected: it cannot recover the source AST or exact
grammar/type provenance. Migrate the source AST and recompile it.

## Isolation

Legacy decoding is linked only into migration executables/libraries and their
tests or benchmarks. The normal AST/bytecode codecs, compiler, runtime,
generation, reproduction, and product CLI do not accept this schema or legacy
side tables. Checked files under `configs/grammar/compat/` and
`configs/grammar/packages/` are release-2 definitions; historical names
describe compatibility intent only.

## Validation

For every maintained release-1 preset, conversion is deterministic for the
same source, case schema, and explicit choices. Tests compare canonical output
and diagnostics. Semantic equivalence is established with materialized
programs and the frozen release-1 oracle; a filename or seed alone is
insufficient evidence.
