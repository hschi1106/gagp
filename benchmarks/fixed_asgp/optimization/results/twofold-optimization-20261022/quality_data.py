"""Post-freeze, paired inputs with disjoint training/test cases."""
import hashlib
import fcntl
import importlib.util
import json

import serial

HELPER=serial.ROOT/'benchmarks/fixed_asgp/optimization/results/day24-20261017/quality_data.py'
spec=importlib.util.spec_from_file_location('twofold_split_helper',HELPER)
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)


def digest(rows):
    return hashlib.sha256(json.dumps(rows,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def prepare(stage,freeze):
    assert freeze['first_heldout_use']=='after immutable candidate/scale freeze'
    assert freeze['data_seed']==20261022 and freeze['population']==1024
    stage.m.setdefault('inputs',{});stage.persist()
    for task in freeze['tasks']:
        folder=stage.out/'inputs'/task;folder.mkdir(parents=True,exist_ok=True)
        metadata=dict(task=task,seeds={})
        stage.m['inputs'][task]=metadata;stage.persist()
        pool=folder/'pool.json'
        job=stage.run(task+'.dataset',['dataset',task,pool],
            {'GAGP_DATASET_SEED':str(freeze['data_seed'])},reserve=freeze['reserve_s'])
        if job['status']!='ok':metadata.update(status=job['status']);stage.persist();continue
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            split=helper.split_pool(serial.read(pool),freeze['train_limit'],freeze['test_limit'])
            train_keys=set(helper.unique(split['train']));test_keys=set(helper.unique(split['test']))
            assert not train_keys & test_keys and train_keys and test_keys
            # Historical overlap is disclosed, including finite domains; it is
            # never inferred to be a new unseen-input/generalization guarantee.
            previous=serial.read(serial.ROOT/'logs/day24-20261017/manifest.json')['inputs'][task]
            historical=serial.read(previous['quality']['0']['source'])['cases']+serial.read(previous['test_source'])['cases']
            qc=serial.ROOT/'logs/quality-control-20261018/validation/manifest.json'
            if qc.exists():
                old=serial.read(qc).get('inputs',{}).get(task)
                if old and old.get('split'):historical+=serial.read(old['split'])['train']+serial.read(old['split'])['test']
            seen=set(helper.unique(historical))
            split['metadata'].update(train_sha256=digest(split['train']),test_sha256=digest(split['test']),
                test_overlap_previous_quality=len(test_keys & seen),
                limitation='new data seed and new evolution seeds; train/test disjoint; finite domains and previous-input overlap are disclosed')
            split_path=folder/'split.json';serial.save(split_path,split)
            test_source=folder/'test.source.json'
            serial.save(test_source,dict(format='fixed-asgp-source-v1',task=task,population=[],cases=split['test'],
                data_seed=freeze['data_seed'],quality_data_seed=freeze['data_seed'],quality_split='test'))
            metadata.update(status='prepared',grammar=freeze['inputs'][task]['grammar'],
                grammar_sha256=serial.sha(freeze['inputs'][task]['grammar']),pool=str(pool),pool_sha256=serial.sha(pool),
                split=str(split_path),split_sha256=serial.sha(split_path),test_source=str(test_source),
                test_source_sha256=serial.sha(test_source),metadata=split['metadata'])
            stage.persist()
        for seed in freeze['seeds']:
            frozen=folder/f's{seed}.source.json'
            job=stage.run(f'{task}.s{seed}.freeze',['freeze',task,frozen],
                {'GAGP_FREEZE_POPULATION':'1024','GAGP_FREEZE_POPULATION_SEED':str(seed)},reserve=freeze['reserve_s'])
            current=dict(status=job['status']);metadata['seeds'][str(seed)]=current;stage.persist()
            if job['status']!='ok':continue
            with serial.GPU_LOCK.open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX)
                document=serial.read(frozen)
                assert len(document['population'])==1024 and document['population_seed']==seed
                document.update(cases=split['train'],data_seed=freeze['data_seed'],quality_data_seed=freeze['data_seed'],quality_split='train')
                source=folder/f's{seed}.train.source.json';serial.save(source,document)
                current.update(source=str(source),source_sha256=serial.sha(source),
                    frozen_source=str(frozen),frozen_source_sha256=serial.sha(frozen),train_cases_sha256=digest(document['cases']))
                stage.persist()
            prepared=folder/f's{seed}.prepared.json'
            job=stage.run(f'{task}.s{seed}.prepare',['prepare',source,metadata['grammar'],1024,prepared],reserve=freeze['reserve_s'])
            with serial.GPU_LOCK.open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX)
                current.update(status=job['status'],prepared=str(prepared))
                if job['status']=='ok':
                    current['prepared_sha256']=serial.sha(prepared)
                    try:
                        native=serial.read(prepared)
                        assert len(native['programs'])==1024 and not native.get('admission_rejections',[])
                    except Exception as error:
                        current.update(status='native_unadmitted',error=repr(error))
                        job.update(status='native_unadmitted',error=repr(error))
                stage.persist()
    return stage.m['inputs']
