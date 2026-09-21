from __future__ import annotations

import sys
from collections.abc import Callable, Sequence


Command = Callable[[], int]


def _commands() -> dict[tuple[str, str], tuple[str, Command]]:
    from .datasets.convert_psb import main as convert
    from .datasets.fetch_psb import main as fetch
    from .datasets.materialize_psb import main as materialize
    from .experiments.materialize_population import main as materialize_population
    from .experiments.run_psb import main as run_psb
    from .grammar.commands import inspect_main as grammar_inspect
    from .grammar.commands import init_main as grammar_init
    from .grammar.commands import migrate_main as grammar_migrate
    from .grammar.commands import resolve_main as grammar_resolve
    from .grammar.commands import validate_main as grammar_validate
    from .reports.compare_psb import main as compare
    from .reports.psb_manifest import main as psb_manifest
    from .reports.simple_manifest import main as simple_manifest

    return {
        ("psb", "fetch"): ("fetch PSB datasets", fetch),
        ("psb", "convert"): ("convert PSB JSONL to fitness cases", convert),
        ("psb", "materialize"): ("materialize supported PSB fixtures", materialize),
        ("psb", "run"): ("run the PSB regression matrix", run_psb),
        ("psb", "compare"): ("compare compatible PSB summaries", compare),
        ("benchmark", "population"): ("materialize a compiled-grammar population", materialize_population),
        ("grammar", "init"): ("copy a grammar authoring example", grammar_init),
        ("grammar", "validate"): ("validate a grammar definition", grammar_validate),
        ("grammar", "inspect"): ("inspect a grammar definition", grammar_inspect),
        ("grammar", "resolve"): ("resolve a grammar definition", grammar_resolve),
        ("grammar", "migrate"): ("migrate a legacy grammar artifact", grammar_migrate),
        ("report", "psb-manifest"): ("write compact PSB evidence", psb_manifest),
        ("report", "simple-manifest"): ("write compact simple-expression evidence", simple_manifest),
    }


def help_text() -> str:
    lines = ["usage: gagp-tools <group> <command> [arguments]", "", "commands:"]
    for (group, command), (description, _) in _commands().items():
        lines.append(f"  {group} {command:<20} {description}")
    lines.append("")
    lines.append("Run 'gagp-tools <group> <command> --help' for command arguments.")
    return "\n".join(lines) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if not arguments or arguments in (["-h"], ["--help"]):
        print(help_text(), end="")
        return 0
    if len(arguments) < 2:
        print("gagp-tools: expected <group> <command>", file=sys.stderr)
        return 2
    key = (arguments[0], arguments[1])
    command = _commands().get(key)
    if command is None:
        print(f"gagp-tools: unknown command: {' '.join(key)}", file=sys.stderr)
        return 2

    previous = sys.argv
    sys.argv = [f"gagp-tools {' '.join(key)}", *arguments[2:]]
    try:
        return command[1]()
    finally:
        sys.argv = previous
