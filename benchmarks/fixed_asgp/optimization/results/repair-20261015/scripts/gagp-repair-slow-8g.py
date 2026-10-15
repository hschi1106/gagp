from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, save
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'slow-p1024-g1-workspace8g',binary=root/'cpp/build/release/gagp_fixed_asgp_bench',settings=base/'settings-best.json',stage='bounded-slow-screen',generations=1,rss_gib=4,budget=600,timeout=120,native_depth=None,source_balanced=False)
s=Study(a)
s.env['GAGP_GPU_GLOBAL_PAYLOAD']='1';s.manifest['settings']['GAGP_GPU_GLOBAL_PAYLOAD']='1'
s.env.update(GAGP_GPU_ADAPTIVE_REPLAY='1',GAGP_GPU_PAYLOAD_WORKSPACE_MIB='8192');s.manifest['settings'].update(GAGP_GPU_ADAPTIVE_REPLAY='1',GAGP_GPU_PAYLOAD_WORKSPACE_MIB='8192')
s.manifest['controls_reused']='slow-p1024-g1-global ASGP1T G1 seed0 unchanged source/binary source logic/settings';s.manifest['note']='One generation plus final evaluation. Separate G1 controls; cannot use previous G4 denominators. Fixed source/prepared inputs reused; all1024cases andCPU1T. Bounded globalinitialarena withfusedConcat; no precise repeats.';s.persist()
for task in ['filter_odd','negative_to_zero','cumulative_sum','for_loop_index','double_letters','replace_space_with_newline','even_squares']:
    results=[]
    for mode in ['gpu_overlap']:
        results.append(s.search(task,base/'support-balanced'/task,base/'benchmark-all-g4'/f'{task}.p1024.prepared.json',1024,mode,0))
    s.manifest['tasks'][task]={'status':'passed' if all(x=='ok' for x in results) else 'failed'};s.persist()
