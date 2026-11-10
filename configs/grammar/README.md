# Grammar Definitions

Checked `grammar-definition-v2` files. The format is specified in
[grammar_definition.md](../../spec/grammar_definition.md) and explained in the
[grammar authoring guide](../../docs/guides/grammar-authoring.md).

- `packages/`: importable templates for bounded recursion schemes (divide and
  conquer, linear recursion, one- and two-dimensional dynamic programming) and
  `source_literals.json` with literal nonterminals for every value type.
- `examples/authoring/`: small scalar, sequence, template and memo grammars
  with matching cases.
- `examples/types/`: one grammar and case file per value type.
- `examples/`: further grammars that fill the packages, used by tests.
- `compat/`: grammars that express the four recursion schemes as complete
  programs, used by tests.
- `benchmarks/simple_exp.json`: the grammar of `data/fixtures/simple_exp_1024.json`.

Package names carry no meaning to the engine; a grammar uses a package only
through ordinary imports and references. Imports are relative to the importing
file, and a grammar's identity is a hash of its resolved content, so renaming
or moving files does not change it.

```bash
cpp/build/gagp grammar validate --grammar-definition configs/grammar/examples/authoring/scalar.json
```
