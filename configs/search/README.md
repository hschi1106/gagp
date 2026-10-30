# Search configurations

`gagp-search-config-v1` documents ([spec](../../spec/search_config.md)) select
search semantics only; engines are chosen separately and must not change results.

| File | Search |
| --- | --- |
| `phase_g64_formal.json` | Phase-population semantics of the formal G64 fixed-ASGP study (counted genome-wide sites, crossover every child, 8 attempts) |
| `phase_asgp_site7.json` | Closer to the ASGP operator parameters: crossover gate 0.7, uniform phase, grammar-invocation depth 7 |

Seed, penalty and population size come from the entry point unless a copy of
these files sets them. Use with `gagp_evolve_cli --search-config FILE` or the
fixed ASGP benchmark's `GAGP_SEARCH_CONFIG=FILE`.
