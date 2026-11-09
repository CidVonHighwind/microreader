// ── CRC32 (matches zlib.crc32 / esp_rom_crc32_le) ────────────────────────────
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    t[i] = c;
  }
  return t;
})();
function crc32(data) {
  let c = 0xFFFFFFFF;
  for (const b of data) c = CRC_TABLE[(c ^ b) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

// ── LE helpers ────────────────────────────────────────────────────────────────
function u16le(n) { return new Uint8Array([n & 0xFF, (n >> 8) & 0xFF]); }
function u32le(n) { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, n >>> 0, true); return b; }

// ── Serial connection ─────────────────────────────────────────────────────────
let conn = null;

class SerialConn {
  constructor(port) {
    this.port = port;
    this._buf = [];
    this._waiters = [];
    this._pump();
  }
  async _pump() {
    const reader = this.port.readable.getReader();
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        for (const b of value) {
          if (this._waiters.length > 0) this._waiters.shift()(b);
          else this._buf.push(b);
        }
      }
    } catch (_) {}
    finally {
      reader.releaseLock();
      this._waiters.forEach(w => w(-1));
      this._waiters = [];
    }
  }
  readByte(timeoutMs = 30000) {
    return new Promise((resolve, reject) => {
      if (this._buf.length > 0) { resolve(this._buf.shift()); return; }
      let timer;
      const cb = b => {
        clearTimeout(timer);
        if (b === -1) reject(new Error('Port closed'));
        else resolve(b);
      };
      this._waiters.push(cb);
      timer = setTimeout(() => {
        const i = this._waiters.indexOf(cb);
        if (i !== -1) this._waiters.splice(i, 1);
        reject(new Error('Serial read timeout'));
      }, timeoutMs);
    });
  }
  async readLine(timeoutMs = 5000) {
    const deadline = Date.now() + timeoutMs;
    const bytes = [];
    while (true) {
      const left = deadline - Date.now();
      if (left <= 0) throw new Error('Timeout reading line');
      const b = await this.readByte(left);
      if (b === 0x0A) {
        return new TextDecoder('utf-8').decode(new Uint8Array(bytes));
      }
      if (b !== 0x0D) bytes.push(b);
    }
  }
  async write(data) {
    const w = this.port.writable.getWriter();
    try { await w.write(data); } finally { w.releaseLock(); }
  }
  async close() { try { await this.port.close(); } catch (_) {} }
}

// ── Protocol ──────────────────────────────────────────────────────────────────
const CMND = new Uint8Array([0x43, 0x4D, 0x4E, 0x44]);
const enc = new TextEncoder();

function isEspLog(line) { return /^[IWED] \(\d+\)/.test(line); }

async function readUntil(pred, ms = 8000) {
  const deadline = Date.now() + ms;
  while (true) {
    const left = deadline - Date.now();
    if (left <= 0) throw new Error('Timeout');
    const line = await conn.readLine(left);
    if (line && !isEspLog(line) && pred(line)) return line;
  }
}

// 'A' — list directory with metadata
async function cmdDirList(path) {
  const pb = enc.encode(path);
  await conn.write(new Uint8Array([...CMND, 0x41, ...u16le(pb.length), ...pb]));
  const entries = [];
  const deadline = Date.now() + 8000;
  let started = false;
  while (true) {
    const left = deadline - Date.now();
    if (left <= 0) break;
    let line;
    try { line = await conn.readLine(left); } catch { break; }
    if (!line || isEspLog(line)) continue;
    if (line.startsWith('ERR:')) throw new Error(line);
    if (line.startsWith('DIR:')) { started = true; continue; }
    if (line === 'END') break;
    if (!started) continue;
    const p = line.split('|');
    if (p[0] === 'd') {
      entries.push({ type: 'dir',  name: p[1] || '', size: 0, mtime: 0 });
    } else if (p[0] === 'f') {
      entries.push({ type: 'file', name: p[1] || '', size: parseInt(p[2]) || 0, mtime: parseInt(p[3]) || 0 });
    }
  }
  return entries;
}

// 'W' — write file to full path (chunked + ACK + CRC)
async function cmdWriteFile(fullPath, file, onProgress) {
  const data = new Uint8Array(await file.arrayBuffer());
  const pb = enc.encode(fullPath);
  const checksum = crc32(data);
  await conn.write(new Uint8Array([...CMND, 0x57, ...u16le(pb.length), ...pb, ...u32le(data.length)]));
  await readUntil(l => { if (l.startsWith('ERR:')) throw new Error(l); return l === 'READY'; }, 10000);
  const CHUNK = 2048;
  for (let sent = 0; sent < data.length; ) {
    const end = Math.min(sent + CHUNK, data.length);
    await conn.write(data.slice(sent, end));
    sent = end;
    onProgress(sent / data.length);
    const ack = await conn.readByte(30000);
    if (ack !== 0x06) throw new Error(`Bad ACK 0x${ack.toString(16)}`);
  }
  await conn.write(u32le(checksum));
  const r = await readUntil(l => l === 'OK' || l.startsWith('ERR:'), 30000);
  if (r !== 'OK') throw new Error(r);
}

// 'R' — delete file
async function cmdDelete(path) {
  const pb = enc.encode(path);
  await conn.write(new Uint8Array([...CMND, 0x52, ...u16le(pb.length), ...pb]));
  return readUntil(l => l === 'OK' || l.startsWith('ERR:'), 5000);
}

// 'K' — mkdir
async function cmdMkdir(path) {
  const pb = enc.encode(path);
  await conn.write(new Uint8Array([...CMND, 0x4B, ...u16le(pb.length), ...pb]));
  return readUntil(l => l === 'OK' || l.startsWith('ERR:'), 5000);
}

// 'S' — heap status
async function cmdStatus() {
  await conn.write(new Uint8Array([...CMND, 0x53]));
  return readUntil(l => l.startsWith('STATUS:'), 3000);
}

// 'C' — clear .mrb cache
async function cmdClearCache() {
  await conn.write(new Uint8Array([...CMND, 0x43]));
  return readUntil(l => l.startsWith('CLEARED:') || l.startsWith('ERR:'), 10000);
}

// 'N' — rename/move: src path then dst path, both 2-byte-LE-prefixed
async function cmdRename(src, dst) {
  const sp = enc.encode(src), dp = enc.encode(dst);
  await conn.write(new Uint8Array([...CMND, 0x4E, ...u16le(sp.length), ...sp, ...u16le(dp.length), ...dp]));
  return readUntil(l => l === 'OK' || l.startsWith('ERR:'), 5000);
}

// ── Formatting ────────────────────────────────────────────────────────────────
// 'E' — set the device clock. The reader keeps local wall time with no time
// zone, so send this computer's local time as seconds since 1970 (int64 LE).
async function cmdSetClock() {
  const localSecs = Math.floor(Date.now() / 1000) - new Date().getTimezoneOffset() * 60;
  const b = new Uint8Array(8);
  new DataView(b.buffer).setBigInt64(0, BigInt(localSecs), true);
  await conn.write(new Uint8Array([...CMND, 0x45, ...b]));
  return readUntil(l => l === 'OK' || l.startsWith('ERR:'), 2000);
}

// Keep the reader's clock (used by Reading Stats) in sync on every connect.
// Firmware without the command answers ERR:unknown_cmd, which is ignored.
async function syncClock() {
  try {
    if ((await cmdSetClock()) === 'OK') log('Device clock synced');
  } catch (_) { /* no reply: older firmware or busy device — not fatal */ }
}

function fmtSize(bytes) {
  if (!bytes) return '—';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1048576) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1048576).toFixed(1)} MB`;
}

function fmtDate(unix) {
  if (!unix || unix < 946684800) return '—'; // ignore anything before year 2000
  try { return new Date(unix * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' }); }
  catch { return '—'; }
}

function fileIcon(name) {
  const ext = (name.split('.').pop() || '').toLowerCase();
  return { epub: '📖', mrb: '⚡', png: '🖼', jpg: '🖼', jpeg: '🖼', bmp: '🖼', gif: '🖼', mbf: '🔤', mfb: '🔤', ttf: '🔤', otf: '🔤', dat: '📊', pos: '📊', mgr: '🌙' }[ext] || '📄';
}

// ── UI state ──────────────────────────────────────────────────────────────────
let currentPath = '/sdcard';
let currentEntries = [];
let sortCol = 'name', sortDir = 'asc';
let busy = false;
let autoconnect = false;
try { autoconnect = localStorage.getItem('autoconnect') === '1'; } catch (_) {}

const $ = id => document.getElementById(id);
const btnConnect    = $('btn-connect');
const btnMenu       = $('btn-connect-menu');
const connectMenu   = $('connect-menu');
const autoChk       = $('autoconnect-chk');
const btnRefresh    = $('btn-refresh');
const btnNewFolder  = $('btn-new-folder');
const connBadge     = $('conn-badge');
const statusBar     = $('status-bar');
const breadcrumb    = $('breadcrumb');
const btnUpload     = $('btn-upload');
const fileInput     = $('file-input');
const progressWrap  = $('progress-wrap');
const progressFill  = $('progress-fill');
const progressLabel = $('progress-label');
const fileTbody     = $('file-tbody');
const fileBrowser   = document.querySelector('.file-browser');
const logBox        = $('log-box');
const modal         = $('modal');
const modalTitle    = $('modal-title');
const modalBody     = $('modal-body');
const modalInput    = $('modal-input');
const modalConfirm  = $('modal-confirm');
const modalCancel   = $('modal-cancel');

// Promise-based modal. options: { title, body (HTML), confirmLabel, danger, inputPlaceholder, defaultValue }
// Resolves to true/false for confirm dialogs, or string/null for input dialogs.
function showDialog({ title, body, confirmLabel = 'OK', danger = false, inputPlaceholder = null, defaultValue = '' }) {
  return new Promise(resolve => {
    modalTitle.textContent = title;
    modalBody.innerHTML = body;
    modalConfirm.textContent = confirmLabel;
    modalConfirm.className = danger ? 'btn-danger-solid' : 'btn-primary';

    const hasInput = inputPlaceholder !== null;
    modalInput.style.display = hasInput ? 'block' : 'none';
    if (hasInput) { modalInput.placeholder = inputPlaceholder; modalInput.value = defaultValue; }

    modal.style.display = 'flex';
    requestAnimationFrame(() => { if (hasInput) { modalInput.focus(); modalInput.select(); } else modalConfirm.focus(); });

    const done = result => {
      modal.style.display = 'none';
      modalConfirm.removeEventListener('click', onOk);
      modalCancel.removeEventListener('click', onCancel);
      modal.removeEventListener('click', onBackdrop);
      document.removeEventListener('keydown', onKey);
      resolve(result);
    };
    const onOk     = () => done(hasInput ? modalInput.value.trim() : true);
    const onCancel = () => done(hasInput ? null : false);
    const onBackdrop = e => { if (e.target === modal) onCancel(); };
    const onKey = e => {
      if (e.key === 'Enter') onOk();
      if (e.key === 'Escape') onCancel();
    };
    modalConfirm.addEventListener('click', onOk);
    modalCancel.addEventListener('click', onCancel);
    modal.addEventListener('click', onBackdrop);
    document.addEventListener('keydown', onKey);
  });
}

function log(msg) {
  const ts = new Date().toLocaleTimeString();
  logBox.textContent += `[${ts}] ${msg}\n`;
  logBox.scrollTop = logBox.scrollHeight;
}
function setStatus(msg) { statusBar.textContent = msg; }

function setBusy(b) {
  busy = b;
  btnConnect.disabled = b;
  btnMenu.disabled = b;
  const ok = !b && !!conn;
  btnRefresh.disabled = !ok;
  btnNewFolder.disabled = !ok;
  btnUpload.disabled = !ok;
  fileTbody.querySelectorAll('.btn-del, .btn-ren').forEach(btn => btn.disabled = b);
}

function setConnected(yes) {
  connBadge.textContent = yes ? 'Connected' : 'Disconnected';
  connBadge.className = 'badge' + (yes ? ' connected' : '');
  btnConnect.textContent = yes ? 'Disconnect' : 'Connect Device';
  setBusy(false);
}

function escHtml(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }

function renderBreadcrumb() {
  breadcrumb.innerHTML = '';
  const rel = currentPath.startsWith('/sdcard') ? currentPath.slice('/sdcard'.length) : currentPath;
  const parts = rel.split('/').filter(Boolean);
  const segments = [{ label: 'SD Card', path: '/sdcard' },
    ...parts.map((p, i) => ({ label: p, path: '/sdcard/' + parts.slice(0, i + 1).join('/') }))];

  segments.forEach((seg, i) => {
    if (i > 0) {
      const sep = document.createElement('span');
      sep.className = 'crumb-sep'; sep.textContent = '›';
      breadcrumb.appendChild(sep);
    }
    const span = document.createElement('span');
    const isCurrent = i === segments.length - 1;
    span.className = 'crumb' + (isCurrent ? ' current' : '');
    span.textContent = seg.label;
    if (!isCurrent) span.addEventListener('click', () => navigate(seg.path, { push: true }));
    breadcrumb.appendChild(span);
  });
}

function updateSortUI() {
  document.querySelectorAll('thead th[data-col]').forEach(th => {
    const col = th.dataset.col;
    const active = col === sortCol;
    th.classList.toggle('sort-active', active);
    th.querySelector('.sort-icon').textContent = active ? (sortDir === 'asc' ? '▲' : '▼') : '';
  });
}

function renderEntries(entries) {
  currentEntries = entries.filter(e => !e.name.startsWith('.'));

  const cmp = (a, b) => {
    let v = sortCol === 'size'  ? (a.size  || 0) - (b.size  || 0)
          : sortCol === 'mtime' ? (a.mtime || 0) - (b.mtime || 0)
          : a.name.localeCompare(b.name);
    return sortDir === 'asc' ? v : -v;
  };

  const dirs  = entries.filter(e => e.type === 'dir' ).sort(cmp);
  const files = entries.filter(e => e.type === 'file').sort(cmp);

  if (!dirs.length && !files.length) {
    fileTbody.innerHTML = '<tr class="placeholder-row"><td colspan="4">Empty folder</td></tr>';
    updateSortUI();
    return;
  }

  fileTbody.innerHTML = '';
  const actions = `<button class="btn-sm btn-ren">Rename</button>
                   <button class="btn-sm btn-danger btn-del">Delete</button>`;
  for (const entry of [...dirs, ...files]) {
    const tr = document.createElement('tr');
    const fullPath = currentPath + '/' + entry.name;

    if (entry.type === 'dir') {
      tr.innerHTML = `
        <td class="col-name"><div class="entry-name">
          <span class="entry-icon">📁</span>
          <span class="entry-label is-dir">${escHtml(entry.name)}</span>
        </div></td>
        <td class="col-size file-size">—</td>
        <td class="col-date file-date">—</td>
        <td class="col-actions">${actions}</td>`;
      tr.querySelector('.is-dir').addEventListener('click', () => navigate(fullPath, { push: true }));
    } else {
      tr.innerHTML = `
        <td class="col-name"><div class="entry-name">
          <span class="entry-icon">${fileIcon(entry.name)}</span>
          <span class="entry-label" title="${escHtml(entry.name)}">${escHtml(entry.name)}</span>
        </div></td>
        <td class="col-size file-size">${fmtSize(entry.size)}</td>
        <td class="col-date file-date">${fmtDate(entry.mtime)}</td>
        <td class="col-actions">${actions}</td>`;
    }
    tr.querySelector('.btn-ren').addEventListener('click', () => onRename(fullPath));
    tr.querySelector('.btn-del').addEventListener('click', () => onDelete(fullPath, entry.type));
    fileTbody.appendChild(tr);
  }
  updateSortUI();
}

// hist: { push: true } → pushState, { replace: true } → replaceState, omit → no history change
async function navigate(path, hist = {}) {
  currentPath = path;
  renderBreadcrumb();
  const hash = '#' + encodeURIComponent(path);
  if (hist.push)    history.pushState({ path }, '', hash);
  if (hist.replace) history.replaceState({ path }, '', hash);
  setBusy(true);
  setStatus(`Loading ${path}...`);
  log(`ls ${path}`);
  try {
    const entries = await cmdDirList(path);
    renderEntries(entries);
    const nDirs = entries.filter(e => e.type === 'dir').length;
    const nFiles = entries.filter(e => e.type === 'file').length;
    const summary = [nDirs && `${nDirs} folder${nDirs !== 1 ? 's' : ''}`, nFiles && `${nFiles} file${nFiles !== 1 ? 's' : ''}`].filter(Boolean).join(', ');
    setStatus(path + (summary ? `  —  ${summary}` : ''));
    log(`${entries.length} entries`);
  } catch (e) {
    log('Error: ' + e.message);
    setStatus('Error: ' + e.message);
    fileTbody.innerHTML = `<tr class="placeholder-row"><td colspan="4">Error: ${escHtml(e.message)}</td></tr>`;
  } finally {
    setBusy(false);
  }
}

// ESP32-C3 USB Serial/JTAG VID:PID
const ESP_FILTERS = [{ usbVendorId: 0x303A, usbProductId: 0x1001 }];

async function onConnect() {
  if (conn) {
    log('Disconnecting...');
    await conn.close();
    conn = null;
    setConnected(false);
    fileTbody.innerHTML = '<tr class="placeholder-row"><td colspan="4">Connect to the device to start browsing</td></tr>';
    setStatus('');
    log('Disconnected');
    return;
  }
  try {
    // Reuse a previously-granted ESP32 port if there is exactly one
    let port;
    const granted = (await navigator.serial.getPorts()).filter(p => {
      const { usbVendorId, usbProductId } = p.getInfo();
      return usbVendorId === 0x303A && usbProductId === 0x1001;
    });
    if (autoconnect && granted.length === 1) {
      port = granted[0];
      log('Auto-selected previously granted port');
    } else {
      // Show picker filtered to ESP32 devices only
      port = await navigator.serial.requestPort({ filters: ESP_FILTERS });
    }
    await port.open({ baudRate: 115200 });
    conn = new SerialConn(port);
    setConnected(true);
    log('Connected');
    await syncClock();
    await navigate('/sdcard', { replace: true });
  } catch (e) {
    if (e.name !== 'NotFoundError') { log('Connect error: ' + e.message); setStatus(e.message); }
  }
}

async function onDelete(fullPath, type) {
  const name = fullPath.split('/').pop();
  const body = type === 'dir'
    ? `Delete folder <code>${escHtml(name)}</code> and everything inside it?`
    : `Delete <code>${escHtml(name)}</code>?`;
  const ok = await showDialog({ title: 'Confirm Delete', body, confirmLabel: 'Delete', danger: true });
  if (!ok) return;
  setBusy(true);
  log(`rm ${fullPath}`);
  try {
    const r = await cmdDelete(fullPath);
    log(`rm: ${r}`);
    if (r === 'OK') { setStatus('Deleted'); await navigate(currentPath); }
    else setStatus('Delete failed: ' + r);
  } catch (e) {
    log('Error: ' + e.message); setStatus('Error: ' + e.message);
  } finally { setBusy(false); }
}

async function onNewFolder() {
  const name = await showDialog({ title: 'New Folder', body: `Inside <code>${escHtml(currentPath.replace('/sdcard', 'SD Card'))}</code>`, confirmLabel: 'Create', inputPlaceholder: 'Folder name' });
  if (!name) return;
  const safe = name.replace(/[/\\:*?"<>|]/g, '');
  if (!safe) return;
  const path = currentPath + '/' + safe;
  setBusy(true);
  log(`mkdir ${path}`);
  try {
    const r = await cmdMkdir(path);
    log(`mkdir: ${r}`);
    if (r === 'OK') { setStatus('Folder created'); await navigate(currentPath); }
    else setStatus('Create failed: ' + r);
  } catch (e) {
    log('Error: ' + e.message); setStatus('Error: ' + e.message);
  } finally { setBusy(false); }
}

async function onRename(fullPath) {
  const name = fullPath.split('/').pop();
  const dir  = fullPath.slice(0, fullPath.lastIndexOf('/'));
  const newName = await showDialog({
    title: 'Rename',
    body: `Rename <code>${escHtml(name)}</code>`,
    confirmLabel: 'Rename',
    inputPlaceholder: 'New name',
    defaultValue: name,
  });
  if (!newName || newName === name) return;
  const safe = newName.replace(/[/\\:*?"<>|]/g, '').trim();
  if (!safe) return;
  const newPath = dir + '/' + safe;
  setBusy(true);
  log(`rename ${fullPath} → ${newPath}`);
  try {
    const r = await cmdRename(fullPath, newPath);
    log(`rename: ${r}`);
    if (r === 'OK') { setStatus('Renamed'); await navigate(currentPath); }
    else setStatus('Rename failed: ' + r);
  } catch (e) {
    log('Error: ' + e.message); setStatus('Error: ' + e.message);
  } finally { setBusy(false); }
}

async function onUpload(files) {
  if (!conn) return;
  const list = [...files];
  if (!list.length) return;
  setBusy(true);
  for (let i = 0; i < list.length; i++) {
    const file = list[i];
    const dest = currentPath + '/' + file.name;
    progressWrap.classList.add('visible');
    progressFill.style.width = '0%';
    progressLabel.textContent = `Uploading ${file.name} (${i + 1}/${list.length}) — ${fmtSize(file.size)}`;
    log(`upload ${dest}`);
    try {
      await cmdWriteFile(dest, file, pct => { progressFill.style.width = (pct * 100).toFixed(1) + '%'; });
      progressFill.style.width = '100%';
      log(`OK: ${file.name}`);
      setStatus(`Uploaded ${file.name}`);
    } catch (e) {
      log(`Upload failed: ${e.message}`);
      setStatus('Upload failed: ' + e.message);
    }
  }
  progressWrap.classList.remove('visible');
  setBusy(false);
  await navigate(currentPath);
}

// Events
btnConnect.addEventListener('click', onConnect);

function closeMenu() {
  connectMenu.classList.remove('open');
  btnMenu.setAttribute('aria-expanded', 'false');
}
btnMenu.addEventListener('click', e => {
  e.stopPropagation();
  const isOpen = connectMenu.classList.toggle('open');
  btnMenu.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
});
document.addEventListener('click', e => {
  if (!connectMenu.contains(e.target) && e.target !== btnMenu) closeMenu();
});
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') closeMenu();
});

autoChk.checked = autoconnect;
autoChk.addEventListener('change', () => {
  autoconnect = autoChk.checked;
  try { localStorage.setItem('autoconnect', autoconnect ? '1' : '0'); } catch (_) {}
  log(`Autoconnect ${autoconnect ? 'enabled' : 'disabled'}`);
});
btnRefresh.addEventListener('click', () => navigate(currentPath));
btnNewFolder.addEventListener('click', onNewFolder);
btnUpload.addEventListener('click', () => { if (!busy && conn) fileInput.click(); });
fileInput.addEventListener('change', () => { if (fileInput.files.length) onUpload(fileInput.files); fileInput.value = ''; });
let dragDepth = 0;
fileBrowser.addEventListener('dragenter', e => {
  e.preventDefault();
  if (busy || !conn) return;
  dragDepth++;
  fileBrowser.classList.add('drag-over');
});
fileBrowser.addEventListener('dragover', e => { e.preventDefault(); });
fileBrowser.addEventListener('dragleave', () => {
  dragDepth--;
  if (dragDepth <= 0) { dragDepth = 0; fileBrowser.classList.remove('drag-over'); }
});
fileBrowser.addEventListener('drop', e => {
  e.preventDefault();
  dragDepth = 0;
  fileBrowser.classList.remove('drag-over');
  onUpload(e.dataTransfer.files);
});

// Init
renderBreadcrumb();

// Sortable column headers
document.querySelectorAll('thead th[data-col]').forEach(th => {
  th.addEventListener('click', () => {
    const col = th.dataset.col;
    if (sortCol === col) sortDir = sortDir === 'asc' ? 'desc' : 'asc';
    else { sortCol = col; sortDir = 'asc'; }
    renderEntries(currentEntries);
  });
});

// Browser back / forward
window.addEventListener('popstate', e => {
  if (conn && e.state?.path) navigate(e.state.path);
});

// Auto-reconnect on page load/refresh.
// Strategy: listen for the 'connect' event that fires when the device (re-)enumerates.
// Browsers that persist port grants (Chrome) fire it after the device reboots post-refresh.
// Browsers that don't (Firefox) never fire it — user just clicks Connect, no polling wasted.
if ('serial' in navigator) {
  async function autoOpen(port) {
    if (conn) return;
    const { usbVendorId, usbProductId } = port.getInfo();
    if (usbVendorId !== 0x303A || usbProductId !== 0x1001) return;
    log('[auto] ESP32 detected — opening port…');
    try {
      await port.open({ baudRate: 115200 });
      conn = new SerialConn(port);
      setConnected(true);
      await syncClock();
      await navigate('/sdcard', { replace: true });
    } catch (e) {
      log('[auto] open failed: ' + e.message);
      setStatus('Autoconnect failed — click Connect');
    }
  }

  navigator.serial.addEventListener('connect', e => { if (autoconnect) autoOpen(e.target); });

  // Also check immediately — device may already be enumerated if it didn't reboot.
  (async () => {
    if (!autoconnect) return;
    const all = await navigator.serial.getPorts();
    const esp = all.find(p => {
      const { usbVendorId, usbProductId } = p.getInfo();
      return usbVendorId === 0x303A && usbProductId === 0x1001;
    });
    if (esp) autoOpen(esp);
  })();
}

if (!('serial' in navigator)) {
  document.body.innerHTML = `
    <div style="padding:48px;text-align:center;font-family:system-ui">
      <h2 style="color:var(--danger)">Web Serial API not available</h2>
      <p style="margin-top:12px;color:var(--text-secondary)">Open this page in Chrome, Edge, or Firefox</p>
    </div>`;
}