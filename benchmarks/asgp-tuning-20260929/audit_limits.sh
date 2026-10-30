#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p logs/asgp-tuning
c++ -std=c++17 -O3 -DNDEBUG -DGAGP_HAS_CUDA=1 -Icpp/include \
 benchmarks/asgp-tuning-20260929/audit_limits.cpp -o logs/asgp-tuning/audit_limits \
 -Lcpp/build/release -Wl,--start-group -lgagp_cli_support -lgagp_evolution -lgagp_runtime_cpu -lgagp_grammar -lgagp_gpu -lgagp_region_plan_json -lgagp_json -lgagp_core -Wl,--end-group -lcrypto \
 -L/usr/local/cuda-12.6/targets/x86_64-linux/lib -lcudart_static -lcudadevrt -lrt -lpthread -ldl
logs/asgp-tuning/audit_limits logs/asgp-quick-1024cases/ready-p1024-c1024-s0.inputs.json > logs/asgp-tuning/limits-audit.csv
python3 benchmarks/asgp-tuning-20260929/audit_limits.py
