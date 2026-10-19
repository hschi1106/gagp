import csv
import json
from pathlib import Path
import tempfile
from unittest import TestCase, main
from report import DASHBOARD_MAX_CHARS, dashboard_data, script_json, serial_intervals, totals_for, write_csv


class TestReportEvidence(TestCase):
    def test_dashboard_fits_tool_limit_without_losing_cells_quality_or_failure_statuses(self):
        summary = dict(protocol=dict(generations=8,repetitions=2,cases=1024),
            totals=dict(requested_runs=360,completed_runs=359),
            cells=[dict(task=f'task{i%30}',population=1024 if i<30 else 16384,
                        qualified=i!=59,asgp_vs_improved=12.43) for i in range(60)],
            populations=[dict(population=p,qualified_cells=30) for p in (1024,16384)],
            quality={'task0.p16384':{'asgp':dict(gen0_wrong=3,final_wrong=0)}},cost_note='CPU audit extra',
            runs=[dict(tag=str(i),status='budget_not_run' if i==359 else 'ok',
                       original_status='budget_not_run' if i==359 else 'ok',
                       full_provenance='x'*5000) for i in range(360)])
        data = dashboard_data(summary)
        self.assertEqual(data['cells'],summary['cells'])
        self.assertEqual(data['quality'],summary['quality'])
        self.assertEqual(len(data['runs']),360)
        self.assertEqual(data['runs'][-1]['status'],'budget_not_run')
        self.assertEqual(data['runs'][-1]['original_status'],'budget_not_run')
        self.assertEqual(len(summary['runs'][0]['full_provenance']),5000)
        template = Path(__file__).with_name('dashboard.template.html').read_text()
        html = template.replace('__DATA_JSON__',script_json(data))
        self.assertLessEqual(len(html),DASHBOARD_MAX_CHARS)

    def test_html_data_cannot_close_script_and_roundtrips(self):
        value = dict(task='</script><script>alert(1)</script>',text='行\u2028尾')
        encoded = script_json(value)
        self.assertNotIn('<',encoded)
        self.assertEqual(json.loads(encoded),value)

    def test_csv_keeps_failure_and_reason_without_replacing_missing_ratio(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'rows.csv'
            write_csv(path,[dict(status='budget_not_run',ratio=None,reasons=['deadline','CPU audit missing'])],('status',))
            with path.open() as stream:
                row = next(csv.DictReader(stream))
            self.assertEqual(row['status'],'budget_not_run')
            self.assertEqual(row['ratio'],'')
            self.assertEqual(json.loads(row['reasons']),['deadline','CPU audit missing'])
            with self.assertRaises(FileExistsError):
                write_csv(path,[])

    def test_process_completion_is_distinct_from_provenance_or_fidelity(self):
        jobs = [dict(original_status='ok',status='audit_invalid',process_s=2),
                dict(original_status='ok',status='ok',audit_ok=True,fidelity_ok=False,search_s=1,
                     process_s=2,cpu_audit_s=.5,cpu_audit_program_count=16,cpu_audit_wrong_program_count=16,
                     cpu_audit_fitness_mismatches=1,cpu_audit_wrong_mismatches=0),
                dict(original_status='missing',status='missing')]
        result = totals_for(jobs,[],[dict(qualified=False)],3)
        self.assertEqual(result['completed_runs'],2)
        self.assertEqual(result['audited_work_runs'],1)
        self.assertEqual(result['qualified_cells'],0)
        self.assertEqual(result['native_fidelity_mismatch_runs'],1)
        self.assertEqual(result['cpu_raw_fitness_mismatches'],1)

    def test_overlap_is_rejected_while_dead_child_heartbeat_is_labeled_lower_bound(self):
        a = dict(stage='search',jobs={'a':dict(pid=1,status='ok',started_epoch=10,process_s=2)})
        b = dict(stage='judge',jobs={'b':dict(pid=2,status='ok',started_epoch=11,process_s=1)})
        with self.assertRaises(AssertionError):
            serial_intervals(a,b)
        b['jobs']['b'].update(status='running',started_epoch=13)
        result = serial_intervals(a,b)
        self.assertEqual(result[-1]['end_scope'],'last heartbeat lower bound')


if __name__ == '__main__':
    main()
