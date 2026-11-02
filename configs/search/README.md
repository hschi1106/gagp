# Search configurations

`gagp-search-config-v1` documents ([spec](../../spec/search_config.md)) select
search semantics only; engines are chosen separately and must not change results.

| File | Search |
| --- | --- |
| `phase_g64_formal.json` | Phase-population semantics of the formal G64 fixed-ASGP study (counted genome-wide sites, crossover every child, 8 attempts) |
| `phase_asgp_site7.json` | ASGP comparison profile: the `common` members aligned with ASGP (crossover gate 0.7, uniform phase, grammar-invocation depth 7 approximating ASGP's phase depth 7, mutation 0.3, tournament 2, elitism 1) |
| `gagp_final_asgp.json` | Final GAGP design for the ASGP comparison: `phase_asgp_site7.json` plus the `gagp` schedule choice, synchronous batch steady state with batch = population (omitted) and inverse tournament 2. Smaller batches are slower on GPU and are not part of the design |

Seed, penalty and population size come from the entry point unless a copy of
these files sets them. Use with `gagp_evolve_cli --search-config FILE` or the
fixed ASGP benchmark's `GAGP_SEARCH_CONFIG=FILE`.

When comparing with another system, align every `common` member of the
[spec](../../spec/search_config.md) to that system and record the profile
used; `gagp` members are evaluated only by GAGP ablations.

Engine options are not part of these files. The exact engine options used by
the final measurements (`GAGP_GPU_WARP_VARIATION=1`, `GAGP_GPU_CHOICE_TABLES=1`,
`GAGP_GPU_INITIAL_REPLAY_CAPACITY=1` and the formal runtime settings) are the
defaults of the engine profile (`cpp/include/gagp/core/engine_profile.hpp`);
they change only speed, never the search.
