// Die Seite des Geraete-Webservers (siehe web.cpp). Ohne Bibliotheken, ohne externe Dateien.
// Samples als Ordner-Explorer: Brotkrumen, Ordner zuerst, Vorhoeren, Suche, ganze Ordner
// importieren (Auswahl oder Ziehen, die Unterordner bleiben erhalten).
extern const char WEB_PAGE[];
const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SYNTH CITY CORE</title>
<style>
:root{--bg:#0e0f12;--panel:#16181d;--row:#1b1e24;--sel:#252a35;--line:#2c303a;--txt:#dde1e8;--dim:#7d8390;--faint:#4a4f5a;--acc:#5ec8ff;--lock:#ffb020;--ok:#4fd6a0;--bad:#ff4d5e;--pink:#ff4d9d}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:14px/1.4 system-ui,-apple-system,Segoe UI,sans-serif}
header{padding:14px 20px;border-bottom:2px solid;border-image:linear-gradient(90deg,var(--pink),var(--acc)) 1;display:flex;gap:14px;align-items:baseline;flex-wrap:wrap}
h1{font-size:17px;margin:0;letter-spacing:.08em}header small{color:var(--dim)}
main{max-width:980px;margin:0 auto;padding:14px 16px 60px}
section{background:var(--panel);border-radius:10px;margin:14px 0;overflow:hidden}
.head{display:flex;align-items:center;gap:8px;padding:12px 14px;flex-wrap:wrap;border-bottom:1px solid var(--line)}
.head h2{font-size:12px;letter-spacing:.12em;color:var(--dim);margin:0 auto 0 0}
button,.btn{display:inline-flex;align-items:center;gap:6px;background:var(--sel);color:var(--txt);border:1px solid var(--line);border-radius:7px;padding:6px 11px;font:inherit;font-size:12.5px;cursor:pointer;white-space:nowrap}
button:hover,.btn:hover{border-color:var(--acc)}
.btn.prim{background:#1d3a4a;border-color:#2b5a73}
input[type=file]{display:none}
input[type=search]{background:var(--bg);color:var(--txt);border:1px solid var(--line);border-radius:7px;padding:6px 10px;font:inherit;font-size:13px;width:180px}
svg{width:16px;height:16px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;flex:none}
.crumbs{display:flex;align-items:center;gap:2px;padding:9px 14px;flex-wrap:wrap;font-size:13px;background:var(--row);border-bottom:1px solid var(--line)}
.crumbs a{color:var(--acc);cursor:pointer;padding:2px 5px;border-radius:5px}.crumbs a:hover{background:var(--sel)}
.crumbs span{color:var(--faint)}.crumbs b{padding:2px 5px;font-weight:600}
.list{max-height:62vh;overflow:auto}
.it{display:flex;align-items:center;gap:10px;padding:0 14px;height:38px;border-bottom:1px solid var(--line);cursor:default}
.it:hover{background:var(--row)}
.it .nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.it .nm small{color:var(--dim);margin-right:6px}
.it .mt{color:var(--dim);font-size:12px;white-space:nowrap;min-width:64px;text-align:right}
.it.dir{cursor:pointer}.it.dir .ic{color:var(--lock)}.it.up .ic{color:var(--dim)}
.it .ic{color:var(--acc)}
.ib{background:none;border:none;padding:6px;border-radius:6px;color:var(--dim)}
.ib:hover{background:var(--sel);color:var(--txt)}.ib.del:hover{color:var(--bad)}
.ib.play.on{color:var(--ok)}
.act{display:flex;gap:2px;opacity:.55}.it:hover .act{opacity:1}
.empty{padding:22px 14px;color:var(--dim);text-align:center}
#dropHint{display:none;position:absolute;inset:0;background:rgba(94,200,255,.12);border:2px dashed var(--acc);border-radius:10px;align-items:center;justify-content:center;font-size:15px;pointer-events:none}
#smpSec{position:relative}#smpSec.drag #dropHint{display:flex}
#up{display:none;padding:10px 14px;border-bottom:1px solid var(--line);background:var(--row)}
#up .t{display:flex;justify-content:space-between;gap:8px;font-size:12.5px;color:var(--dim)}
#up .t b{color:var(--txt);font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bar{height:4px;background:var(--line);border-radius:2px;overflow:hidden;margin-top:6px}.bar i{display:block;height:100%;width:0;background:var(--ok);transition:width .15s}
.foot{padding:9px 14px;color:var(--faint);font-size:12px}
#msg{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:var(--sel);border:1px solid var(--line);padding:9px 16px;border-radius:8px;display:none;max-width:90vw;box-shadow:0 6px 24px #0008}
@media (max-width:600px){input[type=search]{width:100%;order:9}.it .mt{display:none}.head h2{flex-basis:100%}}
</style></head><body>
<svg style="display:none"><defs>
<symbol id="i-dir" viewBox="0 0 24 24"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></symbol>
<symbol id="i-up" viewBox="0 0 24 24"><path d="M9 14 4 9l5-5"/><path d="M4 9h11a5 5 0 0 1 0 10h-3"/></symbol>
<symbol id="i-wav" viewBox="0 0 24 24"><path d="M3 12h2l2-6 3 12 3-9 2 5 2-2h4"/></symbol>
<symbol id="i-play" viewBox="0 0 24 24"><path d="M7 5v14l11-7z"/></symbol>
<symbol id="i-stop" viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="1"/></symbol>
<symbol id="i-dl" viewBox="0 0 24 24"><path d="M12 4v11"/><path d="m7 10 5 5 5-5"/><path d="M5 20h14"/></symbol>
<symbol id="i-del" viewBox="0 0 24 24"><path d="M4 7h16"/><path d="M10 11v6M14 11v6"/><path d="M6 7l1 13h10l1-13"/><path d="M9 7V4h6v3"/></symbol>
<symbol id="i-plus" viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></symbol>
<symbol id="i-file" viewBox="0 0 24 24"><path d="M14 3H6v18h12V7z"/><path d="M14 3v4h4"/></symbol>
<symbol id="i-prj" viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="2"/><path d="M7 9v6M11 9v6M15 9v6"/></symbol>
</defs></svg>
<header><h1>SYNTH CITY CORE</h1><small id="hd">device</small></header>
<main>
<section id="prjSec">
  <div class="head"><h2>PROJECTS</h2>
    <label class="btn"><svg><use href="#i-file"/></svg>UPLOAD PROJECT<input type="file" id="upP" accept=".sc,.txt" multiple></label></div>
  <div class="list" id="prj"></div>
  <div class="foot">Same format as the phone app (its SAVE FILE .txt works too). On the device: PROJ &gt; OPEN PROJECT.</div>
</section>
<section id="smpSec">
  <div class="head"><h2>SAMPLES</h2>
    <input type="search" id="q" placeholder="Search all samples">
    <button id="mk"><svg><use href="#i-plus"/></svg>FOLDER</button>
    <label class="btn"><svg><use href="#i-wav"/></svg>FILES<input type="file" id="upS" accept=".wav,audio/wav" multiple></label>
    <label class="btn prim"><svg><use href="#i-dir"/></svg>IMPORT FOLDER<input type="file" id="upD" webkitdirectory directory multiple></label></div>
  <div id="up"><div class="t"><b id="upName"></b><span id="upCnt"></span></div><div class="bar"><i id="prog"></i></div></div>
  <div class="crumbs" id="crumbs"></div>
  <div class="list" id="smp"></div>
  <div class="foot" id="smpFoot"></div>
  <div id="dropHint">Drop .wav files or whole folders - they go into this folder</div>
</section>
<section id="updSec" hidden>
  <div class="head"><h2>APP UPDATE</h2><label class="btn"><svg><use href="#i-dl"/></svg>CHOOSE .tgz<input type="file" id="upU" accept=".tgz,application/gzip"></label></div>
  <div class="foot">The device saves, restarts and installs. If the new version fails to start three times, the previous one comes back.</div>
</section>
</main>
<div id="msg"></div>
<audio id="au"></audio>
<script>
'use strict';
const el = id => document.getElementById(id);
const msg = t => { const m = el('msg'); m.textContent = t; m.style.display = 'block'; clearTimeout(msg.t); msg.t = setTimeout(() => m.style.display = 'none', 3500); };
const size = n => n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';
const enc = s => encodeURIComponent(s);
const ico = (id, cls) => `<svg class="${cls || ''}"><use href="#i-${id}"/></svg>`;
const esc = s => s.replace(/[&<>"]/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'}[c]));
let S = {files: [], dirs: []}, cwd = '', playing = '';
try { cwd = sessionStorage.getItem('sc-cwd') || ''; } catch (e) {}
const setCwd = d => { cwd = d; try { sessionStorage.setItem('sc-cwd', d); } catch (e) {} el('q').value = ''; draw(); };
const parent = p => p.includes('/') ? p.slice(0, p.lastIndexOf('/')) : '';
const base = p => p.slice(p.lastIndexOf('/') + 1);

async function load() {
  try {
    const j = await (await fetch('/api/list')).json();
    el('updSec').hidden = !j.os;
    S.files = j.samples.filter(f => !f.d);
    S.dirs = j.samples.filter(f => f.d).map(f => f.p);
    for (const f of S.files) for (let d = parent(f.p); d; d = parent(d)) if (!S.dirs.includes(d)) S.dirs.push(d);
    if (cwd && !S.dirs.includes(cwd)) cwd = '';
    el('hd').textContent = j.projects.filter(f => !f.d).length + ' projects · ' + S.files.length + ' samples';
    drawProjects(j.projects.filter(f => !f.d));
    draw();
  } catch (e) { msg('Device not reachable'); }
}
function drawProjects(list) {
  const P = el('prj');
  list.sort((a, b) => b.t - a.t);
  P.innerHTML = list.length ? '' : '<div class="empty">No projects yet.</div>';
  for (const f of list) {
    const r = document.createElement('div'); r.className = 'it';
    r.innerHTML = `${ico('prj', 'ic')}<div class="nm">${esc(f.p.replace(/\.sc$/i, ''))}</div>
      <div class="mt">${new Date(f.t * 1000).toLocaleString()}</div><div class="mt">${size(f.s)}</div>
      <div class="act"><button class="ib" title="Download">${ico('dl')}</button><button class="ib del" title="Delete">${ico('del')}</button></div>`;
    const [dl, del] = r.querySelectorAll('button');
    dl.onclick = () => location.href = '/api/file?root=projects&path=' + enc(f.p);
    del.onclick = () => remove('projects', f.p, 'project ' + f.p);
    P.appendChild(r);
  }
}
function stats(dir) {   // Samples und Groesse unter einem Ordner (rekursiv)
  let n = 0, b = 0;
  for (const f of S.files) if (f.p.startsWith(dir + '/')) { n++; b += f.s; }
  return {n, b};
}
function draw() {
  const q = el('q').value.trim().toLowerCase();
  // Brotkrumen
  const C = el('crumbs'); C.innerHTML = '';
  const root = document.createElement(cwd || q ? 'a' : 'b'); root.textContent = 'samples'; root.onclick = () => setCwd('');
  C.appendChild(root);
  if (!q) {
    const parts = cwd ? cwd.split('/') : [];
    parts.forEach((p, i) => {
      const s = document.createElement('span'); s.textContent = '/'; C.appendChild(s);
      const a = document.createElement(i === parts.length - 1 ? 'b' : 'a'); a.textContent = p;
      const target = parts.slice(0, i + 1).join('/'); a.onclick = () => setCwd(target);
      C.appendChild(a);
    });
  } else { const s = document.createElement('span'); s.textContent = ' · search: ' + q; C.appendChild(s); }
  const L = el('smp'); L.innerHTML = '';
  let dirs = [], files = [];
  if (q) files = S.files.filter(f => f.p.toLowerCase().includes(q));
  else {
    dirs = S.dirs.filter(d => parent(d) === cwd).sort((a, b) => a.localeCompare(b, undefined, {numeric: true}));
    files = S.files.filter(f => parent(f.p) === cwd);
  }
  files.sort((a, b) => a.p.localeCompare(b.p, undefined, {numeric: true}));
  if (cwd && !q) {
    const r = document.createElement('div'); r.className = 'it dir up';
    r.innerHTML = `${ico('up', 'ic')}<div class="nm">..</div>`;
    r.onclick = () => setCwd(parent(cwd));
    L.appendChild(r);
  }
  for (const d of dirs) {
    const st = stats(d);
    const r = document.createElement('div'); r.className = 'it dir';
    r.innerHTML = `${ico('dir', 'ic')}<div class="nm">${esc(base(d))}</div><div class="mt">${st.n} samples</div><div class="mt">${st.n ? size(st.b) : ''}</div>
      <div class="act"><button class="ib del" title="Delete folder">${ico('del')}</button></div>`;
    r.onclick = () => setCwd(d);
    r.querySelector('button').onclick = e => { e.stopPropagation(); removeDir(d, st.n); };
    L.appendChild(r);
  }
  for (const f of files) {
    const r = document.createElement('div'); r.className = 'it';
    const shown = q ? `<small>${esc(parent(f.p))}/</small>${esc(base(f.p))}` : esc(base(f.p));
    const on = playing === f.p;
    r.innerHTML = `<button class="ib play${on ? ' on' : ''}" title="Listen">${ico(on ? 'stop' : 'play')}</button><div class="nm">${shown}</div>
      <div class="mt">${size(f.s)}</div>
      <div class="act"><button class="ib" title="Download">${ico('dl')}</button><button class="ib del" title="Delete">${ico('del')}</button></div>`;
    const [pl, dl, del] = r.querySelectorAll('button');
    pl.onclick = () => play(f.p);
    dl.onclick = () => location.href = '/api/file?root=samples&path=' + enc(f.p);
    del.onclick = () => remove('samples', f.p, base(f.p));
    L.appendChild(r);
  }
  if (!dirs.length && !files.length) L.insertAdjacentHTML('beforeend', `<div class="empty">${q ? 'Nothing found.' : 'Empty folder - drop .wav files or a whole folder here.'}</div>`);
  const here = cwd ? stats(cwd) : {n: S.files.length, b: S.files.reduce((a, f) => a + f.s, 0)};
  el('smpFoot').textContent = here.n + ' samples, ' + size(here.b) + (cwd ? ' in this folder' : ' in total') + '. Uploads go into the folder you are in. On the device: SELECT > Load sample.';
}
function play(p) {
  const a = el('au');
  if (playing === p) { a.pause(); playing = ''; draw(); return; }
  a.src = '/api/file?root=samples&path=' + enc(p); a.play().catch(() => {}); playing = p;
  a.onended = () => { playing = ''; draw(); };
  draw();
}
async function remove(root, p, what) {
  if (!confirm('Delete ' + what + '?')) return;
  const r = await fetch('/api/delete?root=' + root + '&path=' + enc(p), {method: 'POST'});
  msg(r.ok ? 'Deleted ' + what : await r.text()); load();
}
async function removeDir(d, n) {
  if (!confirm('Delete folder "' + d + '"' + (n ? ' with ' + n + ' samples' : '') + '?')) return;
  const r = await fetch('/api/rmdir?root=samples&path=' + enc(d), {method: 'POST'});
  msg(r.ok ? 'Deleted folder ' + d : await r.text());
  if (r.ok && (cwd === d || cwd.startsWith(d + '/'))) cwd = parent(d);
  load();
}
el('mk').onclick = async () => {
  const n = (prompt('New folder in ' + (cwd || 'samples') + ':') || '').trim();
  if (!n) return;
  const p = (cwd ? cwd + '/' : '') + clean(n);
  const r = await fetch('/api/mkdir?root=samples&path=' + enc(p), {method: 'POST'});
  if (r.ok) { await load(); setCwd(p); } else msg(await r.text());
};
el('q').oninput = draw;

// --- Hochladen: Dateien oder ganze Ordner, die Struktur bleibt erhalten ---------------------
const clean = s => s.replace(/[\\:*?"<>|]/g, '_').replace(/^\.+/, '').trim() || '_';
const cleanPath = p => p.split('/').filter(Boolean).map(clean).join('/');
function put(url, file, onp) {
  return new Promise(res => {
    const x = new XMLHttpRequest();
    x.open('PUT', url);
    x.upload.onprogress = e => { if (e.lengthComputable && onp) onp(e.loaded / e.total); };
    x.onload = () => res({ok: x.status === 200, text: x.responseText});
    x.onerror = () => res({ok: false, text: 'connection lost'});
    x.send(file);
  });
}
async function uploadSamples(items) {   // items: [{file, rel}] - rel relativ zum aktuellen Ordner
  const list = items.filter(i => /\.wav$/i.test(i.file.name));
  const skipped = items.length - list.length;
  if (!list.length) { msg(skipped ? 'No .wav files in there' : 'Nothing to upload'); return; }
  const U = el('up'); U.style.display = 'block';
  let ok = 0, fail = 0;
  const total = list.reduce((a, i) => a + i.file.size, 0); let done = 0;
  for (let k = 0; k < list.length; k++) {
    const it = list[k];
    const path = cleanPath((cwd ? cwd + '/' : '') + it.rel);
    el('upName').textContent = path; el('upCnt').textContent = (k + 1) + ' / ' + list.length;
    const r = await put('/api/upload?root=samples&path=' + enc(path), it.file,
      f => el('prog').style.width = (100 * (done + f * it.file.size) / total) + '%');
    done += it.file.size;
    if (r.ok) ok++; else { fail++; console.warn(path, r.text); }
  }
  U.style.display = 'none'; el('prog').style.width = '0';
  msg(ok + ' sample(s) uploaded' + (fail ? ', ' + fail + ' failed' : '') + (skipped ? ', ' + skipped + ' non-wav skipped' : ''));
  load();
}
el('upS').onchange = e => { uploadSamples([...e.target.files].map(f => ({file: f, rel: f.name}))); e.target.value = ''; };
el('upD').onchange = e => { uploadSamples([...e.target.files].map(f => ({file: f, rel: f.webkitRelativePath || f.name}))); e.target.value = ''; };
// Ziehen: Ordner per FileSystem-Eintraegen rekursiv einlesen
function readEntry(entry, path, out) {
  return new Promise(res => {
    if (entry.isFile) entry.file(f => { out.push({file: f, rel: path + f.name}); res(); }, () => res());
    else if (entry.isDirectory) {
      const rd = entry.createReader(), all = [];
      const more = () => rd.readEntries(async ents => {
        if (!ents.length) { for (const en of all) await readEntry(en, path + entry.name + '/', out); res(); }
        else { all.push(...ents); more(); }
      }, () => res());
      more();
    } else res();
  });
}
const sec = el('smpSec');
sec.ondragover = e => { e.preventDefault(); sec.classList.add('drag'); };
sec.ondragleave = e => { if (!sec.contains(e.relatedTarget)) sec.classList.remove('drag'); };
sec.ondrop = async e => {
  e.preventDefault(); sec.classList.remove('drag');
  const out = [];
  const ents = [...e.dataTransfer.items].map(i => i.webkitGetAsEntry && i.webkitGetAsEntry()).filter(Boolean);
  if (ents.length) { msg('Reading folders ...'); for (const en of ents) await readEntry(en, '', out); }
  else for (const f of e.dataTransfer.files) out.push({file: f, rel: f.name});
  uploadSamples(out);
};
el('upP').onchange = async e => {
  let ok = 0;
  for (const f of e.target.files) {
    const low = f.name.toLowerCase();
    let name = clean(f.name);
    if (low.endsWith('.txt')) name = name.slice(0, -4) + '.sc';
    else if (!low.endsWith('.sc')) { msg(f.name + ': only .sc or .txt'); continue; }
    const r = await put('/api/upload?root=projects&path=' + enc(name), f);
    if (r.ok) ok++; else msg(f.name + ': ' + r.text);
  }
  e.target.value = '';
  if (ok) msg(ok + ' project(s) uploaded');
  load();
};
el('upU').onchange = async e => {
  const f = e.target.files[0]; e.target.value = '';
  if (!f) return;
  el('up').style.display = 'block'; el('upName').textContent = f.name; el('upCnt').textContent = 'update';
  const r = await put('/api/update', f, x => el('prog').style.width = (100 * x) + '%');
  el('up').style.display = 'none'; el('prog').style.width = '0';
  msg(r.text);
};
load();
</script></body></html>
)HTML";
