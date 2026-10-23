import ast
from pathlib import Path
import unittest


class TestFormalSnapshotPolicy(unittest.TestCase):
    def test_search_and_native_coverage_body_are_unchanged(self):
        root = Path(__file__).resolve().parent
        def functions(name):
            text = (root / name).read_text()
            return {node.name: ast.dump(node) for node in ast.parse(text).body
                    if isinstance(node, ast.FunctionDef)}
        self.assertEqual(functions('formal_shared.py'), functions('formal_shared_policy.py'))
