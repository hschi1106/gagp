"""Audit per-seed evidence and report missing/failed attempts without hiding them."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

HERE = Path(__file__).resolve().parent


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wilson(successes: int, runs: int) -> list[float] | None:
    if runs == 0:
        return None
    z = 1.959963984540054
    p = successes / runs
    denominator = 1 + z*z/runs
    center = (p + z*z/(2*runs)) / denominator
    radius = z * math.sqrt(p*(1-p)/runs + z*z/(4*runs*runs)) / denominator
    return [max(0.0, center-radius), min(1.0, center+radius)]


def summarize(records: list[dict], planned_seeds: list[int], task: dict) -> dict:
    seen = set()
    for r in records:
        if r["seed"] in seen or r["seed"] not in planned_seeds or r["task"] != task["id"]:
            raise ValueError("Duplicate, unexpected, or misattributed seed")
        seen.add(r["seed"])
    completed = [r for r in records if r["status"] == "completed"]
    for r in completed:
        if r.get("returncode") != 0 or not 1000 <= r["evaluations"] <= 500000:
            raise ValueError("Invalid completed result or exceeded NFE budget")
        if not all(type(r[k]) is bool for k in ("success", "train_solved", "raw_success")):
            raise ValueError("Invalid success flags")
    successes = sum(r["success"] for r in completed)
    success_nodes = [r for r in completed if r["success"]]
    return {
        "task": task["id"], "scheme": task["scheme"], "paper_successes_out_of_100": task["paper_native"],
        "thesis_successes_out_of_25": task["paper_native"] if task["scheme"] == "DP" else None,
        "planned": len(planned_seeds), "completed": len(completed),
        "infrastructure_failures": len(records)-len(completed),
        "missing_seeds": sorted(set(planned_seeds)-seen),
        "successes": successes, "train_solved": sum(r["train_solved"] for r in completed),
        "raw_successes": sum(r["raw_success"] for r in completed),
        "overfit": sum(r["train_solved"] and not r["success"] for r in completed),
        "initial_population_successes": sum(r["success"] and r["evaluations"] == 1000 for r in completed),
        "wilson95": wilson(successes, len(completed)),
        "median_nfe": statistics.median(r["evaluations"] for r in completed) if completed else None,
        "solver_seconds": sum(r["seconds"] for r in completed),
        "mean_success_nodes_before": statistics.mean(r["nodes_before"] for r in success_nodes) if success_nodes else None,
        "mean_success_nodes_after": statistics.mean(r["nodes_after"] for r in success_nodes) if success_nodes else None,
        "train_cases": sorted({r["train_cases"] for r in completed}),
        "test_cases": sorted({r["test_cases"] for r in completed}),
    }


def validate_configuration(configuration: dict, protocol: dict, expected_profile: str) -> None:
    if expected_profile not in {"legacy", "count-aligned"}:
        raise ValueError("Unknown expected data profile")
    if (configuration["unified"] or configuration["data_profile"] != expected_profile or
            not configuration["simplify"] or configuration["evaluations"] != protocol["evaluations"] or
            configuration["base_sizes"] != [0] or configuration["data_seed"] != protocol["data_seed"] or
            configuration["threads"] != protocol["threads"]):
        raise ValueError("Study configuration differs from requested reproduction settings")


def collect(root: Path, study: Path, expected_profile: str = "legacy") -> dict:
    protocol = json.loads((HERE / "protocol.json").read_text())
    manifest = json.loads((study / "manifest.json").read_text())
    configuration = manifest["identity"]["configuration"]
    documents = json.loads((root / "documents.json").read_text())
    for document in documents:
        if sha(Path(document["path"])) != document["sha256"]:
            raise ValueError("Reference document changed")
    validate_configuration(configuration, protocol, expected_profile)
    binary = Path(configuration["exe"])
    if sha(binary) != manifest["identity"]["executable_sha256"]:
        raise ValueError("Executable changed after measurement")
    for rel, digest in manifest["identity"]["source_sha256"].items():
        if sha(root / "source" / rel) != digest or sha(study / "source" / rel) != digest:
            raise ValueError(f"Source changed: {rel}")
    dataset_rows = json.loads((root / "datasets.json").read_text())
    datasets = {r["task"]: r for r in dataset_rows if r["profile"] == expected_profile}
    for dataset in datasets.values():
        if sha(root / dataset["file"]) != dataset["sha256"]:
            raise ValueError("Exported dataset changed")
    expected_tasks = {r["id"]: r for r in protocol["tasks"]}
    if not set(configuration["tasks"]) <= set(expected_tasks):
        raise ValueError("Unexpected task")
    seeds = list(range(configuration["seed"], configuration["seed"]+configuration["runs"]))
    records = []
    for path in sorted(study.glob("*-base*-seed*.json")):
        r = json.loads(path.read_text())
        if path.name != f"{r['task']}-base0-seed{r['seed']}.json" or r["task"] not in configuration["tasks"]:
            raise ValueError(f"Misattributed run: {path}")
        r["record_sha256"] = sha(path)
        if r["status"] == "completed":
            stdout = path.with_suffix(".stdout.txt")
            stderr = path.with_suffix(".stderr.txt")
            r["stdout_sha256"], r["stderr_sha256"] = sha(stdout), sha(stderr)
            # Independently reparse the authoritative CLI summary, including flags.
            log = stdout.read_text()
            summary = re.search(r"^(\w+): ([01])/1 successful, ([01])/1 solved training, ([\d.]+)s total, (\d+) evaluations$", log, re.M)
            nodes = re.search(r"nodes (\d+) -> (\d+), rewrites (\d+)/(\d+), raw_test_success ([01])", log)
            counts = re.search(r"^data\s+(\d+) training, (\d+) test examples$", log, re.M)
            if not summary or not nodes or not counts:
                raise ValueError(f"Missing CLI evidence: {path}")
            actual = (summary[1], summary[2] == "1", summary[3] == "1", float(summary[4]), int(summary[5]),
                      int(nodes[1]), int(nodes[2]), nodes[5] == "1", int(counts[1]), int(counts[2]))
            expected = tuple(r[k] for k in ("task", "success", "train_solved", "seconds", "evaluations",
                                           "nodes_before", "nodes_after", "raw_success", "train_cases", "test_cases"))
            if actual != expected or (r["train_cases"], r["test_cases"]) != (datasets[r["task"]]["train"], datasets[r["task"]]["test"]):
                raise ValueError(f"CLI/dataset evidence differs from result: {path}")
        records.append(r)
    rows = [summarize([r for r in records if r["task"] == task], seeds, expected_tasks[task])
            for task in configuration["tasks"]]
    complete = all(r["completed"] == r["planned"] and not r["infrastructure_failures"] for r in rows)
    return {"format": "asgp-paper-results-v1", "scope": "native scheme quality reproduction; not exact paper replication",
            "complete": complete, "full_paper_native_scope": len(rows) == 30 and len(seeds) == 100 and complete,
            "protocol_sha256": sha(HERE / "protocol.json"), "manifest_sha256": sha(study / "manifest.json"),
            "source_identity": json.loads((root / "source-identity.json").read_text()),
            "documents": documents,
            "binary_sha256": sha(binary), "configuration": configuration, "limitations": protocol["limitations"],
            "datasets": [datasets[t] for t in configuration["tasks"]], "rows": rows, "records": records}


def reference_counts(row: dict, reference: str) -> tuple[int, int]:
    if reference == "bundled-thesis" and row["scheme"] == "DP":
        return row["thesis_successes_out_of_25"], 25
    if reference not in {"short-paper", "bundled-thesis"}:
        raise ValueError("Unknown comparison reference")
    return row["paper_successes_out_of_100"], 100


def page(result: dict) -> str:
    reference = result.get("comparison_reference", "short-paper")
    reference_title = "學位論文" if reference == "bundled-thesis" else "短版 paper"
    displayed = []
    for row in result["rows"]:
        successes, runs = reference_counts(row, reference)
        displayed.append(dict(row, reference_successes=successes, reference_runs=runs,
                              reference_title=reference_title))
    data = json.dumps(displayed, ensure_ascii=False).replace("<", "\\u003c")
    total = sum(r["completed"] for r in result["rows"])
    planned = sum(r["planned"] for r in result["rows"])
    return '''<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:var(--font-sans,system-ui);color:var(--foreground,#202b35);line-height:1.55;margin:0}
*{box-sizing:border-box}h2{font-size:23px;margin:0 0 10px}p{margin:8px 0 15px}.muted{color:var(--muted-foreground,#66727d)}
select{font:inherit;color:inherit;background:var(--background,white);border:1px solid var(--border,#ddd);padding:8px;border-radius:7px;max-width:100%}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:18px 0}.stat{background:var(--secondary,#f1f5f7);padding:16px;border-radius:var(--radius,10px)}
.big{font-size:26px;font-weight:650}.bars{margin:20px 0}.bar{height:24px;border-radius:4px;min-width:1px}.track{background:var(--secondary,#eef2f5);border-radius:4px;margin:5px 0 14px}
.note{background:var(--warning-surface,#fff5db);padding:16px;border-radius:var(--radius,10px);margin-top:20px;font-size:14px}
.table{overflow:auto;margin-top:22px}table{width:100%;border-collapse:collapse;font-size:13px}td,th{padding:8px 6px;border-bottom:1px solid var(--border,#ddd);text-align:left;white-space:nowrap}th{font-weight:600}
@media(max-width:450px){.stats{gap:6px}.big{font-size:22px}}</style>
<h2>ASGP：提供程式的復現結果</h2>
<p class="muted">''' + f'{len(result["rows"])} 題 · 已完成 {total}/{planned} 次搜尋 · 每次最多 500,000 NFE · population 1000 · 資料 {result["configuration"]["data_profile"]} · 對照 {reference_title}' + '''</p>
<label for="task">題目 </label><select id="task"></select><div class="stats"><div class="stat">簡化後成功<div class="big" id="success"></div></div>
<div class="stat">文獻成功<div class="big" id="paper"></div></div><div class="stat">初代已成功<div class="big" id="initial"></div></div></div>
<div id="detail" class="muted"></div><div class="bars"><div id="oursLabel"></div><div class="track"><div id="ours" class="bar" style="background:var(--chart-1,#257e9b)"></div></div>
<div id="paperLabel"></div><div class="track"><div id="published" class="bar" style="background:var(--chart-2,#bb8b48)"></div></div></div>
<div class="note">目前僅是作者程式的診斷結果，尚未建立能代表paper的比較baseline。短版 ASGP.pdf Table III 使用 /100；學位論文 Table 5.4 的相同 DP 成功數分母為 /25，本頁依所選文獻標示。產生表格的原始資料／seeds／設定無對照紀錄，C++ simplifier 也缺少 MATLAB symbolic stage。原版 unified 另含 sliding-window，未當作論文 DC&amp;DP。此處四執行緒時間不作為 ASGP-1T 加速比基準。</div>
<div class="table"><table><thead><tr><th>題目</th><th>完成</th><th>成功</th><th>文獻成功</th><th>95% CI</th></tr></thead><tbody id="rows"></tbody></table></div>
<script>const rows=''' + data + ''';const select=document.getElementById('task');const pct=v=>(100*v).toFixed(1)+'%';
rows.forEach((r,i)=>{const o=document.createElement('option');o.value=i;o.textContent=r.task;select.append(o);
const tr=document.createElement('tr');[r.task,r.completed+'/'+r.planned,r.successes+'/'+r.completed,r.reference_successes+'/'+r.reference_runs,r.wilson95?r.wilson95.map(pct).join(' – '):'待完成'].forEach(t=>{const td=document.createElement('td');td.textContent=t;tr.append(td)});document.getElementById('rows').append(tr)});
function render(){const r=rows[Number(select.value)];document.getElementById('success').textContent=r.successes+'/'+r.completed;
document.getElementById('paper').textContent=r.reference_successes+'/'+r.reference_runs;document.getElementById('initial').textContent=r.initial_population_successes;
document.getElementById('detail').textContent=r.scheme+' · train '+r.train_cases.join('/')+' / test '+r.test_cases.join('/')+' · 簡化前成功 '+r.raw_successes+'/'+r.completed+' · 簡化後過擬合 '+r.overfit+' · 基礎設施失敗 '+r.infrastructure_failures+(r.thesis_successes_out_of_25!==null?' · 學位論文 '+r.thesis_successes_out_of_25+'/25':'');
const rate=r.completed?r.successes/r.completed:0;document.getElementById('ours').style.width=pct(rate);document.getElementById('published').style.width=pct(r.reference_successes/r.reference_runs);
document.getElementById('oursLabel').textContent='復現 '+pct(rate)+(r.wilson95?'（Wilson 95% CI '+r.wilson95.map(pct).join(' – ')+'）':'');
document.getElementById('paperLabel').textContent=r.reference_title+' '+r.reference_successes+'/'+r.reference_runs+'（'+pct(r.reference_successes/r.reference_runs)+'）';}
select.addEventListener('change',render);select.value=String(Math.max(0,rows.findIndex(r=>r.task==='median')));render();
const saved=select.value;let selectionChecks=0;rows.forEach((r,i)=>{select.value=String(i);select.dispatchEvent(new Event('change'));
if(document.getElementById('success').textContent!==r.successes+'/'+r.completed)throw new Error('Selection result mismatch');selectionChecks++});
select.value=saved;select.dispatchEvent(new Event('change'));
console.log(JSON.stringify({tasks:rows.length,selectionChecks,options:select.options.length,overflow:document.documentElement.scrollWidth>innerWidth}));</script></html>'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("logs/asgp-paper-20261025"))
    parser.add_argument("--study", type=Path, required=True)
    parser.add_argument("--data-profile", choices=("legacy", "count-aligned"), default="legacy")
    parser.add_argument("--reference", choices=("short-paper", "bundled-thesis"), default="short-paper")
    parser.add_argument("--out", type=Path, default=HERE / "results")
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    result = collect(args.root.resolve(), args.study.resolve(), args.data_profile)
    if args.reference != "short-paper":
        result["comparison_reference"] = args.reference
    if args.require_complete and not result["complete"]:
        raise RuntimeError("Study incomplete; missing/failed seeds remain")
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "measurements.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n")
    with (args.out / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(result["rows"][0]))
        writer.writeheader()
        writer.writerows(result["rows"])
    (args.out / "index.html").write_text(page(result))
    print(json.dumps({"complete": result["complete"], "completed": sum(r["completed"] for r in result["rows"]),
                      "planned": sum(r["planned"] for r in result["rows"]), "out": str(args.out)}))


if __name__ == "__main__":
    main()
