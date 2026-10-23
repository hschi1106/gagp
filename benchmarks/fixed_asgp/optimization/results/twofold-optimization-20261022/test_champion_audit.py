import copy
import math
import unittest

import champion_audit


class TestNativeChampionAudit(unittest.TestCase):
    def fixture(self):
        search = dict(generations=[dict(best=-2., best_wrong=1), dict(best=0., best_wrong=0)],
                      final_population=[dict(fitness=0., wrong=0)])
        rows = [dict(program=i, cases=3, cpu_cases=3, unscored=0, fitness=f, cpu_fitness=f,
                     wrong=w, cpu_wrong=w, errors=0, cpu_errors=0, timeouts=0, cpu_timeouts=0)
                for i, (f, w) in enumerate([(-2., 1), (0., 0), (0., 0)])]
        return dict(cases=3, population=3, programs=rows, diagnostic_owner='native-gpu-phase-v2-counted'), search

    def test_all_champions_and_final_are_required(self):
        document, search = self.fixture()
        self.assertTrue(champion_audit.validate(document, search, 'train', 3)['native_fidelity_pass'])
        document['programs'].pop()
        with self.assertRaises(AssertionError):champion_audit.validate(document, search, 'train', 3)

    def test_native_wrong_and_error_mismatch_are_rejected(self):
        original, search = self.fixture()
        for field in ('wrong', 'fitness', 'errors', 'timeouts'):
            document = copy.deepcopy(original)
            document['programs'][1][field] += 1
            self.assertFalse(champion_audit.validate(document, search, 'train', 3)['native_fidelity_pass'])

    def test_training_search_score_is_a_separate_check(self):
        document, search = self.fixture()
        search['generations'][0]['best'] = -3.
        result = champion_audit.validate(document, search, 'train', 3)
        self.assertEqual(result['native_mismatches']['search_cpu_fitness'], 1)
        self.assertFalse(result['native_fidelity_pass'])
        self.assertTrue(champion_audit.validate(document, search, 'test', 3)['native_fidelity_pass'])

    def test_rank_rounding_uses_half_away_from_zero(self):
        for sign in (-1, 1):
            value = sign*math.ldexp(2**39+.5, -40)
            self.assertEqual(champion_audit.canonical(value), sign*math.ldexp(2**39+1., -40))


if __name__ == '__main__':unittest.main()
