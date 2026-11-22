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
  const gespeichert = [];
  const b = await chromium.launch();
  const seite = async () => {
    const ctx = await b.newContext();
    await ctx.exposeFunction('__kget', k => store.has(k) ? JSON.stringify(store.get(k)) : null);
    await ctx.exposeFunction('__kset', (k, v) => { store.set(k, JSON.parse(v)); });
    await ctx.exposeFunction('__dl', (name, b64) => { gespeichert.push({name, bytes: Buffer.from(b64, 'base64')}); });
    await ctx.addInitScript(() => {
      const doc = k => ({
        get: async () => { const v = await window.__kget(k); return v ? {exists: true, data: () => JSON.parse(v)} : {exists: false, data: () => undefined}; },
        set: async d => window.__kset(k, JSON.stringify(d)),
      });
      // downloads wie die echte Seite: nur erlaubte Endungen (kein .wav)
      const erlaubt = /\.(gif|png|jpe?g|webp|mp4|webm|mov|txt|json|md|docx|pptx|epub|csv|ttf|html|svg|pdf|xlsx|zip|fig)$/;
      const downloads = {save: async ({filename, data}) => {
        if (!erlaubt.test(filename)) { const e = new Error('rejected'); e.code = 'rejected_extension'; throw e; }
        const u = new Uint8Array(await new Blob([data]).arrayBuffer());
        let s = ''; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000));
        await window.__dl(filename, btoa(s));
      }};
      window.claude = {use: async n => n === 'user' ? {id: async () => 'u1'}
        : n === 'db' ? {collection: c => ({doc: k => doc(c + '/' + k)})} : n === 'downloads' ? downloads : null};
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
  // Song exportieren und speichern: ueber die Download-Funktion als ZIP (sie erlaubt kein .wav)
  await B.p.locator('#smpClose').click().catch(() => {});
  await B.p.locator('#songBtn').click(); await B.p.waitForTimeout(200);
  await B.p.locator('#songAdd').click(); await B.p.waitForTimeout(100);
  await B.p.locator('#songExport').click();
  await B.p.locator('#songSave').waitFor({state: 'visible', timeout: 30000});
  chk((await B.p.locator('#songSave').textContent()).startsWith('SAVE ZIP'), 'mit Download-Funktion heisst der Knopf nicht SAVE ZIP');
  await B.p.locator('#songSave').click(); await B.p.waitForTimeout(1500);
  const z = gespeichert.find(g => g.name.endsWith('.zip'));
  chk(!!z, 'keine ZIP gespeichert: ' + (await B.p.locator('#recState').textContent()));
  if (z) {
    const b = z.bytes;
    const nlen = b.readUInt16LE(26), size = b.readUInt32LE(18), crc = b.readUInt32LE(14);
    const inner = b.subarray(30, 30 + nlen).toString(), wav = b.subarray(30 + nlen, 30 + nlen + size);
    const zlib = require('zlib');
    chk(b.readUInt32LE(0) === 0x04034b50 && inner.endsWith('.wav'), 'die ZIP enthaelt keine WAV');
    chk(zlib.crc32 ? zlib.crc32(wav) === crc : true, 'die CRC der ZIP stimmt nicht');
    chk(wav.subarray(0, 4).toString() === 'RIFF' && wav.readUInt16LE(34) === 24 && wav.length === size, 'die WAV in der ZIP ist kaputt oder nicht 24 Bit');
    chk(b.readUInt32LE(b.length - 22) === 0x06054b50, 'der ZIP fehlt das Ende');
  }
  await B.ctx.close();
  await b.close();
  console.log(fails ? `\nFEHLGESCHLAGEN: ${fails} von ${checks} Pruefungen` : `\nOK: ${checks} Pruefungen ok, 0 Fehler`);
  process.exit(fails ? 1 : 0);
})();
