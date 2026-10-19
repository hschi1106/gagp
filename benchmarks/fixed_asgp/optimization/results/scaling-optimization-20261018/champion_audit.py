"""Post-search common-output CPU audit helpers; never select a new champion."""
import json
import math
import re


FIRST_PROGRAM = re.compile(r'^\s*\{\s*"programs"\s*:\s*\[\s*')


def first_native_program(path, chunk_size=65536, max_prefix_bytes=32*1024*1024):
    """Read the already selected first AST without loading the full population.

    Callers must first verify the complete original export's frozen SHA256.
    Native benchmark exports have exactly a programs array. This routine fails
    closed on a different top-level layout, empty list, or excessive AST prefix.
    """
    decoder = json.JSONDecoder()
    buffer = ''
    with open(path, encoding='utf-8') as stream:
        while len(buffer.encode('utf-8')) < max_prefix_bytes:
            chunk = stream.read(chunk_size)
            if not chunk:
                break
            buffer += chunk
            match = FIRST_PROGRAM.match(buffer)
            if match is None:
                # A truncated key/header may still need more bytes. Never scan
                # for a later matching key nested in a different JSON object.
                if len(buffer) > 128:
                    raise ValueError('native export header is not a programs array')
                continue
            start = match.end()
            try:
                program, end = decoder.raw_decode(buffer, start)
            except json.JSONDecodeError:
                continue
            if not isinstance(program, dict):
                raise ValueError('native final champion is not an AST object')
            trailing = buffer[end:].lstrip()
            if not trailing:
                continue
            if trailing[0] not in ',]':
                raise ValueError('invalid separator after selected AST')
            return program
    raise ValueError('missing, truncated, or oversized first native champion')


def validate_score(score, cases, backend):
    assert score['backend'] == backend
    assert score['cases'] == cases
    rows = score['case_results']
    assert len(rows) == cases and [row['case'] for row in rows] == list(range(cases))
    assert all(row['exact'] in (0, 1) and row['runtime_error'] in (0, 1) for row in rows)
    assert score['wrong'] == cases-sum(row['exact'] for row in rows)
    assert score['runtime_errors'] == sum(row['runtime_error'] for row in rows)
    assert all(math.isfinite(row['common_loss']) and row['common_loss'] >= 0 for row in rows)
    loss_name = 'exact_loss_sum' if 'exact_loss_sum' in score else 'capped_l1_sum'
    assert math.isclose(score[loss_name], sum(row['common_loss'] for row in rows), rel_tol=1e-10, abs_tol=1e-6)
    if backend == 'gagp_cpu_1t':
        assert score['grammar_admitted'] == 1


def validate_judgment(data, task, generations, cases, variant):
    assert data['format'] == 'fixed-quality-source-v1' and data['task'] == task
    assert data['cases'] == cases and len(data['cases_sha256']) == 64
    history = data['history']
    assert 1 <= data['unique_audited_programs'] <= generations+1
    assert [row['generation'] for row in history] == list(range(generations))
    backend = 'asgp_1t' if variant == 'asgp' else 'gagp_cpu_1t'
    for score in [data['final'], *history]:
        validate_score(score, cases, backend)
    initial = history[0]['wrong'] == 0
    final = data['final']['wrong'] == 0
    success_generations = [row['generation'] for row in history if row['wrong'] == 0]
    if final:
        success_generations.append(generations)  # Final evaluation after G reproductions.
    return dict(gen0_wrong=history[0]['wrong'], final_wrong=data['final']['wrong'],
        initial_success=initial, final_success=final,
        first_success_generation=min(success_generations) if success_generations else None,
        final_success_after_gen0=(not initial and final),
        initial_success_lost=(initial and not final),
        unique_audited_programs=data['unique_audited_programs'], cases_sha256=data['cases_sha256'],
        metric='independent common projected exact output / capped-L1 CPU audit; distinct from raw search fitness',
        backend=backend)
