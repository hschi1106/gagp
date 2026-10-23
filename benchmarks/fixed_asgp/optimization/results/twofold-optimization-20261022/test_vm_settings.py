import unittest
from vm_pilot import execution_settings


class TestGlobalExecutionSettings(unittest.TestCase):
    def test_baseline_never_receives_candidate_features(self):
        for mode in ('coarse','full'):
            for width in (0,128,256):
                settings=execution_settings('baseline',mode,width)
                self.assertTrue(all(value=='0' for value in settings.values()))

    def test_physical_widths_keep_one_global_configuration(self):
        for variant,width in [('bank512',0),('bank128',128),('bank256',256),('full128',128)]:
            settings=execution_settings(variant,'coarse')
            self.assertEqual(settings['GAGP_GPU_VIEW_PHYSICAL_BLOCK'],str(width))
            self.assertEqual(settings['GAGP_GPU_REGISTER_PHASE_STACK'],'1')
            self.assertEqual(settings['GAGP_GPU_SIMPLE_PHASES'],'0')
        for width in (0,128,256):
            a,b=[execution_settings(v,'full',width) for v in ('basic','full')]
            self.assertEqual(a['GAGP_GPU_VIEW_PHYSICAL_BLOCK'],b['GAGP_GPU_VIEW_PHYSICAL_BLOCK'])
            differences={key for key in a if a[key]!=b[key]}
            self.assertEqual(differences,{'GAGP_GPU_SHAPE_RESULT_CACHE','GAGP_GPU_PROJECT_SHAPE_INPUTS'})

    def test_unsupported_silent_settings_are_rejected(self):
        for args in [('basic','coarse',0),('bank128','full',0),('full','full',64),('full','invalid',0)]:
            with self.assertRaises(ValueError):execution_settings(*args)


if __name__=='__main__':unittest.main()
