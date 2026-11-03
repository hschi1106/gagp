"""Shared definitions for the final scale experiment (P1024/4096/16384, 1024 train cases).

Settings are the formal G64 quality settings (formal0-quality manifest) plus the
per-task GPU layout extras; the GAGP arm adds the final search profile and the
exact engine flags. All paths are absolute so jobs can run from anywhere."""
import json, os, hashlib
from pathlib import Path

# HERE: this benchmark directory (configuration); ROOT: the data directory with inputs and runs.
HERE = Path(__file__).resolve().parent.parent
ROOT = Path(os.environ.get("GAGP_FINAL_SCALE_DIR", HERE.parents[1] / "logs/final-scale-20261103"))
# BIN ran the recorded arms (commit 51aa1dc); BIN_CLEAN the committed engine (0178362).
BIN = Path(os.environ.get("GAGP_FINAL_SCALE_BIN", ROOT / "bins/bench"))
BIN_CLEAN = Path(os.environ.get("GAGP_FINAL_SCALE_BIN_CLEAN", ROOT / "bins/bench-clean"))
CONFIG = HERE.parents[1] / "configs/search/gagp_final_asgp.json"
CONFIG_G8 = HERE / "config/gagp_final_asgp_g8.json"
CONFIG_GEN = HERE.parents[1] / "configs/search/phase_asgp_site7.json"
LOCK = Path("/tmp/gagp-support-20261015-gpu.lock")
TASKS = ["filter_odd", "sum_of_elements", "is_sorted", "index_of_element", "all_equal", "cumulative_sum",
         "fuel_cost", "luhn", "min", "mpi", "count_odds", "last_index_of_zero", "small_or_large",
         "for_loop_index", "double_letters", "replace_space_with_newline", "even_squares", "sum_of_squares",
         "negative_to_zero", "scrabble_score", "grade", "median", "smallest", "syllables", "fibonacci",
         "tribonacci", "house_robber", "subregion_sum", "fibonacci_array", "stock_with_cooldown"]
POPS = [1024, 4096, 16384]
SEEDS = [1, 2, 3, 4, 5]
DATA_SEED = 20261103
TRAIN, TEST = 1024, 1000
GENERATIONS = 64
ENGINE = {"GAGP_GPU_WARP_VARIATION": "1", "GAGP_GPU_CHOICE_TABLES": "1", "GAGP_GPU_INITIAL_REPLAY_CAPACITY": "1"}


def read(p): return json.loads(Path(p).read_text())
def save(p, x):
    p = Path(p); tmp = p.with_suffix(p.suffix + ".tmp"); tmp.write_text(json.dumps(x, indent=1)); tmp.replace(p)
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def grammar(task): return HERE / "config/grammars" / f"{task}.json"
def source(task, seed): return ROOT / "inputs" / task / f"s{seed}.source.json"
def prepared(task, seed): return ROOT / "inputs" / task / f"s{seed}.prepared.json"
def split_source(task, split): return ROOT / "inputs" / task / f"{split}.source.json"


def base_env(task):
    """Formal settings + per-task layout extras, without per-run keys."""
    s = read(HERE / "config/settings.json")
    env = {k: v for k, v in os.environ.items() if not k.startswith("GAGP_") and k not in ("LD_PRELOAD", "MALLOC_CONF")}
    env.update(s["settings"]); env.update(s["extras"][task])
    env["GAGP_HOST_THREADS"] = "1"
    return env


def clean_env():
    """Committed engine (profile defaults): only the benchmark, allocator and thread settings."""
    s = read(HERE / "config/settings.json")["settings"]
    env = {k: v for k, v in os.environ.items() if not k.startswith("GAGP_") and k not in ("LD_PRELOAD", "MALLOC_CONF")}
    env.update({k: s[k] for k in ("LD_PRELOAD", "MALLOC_CONF", "OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS")})
    env.update(GAGP_HOST_THREADS="1", GAGP_BM_ALIGNED_FITNESS="1", GAGP_BM_GPU_BLOCKSIZE="512")
    return env
