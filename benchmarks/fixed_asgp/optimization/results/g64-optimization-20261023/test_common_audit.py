import copy
import unittest

import common_audit


class TestCommonChampionAudit(unittest.TestCase):
    def fixture(self, backend="gagp_cpu_1t"):
        score = dict(backend=backend, cases=2, correct_count=1, exact=1,
                     wrong=1, runtime_errors=1, exact_accuracy=0.5,
                     case_results=[dict(case=0, exact=1, runtime_error=0),
                                   dict(case=1, exact=0, runtime_error=1)])
        return dict(task="fixture", cases=2, cases_sha256="0" * 64,
                    history=[copy.deepcopy(score) for _ in range(64)], final=score)

    def test_asgp_and_gagp_have_explicit_distinct_cpu_backends(self):
        for backend in ("gagp_cpu_1t", "asgp_1t"):
            doc = self.fixture(backend)
            common_audit.validate(doc, 2, "fixture", 64, backend)
            other = "asgp_1t" if backend == "gagp_cpu_1t" else "gagp_cpu_1t"
            with self.assertRaises(AssertionError):
                common_audit.validate(doc, 2, "fixture", 64, other)

    def test_final_and_each_generation_require_full_case_accounting(self):
        for index in (None, 0, 63):
            for field, value in (("wrong", 0), ("correct_count", 2),
                                 ("runtime_errors", 0), ("exact_accuracy", 1)):
                doc = self.fixture()
                score = doc["final"] if index is None else doc["history"][index]
                score[field] = value
                with self.assertRaises(AssertionError):
                    common_audit.validate(doc, 2, "fixture", 64, "gagp_cpu_1t")

    def test_missing_generation_or_case_and_wrong_case_order_are_rejected(self):
        for damage in ("generation", "case", "order"):
            doc = self.fixture()
            if damage == "generation":
                doc["history"].pop()
            elif damage == "case":
                doc["final"]["case_results"].pop()
            else:
                doc["history"][32]["case_results"].reverse()
            with self.assertRaises(AssertionError):
                common_audit.validate(doc, 2, "fixture", 64, "gagp_cpu_1t")


if __name__ == "__main__":
    unittest.main()
