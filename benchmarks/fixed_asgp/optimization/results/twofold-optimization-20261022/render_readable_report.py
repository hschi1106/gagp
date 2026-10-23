"""Make a NEW presentation of the sealed report; numeric artifacts stay exact.

The first desktop preview exposed a 47px task column because the single long
failure reason took358px. Fix layout/status labels, preserving that reason in
both the data payload and each status cell's title. No statistics are recomputed.
"""
from argparse import ArgumentParser
import fcntl
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import time
import traceback

import serial


def main(parent, out):
    assert not out.exists(), 'preserve every report presentation attempt'
    out.mkdir()
    state=dict(status='running',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
               parent_report=str(parent.resolve()),presentation_source_sha256=serial.sha(__file__))
    try:
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            completion=serial.read(parent/'completion.json')
            assert completion['status']=='ok'
            assert completion['target_reached'] is False
            for name,digest in completion['artifact_sha256'].items():
                assert Path(name).name==name and serial.sha(parent/name)==digest
                if name!='dashboard.html':shutil.copy2(parent/name,out/name)
            preset=parent/'winner_preset.json'
            assert preset.is_file();shutil.copy2(preset,out/preset.name)
            shutil.copy2(parent/'completion.json',out/'parent-completion.json')
            summary=serial.read(parent/'summary.json')
            page=(parent/'dashboard.html').read_text()
            style='''.speed-table{table-layout:fixed}.speed-table th,.speed-table td{overflow-wrap:anywhere}
.speed-table th:nth-child(1),.speed-table td:nth-child(1){width:28%}
.speed-table th:nth-child(2),.speed-table td:nth-child(2){width:9%}
.speed-table th:nth-child(3),.speed-table td:nth-child(3){width:11%}
.speed-table th:nth-child(4),.speed-table td:nth-child(4){width:11%}
.speed-table th:nth-child(5),.speed-table td:nth-child(5){width:10%}
.speed-table th:nth-child(6),.speed-table td:nth-child(6){width:8%}
.speed-table th:nth-child(7),.speed-table td:nth-child(7){width:23%}
'''
            assert page.count('<style>')==1
            page=page.replace('<style>','<style>'+style,1)
            old="<td>${r.qualified?'已核對':esc(r.failures.join('; ')||'未完成')}</td>"
            new="<td title=\"${esc(r.failures.join('; '))}\">${r.qualified?'已核對':(r.failures.join('; ').includes('interrupted')?'核對中斷':'核對未齊')}</td>"
            assert page.count(old)==1;page=page.replace(old,new,1)
            # Keep complete scope/cost prose available, after concise summaries.
            wrappers=[('timing-scope','計時範圍與完整程序成本'),
                      ('native-scope','核對範圍：第0次重複的完整族群，其餘重複的冠軍與分數')]
            for identifier,label in wrappers:
                pattern=rf'(<p class="note" id="{identifier}">.*?</p>)'
                assert len(re.findall(pattern,page,re.S))==1
                page=re.sub(pattern,lambda m:'<details><summary>'+label+'</summary>'+m.group(1)+'</details>',page,count=1,flags=re.S)
            page=page.replace('不能把上表已核對子集的平均','不能把已核對子集的平均',1)
            speed=summary['speed_freeze'];quality=summary['quality_freeze']
            counts=summary['quality_search_timing']['training_case_count_by_task']
            workloads=(f"速度：全30題，P1024／16384、C{speed['cases']}、G{speed['generations']}、每方法{speed['repetitions']}次。"
                       f"品質：P{quality['population']}、G{quality['generations']}、C{min(counts.values())}–{max(counts.values())}、{len(quality['seeds'])}種子。")
            anchor='<p id="decision"></p>'
            assert page.count(anchor)==1;page=page.replace(anchor,anchor+'<p id="workloads">'+html.escape(workloads)+'</p>',1)
            expected=len(quality['tasks'])*len(quality['seeds'])
            totals={}
            for variant in ['baseline','candidate']:
                rows=[r for r in summary['quality']['runs'] if r['variant']==variant and r.get('quality_audited')]
                totals[variant]=dict(audited=len(rows),**{k:sum(bool(r['joint_'+k]) for r in rows)
                    for k in ['gen0','final','new_final','lost_initial']})
            assert totals['baseline']==totals['candidate'] and totals['baseline']['audited']==expected
            t=totals['baseline']
            quality_note=(f"Train+Test：兩版各{t['audited']}次核對；Gen0成功{t['gen0']}，最終成功{t['final']}，"
                          f"演化後新增成功{t['new_final']}，失去初始成功{t['lost_initial']}。"
                          '這是本批觀測；5pp不劣性判定另列。')
            anchor='<h2>品質</h2>'
            assert page.count(anchor)==1;page=page.replace(anchor,anchor+'<p id="quality-totals">'+html.escape(quality_note)+'</p>',1)
            original=(parent/'dashboard.html').read_text()
            data_pattern=r'<script type="application/json" id="data">(.*?)</script>'
            assert re.search(data_pattern,page,re.S).group(1)==re.search(data_pattern,original,re.S).group(1)
            (out/'dashboard.html').write_text(page)
            provenance=dict(parent_completion_sha256=serial.sha(parent/'completion.json'),
                unchanged_artifacts={name:digest for name,digest in completion['artifact_sha256'].items() if name!='dashboard.html'},
                original_dashboard_sha256=completion['artifact_sha256']['dashboard.html'],
                changes=['fixed desktop table columns and wrapping','short status label; original reason retained in title/payload',
                         'collapsed detailed timing/audit prose','visible workload and existing aggregate quality counts'],
                original_desktop_observation=dict(width=728,header_widths=[47,67,63,63,65,33,358],speed_table_height=7012),
                statistics_unchanged=True,data_payload_unchanged=True,new_searches=0)
            serial.save(out/'presentation-provenance.json',provenance)
            for name,digest in provenance['unchanged_artifacts'].items():assert serial.sha(out/name)==digest
            state.update(status='ok',finished_epoch=time.time(),within_original_deadline=time.time()<=serial.DEADLINE,
                target_reached=False,validation_status=completion['validation_status'],
                parent_completion_sha256=provenance['parent_completion_sha256'],
                artifact_sha256={p.name:serial.sha(p) for p in out.iterdir()},
                scope='presentation repair only; all statistics/CSV/PNG/PDF and frozen data payload retained byte-exact from checked parent; actual browser review still required')
    except BaseException as error:
        state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
    finally:serial.save(out/'completion.json',state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--parent',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();main(a.parent,a.out)
