#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p logs/asgp-tuning
c++ -std=c++17 -O3 -DNDEBUG -DGAGP_HAS_CUDA=1 -Icpp/include \
 benchmarks/asgp-tuning-20260929/replay.cpp -o logs/asgp-tuning/replay \
 -Lcpp/build/release -Wl,--start-group -lgagp_cli_support -lgagp_evolution -lgagp_runtime_cpu -lgagp_grammar -lgagp_gpu -lgagp_region_plan_json -lgagp_json -lgagp_core -Wl,--end-group -lcrypto \
 -L/usr/local/cuda-12.6/targets/x86_64-linux/lib -lcudart_static -lcudadevrt -lrt -lpthread -ldl
