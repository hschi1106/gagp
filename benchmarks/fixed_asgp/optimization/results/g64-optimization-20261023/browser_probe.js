/* Inject into a preview copy after the original dashboard script.
 * No result data is changed. Render the unmodified original page for delivery.
 */
window.runG64BrowserChecks = function(expectedWidth) {
  'use strict';
  const checks = [], byId = id => document.getElementById(id);
  function check(condition, name) {
    if (!condition) throw new Error('G64 dashboard check failed: ' + name);
    checks.push(name);
  }
  check(!!window.g64Dashboard, 'dashboard initialized');
  const D = window.g64Dashboard.data;
  const expected = D.frozen_quality.tasks;
  const labels = id => Array.from(byId(id).children, row => row.firstElementChild.textContent);
  const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  const filter = value => { byId('task-filter').value = value; byId('task-filter').dispatchEvent(new Event('input', {bubbles:true})); };
  const select = value => { byId('task-select').value = value; byId('task-select').dispatchEvent(new Event('change', {bubbles:true})); };
  function checkRows(tasks, name) {
    check(same(labels('task-rows'), tasks), name + ': P1024 rows');
    check(same(labels('secondary-rows'), tasks), name + ': P16384 rows');
  }
  function checkCurves(task, split) {
    const svg = byId('quality-chart');
    const grid = Array.from(svg.querySelectorAll('line'), line => Number(line.getAttribute('y1')));
    const zero = Math.max(...grid), full = Math.min(...grid);
    const lines = Array.from(svg.querySelectorAll('polyline'));
    const variants = ['baseline','candidate'].filter(v => Array.isArray(task.curves?.[v]?.[split]) && task.curves[v][split].length === 65);
    check(lines.length === variants.length, split + ': curve count matches audited payload');
    for (let n = 0; n < variants.length; n++) {
      const points = lines[n].getAttribute('points').trim().split(/\s+/).map(p => p.split(',').map(Number));
      check(points.length === 65, split + ': ' + variants[n] + ' generations0–64');
      check(points.every((point, i) => Math.abs((zero - point[1]) / (zero - full) - task.curves[variants[n]][split][i]) < 1e-10),
            split + ': ' + variants[n] + ' plotted accuracy matches payload');
    }
    check(byId(split + '-toggle').getAttribute('aria-pressed') === 'true', split + ': active toggle');
    check(document.documentElement.dataset.split === split, split + ': actual chart state');
    return lines.map(line => line.getAttribute('points'));
  }
  function noOverflow(name) {
    const width = document.documentElement.clientWidth;
    check(document.documentElement.scrollWidth <= width + 1 && document.body.scrollWidth <= width + 1,
          name + ': no horizontal overflow');
  }
  check(expected.length === 30 && new Set(expected).size === 30, 'frozen30 task set');
  check(same(D.tasks.map(r => r.task), expected), 'primary payload retains full task set');
  check(same(D.secondary_tasks.map(r => r.task), expected), 'secondary payload retains full task set');
  check(Math.abs(window.innerWidth - expectedWidth) <= 1, 'actual viewport width');
  let chosen, distinct = false, unknownRows = 0;
  try {
    filter(''); select('all'); byId('test-toggle').click();
    checkRows(expected, 'initial'); noOverflow('initial');
    const needles = expected.filter(task => task.toLowerCase().includes('house'));
    check(needles.length > 0, 'actual HouseRobber task available');
    filter('HOUSE');
    checkRows(needles, 'case-insensitive filter');
    select(needles[0]); checkRows([needles[0]], 'task selection');
    filter('__no_matching_g64_task__'); checkRows([], 'no-match filter');
    filter(''); select('all'); checkRows(expected, 'reset');
    const available = D.tasks.filter(r => ['baseline','candidate'].every(v =>
      ['train','test'].every(s => Array.isArray(r.curves?.[v]?.[s]) && r.curves[v][s].length === 65)));
    const different = available.find(r => ['baseline','candidate'].some(v => !same(r.curves[v].train, r.curves[v].test)));
    chosen = different || available[0] || D.tasks[0];
    distinct = !!different;
    select(chosen.task); checkRows([chosen.task], 'quality task');
    byId('train-toggle').click(); const train = checkCurves(chosen, 'train');
    byId('test-toggle').click(); const test = checkCurves(chosen, 'test');
    if (distinct) check(!same(train, test), 'different audited train/test curves change plotted points');
    noOverflow('selected task');
    filter(''); select('all');
    const displayed = new Map(Array.from(byId('secondary-rows').children, row => [row.firstElementChild.textContent, row]));
    for (const item of D.secondary_tasks) {
      if (item.complete) continue;
      unknownRows++;
      const row = displayed.get(item.task);
      check(row.lastElementChild.textContent === '未完整', item.task + ': incomplete secondary row remains visible');
      if (item.asgp_over_candidate === null)
        check(row.children[2].textContent === '未完成', item.task + ': missing ASGP ratio stays unknown');
    }
    checkRows(expected, 'restored full grid'); noOverflow('full grid');
    const result = {status:'pass', width:window.innerWidth, checks, selected_task:chosen.task,
      distinct_train_test_available:distinct, incomplete_secondary_rows_checked:unknownRows,
      primary_rows:byId('task-rows').children.length, secondary_rows:byId('secondary-rows').children.length,
      contentHeight:document.documentElement.scrollHeight, scrollWidth:document.documentElement.scrollWidth,
      final_split:document.documentElement.dataset.split, final_task:document.documentElement.dataset.task};
    console.log('G64_UI_CHECKS ' + JSON.stringify(result));
    return result;
  } finally {
    filter(''); select('all'); byId('test-toggle').click();
  }
};
