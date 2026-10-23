"""Export the actual frozen global candidate, never a hand-picked or partial recipe."""
from argparse import ArgumentParser
import json
from pathlib import Path

import serial

FORMAT='gagp-twofold-user-preset-v1'


def make_preset(freeze,root):
    if freeze.get('format')!='gagp-twofold-speed-freeze-v3':
        raise ValueError('requires the real formal speed freeze')
    if freeze.get('started_epoch')!=serial.START or freeze.get('deadline_epoch')!=serial.DEADLINE:
        raise ValueError('different study clock')
    if freeze.get('tasks')!=list(serial.PAPER_TASKS) or freeze.get('populations')!=[1024,16384]:
        raise ValueError('the frozen task/population scope changed')
    if freeze.get('generations',0)<8 or freeze.get('repetitions',0)<2:
        raise ValueError('not the frozen formal minimum scale')
    selected=freeze['candidate_selection']['selected'];candidate=freeze['variants']['candidate']
    if candidate['binary_sha256']!=selected['binary_sha256'] or candidate['binary']!=selected['binary']:
        raise ValueError('selection and frozen candidate binary differ')
    if candidate['extra']!=selected['extra']:
        raise ValueError('selection and frozen global options differ')
    binary=Path(candidate['binary'])
    if not binary.is_absolute():raise ValueError('freeze must record the actual absolute binary')
    relative=str(binary.relative_to(root))
    environment=dict(freeze['settings'],**candidate['extra'])
    if environment.get('GAGP_GPU_PAYLOAD_WORKSPACE_MIB')!='16384':
        raise ValueError('workspace must retain16384MiB')
    return dict(format=FORMAT,binary=relative,binary_sha256=candidate['binary_sha256'],
        source_build_name=selected['build_name'],source_sha256=freeze['candidate_source_sha256'],
        settings=environment,default_generations=freeze['generations'],default_cases=freeze['cases'],
        gpu_lock=str(serial.GPU_LOCK),resources=freeze['resources'],
        origin=dict(family=selected['family'],variant=selected['variant'],frozen_epoch=freeze['frozen_epoch']),
        usage_scope='A new user invocation; never append its output to the frozen experiment or claim new formal evidence.')


def invocation(preset,root,source,prepared,grammar,population,out,generations=None,cases=None,seed=0,ambient=None):
    if preset.get('format')!=FORMAT:raise ValueError('unsupported preset format')
    binary=Path(preset['binary'])
    if binary.is_absolute() or '..' in binary.parts:raise ValueError('preset binary escapes repository root')
    if population<=0:raise ValueError('population must be positive')
    generations=preset['default_generations'] if generations is None else generations
    cases=preset['default_cases'] if cases is None else cases
    if generations<1 or cases<1 or seed<0:raise ValueError('invalid generation/case/seed setting')
    # Ambient GAGP switches cannot silently alter a frozen global recipe.
    environment={k:v for k,v in (ambient or {}).items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')}
    environment.update(preset['settings'])
    out=Path(out)
    extra=dict(GAGP_SEARCH_GENERATIONS=str(generations),GAGP_BM_SEED=str(seed),GAGP_BM_CASE_COUNT=str(cases),
        GAGP_SEARCH_EXPORT=str(out.with_suffix('.population.json')),
        GAGP_SEARCH_HISTORY_EXPORT=str(out.with_suffix('.history.json')))
    environment.update(extra)
    command=list(map(str,[Path(root)/binary,'measure',source,prepared,grammar,population,'search_gpu_overlap',out]))
    return command,environment


def main(freeze_path,out):
    freeze_path=Path(freeze_path).resolve()
    out=Path(out).resolve()
    if out.exists():raise ValueError('preserve existing preset; choose a new output')
    freeze=serial.read(freeze_path);preset=make_preset(freeze,serial.ROOT)
    if serial.sha(serial.ROOT/preset['binary'])!=preset['binary_sha256']:raise ValueError('pinned binary changed')
    preset['origin'].update(freeze=str(freeze_path.relative_to(serial.ROOT)),freeze_sha256=serial.sha(freeze_path))
    out.parent.mkdir(parents=True,exist_ok=True)
    with out.open('x') as stream:json.dump(preset,stream,indent=2);stream.write('\n')


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--freeze',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();main(args.freeze,args.out)
