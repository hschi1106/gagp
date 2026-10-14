from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gagp_tools.experiments.fixed_asgp.list_grammar import negative_to_zero_definition
from gagp_tools.experiments.fixed_asgp.negative_to_zero_preflight import ROOT, evidence


class TestFixedAsgpList(unittest.TestCase):
    def test_negative_language_keeps_typed_recursive_boundaries(self):
        document = negative_to_zero_definition(ROOT)
        nts = {nt["id"]: nt for nt in document["nonterminals"]}
        self.assertEqual(document["entry"]["type"], "IntList")
        root = nts["Root"]["alternatives"][0]["expression"]
        self.assertEqual(root["holes"]["solve"], {"ref": "Solve.IntList"})
        self.assertEqual(root["holes"]["divide"], {"ref": "Divide.Int"})
        self.assertEqual(root["holes"]["combine"], {"ref": "Combine.IntList"})
        self.assertEqual(document["templates"][0]["body"]["structured"]["plan"]["result_type"], "IntList")
        for phase in ("Solve", "Divide", "Combine"):
            ints = nts[f"{phase}.Int"]["alternatives"]
            domains = [row["expression"]["constant"] for row in ints if "constant" in row["expression"]]
            self.assertEqual(domains, [{"type": "Int", "values": ["0"]}])
        solve = [a["expression"] for a in nts["Solve.IntList"]["alternatives"]]
        self.assertIn({"constant": {"type": "IntList", "values": [[]]}}, solve)
        combine = [a["expression"] for a in nts["Combine.IntList"]["alternatives"]]
        self.assertEqual([e.get("signature") for e in combine if "signature" in e],
                         ["concat(IntList,IntList)->IntList"])
        self.assertEqual([s["type"] for s in nts["Combine.IntList"]["scope"]], ["IntList", "IntList"])
        self.assertNotIn('"bound": "left"', json.dumps(nts["Combine.Int"]["alternatives"]))
        heads = [a["expression"] for a in nts["Solve.Int"]["alternatives"]
                 if a["expression"].get("signature") == "index(IntList,Int)->Int"]
        self.assertIn({"ref": "Solve.IntList"}, [e["args"][0] for e in heads])

    def test_evidence_rejects_source_drift(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root / "source.cc"
            path.write_text("line one\ncontract anchor\n")
            before = evidence(root, "source.cc", "contract anchor")
            self.assertEqual(before["anchors"][0]["line"], 2)
            path.write_text("new contract\n")
            with self.assertRaisesRegex(ValueError, "capability evidence changed"):
                evidence(root, "source.cc", "contract anchor")


if __name__ == "__main__":
    unittest.main()
