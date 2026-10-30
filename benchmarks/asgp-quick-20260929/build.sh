#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p logs/asgp-quick-1024cases
ASGP_SOURCE=${ASGP_SOURCE:-/home/hschi1106/r13921069}
cmake -S "$ASGP_SOURCE" -B logs/asgp-quick/build-asgp -DCMAKE_BUILD_TYPE=Release -DASGP_BUILD_TESTS=ON
cmake --build logs/asgp-quick/build-asgp -j10
c++ -std=c++20 -O3 -DNDEBUG -DGAGP_HAS_CUDA=1 -Icpp/include -I"$ASGP_SOURCE/src" \
 benchmarks/asgp-quick-20260929/quick.cpp -o logs/asgp-quick-1024cases/quick \
 -Lcpp/build/release -Wl,--start-group -lgagp_cli_support -lgagp_evolution -lgagp_runtime_cpu -lgagp_grammar -lgagp_gpu -lgagp_region_plan_json -lgagp_json -lgagp_core \
 logs/asgp-quick/build-asgp/src/libasgp_core.a -Wl,--end-group -lcrypto \
 -L/usr/local/cuda-12.6/targets/x86_64-linux/lib -lcudart_static -lcudadevrt -lrt -lpthread -ldl
