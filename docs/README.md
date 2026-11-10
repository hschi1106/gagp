# Documentation

Start with the [README](../README.md). Formats and semantics are specified in
[spec/](../spec/README.md); the documents here explain how to use GAGP and how
it works, and link to the specification instead of restating it.

## Guides

| Document | Read it to |
| --- | --- |
| [Getting started](guides/getting-started.md) | Build GAGP, run the example, set up your own task, fix common problems |
| [Grammar authoring](guides/grammar-authoring.md) | Write a grammar: constants, scopes, templates, recursion, GPU admission |
| [List-sum walkthrough](guides/list-sum-grammar.md) | Understand every field of one complete grammar, step by step |
| [Development](guides/development.md) | Build options, tests, sanitizers, fuzzing, and how to make changes |

## Reference

| Document | Lists |
| --- | --- |
| [Command reference](reference/cli.md) | Every `gagp` command, option, search setting and environment variable |
| [Repository layout](reference/repository-layout.md) | Where each part of the code and data lives |

## Design

| Document | Explains |
| --- | --- |
| [Research mainline](design/research-mainline.md) | The research claim, what "exact" means, measured results, retired designs |
| [Architecture](design/architecture.md) | Components, the flow of one run, formats, invariants |
| [GPU reproduction](design/gpu-reproduction.md) | The resident GPU engine and its CPU reference: layouts, capacities, operators, schedules |
| [Payload values](design/payload.md) | How strings and lists are stored on the host and the GPU |

The benchmark suite is documented in [benchmarks/README.md](../benchmarks/README.md),
grammar files in [configs/grammar/README.md](../configs/grammar/README.md), and
releases in [VERSION.md](../VERSION.md).

The repository tests (`python3 -m unittest discover -s tests/repository`) check
that links resolve, that this index lists every document, and that the command
reference matches the code.
