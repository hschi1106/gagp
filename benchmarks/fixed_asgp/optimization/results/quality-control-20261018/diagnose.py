"""Serial diagnostic pipeline; preserves individual stage manifests."""
from pathlib import Path
import study

binary=study.RAW / "diagnostic-bench"
for name,run in (("profile",study.profile),("development",study.development),("checkpoint-audit",study.checkpoint_audit)):
    study.save(study.RAW / "diagnose-status.json",dict(stage=name,status="running"))
    run(binary)
study.save(study.RAW / "diagnose-status.json",dict(stage="complete",status="complete"))
