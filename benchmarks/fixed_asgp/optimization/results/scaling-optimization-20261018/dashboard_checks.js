// Append only to an in-memory preview copy, after the original page scripts.
// Final html_render must receive the unchanged original dashboard.html.
// Execute only after timed controllers/children exit, at widths728 and360.
(() => {
  const run = () => {
    const checks = [], failures = [];
    const el = id => document.getElementById(id);
    const check = (name, condition) => {
      checks.push(name);
      if (!condition) failures.push(name);
    };
    const change = (id, value, kind = 'change') => {
      el(id).value = String(value);
      el(id).dispatchEvent(new Event(kind, { bubbles: true }));
    };
    const fits = () => document.documentElement.scrollWidth <= window.innerWidth + 1
      && document.body.scrollWidth <= window.innerWidth + 1;
    const restored = {
      population: el('population').value, filter: el('filter').value,
      task: el('task').value, dataset: el('dataset').value
    };
    const number = value => Number.isFinite(value) ? value.toFixed(2) : '—';
    const selectedCell = () => DATA.cells.find(c => c.population === Number(el('population').value)
      && c.task === el('task').value);
    const checkSelection = tag => {
      const c = selectedCell();
      check(tag + ': selected data cell exists', Boolean(c));
      if (!c) return;
      check(tag + ': one active table row', el('rows').querySelectorAll('tr.active').length === 1);
      check(tag + ': selected population caption', el('task-caption').textContent.includes('P=' + c.population.toLocaleString()));
      const values = [...el('bars').querySelectorAll('.value')].map(n => n.textContent);
      check(tag + ': all three seconds match report data', JSON.stringify(values) === JSON.stringify(
        [c.asgp_s, c.gpu_original_s, c.gpu_improved_s].map(x => number(x) + ' 秒')));
      check(tag + ': ratio or missing-audit message matches data', c.qualified
        ? el('ratio').textContent.includes(number(c.asgp_vs_improved) + '×')
          && el('ratio').textContent.includes(number(c.original_vs_improved) + '×')
        : el('ratio').textContent.includes('不計算正式倍率'));
      check(tag + ': no document overflow', fits());
    };
    try {
      check('actual frozen scale G8/R2', DATA.protocol.generations === 8 && DATA.protocol.repetitions === 2);
      check('all 60 requested cells visible in data', DATA.cells.length === 60);
      check('all 360 requested runs retained in data', DATA.runs.length === 360 && DATA.totals.requested_runs === 360);
      check('default population16384', el('population').value === '16384');
      check('completion KPI uses original status counts', el('coverage').textContent === `${DATA.totals.completed_runs} / 360`);
      check('initial no document overflow', fits());
      for (const population of [1024, 16384]) {
        change('filter', '', 'input');
        change('population', population);
        check(`P${population}: all30 table rows`, el('rows').children.length === 30);
        check(`P${population}: all30 selector choices`, el('task').options.length === 30);
        for (const task of ['is_sorted', 'house_robber']) {
          change('task', task);
          checkSelection(`P${population}/${task}`);
        }
      }
      const clickRow = [...el('rows').children].find(n => n.getAttribute('aria-label') === 'All Equal');
      check('click target All Equal exists', Boolean(clickRow));
      if (clickRow) clickRow.click();
      check('table click updates task selector', el('task').value === 'all_equal');
      const keyboardRow = [...el('rows').children].find(n => n.getAttribute('aria-label') === 'Is Sorted');
      check('keyboard target Is Sorted exists', Boolean(keyboardRow));
      if (keyboardRow) keyboardRow.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
      check('Enter selects table row', el('task').value === 'is_sorted');
      change('dataset', 'test');
      check('test view explicitly warns not measured', !el('test-notice').hidden && el('test-notice').textContent.includes('沒有新增獨立測試資料'));
      check('test view displays no numeric quality cards', el('quality').children.length === 0);
      check('test note says no independent test numbers', el('quality-note').textContent.includes('沒有獨立測試集數值'));
      check('test view no document overflow', fits());
      change('dataset', 'train');
      check('train view hides test notice', el('test-notice').hidden);
      check('train view restores three CPU audit cards', el('quality').children.length === 3);
      check('train note identifies seed0 first repetition', el('quality-note').textContent.includes('seed0') && el('quality-note').textContent.includes('第一個 repetition'));
      change('filter', 'sOrTeD', 'input');
      check('case-insensitive filtering', el('rows').children.length === 1 && el('task').value === 'is_sorted');
      change('filter', 'is_sorted', 'input');
      check('task identifier filtering with underscore', el('rows').children.length === 1 && el('task').value === 'is_sorted');
      change('filter', 'no-such-task-8e91b2', 'input');
      check('empty filter result has no rows or choices', el('rows').children.length === 0 && el('task').options.length === 0);
      check('empty filter result has no stale bars or quality', el('bars').children.length === 0 && el('quality').children.length === 0);
      check('empty result is explained', el('task-caption').textContent.includes('沒有符合條件'));
      change('dataset', 'test');
      check('empty result test toggle still warns', !el('test-notice').hidden);
      change('dataset', 'train');
      check('empty result train toggle hides notice', el('test-notice').hidden);
      check('empty result no document overflow', fits());
      change('filter', '', 'input');
      check('clearing filter restores all30 rows', el('rows').children.length === 30);
      check('table overflow stays inside tablebox', [...document.querySelectorAll('.tablebox')]
        .every(n => n.getBoundingClientRect().right <= window.innerWidth + 1 && n.getBoundingClientRect().left >= -1));
      check('scientific PNG/PDF links present', ['speedup.png','speedup.pdf'].every(href =>
        document.querySelector(`a[href="${href}"]`)));
    } catch (error) {
      const failure = 'unexpected exception: ' + error.stack;
      checks.push(failure);
      failures.push(failure);
    } finally {
      change('filter', restored.filter, 'input');
      change('population', restored.population);
      change('task', restored.task);
      change('dataset', restored.dataset);
    }
    const result = { kind: 'scaling-dashboard-interaction-checks-v1', width: window.innerWidth,
      checks: checks.length, passed: checks.length - failures.length, failures,
      originalStateRestored: el('population').value === restored.population && el('filter').value === restored.filter
        && el('task').value === restored.task && el('dataset').value === restored.dataset,
      documentWidth: document.documentElement.scrollWidth };
    if (failures.length) console.error('DASHBOARD_CHECKS ' + JSON.stringify(result));
    else console.log('DASHBOARD_CHECKS ' + JSON.stringify(result));
  };
  requestAnimationFrame(() => requestAnimationFrame(run));
})();
