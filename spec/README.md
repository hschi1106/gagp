# Specifications

These files define GAGP's formats and behavior. Guides and design documents
explain use and implementation but do not override them.

| Specification | Defines |
| --- | --- |
| [`task.md`](task.md) | Task files, judging, run results and saved populations |
| [`search_config.md`](search_config.md) | Search configurations and the equivalence of the GPU and CPU engines |
| [`grammar_definition.md`](grammar_definition.md) | Grammar definitions: imports, productions, scopes, constants, templates, generated populations |
| [`fitness_cases.md`](fitness_cases.md) | Case files and typed value encoding |
| [`fitness.md`](fitness.md) | Fitness metrics and penalties |
| [`grammar.md`](grammar.md) | Programs: the typed AST, binding, control flow and evaluation order |
| [`builtins_base.md`](builtins_base.md) | Scalar and character operations |
| [`builtins_runtime.md`](builtins_runtime.md) | String and list operations |
| [`bytecode_isa.md`](bytecode_isa.md) | The bytecode instruction set and its execution |
| [`bytecode_format.md`](bytecode_format.md) | Bytecode and value JSON formats |

Each format identifier (`gagp-task-v1`, `grammar-definition-v2`, ...) is owned
by one specification. A change of behavior updates its specification and its
conformance tests together; release history is in [VERSION.md](../VERSION.md).
