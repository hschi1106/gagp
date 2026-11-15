#!/usr/bin/env python3
"""Generate the psb1_g3p benchmark: six PSB1 problems with the G3P grammars of
Forstenlechner et al. ported to the GAGP grammar language.

    python3 benchmarks/psb1_g3p/build.py files            # rewrite the committed grammar/task/search files
    python3 benchmarks/psb1_g3p/build.py files --check    # fail when the committed files are stale
    python3 benchmarks/psb1_g3p/build.py cases --data PONYGE2 [--out DIR]

`files` writes `<problem>/{grammar,task,search}.json`, which are committed so the
grammars can be read and reviewed. `cases` converts the PSB1 case files shipped
with PonyGE2 (`datasets/progsys/<Problem>/{Train,Test}.txt`) into
`<problem>/{train,test}.cases.json`. Those are not committed. `--data` is a
PonyGE2 checkout or its `datasets/progsys` directory. The ported constructs and
the differences from the original are listed in benchmarks/psb1_g3p/README.md.

Sources: the grammars are PonyGE2's `grammars/progsys/*.bnf` (listed in
Forstenlechner's PhD thesis, appendix B). The settings are thesis Table 4.1.

Porting rules
- Same nonterminals, variable lists and operators as the .bnf. A nonterminal's
  productions are chosen uniformly in G3P. Unit-production chains
  (<statement> ::= <simple_stmt> | <compound_stmt> ...) are inlined with the
  probabilities multiplied, so every original production keeps its selection
  probability at its nonterminal.
- `while <bool>: <code>` is while_stmt(LOOP_BOUND, <bool>, <code>), the bounded
  loop of the language. It keeps its original probability: 1/6 of <statement>
  for the list problems (<compound_stmt> has for, if, while) and 1/4 for the
  others (if, while).
- `v op= e` is v = op(v, e). `L.append(e)` is L = append(L, e), and likewise
  insert, deleteListItem and setListIndexTo become insert, remove_at and set_at
  (list edits here return a new list). deleteListItem and setListIndexTo whose
  first argument is not a list variable act on a temporary in Python: that part
  of their probability is a statement that does nothing.
- `for forCounter in <list_int>` is for_range(len(<list_int>), body).
  `for <int_var> in <list_int>` iterates a list variable (not an arbitrary list
  expression): for_range(len(L), [v = index_mod(L, k)] + body) with the hidden
  counter k.
- getIndexIntList / getCharFromString are index_mod, saveRange is range,
  `x in L` is contains(L, x), saveChr(n) is chr(n mod 128) and saveOrd(s) is
  ord of the first character of s + " " (32 for the empty string).
- divInt and mod are idiv0 and imod0: they differ for divisors <= 0 and divInt
  truncates where Python floors (see the README).
- `+<int>` (identity) is dropped from <arith_prefix>, leaving neg.
- int(<number>.0), with <number> a digit string, is an Int constant whose digit
  count is 1, 2, 3, 4 or 5 with probability 1/2, 1/4, 1/8, 1/16, 1/16, uniform
  within that digit count. String literals are one character (printable ASCII
  from space to `}`, tab, newline) or the empty string, plus the problem
  constants ('small', 'large').
- Every temporary variable and res0 start at their type's zero value.
"""
from __future__ import annotations

import argparse
import ast
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

SUITE_DIR = Path(__file__).resolve().parent

# ---------------------------------------------------------------------------
# Tunable constants. Everything that is a choice of this port rather than a
# translation of the original grammar lives here.
# ---------------------------------------------------------------------------
# Execution limit. The thesis stops a program after 1 s of wall time and, with
# the grammars' loopBreak counter, after loopBreakConst loop iterations in
# total. GAGP counts fuel instead, which is deterministic and identical on both
# engines.
FUEL = 20000
# GAGP's grammar depth limit. The original has no depth limit, only the node limit.
MAX_DEPTH = 40
# Node limit. The thesis limits a derivation tree to 250 nodes. GAGP counts AST
# nodes of the body (the fixed prologue is added on top), and one AST node
# stands for about 1.5 derivation-tree nodes, so 250 / 1.5 is about 170.
BODY_NODES = 170
# Bound of every `while`. The original guards each loop with
# `if loopBreak% > loopBreakConst: break`. The thesis sets loopBreakConst in a
# header but its text does not give the value, and the PonyGE2 grammars never
# define it, so 200 is a choice of this port. The longest input of these six
# problems is a list of 49 integers (count_odds, negative_to_zero) or a string of
# 20 characters (replace_space_with_newline), so 200 covers a loop over any input
# with margin, and it leaves about 100 instructions of fuel per iteration at FUEL.
LOOP_BOUND = 200
# Loop translation: the hidden counter of for_range, and the slice end that
# stands for `L[a:]` (slices clamp, and no list or string reaches this length).
LOOP_COUNTER = "k"
SLICE_END = "1000000"
# saveChr(n) is chr(n % 128).
CHAR_MODULUS = 128
# Constants of the fitness policy: the error of a failed case (it is also the
# error of a runtime error) is large so that it never hides behind the cap.
CASE_CAP = 1000000
# Logical limit on the list cells and string bytes one case may allocate, set on every task.
# It bounds what a program can build within its fuel, so both engines score every program
# instead of stopping when their physical payload storage runs out.
LIST_ALLOCATION_LIMIT = 65536
# Thesis Table 4.1.
POPULATION = 1000
TOURNAMENT = 7
CROSSOVER = 0.9
MUTATION = 0.05
ELITISM = 1

CASE_TYPE = {"Int": "int", "Bool": "bool", "String": "string", "IntList": "int_list"}
ZERO = {"Int": "0", "Bool": False, "String": "", "IntList": []}
# <string_literal> of the original: the empty string, newline, tab, then ' ' to '}'.
CHARS = ["\n", "\t", ""] + [chr(c) for c in range(32, 126)]


@dataclass(frozen=True)
class Problem:
    key: str
    pony: str                 # directory name under datasets/progsys
    inputs: tuple[str, ...]
    output: str
    int_vars: tuple[str, ...]
    list_vars: tuple[str, ...]
    str_vars: tuple[str, ...]
    str_consts: tuple[str, ...]
    generations: int
    fitness: dict[str, Any]


INT3 = ("i0", "i1", "i2")
PROBLEMS = [
    Problem("smallest", "Smallest", ("Int",) * 4, "Int", INT3 + ("res0", "in0", "in1", "in2", "in3"), (), (), (),
            200, {"metric": "exact"}),
    Problem("median", "Median", ("Int",) * 3, "Int", INT3 + ("res0", "in0", "in1", "in2"), (), (), (),
            200, {"metric": "exact"}),
    Problem("count_odds", "Count Odds", ("IntList",), "Int", INT3 + ("res0",), ("li0", "li1", "li2", "in0"), (), (),
            300, {"metric": "absolute"}),
    Problem("small_or_large", "Small Or Large", ("Int",), "String", INT3 + ("in0",), (),
            ("s0", "s1", "s2", "res0"), ("small", "large"), 300, {"metric": "levenshtein"}),
    Problem("replace_space_with_newline", "Replace Space with Newline", ("String",), "String", INT3 + ("res1",), (),
            ("s0", "s1", "s2", "res0", "in0"), (), 300, {"metric": "levenshtein"}),
    Problem("negative_to_zero", "Negative To Zero", ("IntList",), "IntList", INT3, ("li0", "li1", "li2", "res0", "in0"),
            (), (), 300, {"metric": "levenshtein"}),
]
BY_KEY = {p.key: p for p in PROBLEMS}


def ref(n: str) -> dict: return {"ref": n}
def sig(s: str, *args: dict) -> dict: return {"signature": s, "args": list(args)}
def const(t: str, values: list) -> dict: return {"constant": {"type": t, "values": values}}
def irange(lo: int, hi: int) -> dict: return {"constant": {"type": "Int", "range": [str(lo), str(hi)]}}
def split(w: float, items: list) -> list: return [(w / len(items), x) for x in items]  # w spread evenly
def scale(w: float, prods: list) -> list: return [(w * x, e) for x, e in prods]


def grammar(p: Problem) -> dict:
    ins = {f"in{k}": t for k, t in enumerate(p.inputs)}
    temps = [("i", "Int"), ("b", "Bool")] + ([("s", "String")] if p.str_vars else []) \
        + ([("li", "IntList")] if p.list_vars else [])
    locals_ = [(f"{c}{k}", t) for c, t in temps for k in range(3)]
    locals_ += [("res0", p.output)] + ([("res1", "Int")] if "res1" in p.int_vars else []) + [(LOOP_COUNTER, "Int")]
    ltype = dict(locals_)
    O = p.output

    def ctl(c: str, *args: dict, **kw: Any) -> dict: return {"control": c, "type": O, "args": list(args), **kw}
    def read(v: str) -> dict: return {"input": v} if v in ins else {"local": v}
    def reads(vs: tuple[str, ...]) -> list: return [read(v) for v in vs]

    def assign(v: str, e: dict, t: str | None = None) -> dict:
        t = t or ltype.get(v) or ins[v]
        return ctl(f"assign({t})->Statement", e, **({"input_name": v} if v in ins else {"name": v}))

    def nt(i: str, t: str, prods: list, **kw: Any) -> dict:
        prods = [(w, e) for w, e in prods if w > 0]
        total = sum(w for w, _ in prods)
        return {"id": i, "type": t, "scope": [], **kw,
                "alternatives": [{"id": f"a{n}", "weight": w / total, "expression": e} for n, (w, e) in enumerate(prods)]}

    I, B, S, L, C = "G.Int", "G.Bool", "G.String", "G.IntList", "G.Code"
    nts = []
    # <int>: one production each, and numbers have 1, 2, 3, 4 or 5 digits.
    number = [(1 / 2, irange(0, 9)), (1 / 4, irange(10, 99)), (1 / 8, irange(100, 999)), (1 / 16, irange(1000, 9999)),
              (1 / 16, irange(10000, 99999))]
    ints = split(1, reads(p.int_vars)) + number + [(1, sig("neg(Int)->Int", ref(I)))] + [
        *split(1, [sig(f"{o}(Int,Int)->Int", ref(I), ref(I)) for o in ("add", "sub", "mul")]),
        *split(1, [sig(f"{o}(Int,Int)->Int", ref(I), ref(I)) for o in ("idiv0", "imod0")]),
        (1, sig("min(Int,Int)->Int", ref(I), ref(I))), (1, sig("max(Int,Int)->Int", ref(I), ref(I))),
        (1, sig("abs(Int)->Int", ref(I)))]
    if p.list_vars:  # getIndexIntList, len
        ints += [(1, sig("index_mod(IntList,Int)->Int", ref(L), ref(I))), (1, sig("len(IntList)->Int", ref(L)))]
    if p.str_vars:   # len, saveOrd
        first_or_space = sig("index_mod(String,Int)->String", sig("concat(String,String)->String", ref(S),
                                                                  const("String", [" "])), irange(0, 0))
        ints += [(1, sig("len(String)->Int", ref(S))),
                 (1, sig("ord(Char)->Int", sig("string_to_char(String)->Char", first_or_space)))]
    nts.append(nt(I, "Int", ints))
    # <bool>
    bools = split(1, [{"local": v} for v in ("b0", "b1", "b2")]) + [
        (1, const("Bool", [True, False])), (1, sig("not(Bool)->Bool", ref(B))),
        *split(1, [sig(f"{o}(Bool,Bool)->Bool", ref(B), ref(B)) for o in ("and", "or")]),
        *split(1, [sig(f"{o}(Int,Int)->Bool", ref(I), ref(I)) for o in ("lt", "gt", "eq", "ge", "le", "ne")])]
    if p.list_vars:  # <int> in / not in <list_int>, <list_int> ==, != <list_int>
        in_list = sig("contains(IntList,Int)->Bool", ref(L), ref(I))
        bools += split(1, [in_list, sig("not(Bool)->Bool", in_list)])
        bools += split(1, [sig(f"{o}(IntList,IntList)->Bool", ref(L), ref(L)) for o in ("eq", "ne")])
    if p.str_vars:   # in, not in, ==, !=, startswith, endswith
        in_string = sig("contains(String,String)->Bool", ref(S), ref(S))
        bools += [(1, in_string), (1, sig("not(Bool)->Bool", in_string)),
                  (1, sig("eq(String,String)->Bool", ref(S), ref(S))), (1, sig("ne(String,String)->Bool", ref(S), ref(S))),
                  (1, sig("startswith(String,String)->Bool", ref(S), ref(S))),
                  (1, sig("endswith(String,String)->Bool", ref(S), ref(S)))]
    nts.append(nt(B, "Bool", bools))
    # <list_int> ::= <list_int_var> | list(saveRange(<int>,<int>)) | <list_int_slice>
    if p.list_vars:
        nts.append(nt(L, "IntList", split(1, reads(p.list_vars)) + [(1, sig("range(Int,Int)->IntList", ref(I), ref(I)))]
                      + split(1, [sig("slice(IntList,Int,Int)->IntList", ref(L), ref(I), ref(I)),
                                  sig("slice(IntList,Int,Int)->IntList", ref(L), irange(0, 0), ref(I)),
                                  sig("slice(IntList,Int,Int)->IntList", ref(L), ref(I), const("Int", [SLICE_END]))])))
    # <string>
    if p.str_vars:
        consts = [(1, const("String", CHARS))] + [(1, const("String", [c])) for c in p.str_consts]
        total = sum(w for w, _ in consts)
        ending = lambda name: sig(f"{name}(String)->String", ref(S))
        with_chars = lambda name: sig(f"{name}(String,String)->String", ref(S), ref(S))
        nts.append(nt(S, "String", split(1, reads(p.str_vars)) + [(w / total, e) for w, e in consts] + split(1, [
            sig("slice(String,Int,Int)->String", ref(S), ref(I), ref(I)),
            sig("slice(String,Int,Int)->String", ref(S), irange(0, 0), ref(I)),
            sig("slice(String,Int,Int)->String", ref(S), ref(I), const("Int", [SLICE_END]))]) + [
            (1, sig("index_mod(String,Int)->String", ref(S), ref(I))),
            (1, sig("char_to_string(Char)->String", sig("chr(Int)->Char", sig(
                "imod0(Int,Int)->Int", ref(I), const("Int", [str(CHAR_MODULUS)]))))),
            (1, sig("concat(String,String)->String", ref(S), ref(S))),
            (1, ending("lstrip")), (1, ending("rstrip")), (1, ending("strip")),
            (1, with_chars("lstrip")), (1, with_chars("rstrip")), (1, with_chars("strip")),
            (1, ending("capitalize"))]))
    # Statements, flattened: <statement> ::= <simple_stmt> | <compound_stmt> (list problems)
    # or <assign> | <compound_stmt>, <compound_stmt> ::= <for> | <if> | <while> (list
    # problems) or <if> | <while> (the others).
    nil = ctl("block_nil()->Block")
    int_assign = split(1 / 2, [assign(v, ref(I)) for v in p.int_vars]) + split(1 / 2, [
        assign(v, sig(f"{o}(Int,Int)->Int", read(v), ref(I))) for v in p.int_vars for o in ("add", "sub", "mul")])
    groups = [split(1, [assign(v, ref(B)) for v in ("b0", "b1", "b2")]), int_assign]
    if p.list_vars: groups.append(split(1, [assign(v, ref(L)) for v in p.list_vars]))
    if p.str_vars: groups.append(split(1, [assign(v, ref(S)) for v in p.str_vars]))
    assigns = [(w / len(groups), e) for group in groups for w, e in group]
    ifs = [(1 / 2, ctl("if_stmt(Bool,Block,Block)->Statement", ref(B), ref(C), nil)),
           (1 / 2, ctl("if_stmt(Bool,Block,Block)->Statement", ref(B), ref(C), ref(C)))]
    whiles = [(1, ctl("while_stmt(Int,Bool,Block)->Statement", const("Int", [str(LOOP_BOUND)]), ref(B), ref(C)))]
    if p.list_vars:
        each = [ctl("for_range(Int,Block)->Statement", sig("len(IntList)->Int", read(lv)),
                    ctl("block_cons(Statement,Block)->Block",
                        assign(v, sig("index_mod(IntList,Int)->Int", read(lv), {"local": LOOP_COUNTER}), "Int"), ref(C)),
                    name=LOOP_COUNTER) for v in p.int_vars for lv in p.list_vars]
        fors = [(1 / 2, ctl("for_range(Int,Block)->Statement", sig("len(IntList)->Int", ref(L)), ref(C),
                            name=LOOP_COUNTER))] + split(1 / 2, each)
        # <call>: four productions of 1/4. The two that take a <list_int> edit a variable only
        # when it is a list variable (1/3 of <list_int>). A slice or a range is a temporary.
        nothing = assign("i0", read("i0"))
        def on_variable(make: Any) -> list:
            return split(1 / 3, [assign(lv, make(read(lv))) for lv in p.list_vars]) + [(2 / 3, nothing)]
        calls = scale(1 / 4, split(1, [assign(lv, sig("append(IntList,Int)->IntList", read(lv), ref(I)))
                                      for lv in p.list_vars])) \
            + scale(1 / 4, split(1, [assign(lv, sig("insert(IntList,Int,Int)->IntList", read(lv), ref(I), ref(I)))
                                    for lv in p.list_vars])) \
            + scale(1 / 4, on_variable(lambda x: sig("remove_at(IntList,Int)->IntList", x, ref(I)))) \
            + scale(1 / 4, on_variable(lambda x: sig("set_at(IntList,Int,Int)->IntList", x, ref(I), ref(I))))
        simple = scale(1 / 2, calls) + scale(1 / 2, assigns)
        compound = scale(1 / 3, fors) + scale(1 / 3, ifs) + scale(1 / 3, whiles)
    else:
        simple, compound = assigns, scale(1 / 2, ifs) + scale(1 / 2, whiles)
    stmts = scale(1 / 2, simple) + scale(1 / 2, compound)
    nts.append(nt("G.Stmt", O, stmts, category="Statement"))
    nts.append(nt(C, O, [(1, ctl("block_cons(Statement,Block)->Block", ref("G.Stmt"), ref(C))),
                         (1, ctl("block_cons(Statement,Block)->Block", ref("G.Stmt"), nil))], category="Block"))
    nts.append(nt("G.Main", O, [
        (1, ctl("block_cons(Statement,Block)->Block", ref("G.Stmt"), ref("G.Main"))),
        (1, ctl("block_cons(Statement,Block)->Block", ref("G.Stmt"),
                ctl("block_cons(Statement,Block)->Block", ctl(f"return({O})->Statement", {"local": "res0"}), nil)))],
        category="Block"))
    body, fixed = ref("G.Main"), 1
    for v, t in reversed(locals_):
        body = ctl("block_cons(Statement,Block)->Block", assign(v, const(t, [ZERO[t]]), t), body)
        fixed += 3
    nts.insert(0, nt("G.Program", O, [(1, ctl("program(Block)->Program", body))], category="Program", variation=False))
    return {"format_version": "grammar-definition-v2",
            "entry": {"category": "Program", "nonterminal": "G.Program", "type": O},
            "inputs": [{"name": n, "type": t} for n, t in ins.items()],
            "locals": [{"name": v, "type": t} for v, t in locals_],
            "search_limits": {"max_nodes": fixed + 3 + BODY_NODES, "max_depth": MAX_DEPTH},
            "execution_limits": {"fuel": FUEL}, "nonterminals": nts}


def search(p: Problem) -> dict:
    return {"format": "gagp-search-config-v1", "population_size": POPULATION, "generations": p.generations,
            "selection": {"method": "tournament", "size": TOURNAMENT}, "elitism": ELITISM,
            "crossover": {"probability": CROSSOVER}, "mutation": {"probability": MUTATION},
            "limits": {"site_depth": 0}, "schedule": {"mode": "generational"}}


def task(p: Problem) -> dict:
    return {"format": "gagp-task-v1", "name": p.key, "grammar": "grammar.json", "train": "train.cases.json",
            "test": "test.cases.json", "fitness": {**p.fitness, "list_allocation_limit": LIST_ALLOCATION_LIMIT,
                                                     "case_cap": CASE_CAP}}


def dump(document: dict, indent: int) -> str:
    return json.dumps(document, indent=indent) + "\n"


def committed_files() -> dict[str, str]:
    """Relative path -> text of every committed file."""
    files: dict[str, str] = {}
    for p in PROBLEMS:
        files[f"{p.key}/grammar.json"] = dump(grammar(p), 1)
        files[f"{p.key}/search.json"] = dump(search(p), 1)
        files[f"{p.key}/task.json"] = dump(task(p), 2)
    return files


def locate_data(data: Path) -> Path:
    for candidate in (data / "datasets" / "progsys", data):
        if (candidate / PROBLEMS[0].pony / "Train.txt").exists():
            return candidate
    raise SystemExit(f"no {PROBLEMS[0].pony}/Train.txt under {data}. Pass a PonyGE2 checkout or its datasets/progsys")


def load_split(directory: Path, split_name: str) -> tuple[list, list]:
    text = (directory / f"{split_name}.txt").read_text()
    inputs, _, outputs = text.partition("outval = ")
    return ast.literal_eval(inputs.split("inval = ", 1)[1].strip()), ast.literal_eval(outputs.strip())


def case_file(p: Problem, rows: list, outputs: list) -> str:
    """Each row lists the inputs and each output row lists the outputs. The first output is the answer."""
    cases = []
    for row, output in zip(rows, outputs, strict=True):
        if len(row) != len(p.inputs):
            raise SystemExit(f"{p.key}: a case has {len(row)} inputs, expected {len(p.inputs)}")
        cases.append({"inputs": {f"in{k}": {"type": CASE_TYPE[t], "value": v}
                                 for k, (t, v) in enumerate(zip(p.inputs, row))},
                      "expected": {"type": CASE_TYPE[p.output], "value": output[0]}})
    return json.dumps({"format_version": "fitness-cases", "cases": cases})


def command_files(args: argparse.Namespace) -> int:
    files = committed_files()
    if args.check:
        stale = [name for name, text in files.items()
                 if not (args.out / name).exists() or (args.out / name).read_text() != text]
        if stale:
            print("stale, run `build.py files`:", *stale, sep="\n  ")
            return 1
        print(f"{len(files)} committed files are current")
        return 0
    for name, text in files.items():
        (args.out / name).parent.mkdir(parents=True, exist_ok=True)
        (args.out / name).write_text(text)
    print(f"wrote {len(files)} files under {args.out}")
    return 0


def command_cases(args: argparse.Namespace) -> int:
    data = locate_data(args.data)
    for p in PROBLEMS:
        (args.out / p.key).mkdir(parents=True, exist_ok=True)
        counts = []
        for split_name, target in (("Train", "train"), ("Test", "test")):
            rows, outputs = load_split(data / p.pony, split_name)
            (args.out / p.key / f"{target}.cases.json").write_text(case_file(p, rows, outputs))
            counts.append(len(rows))
        print(f"{p.key}: {counts[0]} train, {counts[1]} test cases")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    files = commands.add_parser("files", help="write or check the committed grammar, task and search files")
    files.add_argument("--out", type=Path, default=SUITE_DIR)
    files.add_argument("--check", action="store_true", help="compare instead of writing")
    cases = commands.add_parser("cases", help="convert the PonyGE2 case files (not committed)")
    cases.add_argument("--data", type=Path, required=True, help="a PonyGE2 checkout or its datasets/progsys")
    cases.add_argument("--out", type=Path, default=SUITE_DIR, help="suite directory (default: next to this script)")
    args = parser.parse_args(argv)
    return command_files(args) if args.command == "files" else command_cases(args)


if __name__ == "__main__":
    sys.exit(main())
