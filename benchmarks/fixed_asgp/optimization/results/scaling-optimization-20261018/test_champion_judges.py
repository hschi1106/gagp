import json
from pathlib import Path
import tempfile
from unittest import TestCase, main
import serial
from champion_judges import derived_best_export, expected_tags


class TestSelectedChampionProvenance(TestCase):
    def test_derivation_keeps_existing_first_program_and_preserves_original(self):
        with tempfile.TemporaryDirectory() as folder:
            original = Path(folder)/'full.json'
            target = Path(folder)/'selected.json'
            first = dict(nodes=[1], marker='already selected')
            original.write_text(json.dumps(dict(programs=[first, dict(marker='must not select')]))+'\n')
            digest = serial.sha(original)
            proof = derived_best_export(original, digest, target)
            self.assertEqual(serial.read(target), dict(programs=[first]))
            self.assertEqual(serial.sha(original), digest)
            self.assertEqual(proof['parent_export_sha256'], digest)
            self.assertEqual(derived_best_export(original, digest, target), proof)

    def test_changed_formal_export_is_rejected_before_creating_derived_file(self):
        with tempfile.TemporaryDirectory() as folder:
            original, target = Path(folder)/'full.json', Path(folder)/'selected.json'
            original.write_text('{"programs":[{}]}')
            with self.assertRaises(AssertionError):
                derived_best_export(original, 'wrong hash', target)
            self.assertFalse(target.exists())

    def test_existing_different_derivative_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            original, target = Path(folder)/'full.json', Path(folder)/'selected.json'
            original.write_text('{"programs":[{"nodes":[1]}]}')
            target.write_text('previous failed artifact')
            with self.assertRaises(AssertionError):
                derived_best_export(original, serial.sha(original), target)
            self.assertEqual(target.read_text(), 'previous failed artifact')

    def test_ledger_requests_each_frozen_variant_population_and_repetition_once(self):
        entries = list(expected_tags(dict(tasks=['fixture'], populations=[1024,16384], generations=8, repetitions=2)))
        self.assertEqual(len(entries), 12)
        self.assertEqual(len({entry[0] for entry in entries}), 12)
        self.assertEqual({entry[-1] for entry in entries}, {'asgp','gpu_original','gpu_improved'})


if __name__ == '__main__':
    main()
