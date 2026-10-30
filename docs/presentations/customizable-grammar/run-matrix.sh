#!/usr/bin/env bash
# Run from the repository root. Requires a CUDA Release build; no build is started here.
set -euo pipefail
B=cpp/build/release
G=configs/grammar/benchmarks/simple_exp.json
C=data/fixtures/simple_exp_1024.json
OUT="${1:-logs/grammar-slides-$(date +%Y%m%d-%H%M%S)}"
export GAGP_CUDA_DEVICE="${GAGP_CUDA_DEVICE:-0}"
for name in gagp_generate_cli gagp_evolve_cli gagp_grammar_cli; do
  test -x "$B/$name" || { echo "Missing $B/$name; see presentation README." >&2; exit 1; }
done
# Refuse reuse so previous measurement evidence cannot be overwritten.
mkdir "$OUT"
{
  git rev-parse HEAD
  git status --short
  uname -a
  lscpu
  nvidia-smi
  /usr/local/cuda-12.6/bin/nvcc --version
  cat "$B/CMakeCache.txt"
  sha256sum "$B/gagp_generate_cli" "$B/gagp_evolve_cli" "$G" "$C"
  echo "GAGP_CUDA_DEVICE=$GAGP_CUDA_DEVICE"
} > "$OUT/environment.txt" 2>&1
"$B/gagp_grammar_cli" validate --grammar-definition "$G" > "$OUT/grammar-validation.json"
for n in 1024 2048 4096 8192; do
  for seed in 0 1 2; do
    pop="$OUT/p${n}-s${seed}.population.json"
    "$B/gagp_generate_cli" --grammar-definition "$G" --cases "$C" \
      --population-size "$n" --seed "$seed" --out-json "$pop" > "$pop.log" 2>&1
    "$B/gagp_generate_cli" --grammar-definition "$G" --cases "$C" \
      --replay-json "$pop" --out-json "$OUT/replayed.json" > "$OUT/replay.log" 2>&1
    cmp "$pop" "$OUT/replayed.json"
    sha256sum "$pop" >> "$OUT/population-sha256.txt"
    # r=0 is discarded warm-up. Rotate order to reduce systematic order bias.
    modes=(A B C D)
    for rep in 0 1 2 3 4 5; do
      for offset in 0 1 2 3; do
        mode="${modes[$(((rep + seed + offset) % 4))]}"
        case "$mode" in
          A) engine=cpu; repro=cpu; overlap=off ;;
          B) engine=gpu; repro=cpu; overlap=off ;;
          C) engine=gpu; repro=gpu; overlap=off ;;
          D) engine=gpu; repro=gpu; overlap=on ;;
        esac
        dest="$OUT/p${n}-s${seed}-${mode}-r${rep}.json"
        cmd=("$B/gagp_evolve_cli" --grammar-definition "$G" --cases "$C"
          --population-json "$pop" --population-size "$n" --seed "$seed"
          --engine "$engine" --repro-backend "$repro" --repro-overlap "$overlap"
          --generations 1 --skip-final-eval on --blocksize 1024
          --mutation-rate 0.5 --mutation-subtree-prob 0.8
          --selection-pressure 2 --penalty 1.0 --cpu-repro-ablation none
          --retain-final-population off --show-program none --timing all --out-json "$dest")
        printf '%q ' "${cmd[@]}" >> "$OUT/commands.sh"
        printf '\n' >> "$OUT/commands.sh"
        echo "$(date -Is) population=$n seed=$seed mode=$mode repetition=$rep"
        /usr/bin/time -f %e -o "$dest.wall-seconds" "${cmd[@]}" > "$dest.log" 2>&1
      done
    done
  done
done
python3 docs/presentations/customizable-grammar/summarize.py "$OUT"
