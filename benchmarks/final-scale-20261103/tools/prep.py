"""Build inputs: per task a data pool (seed DATA_SEED), a 1024-case train split and a
disjoint held-out test split; per (task, seed) an ASGP-initialized P16384 population
(smaller populations are prefixes) translated into the task grammar.

Split rule: train = first 1024 pool train rows (duplicates kept, as the ASGP generator
produces them); test = unique pool test inputs not seen in train, up to 1000. If that
leaves fewer than 100 held-out inputs (finite input domain), 20% of the unique
inputs (hash order) are reserved for test and excluded from train before taking
1024 train rows."""
import hashlib, json, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent)); import common as c
from quality_data import identity, unique


def run(cmd, env, log):
    with open(log, "w") as f:
        r = subprocess.run([str(c.BIN), *map(str, cmd)], env=env, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode: raise RuntimeError(f"{cmd} failed, see {log}")


def split(pool):
    rows, test_pool = pool["train"], unique(pool["test"])
    keys = list(unique(rows + pool["test"]))
    reserved = set()
    train = rows[:c.TRAIN]
    seen = {identity(r) for r in train}
    test = [v for k, v in test_pool.items() if k not in seen]
    if len(test) < 100:
        order = sorted(keys, key=lambda k: hashlib.sha256(f"final-scale-20261103:{pool['task']}:{k}".encode()).hexdigest())
        reserved = set(order[:max(1, -(-len(order) // 5))])
        kept = [r for r in rows if identity(r) not in reserved]
        train = (kept * (c.TRAIN // len(kept) + 1))[:c.TRAIN]
        seen = {identity(r) for r in train}
        test = [v for k, v in unique(pool["test"] + pool["train"]).items() if k in reserved]
    test = test[:c.TEST]
    assert len(train) == c.TRAIN and test and not seen & {identity(r) for r in test}
    meta = dict(train_rows=len(train), train_unique=len(seen), test=len(test), domain_unique=len(keys),
                finite_reserve=len(reserved))
    return train, test, meta


def task_inputs(task):
    d = c.ROOT / "inputs" / task; d.mkdir(parents=True, exist_ok=True)
    env = c.base_env(task); env["GAGP_DATASET_SEED"] = str(c.DATA_SEED)
    pool = d / "pool.json"
    if not pool.exists(): run(["dataset", task, pool], env, d / "dataset.log")
    train, test, meta = split(c.read(pool))
    for name, cases in (("train", train), ("test", test)):
        c.save(c.split_source(task, name), dict(format="fixed-asgp-source-v1", task=task, population=[], cases=cases,
                                                 data_seed=c.DATA_SEED, split=name))
    c.save(d / "split_meta.json", meta)
    return task, meta


def seed_inputs(arg):
    task, seed = arg
    d = c.ROOT / "inputs" / task
    if c.prepared(task, seed).exists(): return arg, "cached"
    env = c.base_env(task); env.update(GAGP_FREEZE_POPULATION="16384", GAGP_FREEZE_POPULATION_SEED=str(seed))
    frozen = d / f"s{seed}.frozen.json"
    run(["freeze", task, frozen], env, d / f"s{seed}.freeze.log")
    doc = c.read(frozen); assert len(doc["population"]) == 16384
    doc.update(cases=c.read(c.split_source(task, "train"))["cases"], data_seed=c.DATA_SEED, split="train")
    c.save(c.source(task, seed), doc); frozen.unlink()
    env = c.base_env(task); env["GAGP_BM_CASE_COUNT"] = str(c.TRAIN)
    run(["prepare", c.source(task, seed), c.grammar(task), 16384, str(c.prepared(task, seed)) + ".tmp"], env, d / f"s{seed}.prepare.log")
    p = c.read(str(c.prepared(task, seed)) + ".tmp")
    assert len(p["programs"]) == 16384 and not p["admission_rejections"]
    Path(str(c.prepared(task, seed)) + ".tmp").replace(c.prepared(task, seed))
    return arg, "ok"


if __name__ == "__main__":
    tasks = sys.argv[1].split(",") if len(sys.argv) > 1 and sys.argv[1] != "all" else c.TASKS
    seeds = [int(s) for s in sys.argv[2].split(",")] if len(sys.argv) > 2 else c.SEEDS
    with ThreadPoolExecutor(10) as ex:
        for task, meta in ex.map(task_inputs, tasks): print(task, meta, flush=True)
    with ThreadPoolExecutor(int(sys.argv[3]) if len(sys.argv) > 3 else 10) as ex:
        for arg, st in ex.map(seed_inputs, [(t, s) for s in seeds for t in tasks]): print(arg, st, flush=True)
