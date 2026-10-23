"""Build a new report from immutable formal matrices, keeping all failed cells."""
from argparse import ArgumentParser
import csv
import fcntl
import json
from pathlib import Path
import time
import traceback

import report_data
import report_validation
import report_views_implementation as report_views
import serial


def csv_file(path,rows):
    fields=list(dict.fromkeys(key for row in rows for key in row))
    with path.open('x',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader()
        writer.writerows({k:json.dumps(v,ensure_ascii=False) if isinstance(v,(dict,list,tuple)) else v
                         for k,v in row.items()} for row in rows)


def main(speed_path,quality_path,speed_name,quality_name,regression_path,out):
    assert not out.exists(),'reports are immutable attempts; choose a NEW output directory'
    out.mkdir(parents=True)
    source_paths=[Path(__file__)]+[serial.BASE/(name+'.py') for name in (
        'report_data','report_validation','report_views_implementation','report_qualification',
        'reference_reader','shared_cpu_reference','study_statistics','quality_metrics')]
    state=dict(status='running',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
               report_source_sha256={str(p):serial.sha(p) for p in source_paths})
    serial.save(out/'completion.json',state)
    try:
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            for name in (speed_name,quality_name):
                path=serial.RAW/(name+'-controller.json')
                if path.exists():
                    control=serial.read(path);proc=Path(f'/proc/{control["pid"]}/stat')
                    assert not proc.exists() or proc.read_text().split()[2]=='Z','timed controller still alive'
            speed_freeze=serial.read(speed_path);quality_freeze=serial.read(quality_path)
            evidence=report_data.Evidence()
            speed=report_data.read_speed(speed_name,speed_freeze,evidence)
            quality=report_data.read_quality(quality_name,quality_freeze,evidence)
            validation=report_validation.validate(speed_freeze,quality_freeze,speed_name,quality_name,
                evidence,serial.read(regression_path))
            if validation['status']!='ok':
                speed['summary']['all_primary_targets_reached']=False
                for group in speed['summary']['groups'].values():group['target_reached']=False
            bundle=dict(speed=speed,quality=quality,speed_freeze=speed_freeze,quality_freeze=quality_freeze,
                validation_status=validation['status'],report_epoch=time.time())
            serial.save(out/'summary.json',bundle);serial.save(out/'validation.json',validation)
            for filename,rows in [('speed_runs.csv',speed['runs']),('speed_cells.csv',speed['summary']['cells']),
                ('speed_native_audits.csv',speed['native_audits']),('quality_runs.csv',quality['runs']),
                ('quality_pairs.csv',quality['pairs']),('quality_tasks.csv',quality['tasks'])]:csv_file(out/filename,rows)
            (out/'REPORT.zh-TW.md').write_text(report_views.markdown(bundle))
            (out/'dashboard.html').write_text(report_views.dashboard(bundle))
            report_views.plots(bundle,out)
            for path,digest in state['report_source_sha256'].items():
                assert serial.sha(path)==digest,'report source changed during analysis'
            state.update(status='ok',finished_epoch=time.time(),within_original_deadline=time.time()<=serial.DEADLINE,
                target_reached=speed['summary']['all_primary_targets_reached'],validation_status=validation['status'],
                artifact_sha256={p.name:serial.sha(p) for p in out.iterdir() if p.name!='completion.json'})
    except BaseException as error:
        state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
    finally:serial.save(out/'completion.json',state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--speed-freeze',required=True,type=Path);parser.add_argument('--quality-freeze',required=True,type=Path)
    parser.add_argument('--speed-name',required=True);parser.add_argument('--quality-name',required=True)
    parser.add_argument('--regressions',required=True,type=Path);parser.add_argument('--out',required=True,type=Path)
    a=parser.parse_args();main(a.speed_freeze,a.quality_freeze,a.speed_name,a.quality_name,a.regressions,a.out)
