# Search configurations

[`default.json`](default.json) is the search `gagp run` uses when no
`--search` file is given; the CLI test checks that the two stay identical.
Copy it, or write a file with only the members you change, and pass it with
`gagp run TASK.json --search FILE`. Command-line options override the file.

The members are explained in the [command reference](../../docs/reference/cli.md#search-configuration)
and defined in the [specification](../../spec/search_config.md). The
search configuration decides results; the engine (`--engine gpu|cpu`) never does.

When comparing with another GP system, set the `common` members of the
specification to that system's values and record the file used.
