import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC=importlib.util.spec_from_file_location('best',Path(__file__).with_name('summarize_best.py'))
best=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(best)


def save(path,data):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(data))


def fixture(root):
    manifests={stage:[] for stage in ('formal','quality','best-quality')}
    for task,mode,seed in sorted(best.EXPECTED):
        tag=f'{task}.{mode}.{seed}'
        for stage in manifests:
            path=root/stage/f'{tag}.json'
            cell=dict(task=task,mode=mode,seed=seed,status='ok',output=str(path))
            if stage=='formal':
                data=dict(population=1024,cases=1024,mode=mode,seed=seed,
                    generations=[dict(generation=g,best=v) for g,v in enumerate((-30,-10,-20))],final_best=-15)
            else:
                score=dict(cases=1024,case_results=[{}]*1024,exact_accuracy=.5 if stage=='quality' else .6,
                    capped_l1_sum=512,runtime_errors=0)
                data=dict(task=task,cases=[{}]*1024,cases_sha256=task,final=score)
                if stage=='best-quality':cell.update(selected_generation=1,selected_training_fitness=-10)
            manifests[stage].append(cell);save(path,data)
    for stage,cells in manifests.items():save(root/stage/'manifest.json',dict(cells=cells))


class TestBest(unittest.TestCase):
    def test_lost_best_and_cumulative_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root);rows,curves=best.collect(root)
            self.assertEqual(len(rows),30)
            self.assertTrue(all(r['lost_best'] for r in rows))
            self.assertEqual(next(iter(curves.values())),[-30,-10,-10,-10])
            self.assertAlmostEqual(rows[0]['heldout_accuracy_delta'],.1)

    def test_final_tie_must_select_final_and_missing_cells_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root)
            manifest=best.read(root/'formal/manifest.json');path=Path(manifest['cells'][0]['output'])
            data=best.read(path);data['final_best']=-10;save(path,data)
            with self.assertRaisesRegex(ValueError,'tie preference'):best.collect(root)
            archived=best.read(root/'best-quality/manifest.json');archived['cells'][0]['selected_generation']='final'
            save(root/'best-quality/manifest.json',archived)
            rows,_=best.collect(root);self.assertFalse(rows[0]['lost_best'])
            archived['cells'].pop();save(root/'best-quality/manifest.json',archived)
            with self.assertRaisesRegex(ValueError,'Expected 30'):best.collect(root)


if __name__=='__main__':unittest.main()
