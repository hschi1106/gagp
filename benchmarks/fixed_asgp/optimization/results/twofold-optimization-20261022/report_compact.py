"""Build a new report from immutable formal matrices, keeping all failed cells."""
from argparse import ArgumentParser
import csv
import html
import fcntl
import json
import math
from pathlib import Path
import time
import traceback

import report_data
import report_attempt_notes
import native_recovery_evidence
import development_evidence
import process_timing
import quality_timing
import development_limits2 as development_limits
import report_validation4 as report_validation
import report_views_compact as report_views
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
        'report_data','report_validation','report_validation2','report_validation3','report_validation4','report_views_compact','report_qualification',
        'reference_reader','shared_cpu_reference','study_statistics','quality_metrics','dashboard_data',
        'development_limits','development_limits2','process_timing','quality_timing','report_attempt_notes','native_recovery_evidence')]
    source_paths += [serial.BASE/(name+'.py') for name in ('development_evidence','vm_pilot')]
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
            longrun=quality_timing.summarize(quality['runs'],quality['pairs'],quality_freeze['tasks'],
                quality_freeze['seeds'],quality_freeze['population'],quality_freeze['generations'])
            process=process_timing.summarize(speed['runs'],speed['summary']['cells'],
                speed_freeze['tasks'],speed_freeze['populations'],speed_freeze['repetitions'])
            recovery=report_attempt_notes.recovery(speed_freeze,quality_freeze,evidence)
            native_recovery=native_recovery_evidence.verify(evidence)
            serial.save(out/'native_recovery_evidence.json',native_recovery)
            native_cost=report_attempt_notes.native_costs(speed['native_audits'])
            interrupted=[dict(task=row['task'],population=row['population'],variant=row['variant'],
                status=row['status'],process_s_lower_bound=row.get('process_s'),
                recovery=row.get('recovery')) for row in speed['native_audits']
                if row.get('native_execution_coverage_unknown')]
            serial.save(out/'interrupted_native_audits.json',interrupted)
            # The selection data are an appendix, never substitute formal cells
            # or supply a denominator/reference for the fresh validation.
            selected=speed_freeze['candidate_selection']['selected']
            development=development_evidence.read_families(
                [selected['stage_family']],serial.read(serial.BASE/'protocol.json'))
            chosen=[r for r in development['recipes'] if r['variant']==selected['variant']]
            assert len(chosen)==1 and chosen[0]['complete']
            for label,value in selected['geometric_means'].items():
                assert math.isclose(chosen[0]['geometric_means'][label],value,rel_tol=1e-12)
            for row in development['cells']:
                if row['variant']==selected['variant']:
                    assert row['candidate_binary_sha256']==speed_freeze['variants']['candidate']['binary_sha256']
            development['selected_recipe']=chosen[0]
            development['selection_bias_note']='used to select the global candidate; development G8/R1, not independent validation or a primary target qualification'
            validation=report_validation.validate(speed_freeze,quality_freeze,speed_name,quality_name,
                evidence,serial.read(regression_path))
            training_counts={row['task']:row['train_count'] for row in validation['input_splits']}
            longrun['training_case_count_by_task']=training_counts
            for row in longrun['tasks']+longrun['pairs']:
                row['training_cases']=training_counts.get(row['task'])
            if validation['status']!='ok':
                speed['summary']['all_primary_targets_reached']=False
                for group in speed['summary']['groups'].values():group['target_reached']=False
            bundle=dict(speed=speed,quality=quality,speed_freeze=speed_freeze,quality_freeze=quality_freeze,
                validation_status=validation['status'],report_epoch=time.time(),process_timing=process,
                empty_startup_recovery=recovery,native_call_cost=native_cost,native_recovery=native_recovery,
                selected_development=development)
            bundle['quality_search_timing']=longrun
            serial.save(out/'quality_timing.json',longrun)
            csv_file(out/'quality_timing_pairs.csv',longrun['pairs'])
            csv_file(out/'quality_timing_tasks.csv',longrun['tasks'])
            # These counts describe the checked paired traces, not a substitute
            # for success/noninferiority or full final-population AST coverage.
            trace=dict(expected_pairs=len(quality_freeze['tasks'])*len(quality_freeze['seeds']),
                identical_trace_pairs=sum(p['execution_fidelity'] is True for p in quality['pairs']),
                fully_audited_pairs=sum(p['complete_pair'] is True for p in quality['pairs']))
            bundle['quality_trace_summary']=trace
            native_scope_note=('速度矩陣的完整族群原生CPU／GPU核對，安排在每題／P／方法的第0次重複；'
                '後續重複保留CPU16抽查，以及兩方法間逐代冠軍、最終冠軍與族群分數的比較。'
                '後續重複只匯出最終冠軍，不能說其全部個體AST也完成原生核對。'
                '核對組數依原凍結範圍計算，不等於所有重複搜尋的數量。')
            target_gap_note=''
            timing_group=speed['summary']['groups']['pooled']
            timing_mean=timing_group.get('timing_observed_geometric_mean')
            if (timing_group['timing_qualified_cells']==timing_group['expected_cells']
                    and timing_mean is not None and 0<timing_mean<2):
                target_gap_note=(f'目前全題計時觀測為{timing_mean:.3f}×，未達2×。'
                    f'若固定本輪基準與工作量，候選版搜尋時間的幾何平均仍需再降低{100*(1-timing_mean/2):.1f}%才達標。'
                    '這是距離目標的算術換算，不是新優化已能取得的收益；完整核對不足時仍只能列計時觀測。')
            bundle['target_gap_note']=target_gap_note
            timing_only_note=''
            if any(g['timing_qualified_cells']!=g['qualified_cells'] for g in speed['summary']['groups'].values()):
                parts=[]
                for label,group in speed['summary']['groups'].items():
                    value=group['timing_observed_geometric_mean']
                    parts.append(('兩種P合併' if label=='pooled' else 'P='+label)+'：'+
                        ('—' if value is None else f'{value:.3f}×')+
                        f"（量測與配對檢查{group['timing_qualified_cells']}/{group['expected_cells']}格）")
                timing_only_note=('部分格的額外原生核對未齊，不能把上表已核對子集的平均當作全題平均。'
                    '另列尚未套用額外原生核對篩選的實際計時觀測：'+'；'.join(parts)+
                    '。這些數字不替代已核對結果或達標判定；未完成／失敗仍保留。')
            serial.save(out/'development_speed.json',development)
            csv_file(out/'development_speed_cells.csv',development['cells'])
            serial.save(out/'empty_startup_recovery.json',recovery)
            serial.save(out/'native_call_cost.json',native_cost)
            serial.save(out/'process_timing.json',process)
            csv_file(out/'speed_process_cells.csv',process['cells'])
            slower=[]
            for observation_name in ('long-run-training-observation16-0.json',
                                     'long-run-training-observation17-0.json'):
                path=serial.RAW/observation_name
                if not path.exists() and '17-0' in observation_name:
                    continue
                slower.append(development_limits.checked_training_observation(
                    path,serial.ROOT,speed_freeze['tasks']))
            bundle['slower_development_observations']=slower
            serial.save(out/'development_limits.json',slower)
            csv_file(out/'development_longrun_cells.csv',[
                dict(row,candidate_family=item['observation'].get('candidate_family','combined2'))
                for item in slower for row in item['cells']])
            serial.save(out/'summary.json',bundle);serial.save(out/'validation.json',validation)
            for filename,rows in [('speed_runs.csv',speed['runs']),('speed_cells.csv',speed['summary']['cells']),
                ('speed_native_audits.csv',speed['native_audits']),('quality_runs.csv',quality['runs']),
                ('quality_pairs.csv',quality['pairs']),('quality_tasks.csv',quality['tasks'])]:csv_file(out/filename,rows)
            limitations=[development_limits.limitation_text(item) for item in slower]
            source_repair=validation['source_regressions'].get('recovery')
            if source_repair is not None:
                assert source_repair['kind']=='missing_ignored_historical_doc_fixture'
                assert source_repair['cpp_unchanged'] and source_repair['prior_failure_preserved']
                limitations.append('同一候選來源的乾淨Release建置通過；首次完整原生測試為135/136，唯一失敗是隔離來源快照缺少文件連到的歷史manifest。補入原工作樹中雜湊相同的實際檔案後，repository contract定向重測1/1通過；C++、已建置binary、測試與規格均未改動。原失敗與補檔證明保留；這不是一次新的136/136全套通過。')
            if interrupted:
                limitations.append(f'完整族群核對有{len(interrupted)}組因程序中斷而無法判定，保留為interrupted，未重跑。其他尚未開始的核對使用原binary、輸入與設定接續；所有已完成搜尋均未重跑。中斷核對的子程序耗時只有最後觀測下限，實際成本與已執行個體數未知；不能列為核對通過。詳見interrupted_native_audits.json與原始native-resume1/2紀錄；第一次恢復腳本在啟動任何核對前因PID狀態別名錯誤退出，第二次只修正狀態複製，保留第一次失敗。')
            recovery_note=''
            if recovery is not None:
                recovery_note='備援0在資料生成與樣本執行前，因啟動記錄的文字不符檢查而停止。原始紀錄保留；備援1修正記錄標記，版本、輸入、截止時間、資源與P/G/R/種子逐項一致，沒有重抽或替換任何樣本。核對證明見empty_startup_recovery.json。'
                limitations.append(recovery_note)
            native_cost_note=f"額外原生核對包含GPU再評分及CPU參考。呼叫耗時（含Python／JSON與等待）有{native_cost['elapsed_recorded_calls']}/{native_cost['expected_calls']}次紀錄，合計{native_cost['known_elapsed_s']/3600:.2f}h；控制器CPU時間有{native_cost['controller_recorded_calls']}/{native_cost['expected_calls']}次紀錄，合計{native_cost['known_controller_cpu_s']/3600:.2f}h，可能與子程序wall重疊，不能直接相加。成本覆蓋不等於核對成功覆蓋。"
            limitations.append(native_cost_note)
            limitations.append('本輪未啟用搜尋內CPU修正；額外CPU核對不回寫分數或族群。')
            reference_note='P＝每代個體數；G＝演化代數；C＝評分案例數。基準是上一個最快GAGP，候選是本輪凍結版本；加速比＝基準搜尋秒數÷候選搜尋秒數，大於1才是變快。'
            markdown=report_views.markdown(bundle)
            markdown=markdown.replace('# GAGP 全30題額外加速與品質驗證','# GAGP 全30題額外加速與品質驗證\n\n'+reference_note,1)
            if target_gap_note:
                markdown=markdown.replace('## 本輪採用的機制',target_gap_note+'\n\n## 本輪採用的機制',1)
            longrun_value=longrun['all_task_geometric_mean']
            case_label='各題實際training案例數'
            if len(training_counts)==len(quality_freeze['tasks']):
                case_values=list(training_counts.values())
                case_label=f'C{min(case_values)}–{max(case_values)}'
            longrun_note=(f"同一批新資料的長代數搜尋（P{longrun['population']}／G{longrun['generations']}／{case_label}）："+
                ('完整全題倍率尚未齊全' if longrun_value is None else f'{longrun_value:.3f}×')+
                f"；已完成搜尋及冠軍核對的配對{longrun['audited_pairs']}/{longrun['expected_pairs']}，未知{longrun['unknown_pairs']}。"
                f"這個工作量的案例數與代數不同，獨立列示，不替代C{speed_freeze['cases']}／G{speed_freeze['generations']}主速度矩陣；案例數見validation.json。")
            markdown=markdown.replace('## 演化品質\n','## 演化品質\n\n'+longrun_note+
                '\n\n[逐題長代數速度](quality_timing_tasks.csv)、[配對資料](quality_timing_pairs.csv)。\n',1)
            markdown=markdown.replace('完整最終族群有CPU參考核對：','第0次速度重複的完整最終族群有CPU參考核對：',1)
            markdown=markdown.replace('搜尋秒數與倍率均為合格格子的幾何平均。',native_scope_note+
                '\n\n搜尋秒數與倍率均為合格格子的幾何平均。',1)
            if timing_only_note:
                markdown=markdown.replace('## 本輪採用的機制',timing_only_note+'\n\n## 本輪採用的機制',1)
            markdown=markdown.replace('初始admission失敗、','初始admission覆蓋與失敗、')
            trace_note=(f"同一資料與種子的搜尋配對：{trace['identical_trace_pairs']}/{trace['expected_pairs']}對的"
                '逐代冠軍AST／分數、最終冠軍AST及完整族群分數描述一致；'
                f"其中{trace['fully_audited_pairs']}/{trace['expected_pairs']}對已完成兩版的全部train/test原生核對。"
                '最終族群分數描述一致，不代表品質矩陣匯出了所有個體的AST。'
                '這是本批配對軌跡的執行一致性證據；成功率及不劣性界限另列。')
            markdown=markdown.replace('## 演化品質\n','## 演化品質\n\n'+trace_note+'\n',1)
            markdown=markdown.replace('額外完整原生核對程序：','已觀測原生核對子程序合計：')
            markdown=markdown.replace('次冷啟動。','次獨立新程序。').replace('冷啟動重複倍率範圍','新程序重複倍率範圍').replace('冷啟動範圍是描述性變動','新程序重複倍率範圍是描述性變動')
            markdown=markdown.replace('搜尋wall time包含演化函式內的冷啟動admission、GPU初始化、逐代演化及最終族群還原；不包含輸入JSON讀取、結果JSON／AST檔案寫出及額外CPU核對，這些另計程序成本。','搜尋wall time涵蓋演化呼叫內的初始化／匯入驗證、GPU評分與繁殖、最終評分與族群還原；呼叫前的JSON解碼、grammar／AST準備，以及呼叫後的CPU16核對與匯出另計程序成本。')
            markdown=markdown.replace('## 本輪採用的機制',process_timing.markdown(process)+'\n## 本輪採用的機制',1)
            dev_means=chosen[0]['geometric_means']
            dev_note=(f"舊開發資料、全30題／C1024／G8／每方法1次：P1024為{dev_means['1024']:.3f}×，"
                f"P16384為{dev_means['16384']:.3f}×，兩種P合併為{dev_means['pooled']:.3f}×。"
                '完整最終AST、分數描述與逐代冠軍一致，搜尋內CPU16抽查通過。這批資料用於選定版本，'
                '有選擇偏差；不是新驗證，也未完成全部最終族群的原生CPU核對。不能替代上表的驗證倍率或2×達標證據。')
            markdown=markdown.replace('## 本輪採用的機制','## 版本選擇用的開發結果\n\n'+dev_note+
                '\n\n[全60格開發速度](development_speed_cells.csv)、[來源與完整性核對](development_speed.json)。\n\n## 本輪採用的機制',1)
            planning_note=''
            if speed_freeze.get('planning_mode')=='best_effort_after_budget_refusal':
                planning_note='本輪是在原保守成本預估拒絕後執行的盡力驗證：仍規劃完整全30題、兩種P、G8/R2與10個品質種子，但不保證在預算內全部完成，也不宣稱符合原規格的完整達標；未執行與核對缺失均保留。'
                markdown=markdown.replace('## 本輪採用的機制',planning_note+'\n\n## 本輪採用的機制',1)
            markdown=markdown.replace('## 成本與限制\n','## 成本與限制\n\n'+'\n\n'.join(limitations)+
                '\n\n[長代數開發逐題資料](development_longrun_cells.csv)及[核對來源](development_limits.json)。\n',1)
            (out/'REPORT.zh-TW.md').write_text(markdown)
            dashboard=report_views.dashboard(bundle)
            dashboard=dashboard.replace('<h1>GAGP 全30題加速與品質</h1>','<h1>GAGP 全30題加速與品質</h1><p id="reference">'+html.escape(reference_note)+'</p>',1)
            if target_gap_note:
                dashboard=dashboard.replace('<h2>速度</h2>','<p id="target-gap">'+html.escape(target_gap_note)+'</p><h2>速度</h2>',1)
            dashboard=dashboard.replace('<h2>品質</h2>','<h2>品質</h2><p id="quality-search-timing">'+
                html.escape(longrun_note)+' <a href="quality_timing_tasks.csv">逐題速度CSV</a></p>',1)
            dashboard=dashboard.replace('<p id="scope"></p>','<p id="scope"></p><p class="note" id="native-scope">'+html.escape(native_scope_note)+'</p>',1)
            if timing_only_note:
                dashboard=dashboard.replace('<h2>速度</h2>','<p class="note" id="timing-only">'+html.escape(timing_only_note)+'</p><h2>速度</h2>',1)
            dashboard=dashboard.replace('<h2>品質</h2>','<h2>品質</h2><p id="quality-traces">'+html.escape(trace_note)+'</p>',1)
            details=('<details id="development-speed"><summary>版本選擇用的舊開發結果</summary><p class="note">'+html.escape(dev_note)+
                '</p><p><a href="development_speed_cells.csv">開發速度CSV</a></p></details>')
            dashboard=dashboard.replace('<h2>速度</h2>',details+'<h2>速度</h2>',1)
            if planning_note:
                dashboard=dashboard.replace('<h2>速度</h2>','<p class="note" id="planning-note">'+html.escape(planning_note)+'</p><h2>速度</h2>',1)
            process_labels=[]
            for label,group in process['groups'].items():
                value=group['observed_geometric_mean']
                process_labels.append(('兩種P合併' if label=='pooled' else 'P='+label)+'：'+('—' if value is None else f'{value:.2f}×')+f"（{group['qualified_cells']}/{group['expected_cells']}格）")
            timing_note='主要倍率是搜尋函式呼叫時間，未包含呼叫前的JSON／grammar／AST準備或返回後的CPU16核對與匯出。完整benchmark子程序倍率另列：'+'；'.join(process_labels)+'。獨立prepare命令與額外完整原生核對仍另計，程序倍率不替代2×搜尋目標。'
            dashboard=dashboard.replace('<h2>速度</h2>','<p class="note" id="timing-scope">'+html.escape(timing_note)+'</p><h2>速度</h2>',1)
            dashboard=dashboard.replace('<p id="bound"></p>','<p id="bound"></p><details id="limitations">'+
                '<summary>成本與限制（含未加速的工作量）</summary>'+''.join('<p class="note">'+html.escape(text)+
                '</p>' for text in limitations)+'<p><a href="development_longrun_cells.csv">長代數開發CSV</a> · '+
                '<a href="native_call_cost.json">完整核對成本</a> · <a href="empty_startup_recovery.json">啟動修正證明</a></p></details>',1)
            (out/'dashboard.html').write_text(dashboard)
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
