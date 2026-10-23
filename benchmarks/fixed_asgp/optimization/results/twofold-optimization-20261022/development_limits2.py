"""Display distinct complete G64 development candidates without mixing workloads."""
from development_limits import checked_training_observation


def limitation_text(evidence):
    mean = evidence['checked_geometric_ratio']
    family = evidence['observation'].get('candidate_family', 'combined2')
    counts = [row['cases'] for row in evidence['cells']]
    direction = '略慢於基準' if mean < 1 else '相對基準的開發倍率'
    return (f'較少案例的長代數開發測試：{family} 候選在全30題、P1024／G64、'
            f'{min(counts)}–{max(counts)}個舊training案例、seed0／單次對照為 {mean:.3f}×，{direction}；'
            '全部配對最終AST、分數描述與逐代冠軍一致。這是不同工作量的一次開發觀察，'
            '不能用C1024速度矩陣的倍率取代，也不代表統計上已確認退步或新資料品質。'
            '每項觀察只對應此處列出的開發候選。')
