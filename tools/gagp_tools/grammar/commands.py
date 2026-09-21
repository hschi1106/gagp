from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Sequence
from pathlib import Path


EXAMPLE_NAMES = ("scalar", "sequence", "template", "memo")
AUTHORING_EXAMPLES = Path("configs/grammar/examples/authoring")
DEFAULT_GRAMMAR_NATIVE = Path("cpp/build/gagp_grammar_cli")
DEFAULT_MIGRATE_NATIVE = Path("cpp/build/gagp_migrate_artifact")


def _parents(path: Path) -> tuple[Path, ...]:
    resolved = path.resolve()
    return (resolved, *resolved.parents)


def find_examples_root(
    explicit: Path | None,
    *,
    cwd: Path | None = None,
    module_path: Path | None = None,
) -> Path:
    if explicit is not None:
        return explicit

    starts = (
        cwd if cwd is not None else Path.cwd(),
        (module_path if module_path is not None else Path(__file__)).parent,
    )
    visited: set[Path] = set()
    for start in starts:
        for parent in _parents(start):
            if parent in visited:
                continue
            visited.add(parent)
            candidate = parent / AUTHORING_EXAMPLES
            if candidate.is_dir():
                return candidate
    raise FileNotFoundError(
        f"cannot locate {AUTHORING_EXAMPLES}; pass --examples-root PATH"
    )


def _copy_exclusive_atomically(source: Path, output: Path) -> None:
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"refusing to overwrite existing path: {output}")

    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{output.name}.tmp.", dir=output.parent, delete=False
        ) as destination:
            temporary = Path(destination.name)
            with source.open("rb") as origin:
                shutil.copyfileobj(origin, destination)
            destination.flush()
            os.fsync(destination.fileno())

        # A hard link installs the complete temporary file without replacing an
        # output another process may have created after the initial check.
        os.link(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def init_main() -> int:
    parser = argparse.ArgumentParser(
        description="Copy a checked grammar authoring example into a new file."
    )
    parser.add_argument("--example", required=True, choices=EXAMPLE_NAMES)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--examples-root", type=Path)
    args = parser.parse_args()

    try:
        examples_root = find_examples_root(args.examples_root)
        source = examples_root / f"{args.example}.json"
        if not source.is_file():
            raise FileNotFoundError(f"grammar example does not exist: {source}")
        _copy_exclusive_atomically(source, args.out)
    except OSError as error:
        print(f"gagp-tools grammar init: {error}", file=sys.stderr)
        return 2
    return 0


def _run_native(command: Sequence[str]) -> int:
    try:
        return subprocess.run(list(command), check=False).returncode
    except OSError as error:
        print(f"gagp-tools: {error}", file=sys.stderr)
        return 2


def _grammar_native_main(action: str, *, accepts_out: bool = False) -> int:
    parser = argparse.ArgumentParser(
        description=f"Run native grammar {action} without interpreting the grammar in Python."
    )
    parser.add_argument("--native", type=Path, default=DEFAULT_GRAMMAR_NATIVE)
    parser.add_argument("--grammar-definition", required=True, type=Path)
    if accepts_out:
        parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    command = [
        str(args.native),
        action,
        "--grammar-definition",
        str(args.grammar_definition),
    ]
    if accepts_out and args.out is not None:
        command.extend(["--out-json", str(args.out)])
    return _run_native(command)


def validate_main() -> int:
    return _grammar_native_main("validate")


def inspect_main() -> int:
    return _grammar_native_main("inspect")


def resolve_main() -> int:
    return _grammar_native_main("resolve", accepts_out=True)


def migrate_main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the native legacy-artifact migration adapter."
    )
    parser.add_argument("--native", type=Path, default=DEFAULT_MIGRATE_NATIVE)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--cases", type=Path)
    parser.add_argument("--conversion-profile")
    parser.add_argument("--fuel")
    parser.add_argument("--max-nodes")
    parser.add_argument("--max-depth")
    args = parser.parse_args()

    command = [str(args.native), "--input", str(args.input), "--out", str(args.out)]
    for option, value in (
        ("--cases", args.cases),
        ("--conversion-profile", args.conversion_profile),
        ("--fuel", args.fuel),
        ("--max-nodes", args.max_nodes),
        ("--max-depth", args.max_depth),
    ):
        if value is not None:
            command.extend([option, str(value)])
    return _run_native(command)
