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

function makeRow(front = '', back = '') {
  const tr = document.createElement('tr');

  const num = document.createElement('td');
  num.className = 'col-num';

  const tdF = document.createElement('td');
  const inF = document.createElement('input');
  inF.className = 'cell-input'; inF.type = 'text'; inF.value = front; inF.placeholder = 'front';
  tdF.appendChild(inF);

  const tdB = document.createElement('td');
  const inB = document.createElement('input');
  inB.className = 'cell-input'; inB.type = 'text'; inB.value = back; inB.placeholder = 'back';
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
  else cards.forEach(([f, b]) => rowsEl.appendChild(makeRow(f, b)));
  renumber();
}

function showEmpty(msg) {
  rowsEl.replaceChildren(emptyRow(msg));
  metaEl.textContent = '';
}

// Load a deck: tab-delimited if any tab is present, else comma (quote-aware via
// parseDelimited). '#' comments and blank lines are skipped; the first two
// columns become front/back.
// First two columns are front/back; any further columns (a deck's own SM-2
// stats: Repetitions, EasinessFactor, Interval, NextReviewSession) are ignored.
// '#' comments, blank lines, and a "Front/Back" header row are skipped.
function parseDeck(text) {
  const delim = text.includes('\t') ? '\t' : ',';
  return parseDelimited(text, delim)
    .map(r => [(r[0] || '').trim(), (r[1] || '').trim()])
    .filter(r => (r[0] || r[1]) && r[0][0] !== '#' && !(r[0] === 'Front' && r[1] === 'Back'));
}

function csvEscape(s) {
  return /[",\r\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
}

// Serialize the table to the deck's native format: CSV when the filename ends
// in .csv (quote-aware), otherwise TSV. Empty rows are dropped.
function buildDeck() {
  const isCsv = /\.csv$/i.test(currentDeck || '');
  const lines = [];
  for (const tr of rowsEl.children) {
    const inputs = tr.querySelectorAll('input');
    if (inputs.length < 2) continue;
    let f = inputs[0].value, b = inputs[1].value;
    if (isCsv) { f = f.replace(/[\r\n]+/g, ' '); b = b.replace(/[\r\n]+/g, ' '); }
    else { f = clean(f); b = clean(b); }
    if (!f && !b) continue;
    lines.push(isCsv ? csvEscape(f) + ',' + csvEscape(b) : f + '\t' + b);
  }
  return { text: lines.length ? lines.join('\n') + '\n' : '', count: lines.length };
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
  rowsEl.querySelector('input').focus();
}

document.getElementById('addCard').addEventListener('click', () => {
  if (!rowsEl.querySelector('.col-num')) rowsEl.replaceChildren();  // clear an empty-state row
  const tr = makeRow();
  rowsEl.appendChild(tr);
  renumber();
  tr.querySelector('input').focus();
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
