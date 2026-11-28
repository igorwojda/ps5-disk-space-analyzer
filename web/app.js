// Disk Space Analyzer web UI.
//
// Screens: home (pick storage) -> scanning -> browse (select) -> review -> deleting
// or moving. Square on home (or the menu) exports a report: pick one drive or
// all of them, the console scans them (reporting screen), and the HTML file
// with every folder and file is downloaded or saved on a drive. Cut (review screen or menu) keeps the items; Triangle (or the menu)
// pastes them into the open folder. Move to sends them to another storage.
// Browse has up to four tabs: Folders, Biggest Files, Games and Apps (each
// title's folders on every volume, added up). Games and Apps show only when the
// scanned storage holds a folder of one.
//
// Input, all mapped to the same actions:
//   D-pad / arrow keys      move focus          Cross / Enter      open, confirm
//   Circle / Esc / browser back                  back; on home closes the app
//   Square / Space          select             Triangle / Tab     review & clean, paste
//   L1 R1 / Q E             switch tab         L2 R2 / PgUp PgDn  page
//   Options / M             menu
// Browse lists have a menu bar above them (Select All, Rescan): Up from the
// first row moves focus into it, Down goes back to the list.
//   Left stick / mouse      cursor: hover focuses, Cross/click acts on what is under it
// The Gamepad API is used when the browser offers it; otherwise the PS5
// browser's own key events and cursor clicks do the job. The PS5 browser
// sends some buttons as function keys: Triangle F1, Square F2, L1 F5, R1 F6,
// L2 F7, R2 F8. Left unhandled, L1 makes the browser go back.
'use strict';

let VIS = 10;              // list rows on screen, fewer with bigger text
let RVIS = 10;             // review screen list rows (no menu bar above it)
let GVIS = 4;              // Games tab: game rows on screen (cover and a line per kind of data)
let PVIS = 6;              // Games tab: an open game's folder rows (two lines of details)
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

async function api(path, method = 'GET', body) {
  // X-DSA: the server takes changes only with it, which other sites' pages cannot send.
  const opt = { method, cache: 'no-store', headers: method === 'GET' ? {} : { 'X-DSA': '1' } };
  if (body != null) { opt.body = body; opt.headers['Content-Type'] = 'application/x-www-form-urlencoded'; }
  const r = await fetch(path, opt);
  let j = {};
  try { j = await r.json(); } catch (e) { /* empty body */ }
  if (!r.ok) { const e = new Error(j.error || 'HTTP ' + r.status); e.status = r.status; e.data = j; throw e; }
  return j;
}

// ================================================================ icons

const ICON = {
  folder: '<svg viewBox="0 0 40 40"><rect y="5" width="18" height="9" rx="2.5" fill="#3f7fc4"/><rect y="9" width="40" height="27" rx="3.5" fill="#58a6ff"/></svg>',
  file: '<svg viewBox="0 0 40 40"><path d="M7 2h18l8 8v28H7z" fill="#d5dfec"/><path d="M25 2v8h8z" fill="#97a8bd"/></svg>',
  lock: '<svg viewBox="0 0 30 30"><path d="M9 13V9a6 6 0 0 1 12 0v4" fill="none" stroke="rgba(255,255,255,.35)" stroke-width="3"/><rect x="4" y="13" width="22" height="15" rx="3" fill="rgba(255,255,255,.35)"/></svg>',
  box: '<svg viewBox="0 0 30 30"><rect x="1.5" y="1.5" width="27" height="27" rx="6.5" fill="none" stroke="rgba(255,255,255,.45)" stroke-width="2.5"/></svg>',
  checked: '<svg viewBox="0 0 30 30"><rect width="30" height="30" rx="7" fill="#2fbf55"/><path d="M7 15.5l5.5 5.5L23.5 9" fill="none" stroke="#fff" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  gear: '<svg class="gear" viewBox="0 0 24 24"><path fill="currentColor" d="M19.4 13a7.5 7.5 0 0 0 0-2l2.1-1.6-2-3.5-2.5 1a7.6 7.6 0 0 0-1.7-1L15 3.2h-4l-.4 2.7a7.6 7.6 0 0 0-1.7 1l-2.5-1-2 3.5L6.6 11a7.5 7.5 0 0 0 0 2l-2.2 1.6 2 3.5 2.5-1a7.6 7.6 0 0 0 1.7 1l.4 2.7h4l.4-2.7a7.6 7.6 0 0 0 1.7-1l2.5 1 2-3.5zM13 15.5a3.5 3.5 0 1 1 0-7 3.5 3.5 0 0 1 0 7z" transform="translate(-1 0)"/></svg>',
  report: '<svg class="gear" viewBox="0 0 24 24"><path fill="currentColor" d="M6 2h9l5 5v15H6zm8 1.5V8h4.5zM9 18h2v-5H9zm3.5 0h2v-8h-2zm3.5 0h2v-3h-2z" fill-rule="evenodd"/></svg>',
  info: '<svg class="gear" viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M12 2a10 10 0 1 1 0 20 10 10 0 0 1 0-20zm-1.3 8h2.6v8h-2.6zm1.3-4.6a1.6 1.6 0 1 1 0 3.2 1.6 1.6 0 0 1 0-3.2z"/></svg>',
  rescan: '<svg viewBox="0 0 24 24"><path d="M20 12a8 8 0 1 1-2.34-5.66" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round"/><path d="M21.5 2.5v8h-8z" fill="currentColor"/></svg>',
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

// ================================================================ settings

// The console's own browser.
const ON_PS5 = /PlayStation/i.test(navigator.userAgent);

// Kept in this browser's localStorage, so each device has its own.
const SETTINGS = [
  { key: 'fullscreen', label: 'Full screen', values: [false, true], def: false, ps5: true,
    show: (v) => (v ? 'On' : 'Off') },
  { key: 'textSize', label: 'Text size', values: [0.5, 0.6, 0.7, 0.8, 0.9, 1, 1.1, 1.2, 1.3, 1.4, 1.5], def: 1,
    show: (v) => Math.round(v * 100) + '%' },
  { key: 'theme', label: 'Theme', values: ['blue', 'black', 'contrast'], def: 'blue',
    show: (v) => ({ blue: 'Blue', black: 'Pure black', contrast: 'High contrast' })[v] },
].filter((d) => !d.ps5 || ON_PS5);
const SETTINGS_KEY = 'dsa.settings';
const settings = {};

function loadSettings() {
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem(SETTINGS_KEY)) || {}; } catch (e) { /* no storage */ }
  for (const d of SETTINGS) settings[d.key] = d.values.includes(saved[d.key]) ? saved[d.key] : d.def;
}
function saveSettings() {
  try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings)); } catch (e) { /* no storage */ }
}

const ROWS_H = 714, ROW_GAP = 6, ROWS_END = 928;
let LIST_H = ROWS_H;       // browse list height, below the menu bar
function applySettings() {
  const root = document.documentElement;
  const fs = settings.textSize;
  const rowH = Math.round(66 * fs);
  root.style.setProperty('--fs', fs);
  const tallH = Math.round(112 * fs), gameH = Math.round(170 * fs);
  root.style.setProperty('--row', rowH + 'px');
  root.style.setProperty('--row-tall', tallH + 'px');
  root.style.setProperty('--row-game', gameH + 'px');
  // Browse: the menu bar sits between the panel title and the list.
  const menuTop = 148 + Math.round(52 * fs), menuH = Math.round(56 * fs);
  const listTop = menuTop + menuH + 14;
  LIST_H = ROWS_END - listTop;
  root.style.setProperty('--menu-top', menuTop + 'px');
  root.style.setProperty('--menu-h', menuH + 'px');
  root.style.setProperty('--list-top', listTop + 'px');
  root.style.setProperty('--list-h', LIST_H + 'px');
  VIS = Math.floor((LIST_H + ROW_GAP) / (rowH + ROW_GAP));
  PVIS = Math.floor((LIST_H + ROW_GAP) / (tallH + ROW_GAP));
  GVIS = Math.floor((LIST_H + ROW_GAP) / (gameH + ROW_GAP));
  RVIS = Math.floor((ROWS_H + ROW_GAP) / (rowH + ROW_GAP));
  root.dataset.theme = settings.theme;
  // From 140% the home buttons would run into the title: they drop their icons.
  root.dataset.compact = fs >= 1.4 ? '1' : '';
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.content = settings.theme === 'blue' ? '#081d36' : '#000000';
  clampList('focus', 'scroll', S.node ? S.node.children.length : 0);
  clampList('bfocus', 'bscroll', S.biggest.length);
  clampList('rfocus', 'rscroll', S.marked.size, RVIS);
  clampList('gfocus', 'gscroll', S.gameList.length, GVIS);
  clampList('pfocus', 'pscroll', S.game ? S.game.parts.length : 0, PVIS);
  clampList('nfocus', 'nscroll', S.gnode ? S.gnode.children.length : 0);
}

// Step a setting through its values; applied (and saved) at once.
function stepSetting(i, dir) {
  const d = SETTINGS[i];
  const k = d.values.indexOf(settings[d.key]) + dir;
  if (k < 0 || k >= d.values.length) return;
  settings[d.key] = d.values[k];
  saveSettings();
  applySettings();
  render();
  if (d.key === 'fullscreen') applyFullscreen(false);
}

// ================================================================ full screen

// The patched browser gives the page the whole 1920x1080 screen.
function browserFullscreen() {
  return window.innerWidth >= 1920 && window.innerHeight >= 1080;
}

// Brings the browser in line with the Full screen setting. The server closes
// the browser, patches it and opens it again. auto: on start, once per run of
// the payload, so a patch that fails does not reopen the browser in a loop.
async function applyFullscreen(auto) {
  if (!ON_PS5) return;
  const full = settings.fullscreen;
  if (full === browserFullscreen() || (auto && !full)) return;
  let r;
  try {
    r = await api(`/api/browser-layout?full=${full ? 1 : 0}${auto ? '&auto=1' : ''}`, 'POST');
  } catch (e) {
    toast('Could not switch the browser: ' + e.message);
    return;
  }
  if (r.started) toast(full ? 'Switching to full screen…' : 'Switching back to a window…');
}

function openSettings() {
  showDialog({ title: 'Settings', settings: true, buttons: ['Close'], cancel: 0, focus: 0 });
}

let appVersion = '';  // from /api/info, once the server answers

const ABOUT = [
  ['Author', 'Igor Wojda'],
  ['License', 'GPL-3.0-or-later (free software)'],
  ['Source', 'github.com/igorwojda/ps5-disk-space-analyzer', 'https://github.com/igorwojda/ps5-disk-space-analyzer'],
];

function openAbout() {
  showDialog({ title: 'Disk Space Analyzer', about: true, buttons: ['Close'] });
}

// ================================================================ state

const S = {
  screen: 'home',
  storages: [], hfocus: 0,
  storage: null, rootId: 0, rootNode: null,
  node: null, tab: 0, focus: 0, scroll: 0,
  biggest: [], bfocus: 0, bscroll: 0,
  games: 'idle', allGames: [], gameList: [], gfocus: 0, gscroll: 0,  // Games or Apps tab: its list
  gkind: 2, gsaved: {},  // ... which of the two tabs gameList is for, and the other's place
  game: null, pfocus: 0, pscroll: 0,                  // ... and the open game's folders
  gpart: null, gnode: null, nfocus: 0, nscroll: 0,    // ... and a folder browsed inside one of them
  marked: new Map(),  // path -> item
  rfocus: 0, rscroll: 0,
  rbtn: -1,           // review: focused button (RBTN), -1 = the list
  mfocus: -1,         // browse: focused menu bar button (MENU), -1 = the list
  clip: null,         // cut items waiting for a destination folder, on any storage
  clipFrom: null,     // ... and the storage they were cut on
  clipMode: 'cut',    // 'cut' (Cut, Paste) or 'move' (Move to, Move here)
  dialog: null,
  cursor: false,      // the pointer (left stick / mouse) is in use
  hover: null,        // chart segment under the pointer
  scan: null,         // progress while scanning / deleting
  reportBack: 'home', // screen to return to once the report is made
  resultSeq: 0,
  scanSeq: 0,         // the server's scan the node ids on screen belong to
};

// Review screen buttons: Cut, Move to and Clear side by side, Delete below.
const RBTN = { cut: 0, move: 1, clear: 2, del: 3 };

let loadToken = 0;
let scanCancelled = false;

// ================================================================ helpers

function clampList(focusKey, scrollKey, count, vis = VIS) {
  S[focusKey] = Math.max(0, Math.min(S[focusKey], count - 1));
  if (S[focusKey] < S[scrollKey]) S[scrollKey] = S[focusKey];
  if (S[focusKey] >= S[scrollKey] + vis) S[scrollKey] = S[focusKey] - vis + 1;
  S[scrollKey] = Math.max(0, Math.min(S[scrollKey], Math.max(0, count - vis)));
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
  if (S.marked.has(item.path)) return true;
  for (const m of S.marked.values()) if (item.path.startsWith(m.path + '/')) return true;
  return false;
}
function hasMarkedInside(item) {
  for (const m of S.marked.values()) if (m.path.startsWith(item.path + '/')) return true;
  return false;
}

function focusedItem() {
  if (S.tab === 1) return S.biggest[S.bfocus] || null;
  if (isGamesTab()) {
    if (S.gnode) return S.gnode.children[S.nfocus] || null;
    return (S.game ? S.game.parts[S.pfocus] : S.gameList[S.gfocus]) || null;
  }
  return S.node ? S.node.children[S.focus] || null : null;
}

// Games (2) and Apps (3) are the same view over a different part of the list.
function isGamesTab(t = S.tab) { return t === 2 || t === 3; }
function tabList(t) { return S.allGames.filter((g) => (g.akind === 'game') === (t === 2)); }
const tabWord = (games, apps) => (S.gkind === 3 ? apps : games);

// Games and Apps show only when one of their titles has a folder on the scanned
// storage. While the titles are still being counted the open tab stays.
function hasTab(t) {
  if (!isGamesTab(t)) return true;
  if (t === S.tab && S.games !== 'ready') return true;
  return S.allGames.some((g) => (g.akind === 'game') === (t === 2) && g.parts.some((p) => p.id != null));
}
function shownTabs() { return [0, 1, 2, 3].filter(hasTab); }

// Switch tabs; between Games and Apps each keeps its own open title and focus.
function setTab(t) {
  if (isGamesTab(t) && t !== S.gkind) {
    S.gsaved[S.gkind] = { gfocus: S.gfocus, gscroll: S.gscroll, gid: S.game ? S.game.gid : '',
      pfocus: S.pfocus, pscroll: S.pscroll, ppath: S.gpart ? S.gpart.path : '', gnode: S.gnode,
      nfocus: S.nfocus, nscroll: S.nscroll };
    ++loadToken;  // a folder still opening belongs to the other tab
    const v = S.gsaved[t] || {};
    S.gkind = t;
    S.gameList = tabList(t);
    S.gfocus = v.gfocus || 0; S.gscroll = v.gscroll || 0;
    S.pfocus = v.pfocus || 0; S.pscroll = v.pscroll || 0;
    S.nfocus = v.nfocus || 0; S.nscroll = v.nscroll || 0;
    S.game = v.gid ? S.gameList.find((g) => g.gid === v.gid) || null : null;
    S.gpart = S.game && v.ppath ? S.game.parts.find((p) => p.path === v.ppath) || null : null;
    S.gnode = S.gpart ? v.gnode : null;
    clampList('gfocus', 'gscroll', S.gameList.length, GVIS);
    clampList('pfocus', 'pscroll', S.game ? S.game.parts.length : 0, PVIS);
  }
  S.tab = t;
}

let toastTimer = 0;
// Escape text and draw button names (Cross, Square...) as their glyphs.
const glyphText = (s) => esc(s).replace(/\b(Cross|Circle|Square|Triangle)\b/g,
  (m) => `<span class="ig">${GLYPH[m.toLowerCase()]}</span>`);

function toast(msg) {
  const t = $('#toast');
  t.innerHTML = glyphText(msg);
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

// A game's folders, shaped like list items so selection and review treat them
// like any folder. `cid` is the chart's id for it.
// The install folder's label: "Game (PS5)", "App (PS4)", "App (Payload)".
function installLabel(kind, platform) {
  if (kind === 'webapp') return 'App (Payload)';
  const what = kind === 'app' ? 'App' : 'Game';
  return platform ? `${what} (${platform})` : what;
}
function gameItem(g) {
  const title = g.title || g.id;
  const kind = g.kind || 'game';
  const gameFiles = installLabel(kind, g.platform);
  const parts = g.parts.map((p) => ({
    id: p.node != null ? p.node : null, parentId: p.parent != null ? p.parent : -1,
    cid: 'p:' + p.path, gid: g.id, path: p.path, name: p.cat === 'Game' ? gameFiles : p.cat, title, volume: p.volume,
    size: p.size, files: p.files, dir: true, error: p.error, protected: p.protected, warn: p.warn,
    uninstall: p.uninstall, removedWith: p.removedWith,
  }));
  // The install folder first, then the rest biggest first (as sent).
  const main = (p) => (MAIN_FILES.includes(p.name) ? 0 : 1);
  parts.sort((a, b) => main(a) - main(b));
  return { kind: 'game', akind: kind, id: 'g:' + g.id, cid: 'g:' + g.id, gid: g.id, name: title, sub: g.title ? g.id : '', title: g.title,
           icon: g.icon, size: g.size, files: g.files, dir: true, parts };
}

let gamesTimer = 0;
async function loadGames() {
  clearTimeout(gamesTimer);
  let r;
  try { r = await api('/api/games'); } catch (e) { gamesTimer = setTimeout(loadGames, 2000); return; }
  const tabsBefore = shownTabs().join();
  S.games = r.state;
  S.allGames = r.games.map(gameItem);
  S.gameList = tabList(S.gkind);
  if (S.game) S.game = S.gameList.find((g) => g.gid === S.game.gid) || null;
  if (S.gpart) {
    const p = S.game && S.game.parts.find((x) => x.path === S.gpart.path);
    if (p) S.gpart = p; else S.gpart = S.gnode = null;
  }
  clampList('gfocus', 'gscroll', S.gameList.length, GVIS);
  clampList('pfocus', 'pscroll', S.game ? S.game.parts.length : 0, PVIS);
  if (gamesPlace) await restoreGames(r.state !== 'counting');
  if (r.state === 'counting') gamesTimer = setTimeout(loadGames, 1000);
  // The open tab has nothing left on this storage: back to Folders.
  if (!hasTab(S.tab)) { S.tab = 0; S.mfocus = -1; S.hover = null; }
  if (S.screen === 'browse' && (isGamesTab() || shownTabs().join() !== tabsBefore)) render();
}

async function startScan(storage) {
  try { await api('/api/scan?path=' + encodeURIComponent(storage.path), 'POST'); } catch (e) {
    toast('Could not start the scan: ' + e.message);
    return false;
  }
  S.storage = storage;
  S.marked.clear();
  S.screen = 'scanning';
  S.scan = { items: 0, bytes: 0, current: '' };
  scanCancelled = false;
  render();
  pollScan();
  return true;
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
  // A cancelled refresh keeps the previous scan: back to it, same folder.
  const place = restorePlace;
  restorePlace = null;
  if (st.phase === 'ready' && (!scanCancelled || place)) await enterBrowse(st, place);
  else { S.screen = 'home'; await loadStorages(); render(); }
}

async function enterBrowse(st, place) {
  S.storage = st.storage;
  S.rootId = st.root;
  S.rootNode = null;
  S.tab = 0;
  S.resultSeq = st.result ? st.result.seq : 0;
  S.scanSeq = st.scan || 0;
  S.screen = 'browse';
  S.mfocus = -1;
  S.game = S.gpart = S.gnode = null;
  S.gfocus = S.gscroll = 0;
  const back = place && place.storage === S.storage.path ? place : null;
  S.gsaved = {};
  S.games = 'counting';  // the scan just ended, so the titles are counted again
  S.gkind = back && back.tab === 3 ? 3 : 2;
  S.gameList = tabList(S.gkind);
  gamesPlace = back && back.gid ? back : null;
  loadGames();
  await loadBiggest();
  if (back) {
    S.tab = back.tab || 0;
    if (!hasTab(S.tab)) S.tab = 0;  // its titles are gone from this storage
    const i = back.bpath ? S.biggest.findIndex((x) => x.path === back.bpath) : -1;
    if (i >= 0) { S.bfocus = i; S.bscroll = Math.max(0, i - (VIS >> 1)); clampList('bfocus', 'bscroll', S.biggest.length); }
    await openPath(back.path, back.focus);
  } else {
    await openNode(st.root);
  }
}

// ---------------------------------------------------------------- refresh

// Files may change behind the app's back (another app, another device): a
// rescan brings the drive up to date, then the same folder opens again. Node
// ids change with every scan, so the folder is found again by its path.
const PLACE_KEY = 'dsa.place';
let restorePlace = null;   // where to go once the running rescan ends
let gamesPlace = null;     // the Games tab part of it, once the games are counted

// Everything on screen in every tab: the open folder, the focused file in
// Biggest Files, and in Games the open game, its folder and a folder in it.
function currentPlace() {
  const f = S.node ? S.node.children[S.focus] : null;
  const b = S.biggest[S.bfocus];
  const gf = S.game ? null : S.gameList[S.gfocus];
  const pf = S.game && !S.gpart ? S.game.parts[S.pfocus] : null;
  const nf = S.gnode ? S.gnode.children[S.nfocus] : null;
  return {
    storage: S.storage ? S.storage.path : '', tab: S.tab,
    path: S.node ? S.node.path : '', focus: f ? f.name : '',
    bpath: b ? b.path : '',
    gid: S.game ? S.game.gid : gf ? gf.gid : '', gopen: !!S.game,
    ppath: S.gpart ? S.gpart.path : pf ? pf.path : '', popen: !!S.gpart,
    npath: S.gnode ? S.gnode.path : '', nfocus: nf ? nf.name : '',
  };
}

// Kept so that reopening the tile comes back to the same place too. Not while
// a rescan is coming back to it: that would save the half-restored screen.
function savePlace() {
  if (S.screen !== 'browse' || restorePlace || gamesPlace) return;
  try { localStorage.setItem(PLACE_KEY, JSON.stringify(currentPlace())); } catch (e) { /* no storage */ }
}

// The child folders from `n` down to `path`, by name; null when one is gone.
async function walkTo(n, path) {
  const base = n.path.endsWith('/') ? n.path : n.path + '/';
  if (!path || path === n.path) return n;
  if (!path.startsWith(base)) return null;
  for (const name of path.slice(base.length).split('/').filter(Boolean)) {
    const c = n.children.find((x) => x.name === name && x.dir && !x.mount);
    if (!c) return null;
    try { n = await api('/api/node?id=' + c.id); } catch (e) { return null; }
  }
  return n;
}

// Games tab: the game, its folder and the folder inside it, once listed.
// `done`: the count is finished, so what is still missing is gone.
async function restoreGames(done) {
  const p = gamesPlace;
  if (S.game) { gamesPlace = null; return; }  // a game opened by hand meanwhile
  const g = S.gameList.find((x) => x.gid === p.gid);
  if (!g) { if (done) gamesPlace = null; return; }
  if (!p.gopen) {
    S.gfocus = S.gameList.indexOf(g);
    S.gscroll = Math.max(0, S.gfocus - (GVIS >> 1));
    clampList('gfocus', 'gscroll', S.gameList.length, GVIS);
    gamesPlace = null;
    return;
  }
  const part = g.parts.find((x) => x.path === p.ppath);
  if (!part && !done && p.ppath) return;  // its folders are still being counted
  gamesPlace = null;
  S.game = g;
  S.gpart = S.gnode = null;
  S.pfocus = part ? g.parts.indexOf(part) : 0;
  S.pscroll = Math.max(0, S.pfocus - (PVIS >> 1));
  clampList('pfocus', 'pscroll', g.parts.length, PVIS);
  if (!part || !p.popen || part.id == null) return;
  let n;
  try { n = await api('/api/node?id=' + part.id); } catch (e) { return; }
  n = (await walkTo(n, p.npath)) || n;
  if (S.game !== g) return;  // left the game meanwhile
  S.gpart = part;
  const f = p.nfocus ? n.children.find((x) => x.name === p.nfocus) : null;
  await openGameNode(n.id, f ? f.id : null);
}
function loadPlace() {
  try { return JSON.parse(localStorage.getItem(PLACE_KEY)); } catch (e) { return null; }
}

// Open the folder at `path`, walking down from the root by name. A folder that
// is gone (deleted or moved meanwhile) opens the drive root instead.
async function openPath(path, focusName) {
  let root;
  try { root = await api('/api/node?id=' + S.rootId); } catch (e) { return openNode(S.rootId); }
  const n = await walkTo(root, path);
  if (!n) {
    toast(`${path.slice(root.path.length).replace(/^\//, '')} no longer exists: showing the drive root`);
    return openNode(S.rootId);
  }
  const f = focusName ? n.children.find((x) => x.name === focusName) : null;
  return openNode(n.id, f ? f.id : null);
}

// Rescan the open drive and come back to `place` (the folder open now).
async function refreshScan(place = currentPlace()) {
  // Coming back often fires more than one event (visibilitychange, pageshow):
  // a second refresh would be refused as busy and lose the folder to return to.
  if (!S.storage || restorePlace) return false;
  const had = S.marked.size;
  // A selection holds this scan's ids: after a rescan they point elsewhere.
  S.dialog = null;
  stopHold();
  restorePlace = place;
  if (!(await startScan(S.storage))) { restorePlace = null; render(); return false; }
  if (had) toast('Selection cleared: the drive is being rescanned');
  return true;
}

// Back in the app after using another one: what was on screen may be stale.
let hiddenAt = 0;
function onReturn() {
  if (S.screen === 'browse' || S.screen === 'review') refreshScan();
  else if (S.screen === 'home') loadStorages().then(render);
}
document.addEventListener('visibilitychange', () => {
  if (document.hidden) { hiddenAt = Date.now(); return; }
  if (hiddenAt && Date.now() - hiddenAt > 1000) onReturn();
  hiddenAt = 0;
});
// Restored from the browser's page cache: no visibilitychange on some browsers.
window.addEventListener('pageshow', (e) => { if (e.persisted) onReturn(); });

function cancelScan() {
  scanCancelled = true;
  api('/api/cancel', 'POST').catch(() => {});
  toast('Scan cancelled');
}

// ================================================================ menu bar

// The list on screen in the browse tab, with its focus and scroll keys.
function browseList() {
  if (S.tab === 1) return { fk: 'bfocus', sk: 'bscroll', list: S.biggest };
  if (isGamesTab()) {
    if (S.gnode) return { fk: 'nfocus', sk: 'nscroll', list: S.gnode.children };
    if (S.game) return { fk: 'pfocus', sk: 'pscroll', list: S.game.parts };
    return { fk: 'gfocus', sk: 'gscroll', list: S.gameList };
  }
  return { fk: 'focus', sk: 'scroll', list: S.node ? S.node.children : [] };
}

// Items Select All can take: not protected locations or other volumes.
function selectable() {
  return browseList().list.filter((x) => (x.kind === 'game' ? gameMarkState(x) !== 'locked' : !x.protected));
}
function allSelected(list) {
  return list.length > 0 && list.every((x) => (x.kind === 'game' ? gameMarkState(x) === 'all' : isMarked(x)));
}

// Selects every item in the open list; when all already are, unselects them.
function selectAll() {
  if (S.clip) { toast(`${S.clipMode === 'move' ? 'Move' : 'Paste'} the waiting items first, or cancel in the Options menu`); return; }
  const list = selectable();
  if (!list.length) { toast('Nothing here can be selected'); return; }
  // A game row stands for its folders.
  const items = list.flatMap((x) => (x.kind === 'game' ? x.parts.filter((p) => !p.protected) : [x]));
  if (allSelected(list)) {
    for (const x of items) S.marked.delete(x.path);
    toast(items.some(isMarked) ? 'Some items stay selected: a parent folder is selected' : 'Selection cleared');
  } else {
    for (const x of items) if (!isMarked(x)) addMark(x);
    toast(`${fmtCount(list.length)} ${list.length === 1 ? 'item' : 'items'} selected`);
  }
  render();
}

const MENU = [
  { label: () => (allSelected(selectable()) ? 'Unselect All' : 'Select All'),
    icon: () => (allSelected(selectable()) ? ICON.checked : ICON.box), run: selectAll },
  { label: () => 'Rescan', icon: () => ICON.rescan, run: () => refreshScan() },
];

function menuInput(a) {
  if (a === 'left') S.mfocus = Math.max(0, S.mfocus - 1);
  else if (a === 'right') S.mfocus = Math.min(MENU.length - 1, S.mfocus + 1);
  else if (a === 'down' || a === 'back') S.mfocus = -1;
  else if (a === 'confirm') { MENU[S.mfocus].run(); return; }
  else return;
  render();
}

function menuBar() {
  return `<div class="abs menubar">${MENU.map((m, i) =>
    `<div class="mbtn${S.mfocus === i ? ' focus' : ''}" data-act="mbtn" data-i="${i}">
      <span class="mico">${m.icon()}</span><span>${esc(m.label())}</span></div>`).join('')}</div>`;
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

function addMark(item) {
  for (const [k, m] of S.marked) if (m.path.startsWith(item.path + '/')) S.marked.delete(k);
  // Remember the folder it lives in, so the review screen can show it.
  const cur = isGamesTab() && S.gnode ? S.gnode : S.node;
  const parentId = item.parentId != null ? item.parentId : cur ? cur.id : S.rootId;
  S.marked.set(item.path, Object.assign({ parentId }, item));
}

// A game row selects (or unselects) all of its folders at once.
function gameMarkState(g) {
  const parts = g.parts.filter((p) => !p.protected);
  const n = parts.filter(isMarked).length;
  if (!parts.length) return 'locked';
  return n === parts.length ? 'all' : n || parts.some(hasMarkedInside) ? 'some' : 'none';
}
function toggleGame(g) {
  const st = gameMarkState(g);
  if (st === 'locked') { toast('Protected system location: it cannot be deleted'); return; }
  if (st === 'all') {
    for (const p of g.parts) S.marked.delete(p.path);
    if (g.parts.some(isMarked)) toast('Some folders stay selected: a parent folder is selected');
  } else {
    for (const p of g.parts) if (!p.protected && !isMarked(p)) addMark(p);
  }
  render();
}

// The system partition is only looked at: the server marks all of it protected.
const readOnlyScan = () => !!(S.storage && S.storage.kind === 'system');

function toggleMark(item) {
  if (!item) return;
  if (S.clip && S.screen !== 'review') { toast(`${S.clipMode === 'move' ? 'Move' : 'Paste'} the waiting items first, or cancel in the Options menu`); return; }
  if (item.kind === 'game') { toggleGame(item); return; }
  if (S.marked.has(item.path)) {
    S.marked.delete(item.path);
  } else if (item.protected) {
    toast(item.mount ? 'Separate volume: scan it from the storage list'
                     : readOnlyScan() ? 'The system partition is read-only: nothing on it can be deleted'
                     : 'Protected system location: it cannot be deleted');
    return;
  } else if (isMarked(item)) {
    toast('Already included: a parent folder is selected');
    return;
  } else {
    addMark(item);
  }
  render();
}

function openGame(g) {
  if (!g) return;
  S.game = g;
  S.gpart = S.gnode = null;
  S.pfocus = S.pscroll = 0;
  S.hover = null;
  render();
}
function closeGame() {
  if (!S.game) return;
  const i = S.gameList.indexOf(S.game);
  ++loadToken;
  S.game = S.gpart = S.gnode = null;
  S.hover = null;
  if (i >= 0) { S.gfocus = i; clampList('gfocus', 'gscroll', S.gameList.length, GVIS); }
  render();
}

// Games tab: browse inside one of a game's folders without leaving the tab.
function openPart(p) {
  if (!p) return;
  if (p.id == null) { toast(`On ${p.volume}: scan that storage to browse this folder`); return; }
  S.gpart = p;
  openGameNode(p.id);
}
async function openGameNode(id, focusId) {
  const token = ++loadToken;
  let n;
  try { n = await api('/api/node?id=' + id); } catch (e) {
    if (token !== loadToken) return;
    if (e.status !== 404) toast('Could not open folder: ' + e.message);
    if (S.gpart && id !== S.gpart.id) openGameNode(S.gpart.id); else closePart();
    return;
  }
  if (token !== loadToken || !S.gpart) return;
  S.gnode = n;
  S.hover = null;
  S.nfocus = 0;
  if (focusId != null) {
    const i = n.children.findIndex((c) => c.id === focusId);
    if (i >= 0) S.nfocus = i;
  }
  S.nscroll = Math.max(0, S.nfocus - (VIS >> 1));
  clampList('nfocus', 'nscroll', n.children.length);
  render();
}
// Up a folder; past the game folder itself, back to the game's folder list.
function gameUp() {
  const n = S.gnode;
  if (!n || n.id === S.gpart.id || n.parent < 0) closePart();
  else openGameNode(n.parent, n.id);
}
function closePart() {
  ++loadToken;
  const i = S.game && S.gpart ? S.game.parts.indexOf(S.gpart) : -1;
  S.gpart = S.gnode = null;
  S.hover = null;
  if (i >= 0) { S.pfocus = i; clampList('pfocus', 'pscroll', S.game.parts.length, PVIS); }
  render();
}

function openReview() {
  if (!S.marked.size) { toast('Nothing selected yet: press Square on an item'); return; }
  S.screen = 'review';
  S.rfocus = S.rscroll = 0;
  S.rbtn = -1;
  render();
}

function askDelete() {
  const list = markedList();
  if (!list.length) return;
  // Every distinct warning: an uninstall through the console must not hide
  // behind another item's note.
  // A game being uninstalled takes its update, add-ons and home screen info
  // with it: their own warnings (about deleting only part of a game) are moot.
  let files = 0;
  const gone = new Set(list.map((m) => m.uninstall).filter(Boolean));
  const warns = [];
  for (const m of list) {
    files += m.files;
    const moot = !m.uninstall && m.removedWith && gone.has(m.removedWith);
    if (m.warn && !moot && !warns.includes(m.warn)) warns.push(m.warn);
  }
  showDialog({
    title: `Delete ${fmtCount(list.length)} ${list.length === 1 ? 'item' : 'items'}?`,
    lines: [`${fmtSize(markedBytes())} in ${fmtCount(files)} files will be permanently deleted.`, 'This cannot be undone.'],
    warning: warns.slice(0, 3).join(' '),
    buttons: ['Cancel', 'Hold to delete'],
    cancel: 0, hold: 1, focus: 1, danger: true,
    onChoose: (i) => { if (i === 1) runDelete(list); },
  });
}

async function runDelete(list) {
  // Scanned items go by id; game folders on other volumes by path.
  const ids = list.filter((m) => typeof m.id === 'number').map((m) => m.id);
  const paths = list.filter((m) => typeof m.id !== 'number').map((m) => m.path);
  const body = 'scan=' + S.scanSeq + '&ids=' + ids.join(',') + '&paths=' + encodeURIComponent(paths.join('\n'));
  try { await api('/api/delete', 'POST', body); } catch (e) {
    if (e.data && e.data.stale) return reloadStale(e.message);
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
  await reloadAfterChange(st);
  showDialog({
    title: r.errors || (r.notes || []).length ? 'Cleanup finished with problems' : 'Cleanup complete',
    lines: [`Freed ${fmtSize(r.freed)}.`, `${fmtCount(r.removed)} of ${fmtCount(r.total)} selected items removed.`],
    // The server's own words first (an uninstall the console refused), then the count.
    warning: (r.notes || []).slice(0, 3).concat(
      r.errors && !(r.notes || []).length ? [`${fmtCount(r.errors)} entries could not be deleted (in use or read-only).`] : []).join(' '),
    buttons: ['OK'],
  });
}

// ---------------------------------------------------------------- move

const clipBytes = () => (S.clip || []).reduce((t, m) => t + m.size, 0);

// Keep the selection to move into a folder picked afterwards.
function cutSelection() {
  const list = markedList();
  if (!list.length) return;
  S.clip = list;
  S.clipFrom = S.storage ? { path: S.storage.path, name: S.storage.name } : null;
  S.clipMode = 'cut';
  S.marked.clear();
  S.screen = 'browse';
  if (S.tab === 1) S.tab = 0;
  render();
  toast(`Cut ${fmtCount(list.length)}: open a folder on any storage, then press Triangle to paste`);
}

function cancelCut() {
  toast(S.clipMode === 'move' ? 'Move cancelled' : 'Cut cancelled');
  S.clip = null;
  render();
}

// The folder on screen, when cut items can go there.
function pasteFolder() {
  if (S.tab === 0 && S.node) return S.node;
  if (isGamesTab() && S.gnode) return S.gnode;
  return null;
}

function pasteHere() {
  const n = pasteFolder();
  if (!n) { toast('Open a folder in the Folders tab to paste the items there'); return; }
  if (readOnlyScan()) { toast('The system partition is read-only: items cannot be moved here'); return; }
  if (n.protected && n.id !== S.rootId) { toast('Protected system location: items cannot be moved here'); return; }
  if (S.clip.some((m) => n.path === m.path || n.path.startsWith(m.path + '/'))) {
    toast('A folder cannot be moved into itself');
    return;
  }
  askMove(S.clip, n.path, nodeName(n), () => { S.clip = null; },
          { paste: true, verb: S.clipMode === 'move' ? 'Move' : 'Paste', from: S.clipFrom, to: S.storage });
}

// Move the selection to another storage: pick one from the list.
// Move the selection: pick a storage (this one too), then a folder on it;
// Triangle moves the items there. Uses the same waiting list as Cut.
async function askMoveTo() {
  const list = markedList();
  if (!list.length) return;
  await loadStorages();
  const cur = S.storage ? S.storage.path : '';
  const targets = S.storages.filter((s) => s.kind !== 'system')
    .sort((a, b) => (b.path === cur) - (a.path === cur));
  if (!targets.length) return;
  showDialog({
    title: 'Move to',
    lines: [`Choose the storage for the ${fmtCount(list.length)} selected ${list.length === 1 ? 'item' : 'items'} (${fmtSize(markedBytes())}), then pick a folder on it.`],
    vertical: true,
    buttons: targets.map((t) => `${t.name}${t.path === cur ? ' (this storage)' : ''} · ${fmtSize(t.free)} free`).concat(['Cancel']),
    cancel: targets.length,
    onChoose: (i) => { if (i < targets.length) pickMoveFolder(list, targets[i]); },
  });
}

function pickMoveFolder(list, target) {
  S.clip = list;
  S.clipFrom = S.storage ? { path: S.storage.path, name: S.storage.name } : null;
  S.clipMode = 'move';
  S.marked.clear();
  if (S.storage && target.path === S.storage.path) {
    S.screen = 'browse';
    S.tab = 0;
    openNode(S.rootId);
  } else {
    startScan(target);
  }
  toast('Open the folder to move to, then press Triangle');
}

// opt.paste: waiting items (sent by path); opt.verb: Move or Paste;
// opt.from / opt.to: source and destination storages.
function askMove(list, dest, destName, onStart, opt) {
  const paste = opt.paste, verb = opt.verb || 'Move';
  let files = 0, bytes = 0, warn = '';
  for (const m of list) { files += m.files; bytes += m.size; if (!warn && m.warn) warn = m.warn; }
  const lines = [`${fmtSize(bytes)} in ${fmtCount(files)} files will be moved to ${dest}.`];
  const cross = !!(opt.from && opt.to && opt.from.path !== opt.to.path);
  if (cross)
    lines.push('Moving to another storage copies the data first, then removes the original. This can take a while.');
  showDialog({
    title: `${verb} ${fmtCount(list.length)} ${list.length === 1 ? 'item' : 'items'} into ${destName}?`,
    lines, warning: warn,
    buttons: ['Cancel', verb], cancel: 0, focus: 1,
    onChoose: (i) => { if (i === 1) runMove(list, dest, destName, onStart, cross ? opt.from : null, paste); },
  });
}

// from: the source storage when the move leaves it (to report the space freed).
async function runMove(list, dest, destName, onStart, from, paste) {
  let body = 'dest=' + encodeURIComponent(dest);
  if (paste) {
    // Cut items may come from an earlier scan, maybe of another storage: by path.
    body += '&srcs=' + encodeURIComponent(list.map((m) => m.path).join('\n')) + '&sizes=' + list.map((m) => m.size).join(',');
  } else {
    const ids = list.filter((m) => typeof m.id === 'number').map((m) => m.id);
    const paths = list.filter((m) => typeof m.id !== 'number').map((m) => m.path);
    body += '&scan=' + S.scanSeq + '&ids=' + ids.join(',') + '&paths=' + encodeURIComponent(paths.join('\n'));
  }
  try { await api('/api/move', 'POST', body); } catch (e) {
    if (e.data && e.data.stale) return reloadStale(e.message);
    if (e.data && e.data.need) toast(`Not enough space on ${destName}: needs ${fmtSize(e.data.need)}, ${fmtSize(e.data.free)} free`);
    else toast('Move failed to start: ' + e.message);
    return;
  }
  if (onStart) onStart();
  // Only a move to another storage frees space on the source.
  const src = from && S.storages.find((x) => x.path === from.path);
  S.moveFrom = src ? { path: src.path, name: src.name, free: src.free } : null;
  S.moveTo = destName;
  S.screen = 'moving';
  S.scan = { index: 0, total: list.length, moved: 0, goal: list.reduce((t, m) => t + m.size, 0), current: '', dest };
  render();
  pollMove();
}

async function pollMove() {
  let st;
  try { st = await api('/api/status'); } catch (e) { setTimeout(pollMove, 1000); return; }
  if (st.phase === 'moving') {
    S.scan = st;
    render();
    setTimeout(pollMove, 300);
    return;
  }
  const r = st.result;
  const from = S.moveFrom;
  await reloadAfterChange(st);
  const lines = [`${fmtCount(r.removed)} of ${fmtCount(r.total)} items moved${S.moveTo ? ' to ' + S.moveTo : ''}.`];
  let freed = 0;
  if (from) {
    await loadStorages();
    const now = S.storages.find((x) => x.path === from.path);
    if (now) freed = now.free - from.free;
  }
  if (freed > 0) lines.push(`Freed ${fmtSize(freed)} on ${from.name}.`);
  showDialog({
    title: r.errors ? 'Move finished with problems' : 'Move complete',
    lines,
    warning: r.errors ? `${fmtCount(r.errors)} items could not be moved (a file with that name exists there, in use, or read-only).` : '',
    buttons: ['OK'],
  });
}

// Files changed on disk: fetch the folder, biggest files and games again.
async function reloadAfterChange(st) {
  S.storage = st.storage;
  S.resultSeq = st.result.seq;
  S.screen = 'browse';
  S.rootNode = null;
  loadGames();
  await loadBiggest();
  await openNode(S.node ? S.node.id : S.rootId, null);
  if (S.gnode) openGameNode(S.gnode.id);
}

// Another device scanned again since this one selected: the ids on screen name
// other files now. Drop the selection and show the new scan at the same place.
async function reloadStale(message) {
  S.marked.clear();
  S.clip = null;
  S.dialog = null;
  toast(message);
  let st;
  try { st = await api('/api/status'); } catch (e) { st = null; }
  if (st && st.phase === 'ready' && st.storage) await enterBrowse(st, currentPlace());
  else { S.screen = 'home'; await loadStorages(); }
  render();
}

// ---------------------------------------------------------------- report

// The PS5 browser cannot download files (ON_PS5): it saves the report on a drive.
// Choose what the report covers: one drive or all of them. `path`: the drive
// focused first. The one scanned already goes in without a new scan.
async function openExport(path) {
  await loadStorages();
  if (!S.storages.length) { toast('No storage found'); return; }
  let ready = null;
  try {
    const st = await api('/api/status');
    if (st.phase === 'ready' && st.storage) ready = st.storage.path;
  } catch (e) { /* every drive is scanned then */ }
  const all = S.storages.filter((s) => s.kind !== 'system');
  const items = [];
  if (all.length > 1) items.push([`All drives (${all.map((s) => s.name).join(', ')})`, 'all=1']);
  for (const s of S.storages)
    items.push([`${s.name} · ${fmtSize(s.total - s.free)} used${s.path === ready ? ' · scanned' : ''}`,
                'paths=' + encodeURIComponent(s.path)]);
  const focus = Math.max(0, items.findIndex((x) => x[1] === 'paths=' + encodeURIComponent(path)));
  items.push(['Cancel', null]);
  showDialog({
    title: 'Export Report',
    lines: ['Choose what the report covers. The drives are scanned first; a full drive takes a few minutes.'],
    vertical: true,
    buttons: items.map((x) => x[0]),
    cancel: items.length - 1, focus,
    onChoose: (i) => { if (items[i][1]) startReport(items[i][1]); },
  });
}

async function startReport(body) {
  let st;
  try { st = await api('/api/report/start', 'POST', body); } catch (e) {
    toast(e.status === 409 ? 'Busy: wait for the scan, cleanup or move to finish' : 'Could not start the report: ' + e.message);
    return;
  }
  S.reportBack = S.screen === 'browse' || S.screen === 'review' ? S.screen : 'home';
  S.screen = 'reporting';
  S.scan = st;
  render();
  pollReport();
}

async function pollReport() {
  let st;
  try { st = await api('/api/report/status'); } catch (e) { setTimeout(pollReport, 1000); return; }
  if (S.screen !== 'reporting') return;
  if (st.state === 'scanning' || st.state === 'games') {
    S.scan = st;
    render();
    setTimeout(pollReport, 400);
    return;
  }
  S.screen = S.reportBack;
  if (S.screen === 'home') await loadStorages();
  render();
  if (st.state === 'ready') showExport(st);
}

function cancelReport() {
  api('/api/report/cancel', 'POST').catch(() => {});
  S.screen = S.reportBack;
  render();
  toast('Report cancelled');
}

// Where the finished report goes.
async function showExport(st) {
  let targets = [];
  try { targets = await api('/api/report/targets'); } catch (e) { /* download only */ }
  const what = st.drives.length > 1 ? `${st.drives.length} drives (${st.drives.join(', ')})` : st.drives[0];
  const items = [];
  if (!ON_PS5) items.push(['Download to this device', downloadReport]);
  for (const t of targets) items.push([`Save to ${t.name}`, () => saveReport(t)]);
  if (!items.length) { toast('Plug in a USB drive to save the report'); return; }
  items.push(['Close', null]);
  showDialog({
    title: 'Report ready',
    lines: [`One HTML file (${fmtSize(st.size)}) with every folder and file on ${what} and the space each one takes.`,
            'It opens in any web browser, so you can keep it or send it to someone.'],
    vertical: true,
    buttons: items.map((x) => x[0]),
    cancel: items.length - 1,
    onChoose: (i) => { if (items[i][1]) items[i][1](); },
  });
}

function downloadReport() {
  // An attachment: the browser saves it and this page stays.
  window.location.href = '/api/report?download=1';
  toast('Preparing the report…');
}

async function saveReport(t) {
  toast(`Saving the report to ${t.name}…`);
  let r;
  try { r = await api('/api/report/save', 'POST', 'dest=' + encodeURIComponent(t.path)); } catch (e) {
    toast('Could not save the report: ' + e.message);
    return;
  }
  $('#toast').classList.remove('show');
  showDialog({
    title: 'Report saved',
    lines: [`${r.path} (${fmtSize(r.size)})`,
            t.kind === 'usb' ? 'Plug the USB drive into a computer to open the file or share it.'
                             : 'Copy it to a computer (for example over FTP) to open it or share it.'],
    buttons: ['OK'],
  });
  if (S.screen === 'home') loadStorages().then(render);
}

function openMenu() {
  const items = [['Refresh (rescan ' + (S.storage ? S.storage.name : '') + ')', () => refreshScan()],
                 ['Export Report', () => openExport(S.storage ? S.storage.path : '')]];
  if (S.clip) {
    const n = fmtCount(S.clip.length);
    const move = S.clipMode === 'move';
    items.push([`${move ? 'Move' : 'Paste'} ${n} ${S.clip.length === 1 ? 'item' : 'items'} here`, pasteHere],
               [`Cancel ${move ? 'move' : 'cut'} (${n} items)`, cancelCut]);
  } else if (S.marked.size) {
    items.push([`Cut ${fmtCount(S.marked.size)} selected`, cutSelection],
               ['Move selected to…', askMoveTo],
               ['Clear selection', clearSelection]);
  }
  items.push(['Choose another storage', leaveScan], ['Settings', openSettings], ['About', openAbout],
             ['Stop Disk Space Analyzer', stopPayload], ['Close', null]);
  showDialog({
    title: 'Menu',
    vertical: true,
    buttons: items.map((x) => x[0]),
    cancel: items.length - 1,
    onChoose: (i) => { if (items[i][1]) items[i][1](); },
  });
}

function openHomeMenu() {
  const items = [['Refresh storage list', () => loadStorages().then(() => { render(); toast('Storage list refreshed'); })]];
  const f = S.storages[S.hfocus];
  items.push(['Export Report', () => openExport(f ? f.path : '')]);
  if (S.clip) items.push([`Cancel ${S.clipMode === 'move' ? 'move' : 'cut'} (${fmtCount(S.clip.length)} items)`, cancelCut]);
  items.push(['Settings', openSettings], ['About', openAbout], ['Close', null]);
  showDialog({
    title: 'Menu',
    vertical: true,
    buttons: items.map((x) => x[0]),
    cancel: items.length - 1,
    onChoose: (i) => { if (items[i][1]) items[i][1](); },
  });
}

function leaveScan() {
  // Cut items stay: they can be pasted on another storage.
  const go = async () => { S.screen = 'home'; S.marked.clear(); await loadStorages(); render(); };
  const n = S.marked.size;
  if (!n) { go(); return; }
  showDialog({
    title: 'Leave this scan?',
    lines: ['You will return to the storage list.', `Your selection of ${fmtCount(n)} items will be cleared.`],
    buttons: ['Stay', 'Leave'], cancel: 0,
    onChoose: (i) => { if (i === 1) go(); },
  });
}

function stopPayload() {
  showDialog({
    title: 'Stop Disk Space Analyzer?',
    lines: ['The background payload stops. Send disk-space-analyzer.elf again to use the app.'],
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
  if (hold && !hold.done && (src == null || hold.src === src)) stopHold();
}
function stopHold() {
  hold = null;
  const f = document.querySelector('#dialog .fill');
  if (f) f.style.width = '0';
}
// The danger colour fills the button left to right while Cross is held;
// a full button deletes.
function tickHold() {
  if (!hold || hold.done || !S.dialog) return;
  const p = Math.min(1, (performance.now() - hold.t0) / HOLD_MS);
  const f = document.querySelector('#dialog .fill');
  if (f) f.style.width = (p * 100) + '%';
  if (p < 1) { requestAnimationFrame(tickHold); return; }
  hold.done = true;
  const d = S.dialog;
  setTimeout(() => { if (S.dialog === d) closeDialog(d.hold); }, 150);  // show it full first
}

// Settings dialog focus: 0..SETTINGS.length-1 are rows, SETTINGS.length is Close.
function settingsInput(a) {
  const d = S.dialog;
  const n = SETTINGS.length;
  if (a === 'up' || a === 'down') {
    d.focus = Math.max(0, Math.min(n, d.focus + (a === 'down' ? 1 : -1)));
    render();
  } else if ((a === 'left' || a === 'right') && d.focus < n) {
    stepSetting(d.focus, a === 'right' ? 1 : -1);
  } else if (a === 'confirm' && d.focus < n) {
    // Cross cycles through the values, wrapping around.
    const def = SETTINGS[d.focus];
    const k = def.values.indexOf(settings[def.key]);
    stepSetting(d.focus, k === def.values.length - 1 ? -k : 1);
  } else if (a === 'back' || a === 'menu' || a === 'confirm') {
    closeDialog(null);
  }
}

function dialogInput(a) {
  const d = S.dialog;
  if (d.settings) { settingsInput(a); return; }
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
let lastBack = -1e9;  // when the last Circle that did not come from history arrived

function input(a, src) {
  const now = performance.now();
  if (a === 'back' && src !== 'history') lastBack = now;
  // The same press can arrive as a key event and as a gamepad button.
  if (a === last.a && src !== last.src && now - last.t < 90) return;
  last = { a, src, t: now };
  if (['up', 'down', 'left', 'right', 'pageup', 'pagedown'].includes(a)) { S.cursor = false; S.hover = null; }

  if (S.dialog) { dialogInput(a); return; }
  switch (S.screen) {
    case 'home': homeInput(a); break;
    case 'scanning': if (a === 'back') cancelScan(); break;
    case 'reporting': if (a === 'back') cancelReport(); break;
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
  else if (a === 'menu') { openHomeMenu(); return; }
  else if (a === 'review') { openSettings(); return; }
  else if (a === 'tabnext') { openAbout(); return; }
  else if (a === 'mark') { openExport(S.storages[S.hfocus] ? S.storages[S.hfocus].path : ''); return; }
  render();
}

function browseInput(a) {
  if (a === 'tabprev' || a === 'tabnext') {
    const t = shownTabs(), n = t.length;
    setTab(t[(t.indexOf(S.tab) + (a === 'tabnext' ? 1 : n - 1)) % n]);
    S.hover = null; render(); return;
  }
  if (a === 'menu') { openMenu(); return; }
  if (a === 'review') { if (S.clip) pasteHere(); else openReview(); return; }
  if (S.mfocus >= 0) { menuInput(a); return; }
  if (a === 'up') {
    const b = browseList();
    if (S[b.fk] <= 0) { S.mfocus = 0; render(); return; }
  }
  if (isGamesTab()) { gamesInput(a); return; }
  if (S.tab === 1) {
    const n = S.biggest.length;
    const f = S.biggest[S.bfocus];
    if (a === 'up') S.bfocus--;
    else if (a === 'down') S.bfocus++;
    else if (a === 'pageup') S.bfocus -= VIS;
    else if (a === 'pagedown') S.bfocus += VIS;
    else if ((a === 'confirm' || a === 'right') && f) { reveal(f.id, f.parentId); return; }
    else if (a === 'mark') { toggleMark(f); return; }
    else if (a === 'back') { leaveScan(); return; }
    else if (a === 'left') return;
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

function gamesInput(a) {
  if (S.gnode) {
    const n = S.gnode, f = n.children[S.nfocus];
    if (a === 'up') S.nfocus--;
    else if (a === 'down') S.nfocus++;
    else if (a === 'pageup') S.nfocus -= VIS;
    else if (a === 'pagedown') S.nfocus += VIS;
    else if (a === 'confirm' || a === 'right') {
      if (f && f.dir && !f.mount) openGameNode(f.id);
      else if (f && a === 'confirm') toast(f.mount ? 'Separate volume: scan it from the storage list'
                                                   : 'Press Square to select this file for cleanup');
      return;
    } else if (a === 'left' || a === 'back') { gameUp(); return; }
    else if (a === 'mark') { toggleMark(f); return; }
    clampList('nfocus', 'nscroll', n.children.length);
    render();
    return;
  }
  const g = S.game;
  const [fk, sk, list] = g ? ['pfocus', 'pscroll', g.parts] : ['gfocus', 'gscroll', S.gameList];
  const vis = g ? PVIS : GVIS;
  const f = list[S[fk]];
  if (a === 'up') S[fk]--;
  else if (a === 'down') S[fk]++;
  else if (a === 'pageup') S[fk] -= vis;
  else if (a === 'pagedown') S[fk] += vis;
  else if (a === 'confirm' || a === 'right') { if (f) { if (g) openPart(f); else openGame(f); } return; }
  else if (a === 'mark') { toggleMark(f); return; }
  else if (a === 'back' || a === 'left') { if (g) closeGame(); else if (a === 'back') leaveScan(); return; }
  clampList(fk, sk, list.length, vis);
  render();
}

function reviewAction(b) {
  if (b === RBTN.cut) cutSelection();
  else if (b === RBTN.move) askMoveTo();
  else if (b === RBTN.clear) clearSelection();
  else askDelete();
}

function clearSelection() {
  S.marked.clear();
  S.screen = 'browse';
  render();
  toast('Selection cleared');
}

// Focus moves from the list to the buttons on the right:
//   [Cut] [Move to] [Clear]
//   [Delete permanently]
function reviewInput(a) {
  const list = markedList();
  const b = S.rbtn, inList = b < 0;
  if (a === 'up') { if (inList) S.rfocus--; else if (b === RBTN.del) S.rbtn = RBTN.cut; }
  else if (a === 'down') { if (inList) S.rfocus++; else if (b !== RBTN.del) S.rbtn = RBTN.del; }
  else if (a === 'pageup') S.rfocus -= RVIS;
  else if (a === 'pagedown') S.rfocus += RVIS;
  else if (a === 'right') { if (inList) S.rbtn = RBTN.cut; else if (b < RBTN.clear) S.rbtn = b + 1; }
  else if (a === 'left') S.rbtn = b === RBTN.move || b === RBTN.clear ? b - 1 : -1;
  else if (a === 'back') { S.screen = 'browse'; render(); return; }
  else if (a === 'mark' && inList && list[S.rfocus]) {
    toggleMark(list[S.rfocus]);
    if (!S.marked.size) { S.screen = 'browse'; render(); return; }
  } else if (a === 'confirm') {
    if (!inList) reviewAction(b);
    else if (list[S.rfocus]) revealPath(list[S.rfocus]);
    return;
  } else if (a === 'review') { askDelete(); return; }
  clampList('rfocus', 'rscroll', S.marked.size, RVIS);
  render();
}

// Items picked from the list or biggest files carry their parent id.
function revealPath(item) {
  if (item.parentId != null && item.parentId >= 0 && item.id != null) reveal(item.id, item.parentId);
  else if (item.parent != null) reveal(item.id, item.parent);
  else if (item.volume) toast(`On ${item.volume}: scan that storage to browse this folder`);
}

// ---------------------------------------------------------------- pointer actions

function act(el) {
  const k = el.dataset.act, i = Number(el.dataset.i);
  switch (k) {
    case 'storage': S.hfocus = i; startScan(S.storages[i]); break;
    case 'tab': setTab(i); S.hover = null; render(); break;
    case 'crumb': S.tab = 0; openNode(i); break;
    case 'check': {
      const item = S.screen === 'review' ? markedList()[i] : S.tab === 1 ? S.biggest[i]
        : isGamesTab() ? (S.gnode ? S.gnode.children[i] : S.game ? S.game.parts[i] : S.gameList[i]) : S.node.children[i];
      toggleMark(item);
      if (S.screen === 'review' && !S.marked.size) { S.screen = 'browse'; render(); }
      break;
    }
    case 'row':
      if (S.screen === 'review') { S.rfocus = i; S.rbtn = -1; revealPath(markedList()[i]); break; }
      if (S.tab === 1) { S.bfocus = i; reveal(S.biggest[i].id, S.biggest[i].parentId); break; }
      if (isGamesTab()) {
        if (S.gnode) {
          const c = S.gnode.children[i];
          S.nfocus = i;
          if (c.dir && !c.mount) openGameNode(c.id); else toggleMark(c);
        } else if (S.game) { S.pfocus = i; openPart(S.game.parts[i]); } else { S.gfocus = i; openGame(S.gameList[i]); }
        break;
      }
      S.focus = i;
      if (S.node.children[i].dir && !S.node.children[i].mount) enter(S.node.children[i]);
      else toggleMark(S.node.children[i]);
      break;
    case 'seg': {
      const s = chart.segs[i];
      if (!s) break;
      if (S.tab === 1) {
        const j = S.biggest.findIndex((c) => c.id === s.id);
        if (j >= 0) { S.bfocus = j; reveal(S.biggest[j].id, S.biggest[j].parentId); }
        break;
      }
      if (isGamesTab()) {
        if (S.gnode) {
          if (s.dir && s.hasKids) openGameNode(s.id);
          else openGameNode(s.chain.length > 1 ? s.chain[s.chain.length - 2] : chart.rootId, s.id);
          break;
        }
        if (S.game) { const p = S.game.parts.find((x) => x.cid === s.id); if (p) openPart(p); break; }
        const g = S.gameList.find((x) => x.cid === s.chain[0]);
        if (g) {
          openGame(g);
          const j = g.parts.findIndex((x) => x.cid === s.id);
          if (j >= 0) { S.pfocus = j; render(); }
        }
        break;
      }
      if (s.dir && s.hasKids) { S.tab = 0; openNode(s.id); }
      else reveal(s.id, s.chain.length > 1 ? s.chain[s.chain.length - 2] : chart.rootId);
      break;
    }
    case 'center': if (S.tab === 0) goUp(); else if (isGamesTab()) { if (S.gnode) gameUp(); else closeGame(); } break;
    case 'gcrumb': closeGame(); break;
    case 'gpcrumb': closePart(); break;
    case 'gncrumb': openGameNode(i); break;
    case 'hint': input(el.dataset.a, 'click'); break;
    case 'rbtn': S.rbtn = i; reviewAction(i); break;
    case 'mbtn': S.mfocus = i; render(); MENU[i].run(); break;
    case 'dlg':
      if (S.dialog && S.dialog.settings) closeDialog(null);
      else if (S.dialog && i !== S.dialog.hold) closeDialog(i);
      break;
    case 'set': if (S.dialog) { S.dialog.focus = i; settingsInput('confirm'); } break;
    case 'setv': stepSetting(i, Number(el.dataset.d)); break;
    default: break;
  }
}

function hover(el) {
  const k = el.dataset.act;
  let i = Number(el.dataset.i);
  let changed = false;
  if (k === 'storage' && S.hfocus !== i) { S.hfocus = i; changed = true; }
  if ((k === 'row' || k === 'check') && S.screen === 'browse' && S.mfocus >= 0) { S.mfocus = -1; changed = true; }
  if (k === 'mbtn' && S.mfocus !== i) { S.mfocus = i; changed = true; }
  if (k === 'row' || k === 'check') {
    if (S.screen === 'review') { if (S.rfocus !== i || S.rbtn >= 0) { S.rfocus = i; S.rbtn = -1; changed = true; } }
    else if (S.tab === 1) { if (S.bfocus !== i) { S.bfocus = i; changed = true; } }
    else if (isGamesTab()) {
      const fk = S.gnode ? 'nfocus' : S.game ? 'pfocus' : 'gfocus';
      if (S[fk] !== i) { S[fk] = i; changed = true; }
    } else if (S.focus !== i) { S.focus = i; changed = true; }
  }
  if (k === 'rbtn' && S.rbtn !== i) { S.rbtn = i; changed = true; }
  if (k === 'dlg' && S.dialog && S.dialog.settings) i = SETTINGS.length;
  if ((k === 'dlg' || k === 'set' || k === 'setv') && S.dialog && S.dialog.focus !== i) {
    S.dialog.focus = i; stopHold(); changed = true;
  }
  if (k === 'seg') {
    const s = chart.segs[i];
    if (S.hover !== s) {
      S.hover = s;
      // A segment selects its list item (or the list item it lives in) and scrolls to it.
      if (s && S.tab === 0 && S.node) {
        const j = S.node.children.findIndex((c) => c.id === s.chain[0]);
        if (j >= 0) { S.focus = j; clampList('focus', 'scroll', S.node.children.length); }
      } else if (s && S.tab === 1) {
        const j = S.biggest.findIndex((c) => c.id === s.id);
        if (j >= 0) { S.bfocus = j; clampList('bfocus', 'bscroll', S.biggest.length); }
      } else if (s && isGamesTab() && S.gnode) {
        const j = S.gnode.children.findIndex((c) => c.id === s.chain[0]);
        if (j >= 0) { S.nfocus = j; clampList('nfocus', 'nscroll', S.gnode.children.length); }
      } else if (s && isGamesTab()) {
        const [fk, sk, list] = S.game ? ['pfocus', 'pscroll', S.game.parts] : ['gfocus', 'gscroll', S.gameList];
        const j = list.findIndex((c) => c.cid === s.chain[0]);
        if (j >= 0) { S[fk] = j; clampList(fk, sk, list.length, S.game ? PVIS : GVIS); }
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
  F1: 'review', F2: 'mark', F5: 'tabprev', F6: 'tabnext', F7: 'pageup', F8: 'pagedown',
};
const KEYCODES = { 38: 'up', 40: 'down', 37: 'left', 39: 'right', 13: 'confirm', 27: 'back', 8: 'back',
                   32: 'mark', 9: 'review', 33: 'pageup', 34: 'pagedown',
                   112: 'review', 113: 'mark', 116: 'tabprev', 117: 'tabnext', 118: 'pageup', 119: 'pagedown' };
// Buttons the browser also gives a default action (history back, reload).
const BROWSER_KEYS = new Set([112, 113, 116, 117, 118, 119]);
let lastFnKey = 0;

// A held button repeats its keydown every 50 ms, without e.repeat on the PS5.
let heldKey = 0, heldAt = 0;

document.addEventListener('keydown', (e) => {
  const now = performance.now();
  const a = KEYS[e.key] || KEYCODES[e.keyCode];
  const repeat = e.repeat || (e.keyCode === heldKey && now - heldAt < 600);
  heldKey = e.keyCode;
  heldAt = now;
  if (!a) return;
  // Left to the browser, Circle on home finds no page to go back to: the PS5 closes the app.
  if (a === 'back' && homeIdle() && !repeat && !stepping && history.state && history.state.g === 0) return;
  e.preventDefault();
  if (a === 'back' && homeIdle()) return;
  if (BROWSER_KEYS.has(e.keyCode) || /^F\d$/.test(e.key)) lastFnKey = now;
  if (a === 'confirm') startHold('key');
  if (repeat && !REPEATS.has(a)) return;
  input(a, 'key');
});
document.addEventListener('keyup', (e) => {
  if (e.keyCode === heldKey) heldKey = 0;
  const a = KEYS[e.key] || KEYCODES[e.keyCode];
  if (!a) return;
  e.preventDefault();
  if (a === 'confirm') endHold('key');
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
  // A link opens when the pointer clicked it; a Cross press that only lands
  // on it acts on the focused item instead.
  if (e.target.closest('a[href]')) { if (S.cursor) return; e.preventDefault(); }
  const el = e.target.closest('[data-act]');
  // D-pad users press Cross wherever the browser's cursor happens to rest:
  // act on the focused item unless the cursor was moved since.
  if (!S.cursor || !el) { input('confirm', 'click'); return; }
  act(el);
});

// The PS5 browser turns Cross into a mouse press wherever its pointer rests,
// so with the pointer unused a press anywhere holds the focused button.
const pointerDown = (e) => {
  const d = S.dialog;
  if (!d || d.hold < 0) return;
  const el = e.target.closest('[data-act="dlg"]');
  const onHold = el && Number(el.dataset.i) === d.hold;
  if (onHold || (!S.cursor && d.focus === d.hold)) {
    d.focus = d.hold;
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

// Circle reaches the PS5 browser as Escape, and its default action is "back".
// Off home, keep one spare history entry so a back lands here instead of
// leaving the page. On home there is none: Circle is left to the browser, and
// the PS5 closes the app as it does for other browser apps.
let stepping = false;
const homeIdle = () => S.screen === 'home' && !S.dialog;
function syncHistory() {
  if (stepping) return;
  const spare = history.state && history.state.g === 1;
  if (!homeIdle() && !spare) history.pushState({ g: 1 }, '');
  else if (homeIdle() && spare) { stepping = true; history.back(); }
}
history.replaceState({ g: 0 }, '');
window.addEventListener('popstate', () => {
  const now = performance.now();
  if (stepping) { stepping = false; syncHistory(); return; }
  // A Circle already handled as a pad button, a browser default for L1 and co.,
  // or the popstate WebKit fires on load.
  if (now - lastBack >= 400 && now - lastFnKey >= 600 && now > 1500) input('back', 'history');
  syncHistory();
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

// flat: plain files (Biggest Files), coloured like folders.
function buildChart(root, flat) {
  const key = root.id + ':' + root.size + ':' + S.resultSeq + ':' + root.rings.length;
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
      const color = c.dir || flat ? `hsl(${h.toFixed(0)},${92 - depth * 12}%,${52 + depth * 7}%)`
                          : `hsl(${h.toFixed(0)},30%,${82 - depth * 2}%)`;
      let r0 = R0;
      for (let i = 0; i < depth; i++) r0 += RING[i];
      const ch = chain.concat(c.id);
      chart.segs.push({ id: c.id, name: c.name, title: c.title, size: c.size, dir: c.dir, hasKids: !!(c.kids && c.kids.length),
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
  savePlace();
  const main = $('#main');
  let html = '';
  let hints = '';
  switch (S.screen) {
    case 'home': [html, hints] = renderHome(); break;
    case 'scanning': [html, hints] = renderProgress('scan'); break;
    case 'deleting': [html, hints] = renderProgress('delete'); break;
    case 'moving': [html, hints] = renderProgress('move'); break;
    case 'reporting': [html, hints] = renderProgress('report'); break;
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
    else if (f) chain = [f.cid || f.id];
    lightChart(chain || []);
  }
  renderDialog();
  syncHistory();
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
    const names = ['Folders', 'Biggest Files', 'Games', 'Apps'];
    h += `<div class="abs tabs">${GLYPH.l1}<div class="track">
      ${shownTabs().map((t) => `<div class="tab${S.tab === t ? ' on' : ''}" data-act="tab" data-i="${t}">${names[t]}</div>`).join('')}
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
    <div class="abs home-actions">
      <div class="home-btn" data-act="hint" data-a="mark">${GLYPH.square}${ICON.report}<span>Export Report</span></div>
      <div class="home-btn" data-act="hint" data-a="review">${GLYPH.triangle}${ICON.gear}<span>Settings</span></div>
      <div class="home-btn" data-act="hint" data-a="tabnext">${GLYPH.r1}${ICON.info}<span>About</span></div>
    </div>
    <div class="abs cards">`;
  S.storages.forEach((s, i) => {
    const used = s.total ? 1 - s.free / s.total : 0;
    h += `<div class="card${i === S.hfocus ? ' focus' : ''}" data-act="storage" data-i="${i}">
      <div class="ico">${driveIcon(s.kind)}</div>
      <div class="name ellipsis">${esc(s.name)}</div>
      <div class="path ellipsis">${esc(s.path)}</div>
      <div class="bar"><i class="${used > 0.9 ? 'full' : ''}" style="width:${(used * 100).toFixed(1)}%"></i></div>
      <div class="info ellipsis">${fmtSize(s.free)} free of ${fmtSize(s.total)} &middot; ${fmtSize(s.total - s.free)} used</div>
    </div>`;
  });
  h += '</div>';
  if (!S.storages.length) h += '<div class="abs empty">No storage found</div>';
  let hints = hint('cross', 'Scan', 'confirm') + hint('square', 'Export Report', 'mark') +
              hint('triangle', 'Settings', 'review') + hint('r1', 'About', 'tabnext') + hint('circle', 'Close');
  if (S.clip) {
    hints += `<div class="hint-right"><div class="pill move">${fmtCount(S.clip.length)} ${S.clipMode === 'move' ? 'to move' : 'cut'} &middot; ${fmtSize(clipBytes())}
      &middot; scan a storage, open a folder</div></div>`;
  }
  return [h, hints];
}

function renderProgress(op) {
  const p = S.scan || {};
  let h;
  if (op === 'move') {
    const frac = p.goal ? Math.min(1, (p.moved || 0) / p.goal) : 0;
    h = `<div class="abs spinner"></div>
      <div class="abs spin-label">${fmtSize(Math.min(p.moved || 0, p.goal || Infinity))}</div>
      <div class="abs big-label">Moving to ${esc(S.moveTo || p.dest || '')}&hellip;</div>
      <div class="abs mid-label">Item ${Math.min((p.index || 0) + 1, p.total || 1)} of ${p.total || 1} &middot; ${fmtSize(p.goal || 0)} in all</div>
      <div class="abs progress"><i class="move" style="width:${(frac * 100).toFixed(1)}%"></i></div>
      <div class="abs path-label ellipsis">${esc(p.current || '')}</div>`;
    return [h, '<div class="muted">Please wait, do not turn off the console or unplug the drive.</div>'];
  }
  if (op === 'report') {
    const many = p.total > 1;
    const step = many ? `Drive ${Math.min(p.index || 1, p.total)} of ${p.total} · ` : '';
    if (p.state === 'games') {
      h = `<div class="abs spinner"></div>
        <div class="abs big-label">Counting game data&hellip;</div>
        <div class="abs mid-label">Almost done: then the report is ready</div>`;
    } else {
      h = `<div class="abs spinner"></div>
        <div class="abs spin-label">${fmtSize(p.bytes || 0)}</div>
        <div class="abs big-label">Making a report: scanning ${esc(p.name || '')}</div>
        <div class="abs mid-label">${step}${fmtCount(p.items || 0)} items found</div>
        <div class="abs path-label ellipsis">${esc(p.current || '')}</div>`;
    }
    return [h, hint('circle', 'Cancel', 'back')];
  }
  if (op === 'scan') {
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
  if (item.kind === 'game') {
    const st = gameMarkState(item);
    if (st === 'locked') return `<div class="chk">${ICON.lock}</div>`;
    const icon = st === 'all' ? ICON.checked : st === 'some' ? ICON.partial : ICON.box;
    return `<div class="chk" data-act="check" data-i="${i}">${icon}</div>`;
  }
  if (item.protected) return `<div class="chk">${ICON.lock}</div>`;
  const icon = isMarked(item) ? ICON.checked : hasMarkedInside(item) ? ICON.partial : ICON.box;
  return `<div class="chk" data-act="check" data-i="${i}">${icon}</div>`;
}

function iconHtml(item) {
  if (item.icon) return `<img src="/api/icon?id=${encodeURIComponent(item.gid || item.name)}" alt="">`;
  return item.dir ? ICON.folder : ICON.file;
}

// Kinds of data in a game row, in a grid of two columns of three lines filled
// column by column: game files first, then the player's own data and the rest.
// The title's own install folder comes first, whatever its size.
const MAIN_FILES = ['Game', 'Game (PS5)', 'Game (PS4)', 'App', 'App (PS5)', 'App (PS4)', 'App (Payload)'];
const GAME_FILES = [...MAIN_FILES, 'Game dump', 'PS5 title', 'Update', 'Add-ons', 'Downloads', 'Install data'];
const KIND_DOT = { Saves: '#5be0b5', Captures: '#f0a0d8', License: '#f2c46d', Metadata: '#8fa3bb', Other: '#8fa3bb' };

function kindGrid(kinds) {
  if (kinds.length > 6) {  // six lines at most: fold the smallest into the last
    const rest = kinds.slice(5);
    kinds = kinds.slice(0, 5).concat([[`+${rest.length} more`, rest.reduce((t, k) => t + k[1], 0), true]]);
  }
  const line = ([k, v, more]) =>
    `<div class="kind"><i class="dot" style="background:${more ? 'transparent' : KIND_DOT[k] || '#6fb1ff'}"></i>
      <span class="k">${esc(k)}</span><span class="v">${fmtSize(v)}</span></div>`;
  return `<div class="kinds"><div class="kcol">${kinds.slice(0, 3).map(line).join('')}</div>
    <div class="kcol">${kinds.slice(3).map(line).join('')}</div></div>`;
}

// Games tab: cover on the left, title, then each kind of data on its own line.
function gameRow(g, i, focused, total) {
  const by = new Map();
  for (const p of g.parts) by.set(p.name, (by.get(p.name) || 0) + p.size);
  const bySize = [...by].sort((a, b) => b[1] - a[1]);
  const kinds = bySize.filter((k) => MAIN_FILES.includes(k[0]))
    .concat(bySize.filter((k) => GAME_FILES.includes(k[0]) && !MAIN_FILES.includes(k[0])))
    .concat(bySize.filter((k) => !GAME_FILES.includes(k[0])));
  const frac = total ? g.size / total : 0;
  const color = focused ? '#fff' : chart.colors.get(g.cid) || '#6fb1ff';
  const cover = g.icon ? `<img src="/api/icon?id=${encodeURIComponent(g.gid)}" alt="">` : ICON.folder;
  const tid = g.sub ? `<span class="tid">${esc(g.sub)}</span>` : '';
  return `<div class="row game-card${focused ? ' focus' : ''}" data-act="row" data-i="${i}">
    ${checkbox(g, i)}<div class="cov">${cover}</div>
    <div class="mid"><div class="top"><div class="name ellipsis">${esc(g.name)}${tid}</div>
        <div class="size${g.size >= 1e9 ? ' big' : ''}">${fmtSize(g.size)}</div></div>
      ${kindGrid(kinds)}
      <div class="rbar"><i style="width:${(frac * 100).toFixed(2)}%;background:${color}"></i></div></div>
  </div>`;
}

function row(item, i, focused, style, parentSize) {
  if (style === 3) return gameRow(item, i, focused, parentSize);
  const big = item.size >= 1e9;
  let mid, sub = '';
  if (style === 4) {
    // Games tab: one of a game's folders.
    const frac = parentSize ? item.size / parentSize : 0;
    const color = focused ? '#fff' : chart.colors.get(item.cid) || '#6fb1ff';
    // Two lines of details: the folder's volume and path.
    const info = `<div class="info ellipsis">${esc(item.volume)}</div><div class="info ellipsis">${esc(item.path)}</div>`;
    mid = `<div class="name ellipsis">${esc(item.name)}</div>${info}
      <div class="rbar"><i style="width:${(frac * 100).toFixed(2)}%;background:${color}"></i></div>`;
    sub = item.error ? 'partly unreadable' : fmtPct(frac);
  } else if (style === 0) {
    const frac = parentSize ? item.size / parentSize : 0;
    const color = focused ? '#fff' : chart.colors.get(item.id) || (item.dir ? '#6fb1ff' : '#b9cce4');
    // A game folder keeps its name; the game's name goes on a smaller line below.
    mid = `<div class="name ellipsis">${esc(item.name)}</div>
      ${item.title ? `<div class="game ellipsis">${esc(item.title)}</div>` : ''}<div class="rbar"><i style="width:${(frac * 100).toFixed(2)}%;background:${color}"></i></div>`;
    sub = item.mount ? 'volume' : item.error ? 'partly unreadable' : fmtPct(frac);
  } else {
    const folder = style === 1 ? item.folder : item.path.replace(/\/[^/]*$/, '') || '/';
    const where = (item.title ? item.title + ' · ' : '') + shortPath(folder);
    mid = `<div class="name ellipsis">${esc(item.name)}</div><div class="folder ellipsis">${esc(where)}</div>`;
    if (item.dir) sub = fmtCount(item.files) + ' files';
  }
  const game = style === 4 ? ' tall' : style === 0 && item.title ? ' game-row' : '';
  return `<div class="row${focused ? ' focus' : ''}${game}" data-act="row" data-i="${i}">
    ${checkbox(item, i)}<div class="ico">${iconHtml(item)}</div>
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

// A list's rows: the vis that fit, plus the next one peeking in at the bottom.
// At the end of a long list the rows sit at the bottom and the one before peeks in at the top.
function listRows(list, scroll, vis, draw, menu = true) {
  const end = list.length > vis && scroll >= list.length - vis;
  const from = end ? scroll - 1 : scroll, to = Math.min(list.length, scroll + vis + 1);
  let h = `<div class="abs rows${menu ? ' m' : ''}${end ? ' end' : ''}">`;
  for (let i = from; i < to; i++) h += draw(list[i], i);
  return h + '</div>';
}

function scrollbar(first, total, vis = VIS, review = false) {
  if (total <= vis) return '';
  const h = review ? ROWS_H : LIST_H, th = Math.max(40, h * vis / total), ty = (h - th) * first / Math.max(1, total - vis);
  return `<div class="abs scroll${review ? '' : ' m'}"><i style="top:${ty}px;height:${th}px"></i></div>`;
}

function renderBrowse() {
  const st = S.storage || {};
  let h = header(st.name || '', st.path || '', true) + '<div class="abs panel"></div>' + menuBar();
  if (S.tab === 0 && S.node) {
    h += folderPanel(S.node, S.focus, S.scroll);
  } else if (S.tab === 1) {
    h += `<div class="abs panel-title">Biggest files</div>
      <div class="abs panel-meta">Top ${fmtCount(S.biggest.length)} on this storage</div>`;
    h += listRows(S.biggest, S.bscroll, VIS, (x, i) => row(x, i, S.mfocus < 0 && i === S.bfocus, 1, 0));
    if (!S.biggest.length) h += '<div class="abs empty">No files</div>';
    h += scrollbar(S.bscroll, S.biggest.length);
    const total = S.biggest.reduce((t, b) => t + b.size, 0);
    buildChart({ id: -2, size: total, rings: S.biggest.map((b) => ({ id: b.id, name: b.name, size: b.size })) }, true);
  } else if (isGamesTab() && S.gnode) {
    h += folderPanel(S.gnode, S.nfocus, S.nscroll);
  } else if (isGamesTab()) {
    h += renderGames();
  }

  // Center disc: what is under focus, relative to the chart root.
  const croot = S.tab === 0 ? S.node : S.tab === 1 ? S.rootNode : S.gnode || S.game || gamesRoot();
  if (croot && (!isGamesTab() || S.gameList.length)) {
    const f = focusedItem();
    const info = S.hover || f || croot;
    const name = nodeName(info);
    const g2 = info.sub || info.title;
    const game = g2 ? `<div class="g ellipsis">${esc(g2)}</div>` : '';
    const of = S.tab === 1 ? ' of storage' : S.tab === 0 || S.gnode ? ' of folder' : S.game ? tabWord(' of game', ' of app') : tabWord(' of all games', ' of all apps');
    let p;
    if (info !== croot && croot.size) p = fmtPct(info.size / croot.size) + of;
    else p = fmtCount(croot.files) + ' files';
    h += `<div class="abs center" data-act="center"><div class="n ellipsis">${esc(name)}</div>${game}
      <div class="s">${fmtSize(info.size)}</div><div class="p">${p}</div></div>`;
  }

  // Above the chart: the whole name of what is in focus (or under the
  // pointer), wrapping as needed, since rows and the centre disc cut it.
  // Game folders show the game's name with the title ID.
  let ct = null;
  if (isGamesTab() && !S.gnode) {
    const hg = S.hover && !S.game ? S.gameList.find((x) => x.cid === S.hover.chain[0]) : null;
    const g = S.game || hg || S.gameList[S.gfocus];
    if (g) ct = { name: g.name, sub: g.sub };
  } else {
    const it = S.hover || focusedItem() || (S.tab === 0 ? S.node : S.gnode);
    if (it) ct = it.title ? { name: it.title, sub: nodeName(it) } : { name: nodeName(it) };
  }
  if (ct) h += `<div class="abs ctitle">${esc(ct.name)}${ct.sub ? `<span class="tid">${esc(ct.sub)}</span>` : ''}</div>`;

  // Breadcrumb
  if (isGamesTab()) {
    const g = S.game, n = S.gnode;
    let parts = [`<span data-act="gcrumb">${tabWord('Games', 'Apps')}</span>`];
    if (g) parts.push(n ? `<span data-act="gpcrumb">${esc(g.name)}</span>` : `<b>${esc(g.name)}</b>`);
    if (n) {
      // The folder's own crumbs, from the game folder down.
      const cr = n.crumbs.slice(Math.max(0, n.crumbs.findIndex((c) => c.id === S.gpart.id)));
      parts = parts.concat(cr.map((c, i) => {
        const label = esc(i === 0 ? S.gpart.name : c.name);
        return i === cr.length - 1 ? `<b>${label}</b>` : `<span data-act="gncrumb" data-i="${c.id}">${label}</span>`;
      }));
    }
    if (!g) parts = [`<b>${tabWord('Games', 'Apps')}</b>`];
    if (parts.length > 6) parts = parts.slice(0, 2).concat(['&hellip;'], parts.slice(-3));
    h += `<div class="abs crumbs ellipsis">${parts.join(' &nbsp;&rsaquo;&nbsp; ')}</div>`;
  } else if (S.node) {
    const cr = S.node.crumbs;
    let parts = cr.map((c, i) => {
      const label = i === 0 ? st.name : c.name;
      const last = i === cr.length - 1;
      return `<span data-act="crumb" data-i="${c.id}">${last ? '<b>' + esc(label) + '</b>' : esc(label)}</span>`;
    });
    if (parts.length > 6) parts = [parts[0], '&hellip;'].concat(parts.slice(-4));
    h += `<div class="abs crumbs ellipsis">${parts.join(' &nbsp;&rsaquo;&nbsp; ')}</div>`;
  }

  let hints = S.tab !== 1
    ? hint('cross', 'Open', 'confirm') + hint('circle', 'Back', 'back') + hint('square', 'Select', 'mark') +
      hint('lstick', 'Cursor')
    : hint('cross', 'Show in folder', 'confirm') + hint('square', 'Select', 'mark') +
      hint('lstick', 'Cursor');
  if (S.clip) {
    const n = S.clip.length;
    const verb = S.clipMode === 'move' ? 'Move' : 'Paste';
    const label = pasteFolder() ? `${verb} ${fmtCount(n)} ${n === 1 ? 'item' : 'items'} here`
                                : `${fmtCount(n)} waiting · open a folder to ${verb.toLowerCase()}`;
    const from = S.clipFrom && S.storage && S.clipFrom.path !== S.storage.path ? ` from ${S.clipFrom.name}` : '';
    hints += `<div class="hint-right"><div class="pill move" data-act="hint" data-a="review">${GLYPH.triangle}
      ${esc(label + from)} &middot; ${fmtSize(clipBytes())}</div></div>`;
  } else if (S.marked.size) {
    hints += `<div class="hint-right"><div class="pill" data-act="hint" data-a="review">${GLYPH.triangle}
      ${fmtCount(S.marked.size)} selected &middot; ${fmtSize(markedBytes())} &nbsp; Review</div></div>`;
  } else {
    hints += `<div class="hint-right muted">${glyphText('Press Square to select items to delete or move')}</div>`;
  }
  return [h, hints];
}

// A folder's name; the scanned root and a game folder go by friendlier names.
function nodeName(n) {
  if (n.id === S.rootId) return (S.storage || {}).name || n.name;
  if (isGamesTab() && S.gpart && n.id === S.gpart.id) return S.gpart.name;
  return n.name;
}

// A scanned folder's contents: Folders tab, or a game's folder in the Games tab.
function folderPanel(n, focus, scroll) {
  const game = n.title ? ` <span class="tid">${esc(n.title)}</span>` : '';
  let h = `<div class="abs panel-title ellipsis">${esc(nodeName(n))}${game}</div>
    <div class="abs panel-meta">${fmtSize(n.size)} &middot; ${fmtCount(n.files)} files</div>`;
  h += listRows(n.children, scroll, VIS, (x, i) => row(x, i, S.mfocus < 0 && i === focus, 0, n.size));
  if (!n.children.length) h += '<div class="abs empty">Empty folder</div>';
  h += scrollbar(scroll, n.children.length);
  buildChart(n);
  return h;
}

// All games together: the Games tab's chart root.
function gamesRoot() {
  let size = 0, files = 0;
  for (const g of S.gameList) { size += g.size; files += g.files; }
  return { id: 'games', cid: 'games', name: tabWord('All games', 'All apps'), size, files };
}

function renderGames() {
  let h;
  const g = S.game;
  if (g) {
    h = `<div class="abs panel-title ellipsis">${esc(g.name)}${g.sub ? ` <span class="tid">${esc(g.sub)}</span>` : ''}</div>
      <div class="abs panel-meta">${fmtSize(g.size)} &middot; ${fmtCount(g.files)} files</div>`;
    h += listRows(g.parts, S.pscroll, PVIS, (x, i) => row(x, i, S.mfocus < 0 && i === S.pfocus, 4, g.size)) + scrollbar(S.pscroll, g.parts.length, PVIS);
    buildChart({ id: g.cid, size: g.size, rings: g.parts.map((p) => ({ id: p.cid, name: p.name, title: g.name, size: p.size, dir: true })) });
    return h;
  }
  const all = gamesRoot();
  const counting = S.games === 'counting';
  h = `<div class="abs panel-title">${tabWord('Games', 'Apps')}</div>
    <div class="abs panel-meta">${counting ? 'Counting&hellip;' : `${fmtCount(S.gameList.length)} ${tabWord('games', 'apps')} &middot; ${fmtSize(all.size)}`}</div>`;
  h += listRows(S.gameList, S.gscroll, GVIS, (x, i) => row(x, i, S.mfocus < 0 && i === S.gfocus, 3, all.size)) + scrollbar(S.gscroll, S.gameList.length, GVIS);
  if (!S.gameList.length) h += `<div class="abs empty">${counting ? 'Counting data on all storages&hellip;' : tabWord('No games found', 'No apps found')}</div>`;
  if (S.gameList.length) {
    buildChart({ id: 'games', size: all.size, rings: S.gameList.map((x) => ({
      id: x.cid, name: x.name, title: x.sub, size: x.size, dir: true,
      kids: x.parts.map((p) => ({ id: p.cid, name: p.name, title: x.name, size: p.size, dir: true })) })) });
  } else {
    chart.el = null;
    chart.key = '';
  }
  return h;
}

function renderReview() {
  const st = S.storage || {};
  const list = markedList();
  let h = header('Review & clean', (st.name || '') + ' · ' + (st.path || ''), false) +
    `<div class="abs panel"></div><div class="abs summary"></div>
    <div class="abs panel-title">Selected items</div>
    <div class="abs panel-meta">${fmtCount(list.length)} items</div>`;
  h += listRows(list, S.rscroll, RVIS, (x, i) => row(x, i, S.rbtn < 0 && i === S.rfocus, 2, 0), false) + scrollbar(S.rscroll, list.length, RVIS, true);

  const bytes = markedBytes();
  // A game being uninstalled takes its update, add-ons and home screen info
  // with it: their own warnings (about deleting only part of a game) are moot.
  let files = 0;
  const gone = new Set(list.map((m) => m.uninstall).filter(Boolean));
  const warns = [];
  for (const m of list) {
    files += m.files;
    const moot = !m.uninstall && m.removedWith && gone.has(m.removedWith);
    if (m.warn && !moot && !warns.includes(m.warn)) warns.push(m.warn);
  }
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
  const rb = (b, cls, label, glyph) =>
    `<div class="abs rbtn ${cls}${S.rbtn === b ? ' focus' : ''}" data-act="rbtn" data-i="${b}">${glyph || ''}<span>${label}</span></div>`;
  h += rb(RBTN.cut, 'cutbtn', 'Cut') + rb(RBTN.move, 'movebtn', 'Move to&hellip;') + rb(RBTN.clear, 'clearbtn', 'Clear') +
       rb(RBTN.del, 'delbtn', 'Delete permanently', GLYPH.triangle);
  return [h, hint('square', 'Unselect', 'mark') + hint('triangle', 'Delete', 'review') + hint('cross', S.rbtn < 0 ? 'Show' : 'Choose', 'confirm') +
             hint('circle', 'Back', 'back') + hint('dpad', 'Right: Cut, Move, Clear, Delete')];
}

function renderDialog() {
  const box = $('#dialog');
  const d = S.dialog;
  if (!d) { box.innerHTML = ''; return; }
  if (d.settings) { box.innerHTML = settingsHtml(d); return; }
  if (d.about) { box.innerHTML = aboutHtml(d); return; }
  const one = d.buttons.length === 1;
  box.innerHTML = `<div class="dlg"><h2>${esc(d.title)}</h2>
    ${d.lines.map((l) => `<p>${glyphText(l)}</p>`).join('')}
    ${d.warning ? `<div class="warn">${esc(d.warning)}</div>` : ''}
    <div class="btns${d.vertical ? ' v' : ''}${one ? ' one' : ''}">
      ${d.buttons.map((b, i) => `<div class="btn${i === d.focus ? ' focus' : ''}${i === d.hold && d.danger ? ' danger' : ''}"
         data-act="dlg" data-i="${i}">${i === d.hold ? `<i class="fill"></i><span class="btn-ico">${GLYPH.cross}</span>` : ''}<span>${glyphText(b)}</span></div>`).join('')}
    </div></div>`;
}

function settingsHtml(d) {
  const rows = SETTINGS.map((def, i) => {
    const k = def.values.indexOf(settings[def.key]);
    const arrow = (dir, ch, off) =>
      `<div class="arr${off ? ' off' : ''}" data-act="setv" data-i="${i}" data-d="${dir}">${ch}</div>`;
    return `<div class="set-row${d.focus === i ? ' focus' : ''}" data-act="set" data-i="${i}">
      <div class="lbl">${esc(def.label)}</div>
      ${arrow(-1, '&lsaquo;', k === 0)}<div class="val">${esc(def.show(settings[def.key]))}</div>${arrow(1, '&rsaquo;', k === def.values.length - 1)}
    </div>`;
  }).join('');
  const close = d.focus === SETTINGS.length;
  return `<div class="dlg"><h2>Settings</h2>${rows}
    <p class="set-preview">Changes apply right away and are saved on this device.</p>
    <div class="btns one"><div class="btn${close ? ' focus' : ''}" data-act="dlg" data-i="0"><span>Close</span></div></div></div>`;
}

function aboutHtml(d) {
  const ver = appVersion ? `<span class="ver">v${esc(appVersion)}</span>` : '';
  const rows = ABOUT.map(([k, v, href]) => `<div class="k">${esc(k)}</div><div class="v">${
    href ? `<a href="${esc(href)}" target="_blank" rel="noopener">${esc(v)}</a>` : esc(v)}</div>`).join('');
  return `<div class="dlg about"><h2>${esc(d.title)}${ver}</h2>
    <p class="tagline">See what fills your PS5 storage and free up space safely.</p>
    <div class="about-grid">${rows}</div>
    <div class="btns one"><div class="btn${d.focus === 0 ? ' focus' : ''}" data-act="dlg" data-i="0"><span>Close</span></div></div></div>`;
}

// ================================================================ status bar

// Version, and the address another device on the network can open.
async function loadInfo() {
  let info;
  try { info = await api('/api/info'); } catch (e) { return; }
  appVersion = info.version || '';
  const local = /^(localhost|127\.|\[::1\])/.test(location.hostname);
  const host = info.ip || (local ? '' : location.hostname);
  const port = info.port || location.port;
  const url = host ? `http://${host}:${port}/` : `http://&lt;PS5 IP&gt;:${port}/`;
  // The PS5 tile opens 127.0.0.1; any other device already is "in a browser".
  $('#statusbar').innerHTML = `<span><b>v${esc(info.version)}</b></span>` +
    (local ? `<span>Remote Management: <b>${host ? esc(url) : url}</b></span>` : '');
}

// ================================================================ layout + start

function fit() {
  const k = Math.min(window.innerWidth / 1920, window.innerHeight / 1080);
  const stage = $('#stage');
  stage.style.transform = `translate(${(window.innerWidth - 1920 * k) / 2}px, ${(window.innerHeight - 1080 * k) / 2}px) scale(${k})`;
}
window.addEventListener('resize', fit);

async function start() {
  loadSettings();
  applySettings();
  fit();
  render();
  applyFullscreen(true);
  loadInfo();
  let st = null;
  try { st = await api('/api/status'); } catch (e) { /* server just started */ }
  await loadStorages();
  let rep = null;
  try { rep = await api('/api/report/status'); } catch (e) { /* none */ }
  if (rep && (rep.state === 'scanning' || rep.state === 'games')) {
    S.reportBack = 'home';
    S.screen = 'reporting';
    S.scan = rep;
    render();
    pollReport();
    return;
  }
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
  } else if (st && st.phase === 'moving') {
    S.storage = st.storage;
    S.screen = 'moving';
    S.scan = st;
    render();
    pollMove();
  } else if (st && st.phase === 'ready' && st.root != null) {
    // Files may have changed while the app was closed: rescan, same folder.
    S.storage = st.storage;
    let place = loadPlace();
    if (!place || place.storage !== st.storage.path) place = { storage: st.storage.path, path: '', tab: 0, focus: '' };
    if (!(await refreshScan(place))) await enterBrowse(st, place);
  } else {
    render();
  }
}
start();
