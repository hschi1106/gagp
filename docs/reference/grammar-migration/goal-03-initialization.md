# Goal 03 initialization evidence

This is an early timing comparison for Goal 11, not a matched-search-space performance
acceptance result. The reference invokes the unchanged initialization function from
frozen `b04918307eb69ec0f08c6bf5b03a0fae9399dbbf` headers/libraries. Both programs use
Release `-O3 -DNDEBUG` builds. The reference is CUDA-linked but measures CPU initialization;
the candidate is CPU-only. Each size has three warmups and fifteen measured trials,
with seeds `index * population_size`, and runs were serial without concurrent builds.

| Population | Reference median init (ms) | Request candidate median init (ms) | Request candidate setup (ms) | Reference mean nodes/member | Request candidate mean nodes/member |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64 | 14.507139 | 0.818009 | 82.495643 | 35.6083 | 12.2344 |
| 1024 | 228.748732 | 13.177614 | 86.703598 | 36.1581 | 12.5358 |

Candidate setup plus median initialization is 83.313652 ms and 99.881212 ms respectively.
Reference setup was not measured, so these sums are not a complete old/new cold-start
comparison. Candidate setup includes loading/config parsing, case preparation, owned
domain construction, conversion validation and final grammar compilation. Candidate
initialization uses the rebuilt request-backed generation path and includes membership
checks and native bytecode lowering. Summaries, serialization and population destruction
are outside both initialization timers.

The converted scalar grammar uses one initialized Int local, full-prefix limits of
80 nodes/depth 32, six statements per block, loop bounds 0..16 and fuel 20000. It includes
Int -8..8, both Bool values, and all Float thousandths -8..8 plus negative zero. The
reference retains its original recursion-depth-7 limits and generation policy. The
candidate's smaller AST distribution is a material difference; these measurements do
not prove equal evolutionary progress, equivalent sampling, or a speedup gate pass.

The fixture is `data/fixtures/simple_exp_1024.json`. All 1024 cases were verified to
have exactly `x:Float` input and Float expected output. The reference driver supplies
that schema directly because the original Float initializer does not inspect fixture
values. The candidate uses the fixture through the normal case decoder. The resolved
candidate grammar hash is
`2fc5ad8aba85b394368b845c13ed0a485097305fc7883dbd55539c6d12acb47e`.
Each raw reference and request-candidate file has 18 ordered rows: three warmups and
15 measured trials. Their indexes are 0 through 17 and every seed equals
`index * population_size`. The two request-candidate definition files are byte-identical
with SHA-256 `54aa8b44ae45b102f57ec1419c0e3926b69f96bb04838b73e0c661cbe1b5fb0c`.
The request manifest's captured binary and fixture hashes were also rechecked. It records
a dirty source snapshot at parent `193ccb66023ea8f92f70025f3560825b4961c16e`;
the manifest is the capture identity and does not assert that the later live worktree is
unchanged.

Artifacts are under
`/home/hschi1106/gagp-artifacts/grammar-migration/goal-03-initialization/`:

- `comparison-request.json`: latest request-backed checked counts/seeds, raw timing
  summaries, node distributions, setup costs, limitations and artifact hashes.
- `comparison.json`: preserved comparison for the earlier candidate API measurements.
- `reference-64.json`, `reference-1024.json`: all reference rows.
- `reference_init.cpp`, reference compile/run argument files and SHA-256 manifest:
  reproducible driver and frozen dependency identities.
- `candidate-request-64.json`, `candidate-request-1024.json`: all latest request-backed
  candidate rows and policy metadata.
- `candidate-request-64.definition.json`, `candidate-request-1024.definition.json`:
  canonical resolved grammar.
- `candidate-request-commands.json`, `candidate-request-manifest.json`: exact commands,
  binary/source hashes and fixture identity for the uncommitted request-backed Goal 03
  candidate.

The maintained candidate benchmark is
`cpp/src/bench/grammar_initialization_bench.cpp`, built as
`gagp_grammar_initialization_bench` with `GAGP_BUILD_BENCHMARKS=ON`.
Finite-domain membership indexes are compiled once; generation does not rebuild the
16,002-entry Float lookup for each member. Goal 11 must still measure matched workloads,
complete cold/warm costs and evolutionary outcomes before accepting performance claims.
