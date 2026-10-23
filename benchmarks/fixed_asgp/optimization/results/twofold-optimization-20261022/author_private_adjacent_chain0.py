"""Create new immutable harness versions; earlier launched source is preserved."""
from pathlib import Path
b=Path(__file__).resolve().parent

def write(name,text):
    path=b/name;assert not path.exists(),name;path.write_text(text)

s=(b/'short_edit_pilot.py').read_text()
s=s.replace('VARIANTS=(\'baseline\',\'ordinary\',\'short\',\'soa\',\'short_soa\')',"VARIANTS=('baseline','ordinary','adjacent')")
a=s.index('def execution_settings(');z=s.index('\n\ndef run(',a)
s=s[:a]+'''def execution_settings(variant,selected=None,physical=0):
    if variant not in VARIANTS:raise ValueError('invalid adjacent arm')
    import stage_pilot
    settings=stage_pilot.execution_settings('pool_shape')
    settings.update(GAGP_GPU_CUDA_POOL='1',GAGP_GPU_PRIVATE_LAUNCH_BOUND='0',
        GAGP_GPU_PARENT_FITNESS_CACHE='0',GAGP_FUSED_COLD_EXECUTABLE='0',
        GAGP_GPU_PRIVATE_CELL_SOA='0',GAGP_GPU_SHORT_EDIT_DISTANCE='0',
        GAGP_GPU_PRIVATE_ADJACENT_CONCAT='1' if variant=='adjacent' else '0',
        GAGP_PHASE_REPRO_THREADS='1')
    return settings
'''+s[z:]
s=s.replace("'build-attempt23/manifest.json'","'build-attempt24/manifest.json'")
a=s.index("                                if variant in ('soa','short_soa'):")
z=s.index('                                for field, path',a)
s=s[:a]+'''                                if variant=='adjacent':assert all('-private-adjacent-concat' in p for p in profiles)
                                else:assert all('-private-adjacent-concat' not in p for p in profiles)
                                job['adjacent_active_generations']=sum('-private-adjacent-concat' in p and
                                    '-private-adjacent-concat-declined' not in p for p in profiles)
'''+s[z:]
s=s.replace('Global five-arm','Global three-arm')
write('private_adjacent_pilot.py',s)
s=(b/'combined2_pilot.py').read_text().replace('combined2','combined6')
write('combined6_pilot.py',s)
s=(b/'best_selection_short_edit_recipe.py').read_text()
s=s.replace('import best_selection_soa_recipe','import best_selection_short_edit_recipe')
s=s.replace('best_selection_soa_recipe.select()','best_selection_short_edit_recipe.select()').replace('combined5','combined6').replace('build-attempt24','build-attempt25')
write('best_selection_private_adjacent_recipe.py',s)
s=(b/'training_best_short_edit_recipe.py').read_text().replace('best_selection_short_edit_recipe','best_selection_private_adjacent_recipe')
s=s.replace("paths=[Path(__file__),", "paths=[Path(__file__),serial.BASE/'best_selection_short_edit_recipe.py',serial.BASE/'combined6_pilot.py',")
write('training_best_private_adjacent_recipe.py',s)
for src,dst in [('training_timing10.py','training_timing11.py'),('preparation_timing8.py','preparation_timing9.py')]:
 s=(b/src).read_text().replace('training15','training16').replace('training-timing10','training-timing11').replace('preparation-timing8','preparation-timing9')
 write(dst,s)
s=(b/'final_pipeline3.py').read_text().replace('final-pipeline3','final-pipeline4').replace('training15','training16').replace('training-timing10','training-timing11').replace('preparation-timing8','preparation-timing9')
s=s.replace("'combined5','fused-cold-admission1'","'combined5','combined6','fused-cold-admission1'")
s=s.replace("'cell-soa0','short-edit0'","'cell-soa0','short-edit0','private-adjacent0'")
s=s.replace("['combined1','combined2','combined3','combined4','combined5','stage1'","['combined1','combined2','combined3','combined4','combined5','combined6','stage1'")
write('final_pipeline4.py',s)
