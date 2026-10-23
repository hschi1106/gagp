"""Retain a complete slower development workload beside the formal result."""
import hashlib
import json
import math
from pathlib import Path


def checked_training_observation(path, root, tasks):
    path, root = Path(path), Path(root).resolve()
    observation = json.loads(path.read_text())
    source = (root / observation['source']).resolve()
    if not source.is_relative_to(root):
        raise ValueError('development evidence escaped repository')
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != observation['source_sha256']:
        raise ValueError('development source hash mismatch')
    rows = json.loads(source.read_text())
    if len(rows) != len(tasks) or {row['task'] for row in rows} != set(tasks):
        raise ValueError('development workload must contain every task once')
    ratios = []
    for row in rows:
        if row['population'] != 1024 or row['generations'] != 64:
            raise ValueError('development workload scope changed')
        if row['statuses'] != ['ok', 'ok'] or not all(row[key] for key in (
                'full_final_ast_equal', 'final_descriptors_equal', 'champion_ast_fitness_equal')):
            raise ValueError('development pair is incomplete or differs')
        baseline, candidate = row['search_s']
        if not all(math.isfinite(value) and value > 0 for value in (baseline, candidate)):
            raise ValueError('invalid development search time')
        ratio = baseline / candidate
        if not math.isclose(ratio, row['speedup'], rel_tol=1e-12):
            raise ValueError('development ratio differs from search times')
        ratios.append(ratio)
    mean = math.exp(sum(map(math.log, ratios)) / len(ratios))
    if not math.isclose(mean, observation['observed_geometric_ratio'], rel_tol=1e-12):
        raise ValueError('development geometric mean changed')
    return dict(observation=observation, cells=rows, checked_geometric_ratio=mean,
                source_sha256=digest,
                observation_sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def limitation_text(evidence):
    mean = evidence['checked_geometric_ratio']
    return (f'較少案例的長代數開發測試：先前 combined2 候選在全30題、P1024／G64、'
            f'19–200個舊training案例、seed0／單次對照只有 {mean:.3f}×，略慢於基準；'
            '全部配對最終AST、分數描述與逐代冠軍一致。這是不同工作量的一次開發觀察，'
            '不能用C1024速度矩陣的倍率取代，也不代表統計上已確認退步或新資料品質。'
            '若正式候選不同，這項觀察仍指先前combined2版本。')
