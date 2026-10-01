# Measurement archive status

The CPU fragment evolution/reproduction route and its phase-bank predecessor
were removed on 2026-10-02. Their owners, immutable executable certificates,
owned-only capacity buckets, probe modes and runner options are no longer
available in the maintained source. They are not current mainline results or a
recommended path to the GPU evaluation + GPU reproduction target.

Existing JSON/CSV measurements are preserved byte-for-byte. Historical labels
such as `fragments`, `fragments-owned0/1`, `fragments-parsimony`, `phase_bank`,
`owned` and their flags describe withdrawn experiments. This applies to those
rows in `round2-final*`, `round2-quality*`, earlier `round2-*` records, and all
`round2-buckets*` measurements. A historical “retained”, “best”, or reproduction
command in a raw record reflects its date, not current support. Archived logs,
source snapshots and binaries are historical evidence; no current runner exposes
the removed route.

Native rows remain historical native measurements. In particular, native
`round2-final.json` medians are Sum205.448, House207.503, Median178.636 ms at
P1024/cases1024. The withdrawn CPU reproduction results must not be substituted
for them. Cleanup smoke checks do not establish a new performance baseline.

Current settings, validation and supported reproduction commands are in
[PROGRESS.md](../PROGRESS.md). Original frozen inputs were not prepared again.
