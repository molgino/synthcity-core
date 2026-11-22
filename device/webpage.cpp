// Die Seite des Geraete-Webservers (siehe web.cpp). Ohne Bibliotheken, ohne externe Dateien.
extern const char WEB_PAGE[];
const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SYNTH CITY CORE</title>
<style>
:root{--bg:#0e0f12;--panel:#16181d;--sel:#252a35;--line:#343945;--txt:#dde1e8;--dim:#7d8390;--acc:#5ec8ff;--lock:#ffb020;--ok:#4fd6a0;--bad:#ff4d5e}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.4 system-ui,-apple-system,Segoe UI,sans-serif}
header{padding:14px 16px;border-bottom:2px solid;border-image:linear-gradient(90deg,#ff4d9d,#5ec8ff) 1;display:flex;gap:12px;align-items:baseline;flex-wrap:wrap}
h1{font-size:18px;margin:0;letter-spacing:.06em}header small{color:var(--dim)}
main{max-width:860px;margin:0 auto;padding:12px 16px 40px}
section{background:var(--panel);border-radius:8px;padding:12px 14px;margin:14px 0}
h2{font-size:14px;letter-spacing:.08em;color:var(--dim);margin:0 0 10px;display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap}
.row{display:flex;flex-wrap:wrap;gap:6px 10px;align-items:center;padding:8px 4px;border-top:1px solid var(--line)}
.row .n{flex:1 1 100%;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.row .s{color:var(--dim);font-size:13px;white-space:nowrap;flex:1}
button,label.btn{background:var(--sel);color:var(--txt);border:1px solid var(--line);border-radius:6px;padding:6px 10px;font:inherit;font-size:13px;cursor:pointer}
button:hover,label.btn:hover{border-color:var(--acc)}button.del{color:var(--bad)}
input[type=text]{background:var(--bg);color:var(--txt);border:1px solid var(--line);border-radius:6px;padding:6px 8px;font:inherit;font-size:13px;width:150px}
input[type=file]{display:none}
#drop{border:2px dashed var(--line);border-radius:8px;padding:14px;text-align:center;color:var(--dim);margin-bottom:8px}
#drop.on{border-color:var(--acc);color:var(--txt)}
#msg{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);background:var(--sel);border:1px solid var(--line);padding:8px 14px;border-radius:6px;display:none;max-width:90vw}
.bar{height:4px;background:var(--line);border-radius:2px;overflow:hidden;margin-top:6px}.bar i{display:block;height:100%;width:0;background:var(--ok)}
.hint{color:var(--dim);font-size:13px;margin:4px 0 0}
</style></head><body>
<header><h1>SYNTH CITY CORE</h1><small id="hd">device</small></header>
<main>
<section>
  <h2>PROJECTS <span><label class="btn">UPLOAD PROJECT<input type="file" id="upP" accept=".sc,.txt" multiple></label></span></h2>
  <div id="prj"></div>
  <p class="hint">Same format as the phone app: its SAVE FILE (.txt) works here too. On the device: PROJ &gt; OPEN PROJECT, choose with X/Y, A opens.</p>
</section>
<section>
  <h2>SAMPLES <span><input type="text" id="fold" placeholder="folder (optional)"> <label class="btn">UPLOAD .wav<input type="file" id="upS" accept=".wav,audio/wav" multiple></label></span></h2>
  <div id="drop">Drop .wav files here</div>
  <div class="bar"><i id="prog"></i></div>
  <div id="smp"></div>
  <p class="hint">Song exports appear under export/. Samples load on the device via SELECT &gt; Load sample.</p>
</section>
<section id="updSec" hidden>
  <h2>APP UPDATE <span><label class="btn">CHOOSE .tgz<input type="file" id="upU" accept=".tgz,application/gzip"></label></span></h2>
  <p class="hint">The device saves, restarts and installs the update. If the new version fails to start three times, the previous one comes back.</p>
</section>
</main>
<div id="msg"></div>
<script>
'use strict';
const el = id => document.getElementById(id);
const msg = t => { const m = el('msg'); m.textContent = t; m.style.display = 'block'; clearTimeout(msg.t); msg.t = setTimeout(() => m.style.display = 'none', 3500); };
const size = n => n > 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';
const enc = s => encodeURIComponent(s);
function row(root, f) {
  const r = document.createElement('div'); r.className = 'row';
  const n = document.createElement('div'); n.className = 'n'; n.textContent = f.p;
  const s = document.createElement('div'); s.className = 's'; s.textContent = size(f.s) + ' · ' + new Date(f.t * 1000).toLocaleString();
  const a = document.createElement('button'); a.textContent = 'DOWNLOAD';
  a.onclick = () => { location.href = '/api/file?root=' + root + '&path=' + enc(f.p); };
  const d = document.createElement('button'); d.textContent = 'DELETE'; d.className = 'del';
  d.onclick = async () => {
    if (!confirm('Delete ' + f.p + '?')) return;
    const r2 = await fetch('/api/delete?root=' + root + '&path=' + enc(f.p), {method: 'POST'});
    msg(r2.ok ? 'Deleted ' + f.p : await r2.text()); load();
  };
  r.append(n, s, a, d);
  return r;
}
async function load() {
  try {
    const j = await (await fetch('/api/list')).json();
    el('updSec').hidden = !j.os;
    el('hd').textContent = j.projects.length + ' projects · ' + j.samples.length + ' samples';
    const P = el('prj'); P.innerHTML = '';
    j.projects.sort((a, b) => b.t - a.t).forEach(f => P.appendChild(row('projects', f)));
    if (!j.projects.length) P.innerHTML = '<p class="hint">No projects yet.</p>';
    const S = el('smp'); S.innerHTML = '';
    j.samples.sort((a, b) => a.p.localeCompare(b.p)).forEach(f => S.appendChild(row('samples', f)));
    if (!j.samples.length) S.innerHTML = '<p class="hint">No samples yet.</p>';
  } catch (e) { msg('Device not reachable'); }
}
function put(url, file) {
  return new Promise(res => {
    const x = new XMLHttpRequest();
    x.open('PUT', url);
    x.upload.onprogress = e => { if (e.lengthComputable) el('prog').style.width = (100 * e.loaded / e.total) + '%'; };
    x.onload = () => res({ok: x.status === 200, text: x.responseText});
    x.onerror = () => res({ok: false, text: 'connection lost'});
    x.send(file);
  });
}
async function upload(root, files) {
  let ok = 0;
  const fold = root === 'samples' ? el('fold').value.trim().replace(/^\/+|\/+$/g, '') : '';
  for (const f of files) {
    const low = f.name.toLowerCase();
    let name = f.name.replace(/[\\:*?"<>|]/g, '_');
    if (root === 'samples') { if (!low.endsWith('.wav')) { msg(f.name + ': only .wav'); continue; } }
    else if (low.endsWith('.txt')) name = name.slice(0, -4) + '.sc';    // Sicherung aus der Handy-App
    else if (!low.endsWith('.sc')) { msg(f.name + ': only .sc or .txt'); continue; }
    name = (fold ? fold + '/' : '') + name;
    msg('Uploading ' + f.name + ' ...');
    const r = await put('/api/upload?root=' + root + '&path=' + enc(name), f);
    if (r.ok) ok++; else msg(f.name + ': ' + r.text);
  }
  el('prog').style.width = '0';
  if (ok) msg(ok + ' file(s) uploaded');
  load();
}
el('upP').onchange = e => { upload('projects', [...e.target.files]); e.target.value = ''; };
el('upS').onchange = e => { upload('samples', [...e.target.files]); e.target.value = ''; };
el('upU').onchange = async e => {
  const f = e.target.files[0]; e.target.value = '';
  if (!f) return;
  msg('Sending update ...');
  const r = await put('/api/update', f);
  el('prog').style.width = '0';
  msg(r.text);
};
const dz = el('drop');
dz.ondragover = e => { e.preventDefault(); dz.classList.add('on'); };
dz.ondragleave = () => dz.classList.remove('on');
dz.ondrop = e => { e.preventDefault(); dz.classList.remove('on'); upload('samples', [...e.dataTransfer.files]); };
load();
</script></body></html>
)HTML";
