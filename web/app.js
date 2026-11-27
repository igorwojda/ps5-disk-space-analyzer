// Disk Space Analyzer web UI.
//
// Screens: home (pick storage) -> scanning -> browse (select) -> review -> deleting.
//
// Input, all mapped to the same actions:
//   D-pad / arrow keys      move focus          Cross / Enter      open, confirm
//   Circle / Esc / browser back                  back
//   Square / Space          select             Triangle / Tab     review & clean
//   L1 R1 / Q E             switch tab         L2 R2 / PgUp PgDn  page
//   Options / M             menu
//   Left stick / mouse      cursor: hover focuses, Cross/click acts on what is under it
// The Gamepad API is used when the browser offers it; otherwise the PS5
// browser's own key events and cursor clicks do the job.
'use strict';

const VIS = 10;            // list rows on screen
const HOLD_MS = 1200;      // hold Cross this long to delete
const TAU = Math.PI * 2;

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s).replace(/[&<>"']/g, (c) =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

function fmtSize(b) {
  if (!b) return 'Zero KB';
  const u = ['bytes', 'KB', 'MB', 'GB', 'TB', 'PB'];
  let v = b, i = 0;
  while (v >= 1000 && i < 5) { v /= 1000; i++; }
  if (i === 0) return b + ' bytes';
  return (v >= 100 ? v.toFixed(0) : v >= 10 ? v.toFixed(1) : v.toFixed(2)) + ' ' + u[i];
}
const fmtCount = (n) => Number(n).toLocaleString('en-US');
const fmtPct = (f) => (f >= 0.1 ? (f * 100).toFixed(0) : (f * 100).toFixed(1)) + '%';

async function api(path, method = 'GET') {
  const r = await fetch(path, { method, cache: 'no-store' });
  let j = {};
  try { j = await r.json(); } catch (e) { /* empty body */ }
  if (!r.ok) { const e = new Error(j.error || 'HTTP ' + r.status); e.status = r.status; throw e; }
  return j;
}

// ================================================================ icons

const ICON = {
  folder: '<svg viewBox="0 0 40 40"><rect y="5" width="18" height="9" rx="2.5" fill="#3f7fc4"/><rect y="9" width="40" height="27" rx="3.5" fill="#58a6ff"/></svg>',
  file: '<svg viewBox="0 0 40 40"><path d="M7 2h18l8 8v28H7z" fill="#d5dfec"/><path d="M25 2v8h8z" fill="#97a8bd"/></svg>',
  lock: '<svg viewBox="0 0 30 30"><path d="M9 13V9a6 6 0 0 1 12 0v4" fill="none" stroke="rgba(255,255,255,.35)" stroke-width="3"/><rect x="4" y="13" width="22" height="15" rx="3" fill="rgba(255,255,255,.35)"/></svg>',
  box: '<svg viewBox="0 0 30 30"><rect x="1.5" y="1.5" width="27" height="27" rx="6.5" fill="none" stroke="rgba(255,255,255,.45)" stroke-width="2.5"/></svg>',
  checked: '<svg viewBox="0 0 30 30"><rect width="30" height="30" rx="7" fill="#2fbf55"/><path d="M7 15.5l5.5 5.5L23.5 9" fill="none" stroke="#fff" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  partial: '<svg viewBox="0 0 30 30"><rect x="1.5" y="1.5" width="27" height="27" rx="6.5" fill="none" stroke="#2fbf55" stroke-width="2.5"/><rect x="8" y="13" width="14" height="4" rx="2" fill="#2fbf55"/></svg>',
};

function driveIcon(kind) {
  if (kind === 'usb') return '<svg viewBox="0 0 90 90"><rect x="27" y="4" width="36" height="24" rx="4" fill="#8d9bb0"/><rect x="18" y="25" width="54" height="61" rx="11" fill="#e6eefa"/></svg>';
  if (kind === 'folder') return '<svg viewBox="0 0 90 90"><rect y="12" width="40" height="18" rx="5" fill="#a9b8cc"/><rect y="22" width="90" height="58" rx="8" fill="#e6eefa"/></svg>';
  if (kind === 'system') return '<svg viewBox="0 0 90 90"><rect y="14" width="90" height="62" rx="9" fill="#e6eefa"/><rect x="14" y="28" width="62" height="5" fill="#8d9bb0"/><rect x="14" y="43" width="62" height="5" fill="#8d9bb0"/><rect x="14" y="58" width="62" height="5" fill="#8d9bb0"/></svg>';
  const led = kind === 'ext' ? '#4ade80' : '#8d9bb0';
  return `<svg viewBox="0 0 90 90"><rect y="23" width="90" height="45" rx="9" fill="#e6eefa"/><circle cx="72" cy="45" r="5" fill="${led}"/><rect x="11" y="42" width="40" height="6" fill="#8d9bb0"/></svg>`;
}

const GLYPH = {
  cross: '<svg class="glyph" viewBox="0 0 40 40"><circle cx="20" cy="20" r="19" fill="#0a1626" stroke="rgba(255,255,255,.25)"/><path d="M12.5 12.5l15 15M12.5 27.5l15-15" stroke="#8fb8f0" stroke-width="3.4" stroke-linecap="round"/></svg>',
  circle: '<svg class="glyph" viewBox="0 0 40 40"><circle cx="20" cy="20" r="19" fill="#0a1626" stroke="rgba(255,255,255,.25)"/><circle cx="20" cy="20" r="8.5" fill="none" stroke="#ff7b8a" stroke-width="3.4"/></svg>',
  square: '<svg class="glyph" viewBox="0 0 40 40"><circle cx="20" cy="20" r="19" fill="#0a1626" stroke="rgba(255,255,255,.25)"/><rect x="12" y="12" width="16" height="16" rx="1.5" fill="none" stroke="#f0a0d8" stroke-width="3.4"/></svg>',
  triangle: '<svg class="glyph" viewBox="0 0 40 40"><circle cx="20" cy="20" r="19" fill="#0a1626" stroke="rgba(255,255,255,.25)"/><path d="M20 10.5l10 17H10z" fill="none" stroke="#5be0b5" stroke-width="3.4" stroke-linejoin="round"/></svg>',
  options: '<svg class="glyph" viewBox="0 0 40 40"><rect x="2" y="6" width="36" height="28" rx="8" fill="#0a1626" stroke="rgba(255,255,255,.25)"/><path d="M12 14h16M12 20h16M12 26h16" stroke="#dfe7f2" stroke-width="2.6"/></svg>',
  dpad: '<svg class="glyph" viewBox="0 0 40 40"><circle cx="20" cy="20" r="19" fill="#0a1626"/><path d="M16 7h8v9h9v8h-9v9h-8v-9H7v-8h9z" fill="#dfe7f2"/></svg>',
  lstick: '<svg class="glyph" viewBox="0 0 40 40"><circle cx="20" cy="20" r="19" fill="#0a1626"/><circle cx="20" cy="20" r="12" fill="none" stroke="#dfe7f2" stroke-width="3"/><text x="20" y="25" text-anchor="middle" font-size="14" font-weight="700" fill="#dfe7f2" font-family="Lato,sans-serif">L</text></svg>',
  l1: '<span class="glyph wide">L1</span>', r1: '<span class="glyph wide">R1</span>',
  l2: '<span class="glyph wide">L2</span>', r2: '<span class="glyph wide">R2</span>',
};

// ================================================================ state

const S = {
  screen: 'home',
  storages: [], hfocus: 0,
  storage: null, rootId: 0, rootNode: null,
  node: null, tab: 0, focus: 0, scroll: 0,
  biggest: [], bfocus: 0, bscroll: 0,
  marked: new Map(),
  rfocus: 0, rscroll: 0, rbutton: false,
  dialog: null,
  cursor: false,      // the pointer (left stick / mouse) is in use
  hover: null,        // chart segment under the pointer
  scan: null,         // progress while scanning / deleting
  resultSeq: 0,
};

let loadToken = 0;
let scanCancelled = false;

// ================================================================ helpers

function clampList(focusKey, scrollKey, count) {
  S[focusKey] = Math.max(0, Math.min(S[focusKey], count - 1));
  if (S[focusKey] < S[scrollKey]) S[scrollKey] = S[focusKey];
  if (S[focusKey] >= S[scrollKey] + VIS) S[scrollKey] = S[focusKey] - VIS + 1;
  S[scrollKey] = Math.max(0, Math.min(S[scrollKey], Math.max(0, count - VIS)));
}

function markedList() {
  return [...S.marked.values()].sort((a, b) => b.size - a.size);
}
function markedBytes() {
  let b = 0;
  for (const m of S.marked.values()) b += m.size;
  return b;
}
function isMarked(item) {
  if (S.marked.has(item.id)) return true;
  for (const m of S.marked.values()) if (item.path.startsWith(m.path + '/')) return true;
  return false;
}
function hasMarkedInside(item) {
  for (const m of S.marked.values()) if (m.path.startsWith(item.path + '/')) return true;
  return false;
}

function focusedItem() {
  if (S.tab === 1) return S.biggest[S.bfocus] || null;
  return S.node ? S.node.children[S.focus] || null : null;
}

let toastTimer = 0;
function toast(msg) {
  const t = $('#toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 2600);
}

// ================================================================ data flow

async function loadStorages() {
  try { S.storages = await api('/api/storages'); } catch (e) { S.storages = []; toast('Could not list storages: ' + e.message); }
  S.hfocus = Math.min(S.hfocus, Math.max(0, S.storages.length - 1));
}

async function openNode(id, focusId) {
  const token = ++loadToken;
  let n;
  try { n = await api('/api/node?id=' + id); } catch (e) {
    if (e.status === 404 && S.node && id !== S.rootId) return openNode(S.rootId);
    toast('Could not open folder: ' + e.message);
    return;
  }
  if (token !== loadToken) return;
  S.node = n;
  S.hover = null;
  S.focus = 0;
  if (focusId != null) {
    const i = n.children.findIndex((c) => c.id === focusId);
    if (i >= 0) S.focus = i;
  }
  S.scroll = Math.max(0, S.focus - (VIS >> 1));
  clampList('focus', 'scroll', n.children.length);
  if (n.id === S.rootId) S.rootNode = n;
  render();
}

async function loadBiggest() {
  try { S.biggest = await api('/api/biggest'); } catch (e) { S.biggest = []; }
  S.bfocus = S.bscroll = 0;
  if (!S.rootNode || S.rootNode.id !== S.rootId) {
    try { S.rootNode = await api('/api/node?id=' + S.rootId); } catch (e) { /* chart only */ }
  }
}

async function startScan(storage) {
  try { await api('/api/scan?path=' + encodeURIComponent(storage.path), 'POST'); } catch (e) {
    toast('Could not start the scan: ' + e.message);
    return;
  }
  S.storage = storage;
  S.marked.clear();
  S.screen = 'scanning';
  S.scan = { items: 0, bytes: 0, current: '' };
  scanCancelled = false;
  render();
  pollScan();
}

async function pollScan() {
  let st;
  try { st = await api('/api/status'); } catch (e) { setTimeout(pollScan, 1000); return; }
  if (st.phase === 'scanning') {
    S.scan = st;
    render();
    setTimeout(pollScan, 300);
    return;
  }
  if (st.phase === 'ready' && !scanCancelled) await enterBrowse(st);
  else { S.screen = 'home'; await loadStorages(); render(); }
}

async function enterBrowse(st) {
  S.storage = st.storage;
  S.rootId = st.root;
  S.rootNode = null;
  S.tab = 0;
  S.resultSeq = st.result ? st.result.seq : 0;
  S.screen = 'browse';
  await loadBiggest();
  await openNode(st.root);
}

function cancelScan() {
  scanCancelled = true;
  api('/api/cancel', 'POST').catch(() => {});
  toast('Scan cancelled');
}

// ================================================================ actions

function enter(item) {
  if (item.dir && !item.mount) openNode(item.id);
}

function goUp() {
  if (S.node && S.node.parent >= 0) openNode(S.node.parent, S.node.id);
}

function reveal(id, parentId) {
  S.tab = 0;
  S.screen = 'browse';
  openNode(parentId, id);
}

function toggleMark(item) {
  if (!item) return;
  if (S.marked.has(item.id)) {
    S.marked.delete(item.id);
  } else if (item.protected) {
    toast(item.mount ? 'Separate volume: scan it from the storage list'
                     : 'Protected system location: it cannot be deleted');
    return;
  } else if (isMarked(item)) {
    toast('Already included: a parent folder is selected');
    return;
  } else {
    for (const [id, m] of S.marked) if (m.path.startsWith(item.path + '/')) S.marked.delete(id);
    // Remember the folder it lives in, so the review screen can show it.
    const parentId = item.parentId != null ? item.parentId : S.node ? S.node.id : S.rootId;
    S.marked.set(item.id, Object.assign({ parentId }, item));
  }
  render();
}

function openReview() {
  if (!S.marked.size) { toast('Nothing selected yet: press Square on an item'); return; }
  S.screen = 'review';
  S.rfocus = S.rscroll = 0;
  S.rbutton = false;
  render();
}

function askDelete() {
  const list = markedList();
  if (!list.length) return;
  let files = 0, warn = '';
  for (const m of list) { files += m.files; if (!warn && m.warn) warn = m.warn; }
  showDialog({
    title: `Delete ${fmtCount(list.length)} ${list.length === 1 ? 'item' : 'items'}?`,
    lines: [`${fmtSize(markedBytes())} in ${fmtCount(files)} files will be permanently deleted.`, 'This cannot be undone.'],
    warning: warn,
    buttons: ['Cancel', 'Hold Cross to delete'],
    cancel: 0, hold: 1, focus: 1, danger: true,
    onChoose: (i) => { if (i === 1) runDelete(list); },
  });
}

async function runDelete(list) {
  try { await api('/api/delete?ids=' + list.map((m) => m.id).join(','), 'POST'); } catch (e) {
    toast('Delete failed to start: ' + e.message);
    return;
  }
  S.screen = 'deleting';
  S.scan = { index: 0, total: list.length, freed: 0, goal: markedBytes(), current: '' };
  render();
  pollDelete();
}

async function pollDelete() {
  let st;
  try { st = await api('/api/status'); } catch (e) { setTimeout(pollDelete, 1000); return; }
  if (st.phase === 'deleting') {
    S.scan = st;
    render();
    setTimeout(pollDelete, 300);
    return;
  }
  const r = st.result;
  S.marked.clear();
  S.storage = st.storage;
  S.resultSeq = r.seq;
  S.screen = 'browse';
  S.rootNode = null;
  await loadBiggest();
  await openNode(S.node ? S.node.id : S.rootId, null);
  showDialog({
    title: r.errors ? 'Cleanup finished with problems' : 'Cleanup complete',
    lines: [`Freed ${fmtSize(r.freed)}.`, `${fmtCount(r.removed)} of ${fmtCount(r.total)} selected items removed.`],
    warning: r.errors ? `${fmtCount(r.errors)} entries could not be deleted (in use or read-only).` : '',
    buttons: ['OK'],
  });
}

function openMenu() {
  showDialog({
    title: 'Menu',
    vertical: true,
    buttons: ['Rescan ' + (S.storage ? S.storage.name : ''), 'Clear selection', 'Choose another storage',
              'Stop Disk Analyzer', 'Close'],
    cancel: 4,
    onChoose: (i) => {
      if (i === 0 && S.storage) startScan(S.storage);
      if (i === 1) { S.marked.clear(); toast('Selection cleared'); render(); }
      if (i === 2) leaveScan();
      if (i === 3) stopPayload();
    },
  });
}

function leaveScan() {
  const go = async () => { S.screen = 'home'; S.marked.clear(); await loadStorages(); render(); };
  if (!S.marked.size) { go(); return; }
  showDialog({
    title: 'Leave this scan?',
    lines: ['You will return to the storage list.', `Your selection of ${fmtCount(S.marked.size)} items will be cleared.`],
    buttons: ['Stay', 'Leave'], cancel: 0,
    onChoose: (i) => { if (i === 1) go(); },
  });
}

function stopPayload() {
  showDialog({
    title: 'Stop Disk Analyzer?',
    lines: ['The background payload stops. Send disk-analyzer.elf again to use the app.'],
    buttons: ['Cancel', 'Stop'], cancel: 0,
    onChoose: (i) => {
      if (i !== 1) return;
      api('/api/quit', 'POST').catch(() => {});
      document.body.innerHTML = '<p style="font:32px Lato,sans-serif;color:#eaf2ff;text-align:center;margin-top:40vh">Disk Space Analyzer stopped.</p>';
    },
  });
}

// ================================================================ dialog

function showDialog(d) {
  S.dialog = Object.assign({ lines: [], warning: '', buttons: ['OK'], cancel: 0, hold: -1, focus: 0, vertical: false }, d);
  render();
}
function closeDialog(choice) {
  const d = S.dialog;
  S.dialog = null;
  stopHold();
  render();
  if (d && d.onChoose && choice != null) d.onChoose(choice);
}

let hold = null;
function startHold(src) {
  const d = S.dialog;
  if (!d || d.hold < 0 || d.focus !== d.hold || hold) return;
  hold = { src, t0: performance.now() };
  requestAnimationFrame(tickHold);
}
function endHold(src) {
  if (hold && (src == null || hold.src === src)) stopHold();
}
function stopHold() {
  hold = null;
  const f = document.querySelector('#dialog .fill');
  if (f) f.style.width = '0';
}
function tickHold() {
  if (!hold || !S.dialog) return;
  const p = (performance.now() - hold.t0) / HOLD_MS;
  if (p >= 1) { hold = null; closeDialog(S.dialog.hold); return; }
  const f = document.querySelector('#dialog .fill');
  if (f) f.style.width = (p * 100) + '%';
  requestAnimationFrame(tickHold);
}

function dialogInput(a) {
  const d = S.dialog;
  const n = d.buttons.length;
  const prev = d.vertical ? a === 'up' : a === 'left';
  const next = d.vertical ? a === 'down' : a === 'right';
  if (prev || next) {
    d.focus = Math.max(0, Math.min(n - 1, d.focus + (next ? 1 : -1)));
    stopHold();
    render();
  } else if (a === 'back') {
    closeDialog(d.cancel);
  } else if (a === 'confirm' && d.focus !== d.hold) {
    closeDialog(d.focus);
  }
}

// ================================================================ input dispatch

const REPEATS = new Set(['up', 'down', 'left', 'right', 'pageup', 'pagedown']);
let last = { a: '', src: '', t: 0 };

function input(a, src) {
  const now = performance.now();
  // The same press can arrive as a key event and as a gamepad button.
  if (a === last.a && src !== last.src && now - last.t < 90) return;
  last = { a, src, t: now };
  if (['up', 'down', 'left', 'right', 'pageup', 'pagedown'].includes(a)) { S.cursor = false; S.hover = null; }

  if (S.dialog) { dialogInput(a); return; }
  switch (S.screen) {
    case 'home': homeInput(a); break;
    case 'scanning': if (a === 'back') cancelScan(); break;
    case 'browse': browseInput(a); break;
    case 'review': reviewInput(a); break;
    default: break;
  }
}

function homeInput(a) {
  const n = S.storages.length;
  if (a === 'up' && S.hfocus >= 2) S.hfocus -= 2;
  else if (a === 'down') {
    if (S.hfocus + 2 < n) S.hfocus += 2;
    else if (S.hfocus % 2 === 0 && S.hfocus + 1 < n) S.hfocus += 1;
  } else if (a === 'left' && S.hfocus % 2 === 1) S.hfocus--;
  else if (a === 'right' && S.hfocus % 2 === 0 && S.hfocus + 1 < n) S.hfocus++;
  else if (a === 'confirm' && S.storages[S.hfocus]) { startScan(S.storages[S.hfocus]); return; }
  else if (a === 'menu') { loadStorages().then(() => { render(); toast('Storage list refreshed'); }); return; }
  render();
}

function browseInput(a) {
  if (a === 'tabprev' || a === 'tabnext') { S.tab = 1 - S.tab; S.hover = null; render(); return; }
  if (a === 'menu') { openMenu(); return; }
  if (a === 'review') { openReview(); return; }
  if (S.tab === 1) {
    const n = S.biggest.length;
    const f = S.biggest[S.bfocus];
    if (a === 'up') S.bfocus--;
    else if (a === 'down') S.bfocus++;
    else if (a === 'pageup') S.bfocus -= VIS;
    else if (a === 'pagedown') S.bfocus += VIS;
    else if ((a === 'confirm' || a === 'right') && f) { reveal(f.id, f.parentId); return; }
    else if (a === 'mark') { toggleMark(f); return; }
    else if (a === 'back' || a === 'left') S.tab = 0;
    clampList('bfocus', 'bscroll', n);
    render();
    return;
  }
  if (!S.node) return;
  const f = focusedItem();
  const n = S.node.children.length;
  if (a === 'up') S.focus--;
  else if (a === 'down') S.focus++;
  else if (a === 'pageup') S.focus -= VIS;
  else if (a === 'pagedown') S.focus += VIS;
  else if (a === 'confirm' || a === 'right') {
    if (f && f.dir && !f.mount) enter(f);
    else if (f && a === 'confirm') toast(f.mount ? 'Separate volume: scan it from the storage list'
                                                 : 'Press Square to select this file for cleanup');
    return;
  } else if (a === 'left') { goUp(); return; }
  else if (a === 'back') { if (S.node.parent >= 0) goUp(); else leaveScan(); return; }
  else if (a === 'mark') { toggleMark(f); return; }
  clampList('focus', 'scroll', n);
  render();
}

function reviewInput(a) {
  const list = markedList();
  if (a === 'up' && !S.rbutton) S.rfocus--;
  else if (a === 'down' && !S.rbutton) S.rfocus++;
  else if (a === 'pageup') S.rfocus -= VIS;
  else if (a === 'pagedown') S.rfocus += VIS;
  else if (a === 'right') S.rbutton = true;
  else if (a === 'left') S.rbutton = false;
  else if (a === 'back') { S.screen = 'browse'; render(); return; }
  else if (a === 'mark' && !S.rbutton && list[S.rfocus]) {
    toggleMark(list[S.rfocus]);
    if (!S.marked.size) { S.screen = 'browse'; render(); return; }
  } else if (a === 'confirm') {
    if (S.rbutton) askDelete();
    else if (list[S.rfocus]) revealPath(list[S.rfocus]);
    return;
  } else if (a === 'review') { askDelete(); return; }
  clampList('rfocus', 'rscroll', S.marked.size);
  render();
}

// Items picked from the list or biggest files carry their parent id.
function revealPath(item) {
  if (item.parentId != null) reveal(item.id, item.parentId);
  else if (item.parent != null) reveal(item.id, item.parent);
}

// ---------------------------------------------------------------- pointer actions

function act(el) {
  const k = el.dataset.act, i = Number(el.dataset.i);
  switch (k) {
    case 'storage': S.hfocus = i; startScan(S.storages[i]); break;
    case 'tab': S.tab = i; S.hover = null; render(); break;
    case 'crumb': S.tab = 0; openNode(i); break;
    case 'check': {
      const item = S.screen === 'review' ? markedList()[i] : S.tab === 1 ? S.biggest[i] : S.node.children[i];
      toggleMark(item);
      if (S.screen === 'review' && !S.marked.size) { S.screen = 'browse'; render(); }
      break;
    }
    case 'row':
      if (S.screen === 'review') { S.rfocus = i; S.rbutton = false; revealPath(markedList()[i]); break; }
      if (S.tab === 1) { S.bfocus = i; reveal(S.biggest[i].id, S.biggest[i].parentId); break; }
      S.focus = i;
      if (S.node.children[i].dir && !S.node.children[i].mount) enter(S.node.children[i]);
      else toggleMark(S.node.children[i]);
      break;
    case 'seg': {
      const s = chart.segs[i];
      if (!s) break;
      if (s.dir && s.hasKids) { S.tab = 0; openNode(s.id); }
      else reveal(s.id, s.chain.length > 1 ? s.chain[s.chain.length - 2] : chart.rootId);
      break;
    }
    case 'center': if (S.tab === 0) goUp(); break;
    case 'hint': input(el.dataset.a, 'click'); break;
    case 'delbtn': askDelete(); break;
    case 'dlg': if (S.dialog && i !== S.dialog.hold) closeDialog(i); break;
    default: break;
  }
}

function hover(el) {
  const k = el.dataset.act, i = Number(el.dataset.i);
  let changed = false;
  if (k === 'storage' && S.hfocus !== i) { S.hfocus = i; changed = true; }
  if (k === 'row' || k === 'check') {
    if (S.screen === 'review') { if (S.rfocus !== i || S.rbutton) { S.rfocus = i; S.rbutton = false; changed = true; } }
    else if (S.tab === 1) { if (S.bfocus !== i) { S.bfocus = i; changed = true; } }
    else if (S.focus !== i) { S.focus = i; changed = true; }
  }
  if (k === 'delbtn' && !S.rbutton) { S.rbutton = true; changed = true; }
  if (k === 'dlg' && S.dialog && S.dialog.focus !== i) { S.dialog.focus = i; stopHold(); changed = true; }
  if (k === 'seg') {
    const s = chart.segs[i];
    if (S.hover !== s) {
      S.hover = s;
      // A segment of the current folder also moves the list focus.
      if (S.tab === 0 && S.node && s && s.chain.length === 1) {
        const j = S.node.children.findIndex((c) => c.id === s.id);
        if (j >= 0) { S.focus = j; clampList('focus', 'scroll', S.node.children.length); }
      }
      changed = true;
    }
  } else if (S.hover && k !== 'center') { S.hover = null; changed = true; }
  if (changed) render();
}

// ---------------------------------------------------------------- DOM events

const KEYS = {
  ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right', Enter: 'confirm',
  ' ': 'mark', Escape: 'back', Backspace: 'back', Tab: 'review', Delete: 'review',
  q: 'tabprev', e: 'tabnext', PageUp: 'pageup', PageDown: 'pagedown', m: 'menu', ContextMenu: 'menu',
};
const KEYCODES = { 38: 'up', 40: 'down', 37: 'left', 39: 'right', 13: 'confirm', 27: 'back', 8: 'back',
                   32: 'mark', 9: 'review', 33: 'pageup', 34: 'pagedown' };

document.addEventListener('keydown', (e) => {
  const a = KEYS[e.key] || KEYCODES[e.keyCode];
  if (!a) return;
  e.preventDefault();
  if (a === 'confirm') startHold('key');
  if (e.repeat && !REPEATS.has(a)) return;
  input(a, 'key');
});
document.addEventListener('keyup', (e) => {
  if ((KEYS[e.key] || KEYCODES[e.keyCode]) === 'confirm') endHold('key');
});

let lastPointer = { x: -1, y: -1 };
document.addEventListener('mousemove', (e) => {
  if (e.screenX === lastPointer.x && e.screenY === lastPointer.y) return;  // not a real move
  lastPointer = { x: e.screenX, y: e.screenY };
  S.cursor = true;
  const el = e.target.closest('[data-act]');
  if (el) hover(el);
  else if (S.hover) { S.hover = null; render(); }
});

let clickSuppressUntil = 0;
document.addEventListener('click', (e) => {
  if (performance.now() < clickSuppressUntil) return;
  const el = e.target.closest('[data-act]');
  // D-pad users press Cross wherever the browser's cursor happens to rest:
  // act on the focused item unless the cursor was moved since.
  if (!S.cursor || !el) { input('confirm', 'click'); return; }
  act(el);
});

const pointerDown = (e) => {
  const el = e.target.closest('[data-act="dlg"]');
  if (el && S.dialog && Number(el.dataset.i) === S.dialog.hold) {
    S.dialog.focus = S.dialog.hold;
    startHold('ptr');
  }
};
const pointerUp = () => endHold('ptr');
document.addEventListener('mousedown', pointerDown);
document.addEventListener('mouseup', pointerUp);
document.addEventListener('touchstart', pointerDown, { passive: true });
document.addEventListener('touchend', pointerUp);

let wheelAcc = 0;
document.addEventListener('wheel', (e) => {
  wheelAcc += e.deltaY;
  while (Math.abs(wheelAcc) >= 60) {
    const a = wheelAcc > 0 ? 'down' : 'up';
    wheelAcc -= Math.sign(wheelAcc) * 60;
    const cur = S.cursor;
    input(a, 'wheel');
    S.cursor = cur;
  }
  e.preventDefault();
}, { passive: false });

// Circle in the PS5 browser is "back". Keep one spare history entry so it
// lands here instead of leaving the page.
let lastPadBack = 0, exitArmed = false;
history.replaceState({ g: 0 }, '');
history.pushState({ g: 1 }, '');
window.addEventListener('popstate', () => {
  if (performance.now() - lastPadBack < 400) { history.pushState({ g: 1 }, ''); return; }
  if (S.screen === 'home' && !S.dialog) {
    if (exitArmed) return;  // second press leaves
    exitArmed = true;
    toast('Press Circle again to close');
    setTimeout(() => { exitArmed = false; }, 2500);
    history.pushState({ g: 1 }, '');
    return;
  }
  input('back', 'history');
  history.pushState({ g: 1 }, '');
});

// ---------------------------------------------------------------- gamepad

const PAD = { 0: 'confirm', 1: 'back', 2: 'mark', 3: 'review', 4: 'tabprev', 5: 'tabnext',
              6: 'pageup', 7: 'pagedown', 8: 'menu', 9: 'menu', 12: 'up', 13: 'down', 14: 'left', 15: 'right' };
const padPrev = {}, padNext = {};
let rsAcc = 0, padT = 0;

function pollPad(t) {
  const pads = navigator.getGamepads ? navigator.getGamepads() : [];
  const dt = padT ? Math.min(0.1, (t - padT) / 1000) : 0;
  padT = t;
  for (const p of pads) {
    if (!p || !p.connected) continue;
    p.buttons.forEach((b, i) => {
      const a = PAD[i];
      if (!a) return;
      const down = b.pressed || b.value > 0.5, was = padPrev[i];
      if (down && !was) { padNext[i] = t + 380; padPress(a); }
      else if (down && REPEATS.has(a) && t >= padNext[i]) { padNext[i] = t + 65; input(a, 'pad'); }
      else if (!down && was && a === 'confirm') endHold('pad');
      padPrev[i] = down;
    });
    // Right stick: scroll, faster the further it is pushed.
    const ry = p.axes[3] || 0;
    if (Math.abs(ry) > 0.25) {
      const k = (Math.abs(ry) - 0.25) / 0.75;
      rsAcc += (3 + 27 * k * k) * dt * Math.sign(ry);
      while (rsAcc >= 1) { input('down', 'pad'); rsAcc -= 1; }
      while (rsAcc <= -1) { input('up', 'pad'); rsAcc += 1; }
    } else rsAcc = 0;
    break;
  }
  requestAnimationFrame(pollPad);
}

function padPress(a) {
  if (a === 'confirm') {
    if (S.cursor) return;  // the browser turns Cross into a click under the cursor
    clickSuppressUntil = performance.now() + 300;
    startHold('pad');
  }
  if (a === 'back') lastPadBack = performance.now();
  input(a, 'pad');
}
requestAnimationFrame(pollPad);

// ================================================================ chart

const R0 = 135, RING = [100, 82, 64, 48];
const chart = { key: '', el: null, segs: [], colors: new Map(), rootId: -1, hl: null };

function arcPath(r0, r1, a0, a1) {
  const gap = a1 - a0 > 0.02 ? 0.0035 : 0;
  a0 += gap; a1 -= gap; r0 += 1.5;
  if (a1 - a0 > TAU - 0.002) a1 = a0 + TAU - 0.002;
  const pt = (r, a) => (450 + Math.sin(a) * r).toFixed(1) + ',' + (450 - Math.cos(a) * r).toFixed(1);
  const large = a1 - a0 > Math.PI ? 1 : 0;
  return `M${pt(r1, a0)}A${r1},${r1} 0 ${large} 1 ${pt(r1, a1)}L${pt(r0, a1)}A${r0},${r0} 0 ${large} 0 ${pt(r0, a0)}Z`;
}

function buildChart(root) {
  const key = root.id + ':' + root.size + ':' + S.resultSeq;
  if (chart.key === key && chart.el) return;
  chart.key = key;
  chart.rootId = root.id;
  chart.segs = [];
  chart.colors = new Map();
  chart.hl = null;
  const lay = (kids, a0, a1, depth, hue, chain, parentSize) => {
    if (!kids || depth >= RING.length || !parentSize) return;
    let a = a0;
    for (const c of kids) {
      const span = (a1 - a0) * c.size / parentSize;
      if (span < 0.008) break;
      const h = depth === 0 ? (30 + 300 * a / TAU) % 360 : hue;
      const color = c.dir ? `hsl(${h.toFixed(0)},${92 - depth * 12}%,${52 + depth * 7}%)`
                          : `hsl(${h.toFixed(0)},30%,${82 - depth * 2}%)`;
      let r0 = R0;
      for (let i = 0; i < depth; i++) r0 += RING[i];
      const ch = chain.concat(c.id);
      chart.segs.push({ id: c.id, name: c.name, size: c.size, dir: c.dir, hasKids: !!(c.kids && c.kids.length),
                        a0: a, a1: a + span, r0, r1: r0 + RING[depth], color, chain: ch });
      if (depth === 0) chart.colors.set(c.id, color);
      if (c.kids) lay(c.kids, a, a + span, depth + 1, h, ch, c.size);
      a += span;
    }
  };
  lay(root.rings, 0, TAU, 0, 0, [], root.size);
  const paths = chart.segs.map((s, i) =>
    `<path data-act="seg" data-i="${i}" fill="${s.color}" d="${s.d = arcPath(s.r0, s.r1, s.a0, s.a1)}"/>`).join('');
  const el = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  el.setAttribute('viewBox', '0 0 900 900');
  el.setAttribute('id', 'chart');
  el.setAttribute('class', 'abs');
  el.innerHTML = paths;
  chart.el = el;
  chart.segs.forEach((s, i) => { s.el = el.childNodes[i]; });
}

// Light up the focused item's branch, dim the rest.
function lightChart(chain) {
  const fid = chain && chain.length ? chain[chain.length - 1] : null;
  for (const s of chart.segs) {
    const lit = fid == null || s.chain.includes(fid) || chain.includes(s.id);
    s.el.classList.toggle('dim', !lit);
  }
  if (chart.hl && chart.hl.id !== fid) {
    chart.hl.el.setAttribute('d', chart.hl.d);
    chart.hl.el.classList.remove('hl');
    chart.hl = null;
  }
  const s = fid == null ? null : chart.segs.find((x) => x.id === fid);
  if (s && chart.hl !== s) {
    s.el.setAttribute('d', arcPath(s.r0, s.r1 + 10, s.a0, s.a1));
    s.el.classList.add('hl');
    s.el.parentNode.appendChild(s.el);  // draw on top
    chart.hl = s;
  }
}

// ================================================================ render

function render() {
  const main = $('#main');
  let html = '';
  let hints = '';
  switch (S.screen) {
    case 'home': [html, hints] = renderHome(); break;
    case 'scanning': [html, hints] = renderProgress(false); break;
    case 'deleting': [html, hints] = renderProgress(true); break;
    case 'browse': [html, hints] = renderBrowse(); break;
    case 'review': [html, hints] = renderReview(); break;
    default: break;
  }
  main.innerHTML = html;
  $('#hints').innerHTML = hints;
  if (S.screen === 'browse' && chart.el) {
    main.insertBefore(chart.el, main.querySelector('.center'));
    const f = focusedItem();
    let chain = null;
    if (S.hover) chain = S.hover.chain;
    else if (f) chain = S.tab === 1 ? f.chain : [f.id];
    lightChart(chain || []);
  }
  renderDialog();
}

function hint(glyph, label, a) {
  const at = a ? ` data-act="hint" data-a="${a}"` : '';
  return `<div class="hint"${at}>${GLYPH[glyph]}<span>${esc(label)}</span></div>`;
}

function header(title, sub, tabs) {
  const st = S.storage;
  let h = `<div class="abs hdr-icon">${driveIcon(st ? st.kind : 'internal')}</div>
    <div class="abs hdr-title ellipsis">${esc(title)}</div>
    <div class="abs hdr-sub ellipsis">${esc(sub)}</div>`;
  if (tabs) {
    h += `<div class="abs tabs">${GLYPH.l1}<div class="track">
      <div class="tab${S.tab === 0 ? ' on' : ''}" data-act="tab" data-i="0">Folders</div>
      <div class="tab${S.tab === 1 ? ' on' : ''}" data-act="tab" data-i="1">Biggest Files</div>
      </div>${GLYPH.r1}</div>`;
  }
  if (st && st.total) {
    const used = 1 - st.free / st.total;
    h += `<div class="abs space">${fmtSize(st.free)} available of ${fmtSize(st.total)}
      <div class="bar"><i class="${used > 0.9 ? 'full' : ''}" style="width:${(used * 100).toFixed(1)}%"></i></div></div>`;
  }
  return h;
}

function renderHome() {
  let h = `<div class="abs home-title">Disk Space Analyzer</div>
    <div class="abs home-sub">Choose a storage to scan. Find what takes space, select it, clean it up.</div>
    <div class="abs cards">`;
  S.storages.forEach((s, i) => {
    const used = s.total ? 1 - s.free / s.total : 0;
    h += `<div class="card${i === S.hfocus ? ' focus' : ''}" data-act="storage" data-i="${i}">
      <div class="ico">${driveIcon(s.kind)}</div>
      <div class="name ellipsis">${esc(s.name)}</div>
      <div class="path ellipsis">${esc(s.path)}</div>
      <div class="bar"><i class="${used > 0.9 ? 'full' : ''}" style="width:${(used * 100).toFixed(1)}%"></i></div>
      <div class="info">${fmtSize(s.total - s.free)} used &middot; ${fmtSize(s.free)} free of ${fmtSize(s.total)}</div>
    </div>`;
  });
  h += '</div>';
  if (!S.storages.length) h += '<div class="abs empty">No storage found</div>';
  return [h, hint('dpad', 'Move') + hint('cross', 'Scan', 'confirm') + hint('options', 'Refresh', 'menu') +
             hint('lstick', 'Cursor')];
}

function renderProgress(deleting) {
  const p = S.scan || {};
  let h;
  if (!deleting) {
    h = `<div class="abs spinner"></div>
      <div class="abs spin-label">${fmtSize(p.bytes || 0)}</div>
      <div class="abs big-label">Scanning ${esc(S.storage ? S.storage.name : '')}</div>
      <div class="abs mid-label">${fmtCount(p.items || 0)} items found</div>
      <div class="abs path-label ellipsis">${esc(p.current || '')}</div>`;
    return [h, hint('circle', 'Cancel', 'back')];
  }
  const frac = p.goal ? Math.min(1, (p.freed || 0) / p.goal) : 0;
  h = `<div class="abs spinner red"></div>
    <div class="abs spin-label">${fmtSize(p.freed || 0)}</div>
    <div class="abs big-label">Cleaning up&hellip;</div>
    <div class="abs mid-label">Item ${Math.min((p.index || 0) + 1, p.total || 1)} of ${p.total || 1}</div>
    <div class="abs progress"><i style="width:${(frac * 100).toFixed(1)}%"></i></div>
    <div class="abs path-label ellipsis">${esc(p.current || '')}</div>`;
  return [h, '<div class="muted">Please wait, do not turn off the console.</div>'];
}

function checkbox(item, i) {
  if (item.protected) return `<div class="chk">${ICON.lock}</div>`;
  const icon = isMarked(item) ? ICON.checked : hasMarkedInside(item) ? ICON.partial : ICON.box;
  return `<div class="chk" data-act="check" data-i="${i}">${icon}</div>`;
}

function row(item, i, focused, style, parentSize) {
  const big = item.size >= 1e9;
  let mid, sub = '';
  if (style === 0) {
    const frac = parentSize ? item.size / parentSize : 0;
    const color = focused ? '#fff' : chart.colors.get(item.id) || (item.dir ? '#6fb1ff' : '#b9cce4');
    mid = `<div class="name ellipsis">${esc(item.name)}</div>
      <div class="rbar"><i style="width:${(frac * 100).toFixed(2)}%;background:${color}"></i></div>`;
    sub = item.mount ? 'volume' : item.error ? 'partly unreadable' : fmtPct(frac);
  } else {
    const folder = style === 1 ? item.folder : item.path.replace(/\/[^/]*$/, '') || '/';
    mid = `<div class="name ellipsis">${esc(item.name)}</div><div class="folder ellipsis">${esc(shortPath(folder))}</div>`;
    if (item.dir) sub = fmtCount(item.files) + ' files';
  }
  return `<div class="row${focused ? ' focus' : ''}" data-act="row" data-i="${i}">
    ${checkbox(item, i)}<div class="ico">${item.dir ? ICON.folder : ICON.file}</div>
    <div class="mid">${mid}</div>
    <div class="right"><div class="size${big ? ' big' : ''}">${fmtSize(item.size)}</div><div class="sub">${esc(sub)}</div></div>
  </div>`;
}

// Paths relative to the scanned storage read better.
function shortPath(p) {
  const root = S.storage ? S.storage.path : '';
  if (root && root !== '/' && p.startsWith(root)) return (S.storage.name || root) + p.slice(root.length);
  return p;
}

function scrollbar(first, total) {
  if (total <= VIS) return '';
  const h = 714, th = Math.max(40, h * VIS / total), ty = (h - th) * first / Math.max(1, total - VIS);
  return `<div class="abs scroll"><i style="top:${ty}px;height:${th}px"></i></div>`;
}

function renderBrowse() {
  const st = S.storage || {};
  let h = header(st.name || '', st.path || '', true) + '<div class="abs panel"></div>';
  if (S.tab === 0 && S.node) {
    const n = S.node;
    const title = n.id === S.rootId ? st.name : n.name;
    h += `<div class="abs panel-title ellipsis">${esc(title)}</div>
      <div class="abs panel-meta">${fmtSize(n.size)} &middot; ${fmtCount(n.files)} files</div><div class="abs rows">`;
    for (let i = S.scroll; i < n.children.length && i < S.scroll + VIS; i++)
      h += row(n.children[i], i, i === S.focus, 0, n.size);
    h += '</div>';
    if (!n.children.length) h += '<div class="abs empty">Empty folder</div>';
    h += scrollbar(S.scroll, n.children.length);
    buildChart(n);
  } else if (S.tab === 1) {
    h += `<div class="abs panel-title">Biggest files</div>
      <div class="abs panel-meta">Top ${fmtCount(S.biggest.length)} on this storage</div><div class="abs rows">`;
    for (let i = S.bscroll; i < S.biggest.length && i < S.bscroll + VIS; i++)
      h += row(S.biggest[i], i, i === S.bfocus, 1, 0);
    h += '</div>';
    if (!S.biggest.length) h += '<div class="abs empty">No files</div>';
    h += scrollbar(S.bscroll, S.biggest.length);
    if (S.rootNode) buildChart(S.rootNode);
  }

  // Center disc: what is under focus, relative to the chart root.
  const croot = S.tab === 0 ? S.node : S.rootNode;
  if (croot) {
    const f = focusedItem();
    const info = S.hover || f || croot;
    const name = info.id === S.rootId ? st.name : info.name;
    let p;
    if (info !== croot && croot.size) p = fmtPct(info.size / croot.size) + (S.tab === 1 ? ' of storage' : ' of folder');
    else p = fmtCount(croot.files) + ' files';
    h += `<div class="abs center" data-act="center"><div class="n ellipsis">${esc(name)}</div>
      <div class="s">${fmtSize(info.size)}</div><div class="p">${p}</div></div>`;
  }

  // Breadcrumb
  if (S.node) {
    const cr = S.node.crumbs;
    let parts = cr.map((c, i) => {
      const label = i === 0 ? st.name : c.name;
      const last = i === cr.length - 1;
      return `<span data-act="crumb" data-i="${c.id}">${last ? '<b>' + esc(label) + '</b>' : esc(label)}</span>`;
    });
    if (parts.length > 6) parts = [parts[0], '&hellip;'].concat(parts.slice(-4));
    h += `<div class="abs crumbs ellipsis">${parts.join(' &nbsp;&rsaquo;&nbsp; ')}</div>`;
  }

  let hints = S.tab === 0
    ? hint('cross', 'Open', 'confirm') + hint('circle', 'Back', 'back') + hint('square', 'Select', 'mark') +
      hint('options', 'Menu', 'menu') + hint('lstick', 'Cursor')
    : hint('cross', 'Show in folder', 'confirm') + hint('square', 'Select', 'mark') + hint('options', 'Menu', 'menu') +
      hint('lstick', 'Cursor');
  if (S.marked.size) {
    hints += `<div class="hint-right"><div class="pill" data-act="hint" data-a="review">${GLYPH.triangle}
      ${fmtCount(S.marked.size)} selected &middot; ${fmtSize(markedBytes())} &nbsp; Review &amp; clean</div></div>`;
  } else {
    hints += '<div class="hint-right muted">Press Square to select items to clean up</div>';
  }
  return [h, hints];
}

function renderReview() {
  const st = S.storage || {};
  const list = markedList();
  let h = header('Review & clean', (st.name || '') + ' · ' + (st.path || ''), false) +
    `<div class="abs panel"></div><div class="abs summary"></div>
    <div class="abs panel-title">Selected items</div>
    <div class="abs panel-meta">${fmtCount(list.length)} items</div><div class="abs rows">`;
  for (let i = S.rscroll; i < list.length && i < S.rscroll + VIS; i++)
    h += row(list[i], i, !S.rbutton && i === S.rfocus, 2, 0);
  h += '</div>' + scrollbar(S.rscroll, list.length);

  const bytes = markedBytes();
  let files = 0;
  const warns = [];
  for (const m of list) { files += m.files; if (m.warn && !warns.includes(m.warn)) warns.push(m.warn); }
  h += `<div class="abs sum-big">${fmtSize(bytes)}</div>
    <div class="abs sum-label">will be freed</div>
    <div class="abs sum-count">${fmtCount(list.length)} items &middot; ${fmtCount(files)} files</div>`;
  if (st.total) {
    const now = 1 - st.free / st.total, after = Math.max(0, now - bytes / st.total);
    h += `<div class="abs sum-space">Storage after cleanup
      <div class="bar"><i class="now" style="width:${(now * 100).toFixed(1)}%"></i><i class="after" style="width:${(after * 100).toFixed(1)}%"></i></div>
      <div class="row2"><b>${fmtSize(st.free + bytes)} available</b><span>now ${fmtSize(st.free)}</span></div></div>`;
  }
  if (warns.length) h += `<div class="abs warns">${warns.map((w) => `<div>${esc(w)}</div>`).join('')}</div>`;
  h += `<div class="abs delbtn${S.rbutton ? ' focus' : ''}" data-act="delbtn">${GLYPH.triangle}<span>Delete permanently</span></div>`;
  return [h, hint('square', 'Unselect', 'mark') + hint('triangle', 'Delete', 'review') + hint('cross', 'Show', 'confirm') +
             hint('circle', 'Back', 'back') + hint('dpad', 'Right: button')];
}

function renderDialog() {
  const box = $('#dialog');
  const d = S.dialog;
  if (!d) { box.innerHTML = ''; return; }
  const one = d.buttons.length === 1;
  box.innerHTML = `<div class="dlg"><h2>${esc(d.title)}</h2>
    ${d.lines.map((l) => `<p>${esc(l)}</p>`).join('')}
    ${d.warning ? `<div class="warn">${esc(d.warning)}</div>` : ''}
    <div class="btns${d.vertical ? ' v' : ''}${one ? ' one' : ''}">
      ${d.buttons.map((b, i) => `<div class="btn${i === d.focus ? ' focus' : ''}${i === d.hold && d.danger ? ' danger' : ''}"
         data-act="dlg" data-i="${i}">${i === d.hold ? '<i class="fill"></i>' : ''}<span>${esc(b)}</span></div>`).join('')}
    </div></div>`;
}

// ================================================================ status bar

// Version, and the address another device on the network can open.
async function loadInfo() {
  let info;
  try { info = await api('/api/info'); } catch (e) { return; }
  const local = /^(localhost|127\.|\[::1\])/.test(location.hostname);
  const host = info.ip || (local ? '' : location.hostname);
  const port = info.port || location.port;
  const url = host ? `http://${host}:${port}/` : `http://&lt;PS5 IP&gt;:${port}/`;
  $('#statusbar').innerHTML = `<span>Disk Space Analyzer <b>v${esc(info.version)}</b></span>
    <span>Open on a computer or phone: <b>${host ? esc(url) : url}</b></span>`;
}

// ================================================================ layout + start

function fit() {
  const k = Math.min(window.innerWidth / 1920, window.innerHeight / 1080);
  const stage = $('#stage');
  stage.style.transform = `translate(${(window.innerWidth - 1920 * k) / 2}px, ${(window.innerHeight - 1080 * k) / 2}px) scale(${k})`;
}
window.addEventListener('resize', fit);

async function start() {
  fit();
  render();
  loadInfo();
  let st = null;
  try { st = await api('/api/status'); } catch (e) { /* server just started */ }
  await loadStorages();
  // Reopening the tile resumes where you were.
  if (st && st.phase === 'scanning') {
    S.storage = st.storage;
    S.screen = 'scanning';
    S.scan = st;
    render();
    pollScan();
  } else if (st && st.phase === 'deleting') {
    S.storage = st.storage;
    S.screen = 'deleting';
    S.scan = st;
    render();
    pollDelete();
  } else if (st && st.phase === 'ready' && st.root != null) {
    await enterBrowse(st);
  } else {
    render();
  }
}
start();
