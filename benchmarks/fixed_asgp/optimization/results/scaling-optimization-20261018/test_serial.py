import tempfile
from pathlib import Path
from unittest import TestCase,main,mock
import serial


class FakeMemory:
    def sample(self):return 1


class TestImmutableStages(TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.binary=self.root/'original';self.binary.write_text('#!/bin/sh\nexit 7\n');self.binary.chmod(0o755)
        self.patch=mock.patch.object(serial,'RAW',self.root);self.patch.start()
        self.memory=mock.patch.object(serial,'GpuMemory',return_value=FakeMemory());self.memory.start()
        self.inner=mock.patch.object(serial,'GPU_LOCK',self.root/'inner.lock');self.inner.start()
        self.stage=None

    def tearDown(self):
        if self.stage:self.stage.lock.close()
        self.inner.stop();self.memory.stop();self.patch.stop();self.temp.cleanup()

    def make(self,**extra):
        self.stage=serial.Stage('sample',self.binary,settings={},**extra);return self.stage

    def test_requires_explicit_resume_and_same_binary(self):
        s=self.make();s.lock.close();self.stage=None
        with self.assertRaises(RuntimeError):self.make()
        self.binary.write_text('changed')
        with self.assertRaises(AssertionError):self.make(resume=True)

    def test_failed_attempt_is_preserved_without_rerun(self):
        s=self.make();j=s.run('failed',['--fail'],reserve=0);pid=j['pid'];self.assertEqual(j['status'],'failed')
        self.assertIs(s.run('failed',['--fail'],reserve=0),j);self.assertEqual(j['pid'],pid)
        with self.assertRaises(AssertionError):s.run('failed',['--different'],reserve=0)
        s.lock.close();self.stage=None;s=self.make(resume=True)
        self.assertEqual(s.run('failed',['--fail'],reserve=0)['pid'],pid)

    def test_resume_rejects_changed_input(self):
        inputs=[]
        for i in range(3):p=self.root/f'in{i}';p.write_text(str(i));inputs.append(p)
        s=self.make();s.run('failed',['measure',*inputs],reserve=0);inputs[0].write_text('modified')
        with self.assertRaises(AssertionError):s.run('failed',['measure',*inputs],reserve=0)

    def test_budget_missing_work_is_visible_and_not_started(self):
        s=self.make()
        with mock.patch.object(serial,'DEADLINE',0):j=s.run('late',['--fail'],reserve=0)
        self.assertEqual(j['status'],'budget_not_run');self.assertNotIn('pid',j)


if __name__=='__main__':main()
