
(()=>{const get=id=>document.getElementById(id), p=JSON.parse(get('data').textContent), checks=[];
const add=(name,ok,details)=>{checks.push({name,ok,...details});if(!ok)throw new Error('UI check failed: '+name)};
const count=()=>[get('speed').rows.length,get('quality').rows.length];
const event=(el,name)=>el.dispatchEvent(new Event(name,{bubbles:true}));
add('initial_rows',JSON.stringify(count())==='[60,60]',{rows:count()});
get('filter').value='house';event(get('filter'),'input');add('filter_house',JSON.stringify(count())==='[2,2]',{rows:count()});
get('filter').value='NO_MATCH_CHECK';event(get('filter'),'input');add('filter_empty',JSON.stringify(count())==='[0,0]',{rows:count()});
get('filter').value='';event(get('filter'),'input');get('task').value='luhn';event(get('task'),'change');
add('task_selection',JSON.stringify(count())==='[2,2]',{rows:count()});
const unknown=[...get('speed').rows].filter(r=>r.cells[r.cells.length-1].textContent!=='已核對');
add('unknown_retained',unknown.length===1,{status:unknown[0]?.cells[6].textContent,reason:unknown[0]?.cells[6].title});
const task=p.quality_counts.find(r=>JSON.stringify(r.train)!==JSON.stringify(r.test)).task;
get('task').value=task;event(get('task'),'change');
let snapshots={};for(const split of ['train','test','joint']){get('split').value=split;event(get('split'),'change');
const actual=[...get('quality').rows].map(r=>[...r.cells].slice(1).map(c=>c.textContent));
const expected=['baseline','candidate'].map(v=>{const g=p.quality_counts.find(x=>x.task===task&&x.variant===v);return [g.audited+'/'+g.expected,...['gen0','final','new_final','lost_initial'].map(k=>String(g[split][k]))]});
add('split_'+split,JSON.stringify(actual)===JSON.stringify(expected),{task,actual});snapshots[split]=actual}
add('toggle_changes_counts',JSON.stringify(snapshots.train)!==JSON.stringify(snapshots.test),{task});
get('task').value='all';get('split').value='joint';event(get('task'),'change');event(get('split'),'change');
const overflow=[...document.querySelectorAll('body *')].filter(e=>e.getClientRects().length&&e.getBoundingClientRect().right>innerWidth+1).map(e=>e.id||e.tagName).slice(0,10);
add('no_horizontal_overflow',document.documentElement.scrollWidth<=innerWidth&&overflow.length===0,{width:innerWidth,documentWidth:document.documentElement.scrollWidth,overflow});
const lines=[...get('speed').rows].map(row=>{const range=document.createRange();range.selectNodeContents(row.cells[1]);return range.getClientRects().length});add('population_one_line',lines.every(x=>x===1),{lineCounts:[...new Set(lines)]});
console.log('UI_CHECKS '+JSON.stringify({pass:checks.every(x=>x.ok),checks:checks.map(x=>x.name),width:innerWidth,documentWidth:document.documentElement.scrollWidth,rows:count(),populationLineCounts:[...new Set(lines)],headerWidths:[...document.querySelectorAll('.speed-table th')].map(x=>Math.round(x.getBoundingClientRect().width))}));
})();
