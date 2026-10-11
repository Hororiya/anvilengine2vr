import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

spec=importlib.util.spec_from_file_location('camera',Path(__file__).resolve().parents[1]/'tools/shadows_scene_camera.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class CameraCleanupTests(unittest.TestCase):
    def test_delayed_cpu_and_gpu_never_force_finish(self):
        states=[dict(active=1,publishing=False,gpuIdle=True,pending=True,normal=0),
                dict(active=0,publishing=True,gpuIdle=True,pending=True,normal=0),
                dict(active=0,publishing=False,gpuIdle=False,pending=False,normal=1),
                dict(active=0,publishing=False,gpuIdle=True,pending=False,normal=1)]
        events=[]
        exports=SimpleNamespace(stop=lambda:events.append('stop'),status=lambda:states.pop(0),finish=lambda:events.append('finish') or {'done':True})
        result=module.finish_camera(SimpleNamespace(exports_sync=exports),{'reason':None},wait=lambda _:events.append('wait'),now=lambda:0)
        self.assertEqual(result,{'done':True});self.assertEqual(events,['stop','wait','wait','wait','finish'])

    def test_paused_inactive_camera_restores_after_grace(self):
        clock=iter([0,3]);events=[]
        exports=SimpleNamespace(stop=lambda:None,status=lambda:dict(active=0,publishing=False,gpuIdle=True,pending=True,normal=0),finish=lambda:events.append('finish') or {})
        module.finish_camera(SimpleNamespace(exports_sync=exports),{'reason':None},wait=lambda _:self.fail('unexpected wait'),now=lambda:next(clock))
        self.assertEqual(events,['finish'])


if __name__=='__main__':unittest.main()
