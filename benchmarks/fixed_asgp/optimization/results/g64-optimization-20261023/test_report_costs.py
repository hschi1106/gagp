import math
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import report_costs
import serial


class TestExplicitAuditCosts(unittest.TestCase):
    def test_milliseconds_and_zero_are_recorded_but_missing_asgp_cost_is_unknown(self):
        data = dict(cpu_audit_ms=125, cpu_audit_program_count=16)
        self.assertEqual(report_costs.post_search_audit(data, "candidate")["post_search_cpu_audit_s"], .125)
        zero = report_costs.post_search_audit(dict(data, cpu_audit_ms=0), "baseline")
        self.assertTrue(zero["post_search_cpu_audit_known"])
        self.assertEqual(zero["post_search_cpu_audit_s"], 0)
        self.assertIsNone(report_costs.post_search_audit({}, "asgp")["post_search_cpu_audit_s"])
        self.assertFalse(report_costs.post_search_audit({}, "candidate")["post_search_cpu_audit_known"])

    def test_invalid_times_cannot_become_zero_costs(self):
        for value in (-1, math.nan, math.inf, True):
            with self.assertRaises(ValueError):
                report_costs.post_search_audit(dict(cpu_audit_ms=value, cpu_audit_program_count=16), "candidate")

    def test_failed_and_interrupted_calls_stay_in_cost_denominator(self):
        stages = [dict(stage="native-stage", cohort="primary", variant="candidate", jobs={
            "a.native": dict(status="ok", process_s=1, started_epoch=1),
            "b.native": dict(status="timeout", process_s=4, started_epoch=2),
            "c.common": dict(status="budget_not_run"),
            "d.native": dict(status="interrupted", started_epoch=3)})]
        rows = report_costs.offline_rows(stages)
        result = report_costs.summarize({}, rows)
        self.assertEqual(result["offline_recorded_process_s"], 5)
        self.assertEqual(result["offline_failed_recorded_process_s"], 4)
        self.assertEqual(result["offline_unknown_started_costs"], 1)
        self.assertEqual(result["offline_recorded_calls"], 4)
        self.assertEqual([row["status"] for row in rows], ["ok", "timeout", "budget_not_run", "interrupted"])
        self.assertIsNone(rows[-1]["process_s"])

    def test_duplicate_calls_cannot_inflate_cost(self):
        stage = dict(stage="one", cohort="quality", variant="baseline", jobs={"x.native": dict(status="ok", process_s=1)})
        with self.assertRaises(ValueError):
            report_costs.offline_rows([stage, stage])

    def test_search_cohorts_are_added_once_and_asgp_is_not_a_cpu16_run(self):
        a = dict(variant="candidate", **report_costs.post_search_audit(dict(cpu_audit_ms=250, cpu_audit_program_count=16), "candidate"))
        unknown = dict(variant="baseline", **report_costs.post_search_audit({}, "baseline"))
        asgp = dict(variant="asgp", **report_costs.post_search_audit({}, "asgp"))
        result = report_costs.summarize(dict(primary=[a, asgp], quality=[unknown], secondary=[a]), [])
        self.assertEqual(result["post_search_cpu_audit_recorded_s"], .5)
        self.assertEqual(result["post_search_cpu_audit_known"], 2)
        self.assertEqual(result["post_search_cpu_audit_expected"], 3)

    def test_unstarted_quality_cohort_stays_missing_in_cost_provenance(self):
        reader = mock.Mock()
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(serial, "RAW", Path(folder)):
            rows, sources = report_costs.collect_offline("formal", dict(seeds=[35]), {}, {}, reader)
        self.assertEqual(rows, [])
        self.assertEqual(len(sources["missing_manifests"]), 8)
        self.assertTrue(any(path.endswith("formal-quality.s35-candidate-judges/manifest.json")
                            for path in sources["missing_manifests"]))
        reader.stage.assert_not_called()

    def test_corrections_are_read_from_output_and_missing_is_not_zero(self):
        search = dict(verification=dict(programs=5, fitness_corrections=2, wrong_corrections=3,
                                        best_changes=1, wall_ms=500))
        actual = dict(variant="candidate", **report_costs.post_search_audit(search, "candidate"))
        missing = dict(variant="baseline", **report_costs.post_search_audit({}, "baseline"))
        result = report_costs.summarize(dict(primary=[actual, missing]), [])
        self.assertEqual(result["in_search_cpu_verified_programs"], 5)
        self.assertEqual(result["in_search_cpu_fitness_corrections"], 2)
        self.assertEqual(result["in_search_cpu_wrong_corrections"], 3)
        self.assertEqual(result["in_search_cpu_best_changes"], 1)
        self.assertEqual(result["in_search_cpu_verification_s"], .5)
        self.assertEqual(result["in_search_cpu_verification_known"], 1)
        self.assertIsNone(report_costs.summarize(dict(primary=[missing]), [])["in_search_cpu_fitness_corrections"])


if __name__ == "__main__":
    unittest.main()
