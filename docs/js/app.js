import { CONFIG } from './config.js';
import { STRINGS, detectLanguage } from './i18n.js';
import { openDisc } from './xdvdfs.js';
import { inspectGame, writePackage, XEX_SHA256 } from './installer.js';
import { directorySink, zipSink } from './sinks.js';

const $ = (id) => document.getElementById(id);
const state = {
  lang: detectLanguage(),
  game: null,        // result of inspectGame, when ok
  gameView: null,    // last text shown for the game, re-rendered on language change
  buildBlob: null,   // the build zip (downloaded or picked)
  buildName: '',
  buildVersion: null,
  busy: false,
};

const hasFolderPicker = typeof window.showDirectoryPicker === 'function';
const hasSavePicker = typeof window.showSaveFilePicker === 'function';
const t = () => STRINGS[state.lang];

function formatSize(bytes) {
  if (bytes >= 1e9) return (bytes / 1e9).toFixed(2) + ' GB';
  if (bytes >= 1e6) return (bytes / 1e6).toFixed(1) + ' MB';
  return Math.max(1, Math.round(bytes / 1e3)) + ' KB';
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

// ---------------------------------------------------------------- language

function applyLanguage() {
  const s = t();
  document.documentElement.lang = state.lang === 'pt' ? 'pt-BR' : 'en';
  document.title = s.title;
  $('title').textContent = s.title;
  $('subtitle').innerHTML = s.subtitle;
  $('privacy').textContent = s.privacy;
  $('browserWarning').textContent = s.browserWarning;
  $('disclaimer').textContent = s.disclaimer;
  $('releasesLink').textContent = s.releasesLink;
  for (const el of document.querySelectorAll('[data-i18n]')) el.textContent = s[el.dataset.i18n];
  for (const el of document.querySelectorAll('[data-i18n-html]')) el.innerHTML = s[el.dataset.i18nHtml];
  $('chooseIso').textContent = s.chooseIso;
  $('chooseFolder').textContent = s.chooseFolder;
  $('chooseBuildZip').textContent = s.chooseBuildZip;
  $('saveFolder').textContent = s.saveFolder;
  $('saveZip').textContent = s.saveZip;
  $('step3List').innerHTML = s.step3Items.map((item) => `<li>${item}</li>`).join('');
  for (const button of document.querySelectorAll('.lang button')) {
    button.setAttribute('aria-pressed', String(button.dataset.lang === state.lang));
  }
  renderGameStatus();
  renderBuildStatus();
  updateButtons();
}

// ----------------------------------------------------------------- game

function renderGameStatus() {
  const s = t();
  const view = state.gameView;
  const box = $('gameStatus');
  if (!view) {
    box.innerHTML = '';
  } else if (view.kind === 'reading') {
    box.textContent = s.reading;
  } else if (view.kind === 'error') {
    box.innerHTML = `<span class="bad">${escapeHtml(s.problem)}: ${escapeHtml(view.message)}</span>`;
  } else {
    const r = view.report;
    const items = [];
    if (r.ok) {
      items.push(`<span class="ok">✓ ${escapeHtml(s.xexOk)}</span>`);
      items.push(`<span class="ok">✓ ${escapeHtml(s.gameFiles(r.dataFiles, formatSize(r.bytes)))}</span>`);
    } else {
      for (const p of r.problems) items.push(`<span class="bad">✗ ${escapeHtml(p)}</span>`);
      if (r.xexHash) items.push(`<span class="muted">SHA-256: <code>${r.xexHash}</code></span>`);
    }
    box.innerHTML = `<ul>${items.map((i) => `<li>${i}</li>`).join('')}</ul>`;
  }
}

async function loadGame(filesPromise) {
  state.game = null;
  state.gameView = { kind: 'reading' };
  renderGameStatus();
  updateButtons();
  try {
    const files = await filesPromise;
    const report = await inspectGame(files);
    state.gameView = { kind: 'report', report };
    state.game = report.ok ? report : null;
  } catch (error) {
    state.gameView = { kind: 'error', message: error.message || String(error) };
  }
  renderGameStatus();
  updateButtons();
}

async function walkDirectory(handle, prefix, files, depth = 0) {
  if (depth > 6 || files.size > 20000) return;
  for await (const [name, entry] of handle.entries()) {
    if (entry.kind === 'file') files.set(prefix + name, await entry.getFile());
    else await walkDirectory(entry, prefix + name + '/', files, depth + 1);
  }
}

async function pickFolder() {
  if (hasFolderPicker) {
    try {
      const handle = await window.showDirectoryPicker({ mode: 'read' });
      return loadGame((async () => {
        const files = new Map();
        await walkDirectory(handle, '', files);
        return files;
      })());
    } catch (error) {
      if (error.name === 'AbortError') return;
      throw error;
    }
  }
  $('folderInput').click();
}

// ----------------------------------------------------------------- build

function renderBuildStatus() {
  const s = t();
  const box = $('buildStatus');
  if (state.buildBlob) {
    const label = state.buildVersion ? s.buildVersion(state.buildVersion) : state.buildName;
    box.innerHTML = `<span class="ok">✓ ${escapeHtml(label)} (${formatSize(state.buildBlob.size)})</span>`;
  } else if (state.buildError) {
    box.innerHTML = `<span class="bad">${escapeHtml(s.buildMissing)}</span> <a href="${CONFIG.releasesUrl}" rel="noopener">${escapeHtml(s.releasesLink)}</a>`;
  } else {
    box.textContent = '';
  }
}

async function downloadBuild() {
  if (state.buildBlob && state.buildName === 'download') return state.buildBlob;
  state.buildError = false;
  try {
    try {
      const info = await (await fetch(CONFIG.versionUrl, { cache: 'no-cache' })).json();
      state.buildVersion = info.version;
    } catch { /* the version label is optional */ }
    const response = await fetch(CONFIG.buildUrl, { cache: 'no-cache' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const total = Number(response.headers.get('content-length')) || 0;
    const reader = response.body.getReader();
    const chunks = [];
    let received = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
      received += value.length;
      setWriteStatus(t().downloading((received / 1e6).toFixed(1)));
      if (total) setProgress(received, total);
    }
    state.buildBlob = new Blob(chunks, { type: 'application/zip' });
    state.buildName = 'download';
  } catch (error) {
    state.buildBlob = null;
    state.buildError = true;
    renderBuildStatus();
    throw error;
  }
  renderBuildStatus();
  return state.buildBlob;
}

function buildSource() {
  return document.querySelector('input[name="buildSource"]:checked').value;
}

// ----------------------------------------------------------------- write

function setWriteStatus(text, kind = '') {
  $('writeStatus').innerHTML = kind ? `<span class="${kind}">${escapeHtml(text)}</span>` : escapeHtml(text);
}

function setProgress(done, total) {
  const bar = $('progress');
  bar.hidden = false;
  bar.max = total || 1;
  bar.value = done;
}

function updateButtons() {
  const updateOnly = $('updateOnly').checked;
  const ready = !state.busy && (updateOnly || !!state.game);
  $('saveFolder').disabled = !ready || !hasFolderPicker;
  $('saveZip').disabled = !ready || !hasSavePicker;
  $('chooseIso').disabled = state.busy;
  $('chooseFolder').disabled = state.busy;
  $('localBuildRow').hidden = buildSource() !== 'local';
  $('buildZipName').textContent = state.buildName && state.buildName !== 'download' ? state.buildName : '';
  $('writeStatus').title = ready ? '' : t().needGameFirst;
}

async function run(makeSink) {
  const s = t();
  if (state.busy) return;
  const updateOnly = $('updateOnly').checked;
  if (!updateOnly && !state.game) {
    setWriteStatus(s.needGameFirst, 'bad');
    return;
  }
  let sink;
  try {
    sink = await makeSink();
  } catch (error) {
    if (error.name === 'AbortError') return;
    setWriteStatus(error.message, 'bad');
    return;
  }
  state.busy = true;
  updateButtons();
  try {
    const buildZip = buildSource() === 'local' ? state.buildBlob : await downloadBuild();
    if (!buildZip) {
      setWriteStatus(s.buildMissing, 'bad');
      return;
    }
    await writePackage({
      buildZip,
      game: updateOnly ? null : state.game.game,
      sink,
      onProgress: (done, total) => {
        setProgress(done, total);
        setWriteStatus(s.writing(formatSize(done), formatSize(total)));
      },
    });
    setWriteStatus(s.finished, 'ok');
  } catch (error) {
    console.error(error);
    setWriteStatus(error.message || String(error), 'bad');
  } finally {
    state.busy = false;
    updateButtons();
  }
}

// ----------------------------------------------------------------- events

function wire() {
  $('chooseIso').addEventListener('click', () => $('isoInput').click());
  $('isoInput').addEventListener('change', (event) => {
    const file = event.target.files[0];
    if (file) loadGame(openDisc(file));
    event.target.value = '';
  });
  $('chooseFolder').addEventListener('click', () => pickFolder().catch((e) => {
    state.gameView = { kind: 'error', message: e.message };
    renderGameStatus();
  }));
  $('folderInput').addEventListener('change', (event) => {
    const list = [...event.target.files];
    if (list.length) {
      loadGame(Promise.resolve(new Map(list.map((f) => [f.webkitRelativePath || f.name, f]))));
    }
    event.target.value = '';
  });
  for (const radio of document.querySelectorAll('input[name="buildSource"]')) {
    radio.addEventListener('change', updateButtons);
  }
  $('chooseBuildZip').addEventListener('click', () => $('buildZipInput').click());
  $('buildZipInput').addEventListener('change', (event) => {
    const file = event.target.files[0];
    if (file) {
      state.buildBlob = file;
      state.buildName = file.name;
      state.buildVersion = null;
      state.buildError = false;
      renderBuildStatus();
      updateButtons();
    }
    event.target.value = '';
  });
  $('updateOnly').addEventListener('change', updateButtons);
  $('saveFolder').addEventListener('click', () => run(async () => {
    const handle = await window.showDirectoryPicker({ mode: 'readwrite' });
    return directorySink(handle);
  }));
  $('saveZip').addEventListener('click', () => run(async () => {
    const handle = await window.showSaveFilePicker({
      suggestedName: 'superman_returns.zip',
      types: [{ description: 'ZIP', accept: { 'application/zip': ['.zip'] } }],
    });
    return zipSink(await handle.createWritable(), 'superman_returns');
  }));
  for (const button of document.querySelectorAll('.lang button')) {
    button.addEventListener('click', () => {
      state.lang = button.dataset.lang;
      try { localStorage.setItem('sr-lang', state.lang); } catch { /* private mode */ }
      applyLanguage();
    });
  }
}

$('expectedHash').textContent = XEX_SHA256;
$('repoLink').href = CONFIG.repoUrl;
$('releasesLink').href = CONFIG.releasesUrl;
if (!hasFolderPicker || !hasSavePicker) $('browserWarning').hidden = false;
wire();
applyLanguage();
