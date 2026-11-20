// Konto-Speicher mit nachgebauter Schnittstelle: Projekt UND Samples kommen auf einem frischen
// Geraet (leerer Browser-Speicher) aus dem Konto zurueck.
const {chromium} = require('playwright');
const fs = require('fs'), os = require('os'), path = require('path');
let fails = 0, checks = 0;
const chk = (ok, was) => { checks++; if (!ok) { fails++; console.log('  FEHLER: ' + was); } };
function wav(datei) {
  const n = 24000, b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVE', 8); b.write('fmt ', 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(48000, 24);
  b.writeUInt32LE(96000, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(Math.sin(i * 0.05) * 20000), 44 + i * 2);
  fs.writeFileSync(datei, b); return datei;
}
(async () => {
  const url = process.argv[2] || 'http://localhost:8766/index.html';
  console.log('Konto-Tests (' + url + ')');
  const store = new Map();
  const b = await chromium.launch();
  const seite = async () => {
    const ctx = await b.newContext();
    await ctx.exposeFunction('__kget', k => store.has(k) ? JSON.stringify(store.get(k)) : null);
    await ctx.exposeFunction('__kset', (k, v) => { store.set(k, JSON.parse(v)); });
    await ctx.addInitScript(() => {
      const doc = k => ({
        get: async () => { const v = await window.__kget(k); return v ? {exists: true, data: () => JSON.parse(v)} : {exists: false, data: () => undefined}; },
        set: async d => window.__kset(k, JSON.stringify(d)),
      });
      window.claude = {use: async n => n === 'user' ? {id: async () => 'u1'}
        : n === 'db' ? {collection: c => ({doc: k => doc(c + '/' + k)})} : null};
    });
    const p = await ctx.newPage();
    await p.goto(url); await p.waitForTimeout(1500);
    return {ctx, p};
  };
  // Geraet A: Sample laden, auf Spur 1 legen, Tempo aendern
  const A = await seite();
  await A.p.locator('#smpBtn').click(); await A.p.waitForTimeout(300);
  await A.p.locator('#smpFile').setInputFiles(wav(path.join(os.tmpdir(), 'konto-test.wav')));
  await A.p.waitForTimeout(1500);
  await A.p.locator('#smpUse').click(); await A.p.waitForTimeout(300);
  await A.p.locator('#bpmUp').click(); await A.p.waitForTimeout(7000);
  const bpmA = (await A.p.locator('#bpm').textContent()).trim();
  chk(store.has('data/users/u1/projekt'), 'das Projekt liegt nicht im Konto');
  chk([...store.keys()].some(k => /smp-[0-9a-f]+-0$/.test(k)), 'das Sample liegt nicht im Konto');
  await A.p.locator('#docBtn').click(); await A.p.waitForTimeout(200);
  const standA = await A.p.locator('#saveState').textContent();
  chk(/Account: saved/.test(standA), 'die Anzeige sagt nicht, dass ins Konto gespeichert wurde: ' + standA);
  await A.ctx.close();
  // Geraet B: frischer Browser-Speicher
  const B = await seite();
  await B.p.waitForTimeout(2500);
  chk((await B.p.locator('#bpm').textContent()).trim() === bpmA, 'das Projekt kam nicht aus dem Konto');
  await B.p.locator('#smpBtn').click(); await B.p.waitForTimeout(300);
  const eintrag = (await B.p.locator('#smpList button').first().textContent()).trim();
  chk(eintrag.includes('konto-test') && eintrag.includes('0.50 s'), 'das Sample kam nicht aus dem Konto: ' + eintrag);
  await B.ctx.close();
  await b.close();
  console.log(fails ? `\nFEHLGESCHLAGEN: ${fails} von ${checks} Pruefungen` : `\nOK: ${checks} Pruefungen ok, 0 Fehler`);
  process.exit(fails ? 1 : 0);
})();
