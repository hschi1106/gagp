"""Publish the completed day24 matrix without mixing longer pilot rows."""
from argparse import ArgumentParser
import csv
import json
import math
from pathlib import Path
import shutil
import statistics

from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, read, save, sha
from run import validate, POPS, MODES

BASE = Path(__file__).resolve().parent


def median(values):
    return statistics.median(values) if values else None


def write_csv(path, rows):
    with path.open("w", newline="") as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0]),lineterminator="\n")
        writer.writeheader();writer.writerows(rows)


def collect(matrix):
    m=read(matrix/"manifest.json")
    assert m.get("requested_phase_complete") and m["stage"]=="complete"
    assert m["selection"] is not None
    assert sha(matrix/"bench")==m["binary_sha256"]
    assert sha(BASE/"run.py")==m["runner_sha256"]
    assert sha(BASE/"quality_data.py")==m["fixture_helper_sha256"]
    assert m["settings"]["GAGP_GPU_PAYLOAD_WORKSPACE_MIB"]=="16384"
    assert len(m["inputs"])==30
    for path,expected in m["runtime_source_sha256"].items():
        assert sha(Path(path))==expected
    g=m["selection"]["speed_generations"];q=m["selection"]["quality_generations"]
    speeds={(r["task"],r["population"],r["mode"]):r for r in m["speed"].values() if r["generations"]==g}
    qualities={(r["task"],r["seed"],r["mode"]):r for r in m["quality"].values() if r["generations"]==q}
    assert len(speeds)==300 and len(qualities)==300
    speedrows=[]
    for task in PAPER_TASKS:
        for p in POPS:
            a=speeds[task,p,"asgp_1t"];b=speeds[task,p,"gpu_overlap"]
            ok=a["status"]==b["status"]=="ok"
            for record in (a,b):
                if record["status"]=="ok":
                    validate(read(Path(record["output"])),p,g,record["mode"],1024)
            speedrows.append(dict(task=task,population=p,generations=g,cases=1024,
                asgp_status=a["status"],gpu_status=b["status"],
                asgp_s=a.get("evolve_wall_s"),gpu_s=b.get("evolve_wall_s"),
                speedup=a["evolve_wall_s"]/b["evolve_wall_s"] if ok else None,
                asgp_process_s=a.get("process_s"),gpu_process_s=b.get("process_s"),
                sampled_device_peak_gib=(b.get("sampled_device_peak_bytes") or 0)/1024**3,
                cpu_mismatches=b.get("cpu_audit_mismatches"),
                asgp_output=a["output"],gpu_output=b["output"]))
    qualityrows=[];curves={};details={}
    for task in PAPER_TASKS:
        inputs=m["inputs"][task]
        n=inputs["split"]["train_count"];nt=inputs["split"]["test_count"]
        train=read(Path(inputs["quality"]["0"]["source"]))["cases"]
        test=read(Path(inputs["test_source"]))["cases"]
        from quality_data import identity
        assert not {identity(r) for r in train}&{identity(r) for r in test}
        assert n==len(train) and nt==len(test)
        for seed in range(5):
            for mode in MODES:
                r=qualities[task,seed,mode]
                ok=r["status"]=="ok" and all(r.get(split+"_audit",{}).get("status")=="ok" for split in ("train","test"))
                row=dict(task=task,mode=mode,seed=seed,population=1024,generations=q,
                    nfe=1024*(q+1),train_cases=n,test_cases=nt,
                    search_status=r["status"],audit_status="ok" if ok else "incomplete_or_failed",
                    final_train_accuracy=None,final_test_accuracy=None,
                    final_train_wrong=None,final_test_wrong=None,
                    final_test_runtime_errors=None,final_test_loss=None,
                    initial_test_accuracy=None,initial_success=None,success=None,
                    evolve_wall_s=r.get("evolve_wall_s"),output=r["output"])
                if ok:
                    validate(read(Path(r["output"])),1024,q,mode,n)
                    tr=read(Path(r["train_audit"]["output"]));te=read(Path(r["test_audit"]["output"]))
                    assert tr["cases"]==n and te["cases"]==nt
                    assert len(tr["history"])==len(te["history"])==q
                    tf=tr["final"];vf=te["final"]
                    success=tf["wrong"]==0 and vf["wrong"]==0
                    initial=tr["history"][0]["wrong"]==0 and te["history"][0]["wrong"]==0
                    row.update(final_train_accuracy=tf["exact_accuracy"],final_test_accuracy=vf["exact_accuracy"],
                        final_train_wrong=tf["wrong"],final_test_wrong=vf["wrong"],
                        final_test_runtime_errors=vf["runtime_errors"],
                        final_test_loss=vf.get("capped_l1_mean",vf.get("exact_loss_mean")),
                        initial_test_accuracy=te["history"][0]["exact_accuracy"],
                        initial_success=initial,success=success)
                    key=task+"."+mode+"."+str(seed)
                    curves[key]=dict(train=[x["exact_accuracy"] for x in tr["history"]]+[tf["exact_accuracy"]],
                        test=[x["exact_accuracy"] for x in te["history"]]+[vf["exact_accuracy"]])
                qualityrows.append(row)
        details[task]=dict(split=inputs["split"],asgp={},gpu={})
        for mode,label in (("asgp_1t","asgp"),("gpu_overlap","gpu")):
            group=[r for r in qualityrows if r["task"]==task and r["mode"]==mode]
            valid=[r for r in group if r["audit_status"]=="ok"]
            details[task][label]=dict(audited=len(valid),successes=sum(r["success"] for r in valid),
                initial_successes=sum(r["initial_success"] for r in valid),
                evolved_successes=sum(r["success"] and not r["initial_success"] for r in valid),
                median_test_accuracy=median([r["final_test_accuracy"] for r in valid]),
                seeds=[dict(seed=r["seed"],status=r["audit_status"],success=r["success"],
                    initial=r["initial_success"],accuracy=r["final_test_accuracy"]) for r in group])
        for split in ("train","test"):
            for mode,label in (("asgp_1t","asgp"),("gpu_overlap","gpu")):
                runs=[curves[task+"."+mode+"."+str(seed)][split] for seed in range(5)
                      if task+"."+mode+"."+str(seed) in curves]
                details[task][label][split+"_median_curve"]=[
                    median([values[i] for values in runs]) for i in range(q+1)] if runs else []
    bypop=[]
    for p in POPS:
        group=[r for r in speedrows if r["population"]==p]
        values=[r["speedup"] for r in group if r["speedup"] is not None]
        bypop.append(dict(population=p,qualified=len(values),faster=sum(v>1 for v in values),
            median=median(values),geomean=math.exp(statistics.mean(map(math.log,values))) if values else None))
    summary=dict(speed_generations=g,quality_generations=q,workspace_mib=16384,
        speed_cells=150,speed_qualified=sum(r["speedup"] is not None for r in speedrows),
        quality_runs=300,quality_audited=sum(r["audit_status"]=="ok" for r in qualityrows),
        formal_speed_peak_gib=max(r["sampled_device_peak_gib"] for r in speedrows),
        sampled_device_peak_gib=max([0]+[r.get("sampled_device_peak_bytes",0) for r in m["jobs"].values()])/1024**3,
        elapsed_h=m["elapsed_s"]/3600,deadline_epoch=m["deadline_epoch"],
        binary_sha256=m["binary_sha256"],source_head=m["source_head"],
        by_population=bypop,speed_rows=speedrows,quality_rows=qualityrows,quality_details=details)
    return m,summary


def main():
    parser=ArgumentParser(description=__doc__)
    parser.add_argument("--matrix",type=Path,default=Path("logs/day24-20261017"))
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    m,s=collect(args.matrix)
    args.out.mkdir(parents=True,exist_ok=False)
    save(args.out/"summary.json",s)
    write_csv(args.out/"speed.csv",s["speed_rows"]);write_csv(args.out/"quality.csv",s["quality_rows"])
    for source,name in [(args.matrix/"environment.json","environment.json"),(BASE/"protocol.json","protocol.json"),
                        (BASE/"validation.json","validation.json"),
                        (args.matrix/"asgp-source-hashes.json","asgp-source-hashes.json")]:
        shutil.copy2(source,args.out/name)
    save(args.out/"provenance.json",dict(manifest=str(args.matrix/"manifest.json"),
        manifest_sha256=sha(args.matrix/"manifest.json"),binary_sha256=m["binary_sha256"],
        source_head=m["source_head"],runner_sha256=m["runner_sha256"],
        report_sha256=sha(Path(__file__)),template_sha256=sha(BASE/"dashboard.template.html"),
        fixture_helper_sha256=m["fixture_helper_sha256"],runtime_source_sha256=m["runtime_source_sha256"],
        external_asgp_source_manifest_sha256=sha(args.matrix/"asgp-source-hashes.json")))
    save(args.out/"dataset-splits.json",{t:m["inputs"][t]["split"] for t in PAPER_TASKS})
    lines=["# 單機24小時：ASGP／GAGP多代速度與品質","",
        f"速度矩陣G{s['speed_generations']}：合格{s['speed_qualified']}/150格；品質P1024/G{s['quality_generations']}：完成獨立CPU判題{s['quality_audited']}/300 runs。","",
        f"控制器總時間{s['elapsed_h']:.2f}小時。所有P使用同一binary與16GiB workspace；整卡NVML取樣最高{s['sampled_device_peak_gib']:.2f}GiB，包含桌面、可能漏短尖峰。","",
        "## Population加速比","",
        "| P | 合格題數 | 快於ASGP | 倍率中位數 | 幾何平均 |",
        "| --- | ---: | ---: | ---: | ---: |"]
    for r in s["by_population"]:
        lines.append(f"| {r['population']} | {r['qualified']}/30 | {r['faster']}/30 | {r['median']:.2f}× | {r['geomean']:.2f}× |" if r["qualified"] else f"| {r['population']} | 0/30 | — | — | — |")
    lines+=["","## 全題探索性品質","",
        "兩方法使用相同訓練／測試切分及配對初代／variation seeds0–4。成功需要final training-selected candidate在train及input-disjoint test均exact全對。沒有以test挑選candidate，沒有simplification。括號為gen0已成功的runs數，避免把initializer成功誤稱演化收益。","",
        "| 題目 | Train/Test unique | ASGP成功/5（初始） | GPU成功/5（初始） | ASGP test accuracy中位數 | GPU test accuracy中位數 |",
        "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for task,d in s["quality_details"].items():
        a,b=d["asgp"],d["gpu"]
        acc=lambda x:f"{100*x:.1f}%" if x is not None else "—"
        lines.append(f"| {task} | {d['split']['train_count']}/{d['split']['test_count']} | {a['successes']}/5 ({a['initial_successes']}) | {b['successes']}/5 ({b['initial_successes']}) | {acc(a['median_test_accuracy'])} | {acc(b['median_test_accuracy'])} |")
    lines+=["","## 設定與解讀","",
        "- 全部GAGP採whole AST2048、physical depth24、每phase1024、GPU繁殖、CPU1T。ASGP logical phase depth7、operator/RNG、ranking及resource units等差異保留，fully_aligned=false。",
        "- ASGP比較組是benchmark search_asgp_1t：原生Score／operators順序執行、elite1、每輪重評完整population；不是含cross-generation program-fitness cache的stock Run入口。倍率只相對這個公開1T控制組。",
        "- 速度：完整1024cases、seed0、每格單次cold process；evolve_wall_ms包含呼叫內初始化／admission、演化、最後評分與清理。CLI解析／匯入準備、export與獨立CPU audit另列process wall；不能稱完整CLI加速或純硬體加速。",
        "- 品質採本地ASGP BuildTask domain，data seed20261017；不是official PSB fixture，也不是原論文100run／500000NFE重現。每題unique train最多200、test最多1000，input overlap=0；有限域見dataset-splits.json。其heldout_outside_training_support欄位實際計算test輸入未出現在4096筆原始training pool的數量，不能僅依此判定分布外推。",
        "- 品質只比較P1024，不能從此判斷不同population的收斂優劣；每題五個seeds只屬探索性，沒有品質等價／非劣性宣稱。Test曲線為事後判題的training-selected champion，不向搜尋回饋。",
        "- 每次GPU搜尋先以CPU核對first16 final programs；品質的每代champion及final candidate另用各自原生CPU表示對指定train/test做exact判題，post-search去重只節省重複audit。未做新的全population CPU重算，不能宣稱全部個體parity。",
        "- 每題native錯誤／timeout仍照原契約計入fitness；程序失敗、資源限制、未完成與未解題分開記錄。失敗不得提供合格倍率，成功率分母保留預訂五次。",
        "- 舊G4與1024-node資料只用作時間預測／歷史背景；正式分子分母皆來自本輪選定設定。時間pilot只用耗時／資源選規模；匹配正式設定的pilot作該格唯一sample，較大pilot不混入縮小後主表。","",
        "[重現與範圍](../README.md) · [互動表](dashboard.html) · [速度CSV](speed.csv) · [品質CSV](quality.csv) · [速度PNG](speedup.png) · [品質PNG](quality.png) · [來源](provenance.json)",""]
    (args.out/"REPORT.zh-TW.md").write_text("\n".join(lines))
    html=(BASE/"dashboard.template.html").read_text().replace("__DATA__",json.dumps(s,separators=(",",":")).replace("<","\\u003c"))
    (args.out/"dashboard.html").write_text(html)
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    values=np.array([[next(r["speedup"] for r in s["speed_rows"] if r["task"]==t and r["population"]==p) or np.nan for p in POPS] for t in PAPER_TASKS])
    fig,ax=plt.subplots(figsize=(8,12));image=ax.imshow(np.log2(values),aspect="auto",cmap="RdYlGn",vmin=-2,vmax=6)
    ax.set_yticks(range(30),[t.replace("_"," ").title() for t in PAPER_TASKS],fontsize=8)
    ax.set_xticks(range(5),[str(p) for p in POPS])
    for i in range(30):
        for j in range(5):
            value=values[i,j];ax.text(j,i,f"{value:.1f}x" if math.isfinite(value) else "NA",ha="center",va="center",fontsize=8)
    ax.set_title(f"ASGP1T / GPU-er-o; full1024 cases; G{s['speed_generations']}+final\nUniform2048 nodes /16GiB; one cold sample",fontsize=10)
    fig.colorbar(image,ax=ax,label="log2(speedup)");fig.tight_layout()
    fig.savefig(args.out/"speedup.png",dpi=180);fig.savefig(args.out/"speedup.pdf");plt.close(fig)
    fig,axes=plt.subplots(6,5,figsize=(16,16))
    for ax,(task,d) in zip(axes.flat,s["quality_details"].items()):
        for label,color in (("asgp","#b37436"),("gpu","#177d68")):
            curve=d[label]["test_median_curve"]
            if curve:ax.plot(range(len(curve)),curve,label=label,color=color,lw=1.4)
        ax.set_title(task.replace("_"," ").title(),fontsize=8);ax.set_ylim(-.03,1.03);ax.grid(alpha=.2)
    axes[0,0].legend(fontsize=7)
    fig.suptitle("Held-out exact accuracy of training-selected champions / median of5 seeds\nP1024; native CPU post-search audits; no test feedback",fontsize=12)
    fig.tight_layout(rect=(0,0,1,.96));fig.savefig(args.out/"quality.png",dpi=150);fig.savefig(args.out/"quality.pdf");plt.close(fig)
    print(json.dumps({k:v for k,v in s.items() if k not in ("speed_rows","quality_rows","quality_details")},indent=2))


if __name__=="__main__":
    main()
