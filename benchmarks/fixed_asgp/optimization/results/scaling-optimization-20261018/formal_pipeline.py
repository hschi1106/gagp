"""Wait for final regression/diagnosis, freeze once, and run the fresh matrix.

No tuning or retries. Uses the predeclared selection and timing allocation rule.
"""
from argparse import ArgumentParser
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import sys
import time
import serial
from development import alive, gpu_lock
import freeze_formal
import formal


def append(path, text):
    with Path(path).open('a') as stream:
        stream.write(text)


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--attempt', type=int, default=0)
    parser.add_argument('--operator-stage', default='operator-diagnosis-attempt0')
    args = parser.parse_args()
    suffix = '' if args.attempt == 0 else f'-attempt{args.attempt}'
    path = serial.RAW/f'formal-pipeline{suffix}-controller.json'
    assert not path.exists(), 'pipeline exists; inspect and preserve prior attempt'
    assert not (serial.RAW/'formal-freeze.json').exists(), 'formal already frozen'
    record = dict(pid=os.getpid(), status='waiting_final_build_and_diagnosis',
                  started_epoch=serial.START, deadline_epoch=serial.DEADLINE)
    serial.save(path, record)
    try:
        prerequisites = ('final-build-attempt0', args.operator_stage)
        while True:
            statuses = []
            for name in prerequisites:
                item = serial.RAW/name/'manifest.json'
                current = serial.read(item) if item.exists() else {}
                statuses.append(current if current.get('pid') and not alive(current['pid']) else None)
            if all(statuses):
                break
            assert time.time() < serial.DEADLINE-16*3600, 'formal start cutoff reached; preserve partial work'
            time.sleep(15)
        assert statuses[0]['status'] == 'ok' and statuses[0]['returncodes'] == [0, 0, 0], 'final build/regressions failed'
        # Diagnostic row failures remain failures. A stopped harness/build is
        # different and must be inspected before starting the formal matrix.
        assert statuses[1]['status'] == 'requested_phase_complete', 'diagnosis harness stopped; inspect cause'
        selection = serial.read(serial.RAW/'compact-root-selection.json')
        assert selection['selected_compact_root'] is False and selection['valid'], 'unexpected candidate decision'
        assert len(selection['comparisons']) == 20 and all(row['valid'] for row in selection['comparisons'])
        assert selection['geometric_mean'] < 1.03, 'do not override frozen acceptance gate'
        assert (serial.RAW/'compact-root-rejection/manifest.json').exists(), 'rejected prototype archive missing'
        sys.argv = ['freeze_formal.py', '--binary-provenance', str(serial.RAW/'final-build-attempt0/manifest.json'), '--attempt', str(args.attempt)]
        freeze_formal.main()
        frozen = serial.read(serial.RAW/'formal-freeze.json')
        stamp = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')
        text = (f'\n\n正式凍結（{stamp}）：全30題 × P1024/16384 × ASGP1T/原GPU/tuple優化GPU，'
                f'共同G{frozen["generations"]}、每格{frozen["repetitions"]}次同seed0冷程序量測，'
                f'共{frozen["planned_runs"]}筆；完整程序／雙GPU全最終population CPU核對預估'
                f'{frozen["forecast_s"]/3600:.3f}小時（含1.2倍margin），另保留4小時診斷／報告。'
                f'Block512、CPU1T、GPU評分及繁殖、16GiB、physical2048/depth24/phase1024。'
                f'compact-root已因平均僅0.9%而淘汰移除；正式binary SHA256 {frozen["binary_sha256"]}。'
                '全部ASGP分母新量測；重複數只衡量計時波動，不當作多seed品質樣本。'
                '原始formal-freeze.json／formal-time-forecast.json與所有失敗保留。\n')
        append(serial.BASE/'README.md', text)
        append(serial.BASE/'RECOVERY.md', text+f'\nACTIVE formal pipeline PID{os.getpid()}，同PID直接執行formal.main，另寫formal-controller.json。先查實際PID/child與formal/manifest.json，不能重複啟動；所有正式hash／settings／scripts不可改。流程在requested_phase_complete後仍待報告與champion CPU judges，並非交付完成。\n')
        progress = serial.ROOT/'benchmarks/fixed_asgp/optimization/PROGRESS.md'
        current = progress.read_text()
        separator = current.index('\n---')
        progress.write_text(current[:separator]+text+current[separator:])
        with gpu_lock():
            snapshot = serial.RAW/'formal-source-at-launch'
            snapshot.mkdir()
            for name in frozen['harness_sha256']:
                shutil.copy2(name, snapshot/Path(name).name)
            record.update(status='formal_frozen_running', generations=frozen['generations'],
                repetitions=frozen['repetitions'], planned_runs=frozen['planned_runs'],
                forecast_s=frozen['forecast_s'], binary_sha256=frozen['binary_sha256'])
            serial.save(path, record)
        sys.argv = ['formal.py']
        formal.main()
        record.update(status='formal_requested_phase_complete', finished_epoch=time.time())
    except Exception as error:
        record.update(status='stopped', error=repr(error), finished_epoch=time.time())
        raise
    finally:
        serial.save(path, record)


if __name__ == '__main__':
    main()
