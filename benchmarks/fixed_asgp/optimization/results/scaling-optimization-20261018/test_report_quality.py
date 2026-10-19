import json
from pathlib import Path
import tempfile
from unittest import TestCase, main
from champion_audit import validate_judgment
from champion_judges import derived_best_export
from report_audit import FileHashes, fingerprint
from report_quality import audit_judges, quality_counts, verify_selection
from test_champion_audit import judgment
import serial


def fixture(folder):
    root = Path(folder)
    matrix, judges = root/'matrix',root/'judges'
    matrix.mkdir();judges.mkdir()
    paths = {name:root/(name+'.json') for name in ('source','grammar','export','history','raw')}
    cases = [{'input':['1'],'expected':'2'},{'input':['3'],'expected':'4'}]
    serial.save(paths['source'],dict(cases=cases))
    serial.save(paths['grammar'],{})
    serial.save(paths['export'],dict(programs=[dict(nodes=['chosen']),dict(nodes=['later'])]))
    serial.save(paths['history'],dict(history=[]))
    serial.save(paths['raw'],{})
    binary = judges/'bench';binary.write_text('pinned binary')
    tag = 'fixture.p16.gpu_improved.s0.g2.r0'
    source = dict(status='ok',export=str(paths['export']),history=str(paths['history']),output=str(paths['raw']),
                  started_epoch=serial.START+.1,process_s=.1,pid=999999999)
    for field in ('export','history','output'):
        source[field+'_sha256'] = serial.sha(source[field])
    selected = judges/(tag+'.selected-final.json')
    proof = derived_best_export(paths['export'],source['export_sha256'],selected)
    output = judges/(tag+'.cpu.json')
    data = judgment()
    data['cases_sha256'] = fingerprint(cases)
    serial.save(output,data)
    command = [str(binary),'quality-source',str(paths['source']),str(selected),str(paths['grammar']),str(output),str(paths['history'])]
    inputs = {str(p):serial.sha(p) for p in (paths['source'],selected,paths['grammar'],paths['history'])}
    job = dict(status='ok',task='fixture',population=16,repetition=0,variant='gpu_improved',
               command=command,extra=dict(GAGP_BM_CASE_COUNT='1024'),input_sha256=inputs,
               selected_final_proof=proof,output=str(output),output_sha256=serial.sha(output),
               started_epoch=serial.START+1,process_s=.1,returncode=0,log=str(judges/(tag+'.log')),
               summary=validate_judgment(data,'fixture',2,2,'gpu_improved'))
    frozen = dict(tasks=['fixture'],populations=[16],repetitions=1,generations=2,cases=2,settings={},
                  binary_sha256=serial.sha(binary),inputs={'fixture':dict(source=str(paths['source']),
                  source_sha256=serial.sha(paths['source']),grammar=str(paths['grammar']),grammar_sha256=serial.sha(paths['grammar']))})
    parent = dict(freeze_sha256='f'*64,jobs={tag:source})
    serial.save(judges/'manifest.json',dict(parent_manifest_sha256='a'*64,parent_freeze_sha256='f'*64,
        settings={},binary_sha256=serial.sha(binary),judge_harness_sha256={},jobs={tag:job}))
    return frozen,parent,matrix,judges,tag


class TestReportQuality(TestCase):
    def test_missing_judges_remain_requested_missing_records(self):
        with tempfile.TemporaryDirectory() as folder:
            frozen,parent,matrix,judges,tag = fixture(folder)
            (judges/'manifest.json').unlink()
            result = audit_judges(frozen,parent,matrix,judges,'a'*64)
            self.assertEqual(len(result['records']),3)
            self.assertEqual({r['status'] for r in result['records']},{'missing'})
            self.assertEqual(result['quality'],{})

    def test_checks_original_case_results_and_final_generation_without_new_selection(self):
        with tempfile.TemporaryDirectory() as folder:
            frozen,parent,matrix,judges,tag = fixture(folder)
            result = audit_judges(frozen,parent,matrix,judges,'a'*64)
            record = next(r for r in result['records'] if r['tag'] == tag)
            self.assertEqual(record['status'],'ok',result['issues'])
            self.assertTrue(record['final_success_after_gen0'])
            self.assertEqual([r['generation'] for r in result['champions']],[0,1,2])
            self.assertEqual(result['champions'][-1]['stage'],'final')

    def test_case_substitution_is_invalid_even_with_internally_consistent_summary(self):
        with tempfile.TemporaryDirectory() as folder:
            frozen,parent,matrix,judges,tag = fixture(folder)
            manifest = serial.read(judges/'manifest.json')
            job = manifest['jobs'][tag]
            data = serial.read(job['output']);data['cases_sha256'] = 'b'*64
            serial.save(job['output'],data)
            job['output_sha256'] = serial.sha(job['output'])
            job['summary'] = validate_judgment(data,'fixture',2,2,'gpu_improved')
            serial.save(judges/'manifest.json',manifest)
            result = audit_judges(frozen,parent,matrix,judges,'a'*64)
            record = next(r for r in result['records'] if r['tag'] == tag)
            self.assertEqual(record['status'],'audit_invalid')
            self.assertEqual(record['original_status'],'ok')
            self.assertEqual(record['process_s'],.1)  # Invalid audit still consumed CPU time.
            self.assertEqual(record['returncode'],0)  # Exit success is not a valid CPU judgment.
            self.assertEqual(record['output'],job['output'])
            self.assertEqual(record['log'],job['log'])
            self.assertEqual(result['quality'],{})

    def test_updated_derivative_hash_cannot_hide_selection_of_second_candidate(self):
        with tempfile.TemporaryDirectory() as folder:
            frozen,parent,matrix,judges,tag = fixture(folder)
            proof = serial.read(judges/'manifest.json')['jobs'][tag]['selected_final_proof']
            serial.save(proof['derived_export'],dict(programs=[dict(nodes=['later'])]))
            proof['derived_export_sha256'] = serial.sha(proof['derived_export'])
            proof['preselected_first_program_sha256'] = fingerprint(dict(nodes=['later']))
            with self.assertRaises(AssertionError):
                verify_selection(proof,parent['jobs'][tag],FileHashes())

    def test_timing_repetitions_do_not_inflate_quality_sample_size(self):
        records = []
        for repetition in (0,1):
            records.append(dict(population=16,variant='asgp',repetition=repetition,status='ok',audit_ok=True,
                initial_success=False,final_success=True,final_success_after_gen0=True,initial_success_lost=False))
        records.append(dict(population=16,variant='asgp',repetition=0,status='budget_not_run'))
        counts = quality_counts(records,16,'asgp')
        self.assertEqual(counts['requested_tasks'],2)
        self.assertEqual(counts['audited_tasks'],1)
        self.assertEqual(counts['final_success_after_gen0'],1)
        self.assertEqual(counts['independent_search_seeds'],1)


if __name__ == '__main__':
    main()
