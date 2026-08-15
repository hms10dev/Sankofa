// Flashcards deck editor. Deck content travels as raw TSV (the device just
// reads/writes the .tsv file); only the deck list is JSON. Rows are built with
// DOM APIs + textContent so card/deck text can never inject markup.
const rowsEl = document.getElementById('rows');
const metaEl = document.getElementById('deckMeta');
const statusEl = document.getElementById('status');
const selectEl = document.getElementById('deckSelect');
let currentDeck = null;

function flash(msg, ok = true) {
  statusEl.className = ok ? 'status-ok' : 'status-err';
  statusEl.textContent = msg;
}

function clean(s) { return s.replace(/[\t\r\n]+/g, ' ').trim(); }

// Size a textarea to fit its content (cards can be long, so cells wrap+grow).
function autoGrow(ta) {
  ta.style.height = 'auto';
  ta.style.height = Math.max(ta.scrollHeight, 24) + 'px';
}
function growAll() {
  for (const ta of rowsEl.querySelectorAll('textarea.cell-input')) autoGrow(ta);
}
function makeCell(value, placeholder) {
  const ta = document.createElement('textarea');
  ta.className = 'cell-input';
  ta.rows = 1;
  ta.spellcheck = false;
  ta.value = value;
  ta.placeholder = placeholder;
  ta.addEventListener('input', () => autoGrow(ta));
  return ta;
}

function makeRow(front = '', back = '', sched = null) {
  const tr = document.createElement('tr');
  tr._sched = sched;  // preserved SM-2 columns [reps, ef, interval, nextSession], or null for a new card

  const num = document.createElement('td');
  num.className = 'col-num';

  const tdF = document.createElement('td');
  const inF = makeCell(front, 'front');
  tdF.appendChild(inF);

  const tdB = document.createElement('td');
  const inB = makeCell(back, 'back');
  tdB.appendChild(inB);

  const tdX = document.createElement('td');
  tdX.className = 'col-x';
  const x = document.createElement('button');
  x.className = 'x-btn'; x.type = 'button'; x.textContent = '✕'; x.title = 'Remove card';
  x.addEventListener('click', () => { tr.remove(); renumber(); });
  tdX.appendChild(x);

  tr.append(num, tdF, tdB, tdX);
  return tr;
}

function renumber() {
  let i = 0;
  for (const tr of rowsEl.children) {
    const n = tr.querySelector('.col-num');
    if (n) n.textContent = ++i;
  }
}

function emptyRow(msg) {
  const tr = document.createElement('tr');
  const td = document.createElement('td');
  td.colSpan = 4;
  const p = document.createElement('p');
  p.className = 'empty';
  p.textContent = msg;
  td.appendChild(p);
  tr.appendChild(td);
  return tr;
}

function setMeta(name, count) {
  metaEl.replaceChildren();
  const b = document.createElement('b');
  b.textContent = name;
  metaEl.append(b, document.createTextNode(' · ' + count + ' card' + (count === 1 ? '' : 's')));
}

function setRows(cards) {
  rowsEl.replaceChildren();
  if (!cards.length) rowsEl.appendChild(makeRow());
  else cards.forEach(c => rowsEl.appendChild(makeRow(c.front, c.back, c.sched)));
  renumber();
  growAll();
}

function showEmpty(msg) {
  rowsEl.replaceChildren(emptyRow(msg));
  metaEl.textContent = '';
}

// Load a deck: tab-delimited if any tab is present, else comma (quote-aware via
// parseDelimited). Columns 0/1 are front/back; columns 2-5 (Repetitions,
// EasinessFactor, Interval, NextReviewSession) are the SM-2 schedule, kept per
// card so a save preserves review progress. '#' comments, blank lines, and a
// "Front/Back" header row are skipped.
function parseDeck(text) {
  const delim = text.includes('\t') ? '\t' : ',';
  return parseDelimited(text, delim)
    .map(r => ({
      front: (r[0] || '').trim(),
      back: (r[1] || '').trim(),
      sched: r.length >= 6 ? [r[2], r[3], r[4], r[5]].map(x => (x || '').trim()) : null,
    }))
    .filter(c => (c.front || c.back) && c.front[0] !== '#' && !(c.front === 'Front' && c.back === 'Back'));
}

// Quote a field if it contains the delimiter, a quote, or a newline (RFC 4180).
function escapeField(s, delim) {
  return (s.includes(delim) || s.includes('"') || s.includes('\n') || s.includes('\r'))
    ? '"' + s.replace(/"/g, '""') + '"'
    : s;
}

// Serialize the table to the deck's native format (CSV when the filename ends in
// .csv, else TSV), writing the Front/Back + SM-2 header and all six columns.
// Each card keeps its preserved schedule; new/imported cards get SM-2 defaults
// (reps 0, EF 2500, interval 0, due now). Empty rows are dropped.
function buildDeck() {
  const delim = /\.csv$/i.test(currentDeck || '') ? ',' : '\t';
  const header = ['Front', 'Back', 'Repetitions', 'EasinessFactor', 'Interval', 'NextReviewSession'].join(delim);
  const lines = [header];
  let count = 0;
  for (const tr of rowsEl.children) {
    const inputs = tr.querySelectorAll('.cell-input');
    if (inputs.length < 2) continue;
    const f = inputs[0].value.replace(/[\t\r\n]+/g, ' ').trim();
    const b = inputs[1].value.replace(/[\t\r\n]+/g, ' ').trim();
    if (!f && !b) continue;
    const s = tr._sched || ['0', '2500', '0', '0'];
    lines.push([escapeField(f, delim), escapeField(b, delim), s[0], s[1], s[2], s[3]].join(delim));
    count++;
  }
  return { text: count ? lines.join('\n') + '\n' : '', count };
}

// ---- import (CSV/TSV, quote-aware) ----------------------------------------
function parseDelimited(text, delim) {
  const rows = []; let field = '', row = [], inQ = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (inQ) {
      if (c === '"') { if (text[i + 1] === '"') { field += '"'; i++; } else inQ = false; }
      else field += c;
    } else if (c === '"') inQ = true;
    else if (c === delim) { row.push(field); field = ''; }
    else if (c === '\n') { row.push(field); rows.push(row); row = []; field = ''; }
    else if (c !== '\r') field += c;
  }
  if (field.length || row.length) { row.push(field); rows.push(row); }
  return rows;
}
function detectDelim(text) {
  const line = text.split('\n').find(l => l.trim()) || '';
  return line.includes('\t') ? '\t' : ',';
}
function doImport() {
  const text = document.getElementById('importText').value;
  if (!text.trim()) { flash('Nothing to import — paste rows or choose a file.', false); return; }
  const sel = document.getElementById('delim').value;
  const delim = sel === 'tab' ? '\t' : sel === 'comma' ? ',' : detectDelim(text);
  const cards = parseDelimited(text, delim)
    .map(r => [clean(r[0] || ''), clean(r[1] || '')])
    .filter(r => r[0] || r[1]);
  if (!cards.length) { flash('No rows found in that input.', false); return; }
  // Clear when replacing, or when the table currently holds only an empty-state row.
  const replace = document.getElementById('mode').value === 'replace';
  if (replace || !rowsEl.querySelector('.col-num')) rowsEl.replaceChildren();
  cards.forEach(([f, b]) => rowsEl.appendChild(makeRow(f, b)));
  renumber();
  growAll();
  flash('Imported ' + cards.length + ' card' + (cards.length === 1 ? '' : 's') +
    ' (' + (delim === '\t' ? 'TSV' : 'CSV') + ').');
}

// ---- server calls ----------------------------------------------------------
function fillSelect(decks, selected) {
  selectEl.replaceChildren();
  for (const d of decks) {
    const o = document.createElement('option');
    o.value = d; o.textContent = d;
    selectEl.appendChild(o);
  }
  if (selected && decks.includes(selected)) selectEl.value = selected;
}

async function loadDeckList(select) {
  try {
    const res = await fetch('/api/flashcards/decks');
    const data = await res.json();
    const decks = data.decks || [];
    fillSelect(decks, select);
    if (decks.length) { currentDeck = selectEl.value; await loadDeck(currentDeck); }
    else { currentDeck = null; showEmpty('No decks yet. Click "New deck" to create one.'); }
  } catch (e) {
    showEmpty('Failed to load decks: ' + e.message);
  }
}

async function refreshList(keep) {
  try {
    const res = await fetch('/api/flashcards/decks');
    const data = await res.json();
    fillSelect(data.decks || [], keep);
  } catch (e) { /* leave the dropdown as-is */ }
}

async function loadDeck(name) {
  try {
    const res = await fetch('/api/flashcards/deck?name=' + encodeURIComponent(name));
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const cards = parseDeck(await res.text());
    currentDeck = name;
    setRows(cards);
    setMeta(name, cards.length);
  } catch (e) {
    showEmpty('Failed to load ' + name + ': ' + e.message);
  }
}

async function saveDeck() {
  if (!currentDeck) { flash('No deck selected — use New deck first.', false); return; }
  const { text, count } = buildDeck();
  try {
    const res = await fetch('/api/flashcards/deck?name=' + encodeURIComponent(currentDeck), {
      method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: text
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    setMeta(currentDeck, count);
    flash('Saved ' + currentDeck + ' · ' + count + ' card' + (count === 1 ? '' : 's') + '.');
    await refreshList(currentDeck);
  } catch (e) {
    flash('Save failed: ' + e.message, false);
  }
}

async function deleteDeck() {
  if (!currentDeck) return;
  if (!confirm('Delete deck "' + currentDeck + '" and its review history?')) return;
  try {
    const res = await fetch('/api/flashcards/deck/delete', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: currentDeck })
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const gone = currentDeck;
    currentDeck = null;
    await loadDeckList();
    flash('Deleted ' + gone + '.');
  } catch (e) {
    flash('Delete failed: ' + e.message, false);
  }
}

function newDeck() {
  let name = (prompt('New deck name (e.g. spanish):') || '').trim();
  if (!name) return;
  name = name.replace(/[^A-Za-z0-9 _-]/g, '').trim().replace(/\s+/g, '-');
  if (!name) { flash('Please use letters, numbers, spaces, - or _.', false); return; }
  if (!/\.tsv$/i.test(name)) name += '.tsv';
  currentDeck = name;
  rowsEl.replaceChildren(makeRow());
  renumber();
  setMeta(name, 0);
  flash('New deck "' + name + '" — add cards, then Save.');
  rowsEl.querySelector('.cell-input').focus();
}

document.getElementById('addCard').addEventListener('click', () => {
  if (!rowsEl.querySelector('.col-num')) rowsEl.replaceChildren();  // clear an empty-state row
  const tr = makeRow();
  rowsEl.appendChild(tr);
  renumber();
  const cell = tr.querySelector('.cell-input');
  autoGrow(cell);
  cell.focus();
});
document.getElementById('saveDeck').addEventListener('click', saveDeck);
document.getElementById('newDeck').addEventListener('click', newDeck);
document.getElementById('delDeck').addEventListener('click', deleteDeck);
document.getElementById('importBtn').addEventListener('click', doImport);
document.getElementById('importFile').addEventListener('change', function () {
  const file = this.files[0];
  if (!file) return;
  const r = new FileReader();
  r.onload = () => {
    document.getElementById('importText').value = r.result;
    if (/\.tsv$/i.test(file.name)) document.getElementById('delim').value = 'tab';
    else if (/\.csv$/i.test(file.name)) document.getElementById('delim').value = 'comma';
    flash('Loaded ' + file.name + ' — review below, then Import.');
  };
  r.readAsText(file);
});
selectEl.addEventListener('change', () => loadDeck(selectEl.value));

loadDeckList();
