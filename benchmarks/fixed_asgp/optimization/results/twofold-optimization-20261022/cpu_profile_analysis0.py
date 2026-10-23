"""Summarize actual nsys CPU callchains; sample counts are not wall-time shares."""
import fcntl
import os
from pathlib import Path
import sqlite3
import time
import traceback

import serial

out=serial.RAW/'cpu-profile-analysis0';out.mkdir();record=out/'manifest.json'
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           scope='nsys oldTRAINING GradeP16384 CPU samples, diagnostic only; not search ratios or wall-time percentages')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        profile=serial.read(serial.RAW/'profile-cold19-0-controller.json')
        assert profile['status']=='requested_phase_complete'
        database=Path(profile['sqlite']);assert serial.sha(database)==profile['sqlite_sha256']
        connection=sqlite3.connect('file:'+str(database)+'?mode=ro',uri=True)
        tables=[r[0] for r in connection.execute("select name from sqlite_master where type='table'")]
        required={'COMPOSITE_EVENTS','SAMPLING_CALLCHAINS','StringIds'};assert required<=set(tables)
        samples=connection.execute('select count(distinct id) from SAMPLING_CALLCHAINS').fetchone()[0]
        assert samples>0,'CPU samples absent; process completion does not prove CPU profiling'
        first=connection.execute('select id from SAMPLING_CALLCHAINS limit 1').fetchone()[0]
        example=list(connection.execute('''select c.stackDepth,s.value from SAMPLING_CALLCHAINS c
            join StringIds s on s.id=c.symbol where c.id=? order by c.stackDepth''',(first,)))
        state.update(status='analyzing',cpu_sample_callchains=samples,example_stack=example,
                     database=str(database),database_sha256=profile['sqlite_sha256'])
        roots={}
        for label,pattern in [('native_population','%NativePhasePopulation%'),
                              ('import_layout','%import_native_phase_layout%'),
                              ('grammar_admission','%reconstruct_derivation%'),
                              ('bytecode_verification','%verify_bytecode%'),
                              ('skeleton_key','%native_phase_skeleton_key%')]:
            ids_query='''select distinct c.id from SAMPLING_CALLCHAINS c
                join StringIds s on s.id=c.symbol where s.value like ?'''
            count=connection.execute('select count(*) from ('+ids_query+')',(pattern,)).fetchone()[0]
            leaf_query='''select s.value,count(distinct c.id) as samples from SAMPLING_CALLCHAINS c
                join StringIds s on s.id=c.symbol where c.stackDepth=0 and c.id in ('''+ids_query+''')
                group by c.symbol order by samples desc limit 25'''
            inclusive_query='''select s.value,count(distinct c.id) as samples from SAMPLING_CALLCHAINS c
                join StringIds s on s.id=c.symbol where c.id in ('''+ids_query+''')
                group by c.symbol order by samples desc limit 30'''
            roots[label]=dict(pattern=pattern,sample_callchains=count,
                              leaves=list(connection.execute(leaf_query,(pattern,))),
                              inclusive=list(connection.execute(inclusive_query,(pattern,))))
        assert roots['native_population']['sample_callchains']>0,'native constructor/evolution CPU stacks not captured'
        serial.save(out/'roots.json',roots)
        serial.save(out/'schema.json',{name:list(connection.execute('pragma table_info("'+name+'")'))
                                      for name in required})
        state.update(status='ok',roots=roots,roots_sha256=serial.sha(out/'roots.json'),
                     source_sha256=serial.sha(Path(__file__)),finished_epoch=time.time(),
                     caveat='inclusive counts overlap; process-tree CPU sample populations differ from wall timers; unresolved/inlined symbols may limit attribution')
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
