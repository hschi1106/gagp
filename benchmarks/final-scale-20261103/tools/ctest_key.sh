#!/bin/bash
cd "$(dirname "$0")/../../.."  # repository root
for t in gagp_test_engine_profile gagp_test_search_config gagp_test_vm_gpu_smoke gagp_test_fitness_cpu_gpu_parity gagp_test_evolution_cpu_gpu_parity gagp_test_phase_population_cpu_reference gagp_test_fitness_cpu_gpu_parity_mainline gagp_test_evolution_cpu_gpu_parity_mainline gagp_test_phase_population_cpu_reference_mainline; do
  flock /tmp/gagp-support-20261015-gpu.lock ctest --test-dir cpp/build/release -R "^${t}\$" --output-on-failure > /tmp/ctest_$t.log 2>&1
  echo "$t exit=$? $(grep -E 'tests passed|tests failed' /tmp/ctest_$t.log)"
done
echo CTEST-DONE
