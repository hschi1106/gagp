"""Fixture independence/oracle checks for the new validation study."""
import copy
import unittest

from validation_data import identity, split_pool


def case(value, **extra):
    return dict(input=[str(value)], expected=str(value), scalars=[], start=0,
                start_b=0, extent=1, extent_b=0, **extra)


class TestValidationFixtures(unittest.TestCase):
    def test_excludes_new_training_and_previous_train_and_test(self):
        pool=dict(task='toy',data_seed=20261018,
                  train=[case(1),case(1),case(2)],
                  test=[case(i) for i in (1,2,3,4,5)])
        split=split_pool(pool,[case(1),case(4)])
        self.assertEqual(split['train'],[case(1),case(2)])
        self.assertEqual(split['test'],[case(3),case(5)])
        self.assertEqual(split['metadata']['test_count'],2)
        self.assertEqual(split['metadata']['requested_test_count'],1000)
        self.assertEqual(split,split_pool(copy.deepcopy(pool),[case(1),case(4)]))

    def test_parameters_are_part_of_input_identity(self):
        first=case(1); second=dict(first,start=1);third=dict(first,scalars=['2'])
        self.assertEqual(len({identity(first),identity(second),identity(third)}),3)
        split=split_pool(dict(task='toy',data_seed=1,train=[first],test=[second,third]),[])
        self.assertEqual(len(split['test']),2)

    def test_no_resampling_when_domain_cannot_supply_heldout(self):
        with self.assertRaises(ValueError):
            split_pool(dict(task='toy',data_seed=1,train=[case(1)],test=[case(2)]),[case(2)])

    def test_oracle_inconsistency_rejected_and_labels_do_not_select_inputs(self):
        with self.assertRaises(ValueError):
            split_pool(dict(task='toy',data_seed=1,train=[case(1)],test=[dict(case(1),expected='99')]),[])
        pool=dict(task='toy',data_seed=1,train=[case(1),case(2)],test=[case(3),case(4)])
        relabeled=copy.deepcopy(pool)
        for row in relabeled['train']+relabeled['test']:row['expected']='-100'
        a,b=split_pool(pool,[]),split_pool(relabeled,[])
        self.assertEqual([identity(r) for r in a['train']],[identity(r) for r in b['train']])
        self.assertEqual([identity(r) for r in a['test']],[identity(r) for r in b['test']])


if __name__=='__main__':unittest.main()
