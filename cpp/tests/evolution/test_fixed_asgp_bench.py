"""Integration checks for the optional external-ASGP benchmark adapter."""
from __future__ import annotations

import json
import os
import resource
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
ROOT = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from gagp_tools.experiments.fixed_asgp.grammar import TASKS, definition


class TestFixedAsgpNative(unittest.TestCase):
    def call(self, *args, env=None, timeout=120, memory_limit=None):
        def limit_memory():
            resource.setrlimit(resource.RLIMIT_AS, (memory_limit, memory_limit))
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True,
                                text=True, env=env, timeout=timeout,
                                preexec_fn=limit_memory if memory_limit else None)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_three_schemes_known_solutions_and_five_modes(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in TASKS:
                with self.subTest(task=name):
                    source = folder / f"{name}.source.json"
                    grammar = folder / f"{name}.grammar.json"
                    prepared = folder / f"{name}.prepared.json"
                    audit = folder / f"{name}.audit.json"
                    grammar.write_text(json.dumps(definition(ROOT, name)))
                    self.call("freeze", name, source)
                    raw = json.loads(source.read_text())
                    self.assertEqual(len(raw["population"]), 8192)
                    self.assertEqual(len(raw["cases"]), 1024)
                    self.call("prepare", source, grammar, 8, prepared)
                    self.assertEqual(len(json.loads(prepared.read_text())["programs"]), 8)
                    if name == "house_robber":
                        # This frozen parent exceeded 1024 physical nodes when
                        # every StateValue included a redundant upper clamp.
                        boundary = dict(raw, population=[raw["population"][2255]])
                        boundary_source = folder / "boundary.source.json"
                        boundary_prepared = folder / "boundary.prepared.json"
                        boundary_source.write_text(json.dumps(boundary))
                        self.call("prepare", boundary_source, grammar, 1, boundary_prepared)
                        self.assertEqual(len(json.loads(boundary_prepared.read_text())["programs"]), 1)
                    self.call("audit", source, grammar, audit)
                    checked = json.loads(audit.read_text())
                    self.assertEqual(checked["known_solution_asgp_wrong"], 0)
                    self.assertEqual(checked["known_solution_cpu_mismatches"], 0)
                    if name != "sum_of_elements":
                        self.assertEqual(checked["known_solution_gpu_fitness"], 0)
                    aligned_initial = []
                    for mode in ("asgp_1t", "gagp_cpu", "gpu_eval", "gpu_repro", "gpu_overlap"):
                        output = folder / f"{name}.{mode}.jsonl"
                        self.call("measure", source, prepared, grammar, 8, mode, output)
                        rows = [json.loads(line) for line in output.read_text().splitlines()]
                        self.assertEqual([r["rep"] for r in rows], [-1, 0, 1, 2])
                        self.assertTrue(all(r["cases"] == 1024 and r["population"] == 8 for r in rows))
                        self.assertTrue(all(r["generation_ms"] > 0 for r in rows))
                        if mode != "asgp_1t":
                            self.assertTrue(all(r["evolve_wall_ms"] >= r["evolve_call_ms"] for r in rows))
                        if name == "median":
                            search_output = folder / f"{name}.{mode}.search.json"
                            env = dict(os.environ, GAGP_SEARCH_GENERATIONS="2", GAGP_BM_SEED="0", GAGP_HOST_THREADS="1")
                            self.call("measure", source, prepared, grammar, 8, "search_" + mode, search_output, env=env)
                            report = json.loads(search_output.read_text())
                            self.assertEqual(report["mode"], mode)
                            self.assertEqual(report["population"], 8)
                            self.assertEqual(report["cases"], 1024)
                            self.assertEqual([r["generation"] for r in report["generations"]], [0, 1])
                            sign = -1 if mode == "asgp_1t" else 1
                            self.assertEqual(report["generations"][0]["best"], sign * rows[0]["best_native_fitness"])
                            self.assertEqual(report["generations"][0]["mean"], sign * rows[0]["mean_native_fitness"])
                            self.assertEqual(len(report["final_population"]), 8)
                            self.assertGreater(report["final_eval_ms"], 0)
                            self.assertGreater(report["evolve_wall_ms"], sum(r["generation_ms"] for r in report["generations"]))
                            self.assertTrue(all(r["expected_program_cases"] == 8192 for r in report["generations"]))
                            self.assertLess(report["generations"][0]["elapsed_search_ms"], report["generations"][1]["elapsed_search_ms"])
                            if mode != "asgp_1t":
                                self.assertEqual(report["cpu_audit_mismatches"], 0)
                            exact_output = folder / f"{name}.{mode}.exact.json"
                            env["GAGP_SEARCH_PENALTY"] = "1"
                            self.call("measure", source, prepared, grammar, 8, "search_" + mode, exact_output, env=env)
                            exact = json.loads(exact_output.read_text())
                            aligned_initial.append((exact["generations"][0]["best"], exact["generations"][0]["mean"]))
                            if mode != "asgp_1t":
                                self.assertEqual(exact["case_penalty"], 1)
                                self.assertEqual(exact["cpu_audit_mismatches"], 0)
                                self.assertGreaterEqual(exact["final_best"], -1024)
                    if name == "median":
                        native_snapshot = folder / "asgp.snapshot.json"
                        self.call("measure", source, prepared, grammar, 8, "asgp_snapshot", native_snapshot)
                        native_scores = json.loads(native_snapshot.read_text())["programs"]
                        self.assertEqual(max(r["fitness"] for r in native_scores), aligned_initial[0][0])
                        self.assertTrue(all(r["fitness"] == -r["wrong"] and r["cases"] == 1024 for r in native_scores))
                        self.assertTrue(all(pair == aligned_initial[0] for pair in aligned_initial))
                        snapshot = folder / "exact.snapshot.json"
                        env = dict(os.environ, GAGP_SNAPSHOT_PENALTY="1", GAGP_SNAPSHOT_CPU="1")
                        self.call("measure", source, prepared, grammar, 8, "snapshot", snapshot, env=env)
                        scores = json.loads(snapshot.read_text())["programs"]
                        self.assertEqual(max(r["fitness"] for r in scores), aligned_initial[0][0])
                        self.assertTrue(all(r["fitness"] == r["cpu_fitness"] for r in scores))
                        for value in ("0", "-1", "nan", "inf"):
                            env["GAGP_SNAPSHOT_PENALTY"] = value
                            rejected = subprocess.run([str(BINARY), "measure", str(source), str(prepared), str(grammar), "8", "snapshot", str(snapshot)], env=env, capture_output=True, text=True)
                            self.assertNotEqual(rejected.returncode, 0)


    def test_expanded_common_policy_and_heldout_audit(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in ("count_odds", "last_index_of_zero"):
                with self.subTest(task=name):
                    source=folder/f"{name}.source.json"
                    grammar=folder/f"{name}.grammar.json"
                    prepared=folder/f"{name}.prepared.json"
                    grammar.write_text(json.dumps(definition(ROOT,name)))
                    env=dict(os.environ,GAGP_BM_COMMON_FITNESS="1",GAGP_HOST_THREADS="1",
                             GAGP_VIEW_PROFILE="1",GAGP_TYPED_VIEW_PHASE="1",
                             GAGP_SEARCH_GENERATIONS="2",GAGP_BM_SEED="0",
                             GAGP_SEARCH_PENALTY="0" if name=="negative_to_zero" else "1000")
                    self.call("freeze",name,source,env=env)
                    self.call("prepare",source,grammar,8,prepared,env=env)
                    audit=folder/f"{name}.audit.json"
                    self.call("audit",source,grammar,audit,env=env)
                    report=json.loads(audit.read_text())
                    self.assertEqual(report["known_solution_cpu_mismatches"],0)
                    self.assertEqual(report["known_solution_asgp_wrong"],0)
                    if name!="negative_to_zero":
                        self.assertEqual(report["known_solution_gpu_fitness"],0)
                    initial=[]
                    modes=("asgp_1t","gagp_cpu") if name=="negative_to_zero" else ("asgp_1t","gagp_cpu","gpu_eval","gpu_repro","gpu_overlap")
                    for mode in modes:
                        output=folder/f"{name}.{mode}.json"
                        export=folder/f"{name}.{mode}.export.json"
                        history=folder/f"{name}.{mode}.history.json"
                        env.update(GAGP_SEARCH_EXPORT=str(export),GAGP_SEARCH_HISTORY_EXPORT=str(history))
                        self.call("measure",source,prepared,grammar,8,"search_"+mode,output,env=env)
                        r=json.loads(output.read_text())
                        self.assertEqual(len(r["generations"]),2)
                        self.assertEqual(len(r["final_population"]),8)
                        self.assertEqual(r.get("cpu_audit_mismatches",0),0)
                        initial.append(r["generations"][0]["mean"]+(1024 if name=="negative_to_zero" and mode=="asgp_1t" else 0))
                        quality=folder/f"{name}.{mode}.quality.json"
                        self.call("quality",name,export,grammar,quality,history,env=env)
                        q=json.loads(quality.read_text())
                        self.assertEqual(len(q["cases"]),1024)
                        self.assertEqual(q["final"]["cases"],1024)
                        self.assertEqual(len(q["history"]),2)
                    # Legacy grammar profiles remain separate from the strict
                    # source-contract and integer-output regression below.
                    self.assertTrue(all(abs(x-initial[1])<1e-6 for x in initial[1:]),initial)
                    if name == "count_odds":
                        self.assertAlmostEqual(initial[0], initial[1])

    def source_grammar(self, name, folder, env):
        contract=folder/"contract.json"
        grammar=folder/"grammar.json"
        self.call("contract",name,contract,env=env)
        grammar.write_text(json.dumps(definition(ROOT,name,contract=json.loads(contract.read_text()))))
        return grammar

    def test_last_index_initial_asgp_integer_semantics_parity(self):
        # Parent zero repeatedly squares. Compare full values independently of
        # fitness so floating-point score conversion cannot hide lost low bits.
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);source=folder/"source.json";prepared=folder/"prepared.json"
            env=dict(os.environ,GAGP_BM_ALIGNED_FITNESS="1",GAGP_HOST_THREADS="1",
                     GAGP_VIEW_PROFILE="1",GAGP_TYPED_VIEW_PHASE="1",GAGP_SNAPSHOT_CPU="1")
            env.pop("GAGP_BM_COMMON_FITNESS",None)
            grammar=self.source_grammar("last_index_of_zero",folder,env)
            self.call("freeze","last_index_of_zero",source,env=env)
            raw=json.loads(source.read_text());raw["population"]=raw["population"][:1]
            self.assertEqual(len(raw["cases"]),1024)
            source.write_text(json.dumps(raw))
            self.call("prepare",source,grammar,1,prepared,env=env)
            values=folder/"values.json"
            self.call("sampled-audit",source,grammar,values,env=env)
            checked=json.loads(values.read_text())
            self.assertEqual(checked["sampled_programs"],1)
            self.assertEqual(checked["cases_per_program"],1024)
            self.assertEqual(checked["mismatches"],0)
            a=folder/"asgp.json";g=folder/"gagp.json"
            self.call("measure",source,prepared,grammar,1,"asgp_snapshot",a,env=env)
            self.call("measure",source,prepared,grammar,1,"snapshot",g,env=env)
            left=json.loads(a.read_text())["programs"][0]["fitness"]
            right=json.loads(g.read_text())["programs"][0]
            self.assertEqual(right["cases"],1024)
            self.assertEqual(right["fitness"],right["cpu_fitness"])
            # ASGP sums raw doubles; native reductions round to48mantissa bits.
            self.assertAlmostEqual(left,right["cpu_fitness"],places=8)

    def test_negative_random_evolution_resource_support(self):
        # Exercise all1024cases and changing random offspring, without allowing
        # an allocation regression to consume the host's memory indefinitely.
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);source=folder/"source.json";prepared=folder/"prepared.json"
            output=folder/"search.json"
            env=dict(os.environ,GAGP_BM_ALIGNED_FITNESS="1",GAGP_HOST_THREADS="1",
                     GAGP_SEARCH_GENERATIONS="2",GAGP_BM_SEED="0",GAGP_SEARCH_PENALTY="1000")
            env.pop("GAGP_BM_COMMON_FITNESS",None)
            grammar=self.source_grammar("negative_to_zero",folder,env)
            self.call("freeze","negative_to_zero",source,env=env)
            self.call("prepare",source,grammar,8,prepared,env=env)
            self.call("measure",source,prepared,grammar,8,"search_gagp_cpu",output,
                      env=env,timeout=120,memory_limit=2*1024**3)
            report=json.loads(output.read_text())
            self.assertEqual(report["cases"],1024)
            self.assertEqual(report["population"],8)
            self.assertEqual(len(report["generations"]),2)
            self.assertEqual(len(report["final_population"]),8)
            self.assertEqual(report["cpu_audit_mismatches"],0)
            self.assertEqual(report["elite_count"],1)

    def test_negative_gpu_known_solution_arena_limit(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            source=folder/"long.source.json";audit=folder/"audit.json"
            source.write_text(json.dumps({"task":"negative_to_zero","cases":[
                {"input":["-1"]*50,"expected":["0"]*50,"start":0,"extent":0}]*1024}))
            env=dict(os.environ,GAGP_BM_ALIGNED_FITNESS="1",GAGP_HOST_THREADS="1",
                     GAGP_VIEW_PROFILE="1",GAGP_TYPED_VIEW_PHASE="1")
            env.pop("GAGP_BM_COMMON_FITNESS",None)
            grammar=self.source_grammar("negative_to_zero",folder,env)
            self.call("audit",source,grammar,audit,env=env)
            report=json.loads(audit.read_text())
            self.assertEqual(report["known_solution_asgp_wrong"],0)
            self.assertEqual(report["known_solution_cpu_mismatches"],0)
            self.assertEqual(report["known_solution_gpu_fitness"],0)




if __name__ == "__main__":
    unittest.main()
