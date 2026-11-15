"""Checks of the psb1_g3p benchmark: committed files, probabilities of the ported grammars, case conversion."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SUITE = ROOT / "benchmarks" / "psb1_g3p"
spec = importlib.util.spec_from_file_location("psb1_g3p_build", SUITE / "build.py")
build = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = build
spec.loader.exec_module(build)

LIST_PROBLEMS = ("count_odds", "negative_to_zero")


def load(problem: str, name: str = "grammar") -> dict:
    return json.loads((SUITE / problem / f"{name}.json").read_text())


def alternatives(grammar: dict, nonterminal: str) -> list[dict]:
    return next(n for n in grammar["nonterminals"] if n["id"] == nonterminal)["alternatives"]


def signatures(expression: object) -> set[str]:
    """Every signature or control name below an expression."""
    found: set[str] = set()
    if isinstance(expression, dict):
        for key in ("signature", "control"):
            if key in expression:
                found.add(expression[key])
        for value in expression.values():
            found |= signatures(value)
    elif isinstance(expression, list):
        for value in expression:
            found |= signatures(value)
    return found


def top(expression: dict) -> str:
    return expression.get("signature") or expression.get("control") or next(iter(expression))


def function(expression: dict) -> str:
    """The builtin name of an expression, or `constant`, `variable` for leaves."""
    kind = top(expression)
    if kind == "constant":
        return "constant"
    return kind.split("(")[0] if "(" in kind else "variable"


def shares(grammar: dict, nonterminal: str, classify) -> dict[str, float]:
    """Probability of each class of production, by the classifier."""
    total: dict[str, float] = defaultdict(float)
    for alternative in alternatives(grammar, nonterminal):
        total[classify(alternative["expression"])] += alternative["weight"]
    return total


def statement_kind(expression: dict) -> str:
    kind = top(expression)
    if kind.startswith("if_stmt"):
        return "if"
    if kind.startswith("for_range"):
        return "for"
    assert kind.startswith("assign"), kind
    argument = expression["args"][0]
    if argument.get("local") == expression.get("name") and expression.get("name") == "i0":
        return "nothing"
    return {"append": "append", "insert": "insert", "remove_at": "remove_at", "set_at": "set_at"}.get(
        top(argument).split("(")[0], "assign")


class CommittedFilesTest(unittest.TestCase):
    def test_committed_files_equal_the_generator_output(self) -> None:
        expected = build.committed_files()
        self.assertEqual(len(expected), 3 * len(build.PROBLEMS))
        stale = [name for name, text in expected.items() if (SUITE / name).read_text() != text]
        self.assertEqual([], stale, "run `python3 benchmarks/psb1_g3p/build.py files`")
        on_disk = {str(path.relative_to(SUITE)) for path in SUITE.glob("*/*.json")
                   if not path.name.endswith(".cases.json")}
        self.assertEqual(set(expected), on_disk)

    def test_check_mode_reports_a_stale_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(1, build.main(["files", "--check", "--out", tmp]))
                self.assertEqual(0, build.main(["files", "--out", tmp]))
                self.assertEqual(0, build.main(["files", "--check", "--out", tmp]))
                (out / "median" / "search.json").write_text("{}")
                self.assertEqual(1, build.main(["files", "--check", "--out", tmp]))

    def test_search_files_follow_thesis_table_4_1(self) -> None:
        for problem in build.PROBLEMS:
            config = load(problem.key, "search")
            self.assertNotIn("representation", config)
            self.assertEqual(config["format"], "gagp-search-config-v1")
            self.assertEqual(config["generations"], 200 if problem.key in ("smallest", "median") else 300)
            self.assertEqual((config["population_size"], config["selection"], config["elitism"]),
                             (1000, {"method": "tournament", "size": 7}, 1))
            self.assertEqual((config["crossover"], config["mutation"], config["schedule"]),
                             ({"probability": 0.9}, {"probability": 0.05}, {"mode": "generational"}))

    def test_tasks_point_at_their_files_and_fitness(self) -> None:
        metrics = {"smallest": "exact", "median": "exact", "count_odds": "absolute", "small_or_large": "levenshtein",
                   "replace_space_with_newline": "levenshtein", "negative_to_zero": "levenshtein"}
        for problem, metric in metrics.items():
            document = load(problem, "task")
            self.assertEqual((document["grammar"], document["train"], document["test"]),
                             ("grammar.json", "train.cases.json", "test.cases.json"))
            self.assertEqual(document["fitness"]["metric"], metric)
        for problem in build.PROBLEMS:
            limit = load(problem.key, "task")["fitness"].get("list_allocation_limit")
            self.assertEqual(limit, 65536 if problem.list_vars else None, problem.key)

    def test_named_constants_reach_the_grammars(self) -> None:
        for problem in build.PROBLEMS:
            grammar = load(problem.key)
            self.assertEqual(grammar["execution_limits"], {"fuel": build.FUEL})
            self.assertEqual(grammar["search_limits"]["max_depth"], build.MAX_DEPTH)
            self.assertEqual(grammar["search_limits"]["max_nodes"] - build.BODY_NODES,
                             1 + 3 * len(grammar["locals"]) + 3)

    def test_literature_covers_the_suite(self) -> None:
        literature = json.loads((SUITE / "literature.json").read_text())
        self.assertEqual(set(literature["solved_of_runs"]), {p.key for p in build.PROBLEMS})
        ids = {source["id"] for source in literature["sources"]}
        for counts in literature["solved_of_runs"].values():
            self.assertEqual(set(counts), ids)
        self.assertTrue(all(source["citation"] and source["runs"] == 100 for source in literature["sources"]))


class ProbabilityTest(unittest.TestCase):
    """Every production of the original grammar keeps its probability at its nonterminal."""

    def test_weights_sum_to_one(self) -> None:
        for problem in build.PROBLEMS:
            for nonterminal in load(problem.key)["nonterminals"]:
                self.assertAlmostEqual(1.0, sum(a["weight"] for a in nonterminal["alternatives"]), places=12,
                                       msg=f"{problem.key} {nonterminal['id']}")

    def test_while_loops_are_the_only_removed_statement(self) -> None:
        for problem in build.PROBLEMS:
            grammar = load(problem.key)
            text = json.dumps(grammar)
            self.assertNotIn("while", text)
            kinds = shares(grammar, "G.Stmt", statement_kind)
            if problem.key in LIST_PROBLEMS:
                # <statement>: simple 1/2 (call 1/2: append, insert, deleteListItem, setListIndexTo; assign 1/2),
                # compound 1/2 (for and if, which share the removed while third).
                expected = {"append": 1 / 16, "insert": 1 / 16, "assign": 1 / 4, "for": 1 / 4, "if": 1 / 4,
                            # deleteListItem and setListIndexTo: 1/3 edit a list variable, 2/3 a temporary
                            "remove_at": 1 / 48, "set_at": 1 / 48, "nothing": 2 * 2 / 3 / 16}
            else:
                expected = {"assign": 1 / 2, "if": 1 / 2}
            self.assertEqual(sorted(expected), sorted(kinds), problem.key)
            for kind, share in expected.items():
                self.assertAlmostEqual(share, kinds[kind], places=12, msg=f"{problem.key} {kind}")

    def test_int_productions_are_uniform(self) -> None:
        for problem in build.PROBLEMS:
            kinds = shares(load(problem.key), "G.Int", function)
            # <int> has eight productions, plus two for list problems (getIndexIntList, len)
            # or two for string problems (len, saveOrd).
            n = 8 + 2 * bool(problem.list_vars) + 2 * bool(problem.str_vars)
            expected = {"variable": 1, "constant": 1, "neg": 1, "min": 1, "max": 1, "abs": 1}
            for name, count in expected.items():
                self.assertAlmostEqual(count / n, kinds[name], places=12, msg=f"{problem.key} {name}")
            self.assertAlmostEqual(1 / n, kinds["idiv0"] + kinds["imod0"], places=12)
            self.assertAlmostEqual(1 / n, kinds["add"] + kinds["sub"] + kinds["mul"], places=12)
            self.assertAlmostEqual(1 / n * (bool(problem.list_vars) + bool(problem.str_vars)), kinds["len"], places=12)
            self.assertAlmostEqual(1 / n * bool(problem.list_vars), kinds.get("index_mod", 0), places=12)
            self.assertAlmostEqual(1 / n * bool(problem.str_vars), kinds.get("ord", 0), places=12)

    def test_restored_list_productions(self) -> None:
        for problem in LIST_PROBLEMS:
            grammar = load(problem)

            def classify(expression: dict) -> str:
                if top(expression) == "not(Bool)->Bool" and signatures(expression["args"][0]):
                    return "not_" + function(expression["args"][0])
                return function(expression)
            bools = shares(grammar, "G.Bool", classify)
            # <bool> has seven productions: bool_var, bool_const, not, and/or, comparison,
            # `in`/`not in` (half each) and ==/!=.
            self.assertAlmostEqual(1 / 14, bools["contains"], places=12)
            self.assertAlmostEqual(1 / 14, bools["not_contains"], places=12)
            self.assertAlmostEqual(1 / 7, bools["not"], places=12)
            by_signature = shares(grammar, "G.Bool", top)
            self.assertAlmostEqual(1 / 7, by_signature["eq(IntList,IntList)->Bool"]
                                   + by_signature["ne(IntList,IntList)->Bool"], places=12)
            # <list_int> has three productions: variable, list(saveRange(..)), slice.
            lists = shares(grammar, "G.IntList", function)
            for name in ("variable", "range", "slice"):
                self.assertAlmostEqual(1 / 3, lists[name], places=12, msg=f"{problem} {name}")

    def test_restored_string_productions(self) -> None:
        for problem in ("small_or_large", "replace_space_with_newline"):
            grammar = load(problem)
            strings = shares(grammar, "G.String", top)
            # <string> has thirteen productions.
            for name in ("capitalize(String)->String", "concat(String,String)->String",
                         "index_mod(String,Int)->String", "strip(String)->String", "lstrip(String)->String",
                         "rstrip(String)->String", "strip(String,String)->String", "lstrip(String,String)->String",
                         "rstrip(String,String)->String"):
                self.assertAlmostEqual(1 / 13, strings[name], places=12, msg=f"{problem} {name}")
            self.assertAlmostEqual(1 / 13, strings["char_to_string(Char)->String"], places=12)
            # <bool> has eleven productions.
            bools = shares(grammar, "G.Bool", top)
            for name in ("startswith(String,String)->Bool", "endswith(String,String)->Bool",
                         "eq(String,String)->Bool", "ne(String,String)->Bool", "contains(String,String)->Bool"):
                self.assertAlmostEqual(1 / 11, bools[name], places=12, msg=f"{problem} {name}")

    def test_group_membership_of_restored_builtins(self) -> None:
        wanted = {"count_odds": {"contains(IntList,Int)->Bool", "insert(IntList,Int,Int)->IntList",
                                 "remove_at(IntList,Int)->IntList", "set_at(IntList,Int,Int)->IntList",
                                 "index_mod(IntList,Int)->Int", "range(Int,Int)->IntList"},
                  "small_or_large": {"strip(String)->String", "lstrip(String)->String", "rstrip(String)->String",
                                     "strip(String,String)->String", "lstrip(String,String)->String",
                                     "rstrip(String,String)->String", "capitalize(String)->String",
                                     "startswith(String,String)->Bool", "endswith(String,String)->Bool",
                                     "index_mod(String,Int)->String"}}
        for problem, builtins in wanted.items():
            self.assertLessEqual(builtins, signatures(load(problem)), problem)


class CaseConversionTest(unittest.TestCase):
    def test_case_file_types_each_input_and_takes_the_first_output(self) -> None:
        problem = build.BY_KEY["replace_space_with_newline"]
        text = build.case_file(problem, [["a b"], [""]], [["a\nb", 2], ["", 0]])
        cases = json.loads(text)["cases"]
        self.assertEqual(cases[0], {"inputs": {"in0": {"type": "string", "value": "a b"}},
                                    "expected": {"type": "string", "value": "a\nb"}})
        self.assertEqual(cases[1]["expected"]["value"], "")
        median = build.BY_KEY["median"]
        self.assertEqual(json.loads(build.case_file(median, [[1, 2, 3]], [[2]]))["cases"][0]["inputs"]["in2"],
                         {"type": "int", "value": 3})
        with self.assertRaises(SystemExit):
            build.case_file(median, [[1, 2]], [[2]])

    def test_cases_command_reads_ponyge2_files(self) -> None:
        sample = {"Int": 7, "String": "a b", "IntList": [3, -4]}
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "PonyGE2" / "datasets" / "progsys"
            for problem in build.PROBLEMS:
                (data / problem.pony).mkdir(parents=True)
                inputs = [[sample[t] for t in problem.inputs]] * 2
                for split_name in ("Train", "Test"):
                    (data / problem.pony / f"{split_name}.txt").write_text(
                        f"inval = {inputs}\noutval = {[[sample[problem.output]]] * 2}\n")
            out = Path(tmp) / "suite"
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(0, build.main(["cases", "--data", str(Path(tmp) / "PonyGE2"), "--out", str(out)]))
            for problem in build.PROBLEMS:
                for name in ("train", "test"):
                    cases = json.loads((out / problem.key / f"{name}.cases.json").read_text())["cases"]
                    self.assertEqual(len(cases), 2)
            with self.assertRaises(SystemExit):
                build.locate_data(Path(tmp) / "nowhere")


if __name__ == "__main__":
    unittest.main()
