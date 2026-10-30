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

    def test_explicit_quality_cases_seed_and_native_judge(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            base=dict(os.environ,GAGP_FREEZE_POPULATION='8',GAGP_HOST_THREADS='1',
                      GAGP_BM_ALIGNED_FITNESS='1',GAGP_SEARCH_GENERATIONS='2')
            sources=[]
            for seed in (0,1):
                path=folder/f'seed{seed}.json'
                self.call('freeze','min',path,env=dict(base,GAGP_FREEZE_POPULATION_SEED=str(seed)))
                sources.append(json.loads(path.read_text()))
            self.assertNotEqual(sources[0]['population'],sources[1]['population'])
            default=folder/'default.json';self.call('freeze','min',default,env=base)
            self.assertEqual(json.loads(default.read_text()),sources[0])
            source=folder/'train.json';document=sources[0];document['cases']=document['cases'][:3]
            source.write_text(json.dumps(document))
            grammar=self.source_grammar('min',folder,base)
            prepared=folder/'prepared.json';self.call('prepare',source,grammar,8,prepared,env=base)
            rejected=subprocess.run([str(BINARY),'measure',str(source),str(prepared),str(grammar),
                '8','search_asgp_1t',str(folder/'bad.json')],env=base,capture_output=True,text=True)
            self.assertNotEqual(rejected.returncode,0)
            env=dict(base,GAGP_BM_CASE_COUNT='3',GAGP_SEARCH_EXPORT_BEST_ONLY='1')
            for mode in ('asgp_1t','gagp_cpu'):
                output=folder/f'{mode}.json';export=folder/f'{mode}.export.json';history=folder/f'{mode}.history.json'
                current=dict(env,GAGP_SEARCH_EXPORT=str(export),GAGP_SEARCH_HISTORY_EXPORT=str(history))
                self.call('measure',source,prepared,grammar,8,'search_'+mode,output,env=current)
                result=json.loads(output.read_text())
                self.assertEqual(result['cases'],3)
                self.assertTrue(all(row['expected_program_cases']==24 for row in result['generations']))
                if mode=='gagp_cpu':
                    self.assertEqual(result['gpu_blocksize'],512)
                    self.assertEqual(result['cpu_audit_reduction_lanes'],512)
                    self.assertEqual(result['cpu_audit_mismatches'],0)
                    self.assertEqual(len(json.loads(export.read_text())['programs']),1)
                    self.assertEqual(len(result['final_population']),8)
                judged=folder/f'{mode}.judge.json'
                self.call('quality-source',source,export,grammar,judged,history,env=env)
                audit=json.loads(judged.read_text())
                self.assertEqual(audit['cases'],3)
                self.assertEqual(audit['final']['cases'],3)
                self.assertEqual(len(audit['history']),2)
                self.assertEqual(len(audit['final']['case_results']),3)
                self.assertLessEqual(audit['unique_audited_programs'],3)
            # Native resident scoring remains the owner when CPU reference work
            # is enabled; checkpoints and default-off verification preserve RNG.
            resident=dict(env,GAGP_GPU_PHASE_POPULATION='1',GAGP_GPU_DIAGNOSTICS='1',
                          GAGP_BATCH_PHASE_EXPORT='1')
            artifacts=[]
            for label,extra in (('off',{}),('guard',{'GAGP_VERIFY_TOP_K':'2','GAGP_VERIFY_INTERVAL':'2'}),
                                ('zero',{'GAGP_VERIFY_TOP_K':'2','GAGP_VERIFY_INTERVAL':'9','GAGP_VERIFY_NEAR_ZERO':'1000'})):
                checkpoints=folder/label
                out=folder/f'{label}.json'
                self.call('measure',source,prepared,grammar,8,'search_gpu_overlap',out,
                          env=dict(resident,GAGP_SEARCH_CHECKPOINT_DIR=str(checkpoints),
                                   GAGP_SEARCH_CHECKPOINTS='0,1,2',**extra))
                doc=json.loads(out.read_text());artifacts.append(doc)
                self.assertTrue(doc['reproduction_profile'].startswith('native-gpu-phase-'))
                self.assertEqual(doc['reproduction_fallback_reason'],'')
                self.assertEqual(doc['verification']['fitness_corrections'],0)
                self.assertEqual(doc['verification']['wrong_corrections'],0)
                expected={'off':0,'guard':4,'zero':6}[label]
                self.assertEqual(doc['verification']['programs'],expected)
                self.assertEqual(doc['verification']['cases'],expected*3)
                self.assertEqual(len(list(checkpoints.glob('gen*.native.json'))),3)
                for gen in (0,1,2):
                    snap=json.loads((checkpoints/f'gen{gen}.native.json').read_text())
                    self.assertEqual(len(snap['programs']),8)
                    self.assertEqual(len(snap['scores']),8)
            self.assertEqual(artifacts[0]['final_population'],artifacts[1]['final_population'])
            self.assertEqual([r['best'] for r in artifacts[0]['generations']],
                             [r['best'] for r in artifacts[1]['generations']])
            # The CPU reference engine reproduces the resident GPU search exactly,
            # and an explicit search config with the same semantics changes nothing.
            config=folder/'search.json'
            config.write_text(json.dumps({'format':'gagp-search-config-v1','representation':'phase_population',
                'generations':2,'crossover':{'probability':1,'attempts':8},
                'sites':{'scope':'genome','draw':'reservoir'}}))
            for label,mode,extra in (('cpu','search_phase_cpu',{}),
                                     ('config','search_gpu_overlap',{'GAGP_SEARCH_CONFIG':str(config)}),
                                     ('cpuconfig','search_phase_cpu',{'GAGP_SEARCH_CONFIG':str(config)})):
                out=folder/f'{label}.json'
                self.call('measure',source,prepared,grammar,8,mode,out,env=dict(resident,**extra))
                doc=json.loads(out.read_text())
                self.assertEqual(doc['final_population'],artifacts[0]['final_population'],label)
                self.assertEqual([(r['best'],r['mean'],r['best_wrong']) for r in doc['generations']],
                                 [(r['best'],r['mean'],r['best_wrong']) for r in artifacts[0]['generations']],label)
                self.assertEqual(doc['search_config']['sites'],{'scope':'genome','draw':'reservoir'})
                self.assertEqual(doc['reproduction_profile'].startswith('native-cpu-phase-'),mode=='search_phase_cpu')
            config.write_text(json.dumps({'format':'gagp-search-config-v1','generations':3}))
            conflict=subprocess.run([str(BINARY),'measure',str(source),str(prepared),str(grammar),'8',
                'search_phase_cpu',str(folder/'conflict.json')],env=dict(resident,GAGP_SEARCH_CONFIG=str(config)),
                capture_output=True,text=True)
            self.assertNotEqual(conflict.returncode,0)
            self.assertIn('GAGP_SEARCH_GENERATIONS',conflict.stderr+conflict.stdout)
            # Launch geometry is an explicit search-only benchmark setting.
            # Its CPU audit must use the same logical reduction lanes.
            self.assertEqual(artifacts[0]['gpu_blocksize'],512)
            tuned=folder/'block128.json'
            self.call('measure',source,prepared,grammar,8,'search_gpu_overlap',tuned,
                      env=dict(resident,GAGP_BM_GPU_BLOCKSIZE='128'))
            tuned_result=json.loads(tuned.read_text())
            self.assertEqual(tuned_result['gpu_blocksize'],128)
            self.assertEqual(tuned_result['cpu_audit_reduction_lanes'],128)
            self.assertEqual(tuned_result['cpu_audit_mismatches'],0)
            self.assertEqual(tuned_result['cpu_audit_wrong_mismatches'],0)
            self.assertEqual(tuned_result['final_population'],artifacts[0]['final_population'])
            for invalid in ('','0','1025','128junk','+128',' 128','999999999999999999999'):
                with self.subTest(blocksize=invalid):
                    rejected=subprocess.run([str(BINARY),'measure',str(source),str(prepared),
                        str(grammar),'8','search_gpu_overlap',str(folder/'invalid-block.json')],
                        env=dict(resident,GAGP_BM_GPU_BLOCKSIZE=invalid),capture_output=True,text=True)
                    self.assertNotEqual(rejected.returncode,0)
                    self.assertIn('GAGP_BM_GPU_BLOCKSIZE',rejected.stderr)
            source_snapshots=folder/'source-checkpoints'
            self.call('measure',source,prepared,grammar,8,'search_asgp_1t',folder/'asgp.cp.json',
                      env=dict(env,GAGP_SEARCH_CHECKPOINT_DIR=str(source_snapshots),GAGP_SEARCH_CHECKPOINTS='0,2'))
            self.assertEqual(len(json.loads((source_snapshots/'gen2.source.json').read_text())['population']),8)
            pool=folder/'pool.json';self.call('dataset','fibonacci',pool,env=base)
            raw=json.loads(pool.read_text())
            self.assertEqual({r['start'] for r in raw['train']},set(range(21)))
            self.assertEqual({r['start'] for r in raw['test']},set(range(31)))
            self.assertTrue(all(r['expected']=='832040' for r in raw['test'] if r['start']==30))
            fresh_pool=folder/'fresh.pool.json'
            self.call('dataset','fibonacci',fresh_pool,env=dict(base,GAGP_DATASET_SEED='20261018'))
            self.assertEqual(json.loads(fresh_pool.read_text())['data_seed'],20261018)
            snapshot=folder/'resident.snapshot.json';reference=folder/'source.snapshot.json'
            self.call('measure',source,prepared,grammar,8,'snapshot',snapshot,
                      env=dict(env,GAGP_SNAPSHOT_CPU='1',GAGP_SNAPSHOT_NATIVE_OWNER='1'))
            self.call('measure',source,prepared,grammar,8,'asgp_snapshot',reference,env=env)
            native=json.loads(snapshot.read_text());original=json.loads(reference.read_text())
            self.assertTrue(native['diagnostic_owner'].startswith('native-gpu-phase-'))
            self.assertEqual(native['cases'],3);self.assertEqual(native['population'],8)
            for actual,expected in zip(native['programs'],original['programs']):
                self.assertEqual(actual['fitness'],actual['cpu_fitness'])
                self.assertEqual(actual['wrong'],actual['cpu_wrong'])
                self.assertEqual(actual['cpu_fitness'],expected['fitness'])
                self.assertEqual(actual['cpu_wrong'],expected['wrong'])

    def test_list_behavior_snapshot(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            env=dict(os.environ,GAGP_FREEZE_POPULATION='8',GAGP_HOST_THREADS='1',
                     GAGP_BM_ALIGNED_FITNESS='1',GAGP_BM_CASE_COUNT='3',
                     GAGP_SNAPSHOT_BEHAVIOR='1',GAGP_SNAPSHOT_CPU='1',GAGP_SNAPSHOT_NATIVE_OWNER='1')
            source=folder/'source.json';self.call('freeze','filter_odd',source,env=env)
            doc=json.loads(source.read_text());doc['cases']=doc['cases'][:3];source.write_text(json.dumps(doc))
            grammar=self.source_grammar('filter_odd',folder,env)
            prepared=folder/'prepared.json';self.call('prepare',source,grammar,8,prepared,env=env)
            out=folder/'snapshot.json';self.call('measure',source,prepared,grammar,8,'snapshot',out,env=env)
            snapshot=json.loads(out.read_text())
            self.assertEqual(snapshot['cpu_behavior_probe_cases'],3)
            self.assertGreater(snapshot['unique_cpu_behavior_probes'],0)
            self.assertLessEqual(snapshot['unique_cpu_behavior_probes'],8)

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
