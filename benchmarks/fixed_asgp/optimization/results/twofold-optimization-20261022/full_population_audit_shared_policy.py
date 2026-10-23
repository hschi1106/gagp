"""Apply the explicit disabled admission flag to both auxiliary snapshots only."""
import copy

import full_population_audit_shared_timed as admitted

validate = admitted.validate


def snapshot_policy(freeze):
    extra = freeze.get('native_audit_neutral_extra', {})
    if extra not in ({}, {'GAGP_FUSED_COLD_EXECUTABLE': '0'}):
        raise ValueError('unsupported auxiliary snapshot policy')
    result = copy.deepcopy(freeze)
    for variant in ('baseline', 'candidate'):
        original = result['variants'][variant]['extra']
        for key, value in extra.items():
            if key in original and original[key] != value:
                raise ValueError('cannot override an enabled search option')
            original[key] = value
    return result


def run(stage, search_job, inputs, freeze, reference=None):
    # Current cold_admission.hpp admits its shortcut only for the literal "1".
    # Absent and "0" are therefore the same disabled path. Pin "0" on BOTH
    # snapshots so the existing complete CPU-policy identity check can succeed;
    # no field is removed from that proof and no search command is changed.
    return admitted.run(stage, search_job, inputs, snapshot_policy(freeze), reference)
