"""Finish reuse contract fixes before admitting any new scientific sample."""
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import build_projected
import serial

record=serial.RAW/'unchanged-context-corrected-controller.json';assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        previous=serial.read(serial.RAW/'build-attempt11/manifest.json')
        stopped=serial.read(serial.RAW/'unchanged-corrected-controller.json')
        assert previous['status']=='failed' and previous['results']==[0,-15] and stopped['status']=='stopped'
        assert (serial.RAW/'build-attempt11/authorized-interruption.json').exists()
        for line in subprocess.check_output(['ps','-eo','pid,pgid,stat'],text=True).splitlines()[1:]:
            pid,group,status=line.split()
            assert not ((int(pid)==previous['pid'] or int(group)==previous['child_pid']) and not status.startswith('Z')),'old build still active'
        for path,digest in previous['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest,'source changed since interrupted11'
        changes={}
        def replace(relative,old,new):
            path=serial.ROOT/relative;text=changes.get(path,path.read_text())
            assert text.count(old)==1,(relative,old[:80],text.count(old))
            changes[path]=text.replace(old,new)
        replace('cpp/src/runtime/gpu/device/kernels.cuh','''  if(meta.reuse_completed_fitness) {
    // This is block-uniform.''','''  if(meta.reuse_completed_fitness) {
    // The following tiled reduction still visits every logical slot. Give it
    // a defined neutral value even though host copyback restores this score.
    if(tiled_replay && tid==0)workspace.replay_warp_scores[logical_idx]=0.0;
    // This is block-uniform.''')
        header=serial.ROOT/'cpp/include/gagp/runtime/gpu/fitness_gpu.hpp';declarations=header.read_text()
        trailing='      const std::vector<int>* reused_parents=nullptr,const FitnessEvalResult* completed=nullptr) const;'
        assert declarations.count(trailing)==2 # owned + impl
        changes[header]=declarations.replace(trailing,
            '      const std::vector<int>* reused_parents=nullptr,const FitnessEvalResult* completed=nullptr,\n      std::uint64_t expected_revision=0) const;')
        replace('cpp/src/runtime/gpu/fitness_gpu.cu',
            '''    bool capture_case_counts,const std::vector<int>* reused_parents,const FitnessEvalResult* completed) const {
  return eval_programs_impl(owned.programs(),capture_case_counts,{},&owned,reused_parents,completed);''',
            '''    bool capture_case_counts,const std::vector<int>* reused_parents,const FitnessEvalResult* completed,
    std::uint64_t expected_revision) const {
  return eval_programs_impl(owned.programs(),capture_case_counts,{},&owned,reused_parents,completed,expected_revision);''')
        replace('cpp/src/runtime/gpu/fitness_gpu.cu','    const FitnessEvalResult* completed) const {',
            '    const FitnessEvalResult* completed,std::uint64_t expected_revision) const {')
        replace('cpp/src/runtime/gpu/fitness_gpu.cu',
            '  populate_payload_cache(needed_string_tokens, needed_list_tokens, &impl_->host_string_payloads, &impl_->host_list_payloads);',
            '''  const auto known_strings=impl_->host_string_payloads.size(),known_lists=impl_->host_list_payloads.size();
  populate_payload_cache(needed_string_tokens, needed_list_tokens, &impl_->host_string_payloads, &impl_->host_list_payloads);
  // An unresolved token may become available after init. Existing cached
  // backing is immutable, but newly resolved backing changes this context.
  if(known_strings!=impl_->host_string_payloads.size() || known_lists!=impl_->host_list_payloads.size())
    impl_->evaluation_revision=next_fitness_revision.fetch_add(1,std::memory_order_relaxed);''')
        replace('cpp/src/runtime/gpu/fitness_gpu.cu','  if(owned && reused_parents && completed && completed->ok &&',
            '  if(owned && expected_revision==impl_->evaluation_revision && reused_parents && completed && completed->ok &&')
        replace('cpp/src/evolution/repro/gpu/phase_population.cu',
            '     parents.empty()?nullptr:&parents,parents.empty()?nullptr:&impl_->fitness_reuse.completed()));',
            '     parents.empty()?nullptr:&parents,parents.empty()?nullptr:&impl_->fitness_reuse.completed(),revision));')
        replace('cpp/src/evolution/repro/gpu/phase_population.cu','  impl_->fitness_reuse.record(result,revision);',
            '  impl_->fitness_reuse.record(result,session.evaluation_revision());')
        marker='''void test_private_intlist_generations(bool sequence_windows=false,bool staged=false) {'''
        # Add the late-resolution case inside the private-cache helper, before
        # its closing brace. Calling cached first exercises the in-eval gate.
        path=serial.ROOT/'cpp/tests/gpu/test_gpu_phase_population.cu';text=path.read_text()
        a=text.index('void check_private_completed_fitness(');b=text.index(marker,a)
        part=text[a:b];end=part.rfind('\n}')
        assert end>0
        addition=r'''
 const auto missing=Value::from_int_list_hash_len(UINT64_C(0x7ede192d),4);std::vector<Value> unresolved;
 require(!payload::lookup_list(missing,&unresolved),"late payload fixture is already registered");
 auto missing_case=cases.front();
 for(auto& binding:missing_case)if(names[binding.idx]=="source")binding.value=missing;
 FitnessSessionGpu late;
 require(late.init({missing_case},{answers.front()},fuel,32,1000,policy).ok,"late payload session init");
 auto before=reference.evaluate(late,true),cold=cached.evaluate(late,true),warm=cached.evaluate(late,true);
 require(before.ok && cold.ok && warm.ok && before.fitness==cold.fitness && before.wrong==cold.wrong &&
     warm.fitness_reused_programs==parents.size(),"unresolved backing did not establish complete prior scores");
 payload::register_list(missing,{Value::from_int(4),Value::from_int(-7),Value::from_int(12),Value::from_int(2)});
 auto resolved=cached.evaluate(late,true); // Must reject its old revision inside eval_programs_impl.
 auto expected=reference.evaluate(late,true);
 require(resolved.ok && expected.ok && resolved.fitness==expected.fitness && resolved.wrong==expected.wrong &&
     resolved.case_counts==expected.case_counts && resolved.fitness_reused_programs==0,
     "newly available payload reused obsolete error/fitness observations");
 auto resolved_again=cached.evaluate(late,true);
 require(resolved_again.ok && resolved_again.fitness==expected.fitness &&
     resolved_again.fitness_reused_programs==parents.size(),"post-resolution revision was not recorded");
'''
        part=part[:end]+addition+part[end:];changes[path]=text[:a]+part+text[b:]
        replace('docs/design/gpu-reproduction.md','Reinitialization (including a failed attempt), another session, a',
            'Reinitialization (including a failed attempt), newly resolved payload backing, another session, a')
        before={str(path.relative_to(serial.ROOT)):serial.sha(path) for path in changes}
        for path,text in changes.items():path.write_text(text)
        serial.save(serial.RAW/'unchanged-context-source-correction.json',dict(epoch=time.time(),
            reason='define skipped tiled-reduction slots; compare current context revision after resolving previously missing payloads and record post-evaluation revision',
            source_before=before,source_after={str(p.relative_to(serial.ROOT)):serial.sha(p) for p in changes}))
        state['status']='building';serial.save(record,state)
    build_projected.run('build-attempt12')
    build=serial.read(serial.RAW/'build-attempt12/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
