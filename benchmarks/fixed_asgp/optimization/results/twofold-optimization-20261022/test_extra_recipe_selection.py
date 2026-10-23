"""Full-scope selection must keep the actual measured global recipe."""
from copy import deepcopy
from unittest import TestCase
from unittest.mock import patch

import best_selection_extra_recipe as selection
import serial


class TestExtraRecipeSelection(TestCase):
    def fixture(self,ratio=2.1):
        old=dict(family='shape1',stage_family='shape1',variant='shape',build_name='build-attempt8',
                 extra={'GAGP_PHASE_REPRO_THREADS':'1','GAGP_GPU_INTEGER_CASE_REUSE':'1'},
                 geometric_means={'1024':1.3,'16384':1.3,'pooled':1.3},
                 binary='old-bench',binary_sha256='old')
        original=dict(considered=[old],variants={'baseline':{'extra':{'baseline':'1'}}})
        chosen=dict(extra={'GAGP_PHASE_REPRO_THREADS':'64','GAGP_GPU_PARENT_FITNESS_CACHE':'1'},
                    build_name='build-attempt21',binary='new-bench',binary_sha256='pinned')
        control=dict(status='requested_phase_complete',run_full=True,
                     settings_sha256='pinned',selection=chosen)
        tasks=list(serial.PAPER_TASKS)
        rows={p:[dict(task=t,population=p,variant='candidate',speedup=ratio,
                      final_descriptors_equal=True,full_final_ast_equal=True,
                      champion_ast_fitness_equal=True) for t in tasks] for p in (1024,16384)}
        data={str(serial.RAW/'combined2-controller.json'):control,
              str(serial.RAW/'combined2-settings.json'):chosen,
              str(serial.RAW/'build-attempt21/manifest.json'):{'status':'ok','binary_sha256':'pinned'},
              str(serial.BASE/'protocol.json'):{'tasks':tasks}}
        for p in rows:data[str(serial.RAW/f'combined2-p{p}-comparisons.json')]=rows[p]
        return original,data,control,rows

    def run_fixture(self,original,data):
        with patch.object(selection.best_selection_coarse_recipe,'select',return_value=original),\
             patch.object(serial,'read',side_effect=lambda path:deepcopy(data[str(path)])),\
             patch.object(serial,'sha',return_value='pinned'):
            return selection.select()

    def test_new_winner_keeps_block_and_cache(self):
        original,data,control,_=self.fixture()
        result=self.run_fixture(original,data)
        self.assertEqual(result['selected']['family'],'combined2')
        self.assertEqual(result['variants']['candidate']['extra'],control['selection']['extra'])

    def test_slower_new_recipe_does_not_modify_old_fallback(self):
        original,data,_,_=self.fixture(1.1)
        result=self.run_fixture(original,data)
        self.assertEqual(result['selected']['family'],'shape1')
        self.assertEqual(result['variants']['candidate']['extra'],original['considered'][0]['extra'])

    def test_rejected_coarse_never_becomes_full_candidate(self):
        original,data,control,_=self.fixture()
        control['run_full']=False
        result=self.run_fixture(original,data)
        self.assertEqual(len(result['considered']),1)

    def test_incomplete_grid_rejected(self):
        original,data,_,rows=self.fixture();rows[16384].pop()
        with self.assertRaises(AssertionError):self.run_fixture(original,data)

    def test_duplicate_cell_rejected(self):
        original,data,_,rows=self.fixture();rows[16384][-1]=dict(rows[16384][0])
        with self.assertRaises(AssertionError):self.run_fixture(original,data)

    def test_fidelity_failure_rejected(self):
        original,data,_,rows=self.fixture();rows[1024][0]['full_final_ast_equal']=False
        with self.assertRaises(AssertionError):self.run_fixture(original,data)

    def test_altered_recipe_rejected(self):
        original,data,control,_=self.fixture()
        data[str(serial.RAW/'combined2-settings.json')]=dict(control['selection'],extra={})
        with self.assertRaises(AssertionError):self.run_fixture(original,data)
