"""Frozen settings/commands, serial intervals and paired input provenance."""
from pathlib import Path

import quality_data
import serial


def validate_measure_command(job, manifest, freeze):
    command = job['command']
    variant = job['variant']
    spec = freeze['variants'][variant]
    assert manifest['binary_sha256'] == spec['binary_sha256']
    assert len(command) == 8 and command[1] == 'measure'
    mode = command[6]
    expected = dict(spec['extra'])
    if mode == 'search_gpu_overlap':
        assert int(command[5]) == job['population']
        assert all(job['extra'].get(k) == v for k, v in expected.items())
        return
    assert mode == 'snapshot', 'unexpected measure mode in formal matrix'
    neutral = freeze.get('native_audit_neutral_extra', {})
    assert neutral in ({}, {'GAGP_FUSED_COLD_EXECUTABLE': '0'})
    for key, value in neutral.items():
        assert key not in expected or expected[key] == value
        expected[key] = value
    assert all(job['extra'].get(k) == v for k, v in expected.items())
    assert job['extra']['GAGP_BM_CASE_COUNT'] == str(job['cases'])
    assert job['extra']['GAGP_SNAPSHOT_NATIVE_OWNER'] == '1'
    allowed = set(expected) | {'GAGP_BM_CASE_COUNT', 'GAGP_SNAPSHOT_CPU', 'GAGP_SNAPSHOT_NATIVE_OWNER'}
    assert set(job['extra']) <= allowed, 'unexpected auxiliary execution option'
    if job.get('cpu_reference'):
        assert 'GAGP_SNAPSHOT_CPU' not in job['extra']
        assert job['native_cpu_executed_programs'] == 0
        assert int(command[5]) == job['native_cpu_reference_programs'] > 0
        if job['status'] == 'ok':
            assert job['cpu_reference_verified'] is True
    else:
        assert job['extra']['GAGP_SNAPSHOT_CPU'] == '1'
        assert int(command[5]) == job['native_cpu_executed_programs'] > 0


def planning_issues(speed_freeze, quality_freeze):
    mode = speed_freeze.get('planning_mode')
    assert mode == quality_freeze.get('planning_mode')
    if mode is None:
        return []
    if mode != 'best_effort_after_budget_refusal':
        raise ValueError('unknown validation planning mode')
    assert speed_freeze['original_forecast_passed'] is False
    assert quality_freeze['original_forecast_passed'] is False
    return [dict(status='best_effort_outside_fitting_rule', scope='runtime_plan',
                 detail='minimum full grid attempted after explicit budget refusal; original primary protocol qualification is not claimed')]


def validate(speed_freeze,quality_freeze,speed_name,quality_name,evidence,regressions):
    protocol=serial.read(serial.BASE/'protocol.json')
    issues=planning_issues(speed_freeze,quality_freeze);intervals=[];commands=0

    def checked(path,digest):
        path=str(path)
        if path not in evidence.hashes:
            assert serial.sha(path)==digest,('input changed',path)
            evidence.hashes[path]=digest
        else:assert evidence.hashes[path]==digest

    assert speed_freeze['variants']==quality_freeze['variants']
    for freeze in (speed_freeze,quality_freeze):
        assert freeze['settings']==protocol['settings']
        assert freeze['started_epoch']==serial.START and freeze['deadline_epoch']==serial.DEADLINE
        assert freeze['frozen_epoch']<serial.DEADLINE
        for path,digest in freeze['harness_sha256'].items():checked(path,digest)
        for spec in freeze['variants'].values():checked(spec['binary'],spec['binary_sha256'])
    assert quality_freeze['frozen_epoch']==speed_freeze['frozen_epoch']
    for entry in speed_freeze['inputs'].values():
        for field in ('source','grammar'):checked(entry[field],entry[field+'_sha256'])
        for prefix in entry['prefixes'].values():checked(prefix['prepared'],prefix['sha256'])
    for name,freeze in [(speed_name,speed_freeze),(quality_name,quality_freeze)]:
        for folder in sorted(serial.RAW.glob(name+'-*')):
            path=folder/'manifest.json'
            if not path.exists():continue
            manifest=serial.read(path);assert manifest['settings']==freeze['settings']
            for tag,job in manifest['jobs'].items():
                for item,digest in job.get('input_sha256',{}).items():checked(item,digest)
                if 'started_epoch' not in job:continue
                assert job['started_epoch']>=freeze['frozen_epoch']
                assert job['process_s']>=0 and job['started_epoch']+job['process_s']<=serial.DEADLINE
                intervals.append((job['started_epoch'],job['started_epoch']+job['process_s'],name+'/'+tag))
                command=job['command'];commands+=1
                assert serial.sha(command[0])==manifest['binary_sha256']
                if len(command)>1 and command[1]=='measure':
                    validate_measure_command(job,manifest,freeze)
    intervals.sort()
    for a,b in zip(intervals,intervals[1:]):assert b[0]>=a[1]-.005,('timed jobs overlap',a,b)
    preparation=serial.RAW/(quality_name+'-inputs')/'manifest.json'
    rows=[]
    if preparation.exists():
        inputs=serial.read(preparation).get('inputs',{})
        for task in quality_freeze['tasks']:
            entry=inputs.get(task,{})
            if entry.get('status')!='prepared':
                issues.append(dict(task=task,status=entry.get('status','missing'),scope='quality inputs'));continue
            for field in ('grammar','pool','split','test_source'):checked(entry[field],entry[field+'_sha256'])
            split=serial.read(entry['split']);train=split['train'];test=split['test']
            train_keys=set(quality_data.helper.unique(train));test_keys=set(quality_data.helper.unique(test))
            assert train_keys and test_keys and not train_keys & test_keys
            assert len(train_keys)==len(train) and len(test_keys)==len(test)
            assert serial.read(entry['test_source'])['cases']==test
            completed=0
            for seed in quality_freeze['seeds']:
                current=entry.get('seeds',{}).get(str(seed),{})
                if current.get('status')!='ok':
                    issues.append(dict(task=task,seed=seed,status=current.get('status','missing'),scope='quality preparation'));continue
                for field in ('source','prepared','frozen_source'):checked(current[field],current[field+'_sha256'])
                source=serial.read(current['source']);original=serial.read(current['frozen_source'])
                assert source['cases']==train and source['population']==original['population']
                assert source['population_seed']==original['population_seed']==seed
                assert len(source['population'])==1024
                native=serial.read(current['prepared'])
                assert len(native['programs'])==1024 and native['source_indices']==list(range(1024))
                assert not native.get('admission_rejections',[])
                completed+=1
            rows.append(dict(task=task,train_count=len(train),test_count=len(test),overlap=0,
                identical_seed_splits_verified=completed,expected_seeds=len(quality_freeze['seeds']),
                train_sha256=quality_data.digest(train),test_sha256=quality_data.digest(test),
                test_overlap_previous_quality=split['metadata']['test_overlap_previous_quality']))
    else:issues.append(dict(status='missing',scope='quality input manifest'))
    if regressions.get('candidate_binary_sha256')!=speed_freeze['variants']['candidate']['binary_sha256']:
        issues.append(dict(status='candidate binary differs',scope='source/regression validation'))
    if regressions.get('status')!='ok':issues.append(dict(status=regressions.get('status','missing'),scope='source/regression validation'))
    for path,digest in regressions.get('evidence_sha256',{}).items():checked(path,digest)
    return dict(status='ok' if not issues else 'partial',issues=issues,input_splits=rows,
        command_count=commands,serial_intervals_checked=len(intervals),source_regressions=regressions,
        checked_artifact_sha256=evidence.hashes,
        vram_limitation='whole-card NVML sampled~200ms, desktop included, short peaks may be missed')
