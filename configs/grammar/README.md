# Grammar Definitions

This tree contains checked `grammar-definition-v2` source files and the
release-1 inputs retained for offline migration.

- `packages/` contains ordinary importable source definitions. Package names
  have no runtime dispatch meaning; roots select templates/nonterminals through
  normal imports and references.
- `examples/authoring/` contains copyable scalar, typed-sequence, template, and
  memo roots with matching fitness cases.
- `examples/types/` contains a root and matching case file for each public
  value type: `Int`, `Float`, `Bool`, `Char`, `String`, `IntList`, `FloatList`,
  and `StringList`.
- `benchmarks/` contains definitions paired with maintained benchmark fixtures,
  including `simple_exp.json` for `data/fixtures/simple_exp_1024.json`.
- `compat/` contains runnable release-2 roots that represent supported legacy
  search intent using ordinary release-2 constructs.
- `migration/v1/` contains release-1 `grammar-config` inputs for explicit
  offline conversion. Production commands do not accept them directly.

Imports are relative to the importing JSON file. Canonical identity is computed
from resolved content and versioned schema/catalog semantics, not filenames or
directory names. Renaming an imported file and updating only its import path
therefore preserves the resolved hash; changing resolved content changes it.

Grammar JSON is loaded at process startup. Edit, validate, and rerun generation
or evolution without rebuilding C++ or reinstalling the operational tools:

```bash
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition configs/grammar/examples/authoring/scalar.json
```

See the [grammar authoring guide](../../docs/guides/grammar-authoring.md) for
complete workflows and the
[compiled grammar guide](../../docs/guides/grammar-config.md) for artifacts and
migration boundaries.
