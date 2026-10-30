import unittest

from convert_cases import convert, value


class TestFixtureBridge(unittest.TestCase):
    def dataset(self, **changes):
        raw = dict(elements=[1, 2], scalars=[], start=[2, 0], extent=[2, 0],
                   expected=dict(int=2, bool=True, list=[], summary1=1, summary2=2))
        return dict(dict(task="example", sequence_input=True, list_type="intList",
                         scalar_names=[], scheme="divide-and-conquer", output="int",
                         data_seed=20260626, profile="legacy", train=[raw]), **changes)

    def test_empty_lists_keep_declared_integer_tag(self):
        self.assertEqual(value("intList", []), {"type": "int_list", "value": []})

    def test_character_sequences_use_string_tag(self):
        self.assertEqual(value("charList", [65, 10, 66]), {"type": "string", "value": "A\nB"})
        self.assertEqual(value("charList", []), {"type": "string", "value": ""})

    def test_boolean_projection_does_not_export_tuple_as_integer(self):
        result = convert(self.dataset(output="bool"), "train", "hash")
        self.assertEqual(result["cases"][0]["expected"], {"type": "bool", "value": True})
        self.assertEqual(result["schema"]["expected"], "bool")

    def test_dp_retains_start_and_extent_with_no_fake_sequence(self):
        result = convert(self.dataset(sequence_input=False, scheme="dynamic-programming"), "train", "hash")
        inputs = result["cases"][0]["inputs"]
        self.assertNotIn("xs", inputs)
        self.assertEqual(set(inputs), {"state_a", "state_b", "extent_a", "extent_b"})
        self.assertEqual(inputs["state_a"]["value"], 2)

    def test_mismatched_scalars_and_name_collision_rejected(self):
        for names in (["n"], ["xs"]):
            d = self.dataset(scalar_names=names)
            if names == ["xs"]:
                d["train"][0]["scalars"] = [1]
            with self.assertRaises(ValueError):
                convert(d, "train", "hash")


if __name__ == "__main__":
    unittest.main()
