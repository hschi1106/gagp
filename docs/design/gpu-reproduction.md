# GPU Reproduction Backend

## Purpose

GPU reproduction implements tournament selection, typed-subtree crossover,
and mutation from compiled-grammar contracts. The public behavior matches CPU
reproduction; child-for-child RNG identity across backends is not promised.

## Inputs

The backend receives:

- the immutable `grammar-definition-v2` compilation;
- the exact generation request and search limits;
- verified parent ASTs with reconstructed membership witnesses;
- the completed fitness vector;
- mutation, selection, and seed settings.

There is no grammar-config mode, specialized candidate bucket, LinearRec binder
array, ASGP phase table, or DP-pattern metadata.

## Pipeline

### Prepare

Host preparation reconstructs each parent, groups logical replacement sites,
assigns dense compatibility IDs, computes destination node/depth/template
budgets, and generates contextual donors from the same compiled grammar.
Repeated template-hole occurrences remain one atomic site.

Preparation may overlap GPU evaluation because it depends only on immutable
population/grammar state. Tournament selection still waits for fitness.

### Pack and upload

Packing verifies grammar/search identities and flattens AST nodes, names,
constants, lexical/traversal/bounded-region metadata, site groups, contracts,
domains, and donors. It prescans capacities and never truncates a compiled
payload. General bounded-region plans are transported as ordinary v2 AST
metadata; authoring package files are not required.

Buffers are explicitly compiled mode. Low-level entry points reject a mismatched
mode before allocation, pointer access, or kernel launch. Arena buffers are
reused when capacities permit.

### Select and vary

Selection consumes the completed fitness vector and produces parent pairs.
Crossover samples compatible logical sites, checks independent destination
budgets, and replaces every physical occurrence in the selected logical group.
Mutation either regenerates an admitted subtree from packed contextual donors
or resamples a mutable constant from its exact compiled domain.

The device preserves prefix validity, lexical ID remapping, metadata ownership,
atomic occurrence groups, and explicit provenance for copyback. A failed
attempt returns the certified parent according to the shared fallback contract
and updates the corresponding counter.

### Copy back and certify

Compact device results are copied to host storage, decoded as
`ast-prefix-v2`, structurally/type verified, and reconstructed against the
active compiled grammar. A child that cannot satisfy the complete contract is
rejected. No release-1 side table is decoded during copyback.

## Correctness

CPU and GPU backends share:

- tournament selection semantics and crossover-before-mutation order;
- exact type/scope/template compatibility keys;
- atomic repeated-hole handling;
- destination node, depth, and template budgets;
- constant domains and contextual donor frames;
- accepted-child AST verification and grammar membership;
- variation counter meanings.

The GPU uses fixed transport limits for programs, metadata, names, constants,
and total padded storage. Capacity rejection is explicit and does not silently
change the grammar or execute reproduction through an old host path.

## Overlap and timing

`--repro-overlap on` schedules preparation around GPU evaluation. It may hide
some prepare/preprocess/pack wall time; selection, variation, copyback, decode,
and certification remain ordered after their dependencies. Timing field names
and aggregation rules are defined in
[the timing reference](../reference/timing.md).

Variation counters classify operator outputs, including both crossover
children and subsequent mutation outputs. They are not final-population counts.
Performance claims require measured benchmark artifacts; this design contract
does not assert a cutover speedup.
