"""Benchmark-only task grammars; execution semantics remain in native libraries."""
from __future__ import annotations

import copy
import json
from pathlib import Path

TASKS = ("sum_of_elements", "median", "house_robber")


def constant(value, kind="Int"):
    return {"constant": {"type": kind, "values": [str(value) if kind == "Int" else value]}}


def bound(name):
    return {"bound": name}


def call(signature, *args):
    return {"signature": signature, "args": list(args)}


def state_value():
    return call("index(IntList,Int)->Int", bound("xs"),
                call("max(Int,Int)->Int",
                     call("sub(Int,Int)->Int", bound("n"), constant(1)),
                     constant(0)))


def definition(root: Path, task: str) -> dict:
    if task not in TASKS:
        raise ValueError(task)
    dp = task == "house_robber"
    if dp:
        package = json.loads((root / "configs/grammar/packages/dp1d_int.json").read_text())
        body = copy.deepcopy(package["nonterminals"][0]["alternatives"][1]["expression"])
        plan = body["structured"]["plan"]
        plan["parameter_types"] = ["IntList"]
        plan["coordinate_domains"][0]["upper"]["literal"] = "20"
        plan["limits"].update(frames=21, cells=21)
        body["captures"] = [{"input": "source"}]
        binding = [{"bank": "state", "slot": 0, "name": "n"},
                   {"bank": "parameter", "slot": 0, "name": "xs"}]
        body["phases"][0]["bindings"] = binding[:1]
        body["phases"][1]["bindings"] = binding
        body["phases"][2]["bindings"] = binding + [
            {"bank": "result", "slot": 0, "name": "d1"},
            {"bank": "result", "slot": 1, "name": "d2"}]
        body["args"][0] = call("len(IntList)->Int", {"input": "source"})
        body["args"][1] = call("le(Int,Int)->Bool", bound("n"), constant(1))
        body["args"][2] = {"hole": "solve"}
        body["args"][3] = {"hole": "transition"}
        phases = [("Solve", [("n", "Int"), ("xs", "IntList")]),
                  ("Transition", [("n", "Int"), ("xs", "IntList"),
                                  ("d1", "Int"), ("d2", "Int")])]
        template = {"id": "Fixed.HouseRobber", "type": "Int", "scope": [],
                    "holes": [{"id": name.lower(), "type": "Int",
                               "scope": [{"name": n, "type": t} for n, t in scope]}
                              for name, scope in phases], "body": body}
    else:
        package = json.loads((root / "configs/grammar/packages/dc_intlist_int.json").read_text())
        template = copy.deepcopy(next(t for t in package["templates"]
                                      if t["id"] == "Package.DC.IntList.Int"))
        template["body"]["structured"]["plan"]["limits"]["frames"] = 50
        if task == "median":
            template["body"]["phases"][0]["bindings"] = [
                {"bank": "measure", "slot": 0, "name": "n"}]
            template["body"]["args"][2] = call("le(Int,Int)->Bool", bound("n"), constant(3))
        phases = [("Solve", [("xs", "IntList"), ("n", "Int"), ("lo", "Int")]),
                  ("Divide", [("n", "Int")]),
                  ("Combine", [("left", "Int"), ("right", "Int")])]
    low, high = (-20, 20) if task == "median" else (0, 20 if dp else 100)
    nts = []
    for phase, scope in phases:
        i, b = {"ref": f"{phase}.Int"}, {"ref": f"{phase}.Bool"}
        ints = [{"constant": {"type": "Int", "range": [str(low), str(high)]}}]
        if dp:
            ints += [bound("n"), state_value()]
            if phase == "Transition":
                ints += [bound("d1"), bound("d2")]
        elif phase == "Combine":
            ints += [bound("left"), bound("right")]
        else:
            ints += [bound("n")]
            if phase == "Solve":
                ints += [call("index(IntList,Int)->Int", bound("xs"), constant(0)),
                         call("index(IntList,Int)->Int", bound("xs"), i)]
        ints += [call(f"{op}(Int,Int)->Int", i, i) for op in ("add", "sub", "mul", "idiv0", "imod0")]
        ints += [call("if(Bool,Int,Int)->Int", b, i, i)]
        bools = [constant(False, "Bool"), constant(True, "Bool")]
        bools += [call(f"{op}(Int,Int)->Bool", i, i) for op in ("lt", "eq", "gt", "le", "ge", "ne")]
        bools += [call("if(Bool,Bool,Bool)->Bool", b, b, constant(False, "Bool")),
                  call("if(Bool,Bool,Bool)->Bool", b, constant(True, "Bool"), b),
                  call("not(Bool)->Bool", b)]
        for kind, expressions in (("Int", ints), ("Bool", bools)):
            nts.append({"id": f"{phase}.{kind}", "type": kind,
                        "scope": [{"name": n, "type": t} for n, t in scope],
                        "alternatives": [{"id": f"p{k}", "weight": 4 if "constant" in e or "bound" in e else 1,
                                          "expression": e} for k, e in enumerate(expressions)]})
    holes = {p.lower(): {"ref": f"{p}.Int"} for p, _ in phases}
    if not dp:
        holes["source"] = {"input": "source"}
    nts.insert(0, {"id": "Root", "type": "Int", "scope": [], "alternatives": [
        {"id": "scheme", "weight": 1, "expression": {"template": template["id"], "holes": holes}}]})
    return {"format_version": "grammar-definition-v2", "inputs": [{"name": "source", "type": "IntList"}],
            "entry": {"nonterminal": "Root", "type": "Int"},
            "search_limits": {"max_nodes": 1024, "max_depth": 48},
            "execution_limits": {"fuel": 2000000}, "templates": [template], "nonterminals": nts}
