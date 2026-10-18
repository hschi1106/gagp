"""Fresh input-disjoint validation fixtures; never chooses settings by test score."""
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path

import study

HELPER = study.ROOT / 'benchmarks/fixed_asgp/optimization/results/day24-20261017/quality_data.py'
spec = importlib.util.spec_from_file_location('previous_quality_data', HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
identity, unique = helper.identity, helper.unique


def digest_rows(rows):
    return hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def split_pool(pool, previous_selected, train_limit=200, test_limit=1000):
    # Oracle consistency is checked independently of which inputs are selected.
    unique(pool['train'] + pool['test'])
    train_pool, test_pool = unique(pool['train']), unique(pool['test'])
    previous = unique(previous_selected)
    train = dict(list(train_pool.items())[:train_limit])
    eligible = {k: v for k, v in test_pool.items() if k not in train and k not in previous}
    test = dict(list(eligible.items())[:test_limit])
    if not train or not test or train.keys() & test.keys() or previous.keys() & test.keys():
        raise ValueError('cannot provide a nonempty new disjoint test set')
    training, testing = list(train.values()), list(test.values())
    return dict(train=training, test=testing, metadata=dict(
        task=pool['task'], data_seed=pool['data_seed'],
        raw_train_unique=len(train_pool), raw_test_unique=len(test_pool),
        train_count=len(training), test_count=len(testing),
        requested_train_count=train_limit, requested_test_count=test_limit,
        previous_selected_unique=len(previous),
        new_train_overlap_previous_selected=len(train.keys() & previous.keys()),
        test_overlap_train=0, test_overlap_previous_selected=0,
        test_outside_raw_new_train_pool=sum(k not in train_pool for k in test),
        train_cases_sha256=digest_rows(training), test_cases_sha256=digest_rows(testing),
        rule='first unique fresh native train inputs; fresh native test inputs excluding new train and all previous selected train/test; no score-based selection',
        limitation='local ASGP domains; input exclusion is not an out-of-distribution guarantee'))


def prepare(stage, tasks, seeds):
    previous = study.read(study.ROOT / 'logs/day24-20261017/manifest.json')
    stage.m.setdefault('inputs', {})
    stage.persist()
    for task in tasks:
        entry = previous['inputs'][task]
        folder = stage.out / 'inputs' / task
        folder.mkdir(parents=True, exist_ok=True)
        prior=stage.m['inputs'].get(task,{})
        for field in ('pool','split','test_source'):
            if field+'_sha256' in prior:
                assert study.sha(prior[field])==prior[field+'_sha256'], 'resume fixture changed: '+field
        for record in prior.get('seeds',{}).values():
            for field in ('source','frozen_source','prepared'):
                if field+'_sha256' in record:
                    assert study.sha(record[field])==record[field+'_sha256'], 'resume initial population changed: '+field
        pool_path = folder / 'pool.json' 
        job = stage.run(task+'.dataset', ['dataset',task,pool_path], {'GAGP_DATASET_SEED':'20261018'}, reserve=10800)
        if job['status'] != 'ok':
            stage.m['inputs'][task] = dict(status=job['status']);stage.persist();continue
        old_train = study.read(entry['quality']['0']['source'])['cases']
        old_test = study.read(entry['test_source'])['cases']
        old_speed = study.read(entry['source'])['cases']
        try:
            split = split_pool(study.read(pool_path), old_train+old_test+old_speed)
        except ValueError as error:
            stage.m['inputs'][task]=dict(status='invalid_fixture',error=str(error),
                pool=str(pool_path),pool_sha256=study.sha(pool_path))
            stage.persist();continue
        split['metadata'].update(previous_input_sets='old selected quality train/test plus full-case speed fixture',
            previous_train_cases_sha256=digest_rows(old_train),previous_test_cases_sha256=digest_rows(old_test),
            previous_speed_cases_sha256=digest_rows(old_speed))
        split_path = folder / 'split.json'
        if split_path.exists():
            assert study.read(split_path) == split, 'resume split differs'
        else: study.save(split_path,split)
        inputs = dict(status='prepared',grammar=entry['grammar'],grammar_sha256=study.sha(entry['grammar']),
                      pool=str(pool_path),pool_sha256=study.sha(pool_path),split=str(split_path),
                      split_sha256=study.sha(split_path),metadata=split['metadata'],seeds={})
        test_path = folder / 'test.source.json'
        test_document=dict(format='fixed-asgp-source-v1',task=task,population=[],cases=split['test'],
                           data_seed=20261018,quality_data_seed=20261018,quality_split='test')
        if test_path.exists():assert study.read(test_path)==test_document,'resume test differs'
        else:study.save(test_path,test_document)
        inputs.update(test_source=str(test_path),test_source_sha256=study.sha(test_path))
        for seed in seeds:
            frozen = folder / f's{seed}.frozen.source.json'
            job = stage.run(f'{task}.s{seed}.freeze', ['freeze',task,frozen],
                {'GAGP_FREEZE_POPULATION':'1024','GAGP_FREEZE_POPULATION_SEED':str(seed)},reserve=10800)
            if job['status'] != 'ok':
                inputs['seeds'][str(seed)] = dict(status=job['status']);stage.persist();continue
            source = study.read(frozen)
            assert source['population_seed']==seed and len(source['population'])==1024
            source.update(cases=split['train'],data_seed=20261018,quality_data_seed=20261018,quality_split='train')
            train_path = folder / f's{seed}.train.source.json'
            if train_path.exists():assert study.read(train_path)==source,'resume source differs'
            else: study.save(train_path,source)
            prepared = folder / f's{seed}.prepared.json'
            job = stage.run(f'{task}.s{seed}.prepare', ['prepare',train_path,entry['grammar'],1024,prepared],reserve=10800)
            record = dict(status=job['status'],source=str(train_path),source_sha256=study.sha(train_path),
                          frozen_source=str(frozen),frozen_source_sha256=study.sha(frozen),prepared=str(prepared),
                          train_cases_sha256=digest_rows(source['cases']))
            if job['status']=='ok':
                native=study.read(prepared)
                assert len(native['programs'])==1024 and native.get('admission_rejections',[])==[]
                record.update(prepared=str(prepared),prepared_sha256=study.sha(prepared))
            inputs['seeds'][str(seed)]=record
            stage.m['inputs'][task]=inputs;stage.persist()
        stage.m['inputs'][task]=inputs;stage.persist()
    return stage.m['inputs']
