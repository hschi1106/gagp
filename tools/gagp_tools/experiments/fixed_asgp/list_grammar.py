"""NegativeToZero's original ASGP list language, for the generic native backend."""
from __future__ import annotations

import copy
import json
from pathlib import Path

from .grammar import bound, call, constant, definition


def negative_to_zero_definition(root: Path) -> dict:
    result = definition(root, "sum_of_elements")
    package = json.loads((root / "configs/grammar/packages/dc_intlist_int.json").read_text())
    template = copy.deepcopy(next(t for t in package["templates"]
                                  if t["id"] == "Package.DC.IntList.IntList"))
    template["body"]["structured"]["plan"]["limits"]["frames"] = 50
    result["templates"] = [template]
    result["entry"]["type"] = "IntList"
    root_nt = result["nonterminals"][0]
    root_nt["type"] = "IntList"
    expression = root_nt["alternatives"][0]["expression"]
    expression["template"] = template["id"]
    for phase in ("solve", "combine"):
        expression["holes"][phase] = {"ref": f"{phase.title()}.IntList"}
    for nt in result["nonterminals"][1:]:
        phase = nt["id"].split(".")[0]
        if phase == "Combine":
            for slot in nt["scope"]:
                slot["type"] = "IntList"
        alternatives = nt["alternatives"]
        if nt["type"] == "Int":
            alternatives[0]["expression"] = constant(0)
            if phase == "Combine":
                nt["alternatives"] = [a for a in alternatives if "bound" not in a["expression"]]
            if phase == "Solve":
                alternatives[2]["expression"] = call("index(IntList,Int)->Int",
                                                     {"ref": "Solve.IntList"}, constant(0))
    for phase in ("Solve", "Combine"):
        scope = next(nt["scope"] for nt in result["nonterminals"] if nt["id"] == f"{phase}.Int")
        ref = {"ref": f"{phase}.IntList"}
        if phase == "Solve":
            expressions = [bound("xs"), constant([], "IntList"),
                           call("singleton(Int)->IntList", {"ref": "Solve.Int"}),
                           call("if(Bool,IntList,IntList)->IntList", {"ref": "Solve.Bool"}, ref, ref)]
        else:
            expressions = [bound("left"), bound("right"), call("concat(IntList,IntList)->IntList", ref, ref)]
        result["nonterminals"].append({
            "id": f"{phase}.IntList", "type": "IntList", "scope": copy.deepcopy(scope),
            "alternatives": [{"id": f"p{i}", "weight": 4 if "bound" in expr or "constant" in expr else 1,
                              "expression": expr} for i, expr in enumerate(expressions)],
        })
    return result
