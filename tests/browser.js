// Prueft die Oberflaeche in einem echten Browser: laden, tippen, hoeren.
// Gemessen wird am ERGEBNIS - am Pegel des Audio-Kerns, nicht an der Anzeige.
// Start:  node tests/browser.js [http://localhost:8787/index.html]
const {chromium} = require('playwright');
const fs = require('fs');
const os = require('os');
const path = require('path');

// Eine WAV-Datei zum Auswaehlen bauen - 16 Bit stereo, damit der Test nichts voraussetzt.
function testWav(datei, rate = 48000, sek = 0.5, hz = 440) {
  const n = Math.round(rate * sek), bl = 4;
  const b = Buffer.alloc(44 + n * bl);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * bl, 4); b.write('WAVE', 8);
  b.write('fmt ', 12); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20);
  b.writeUInt16LE(2, 22); b.writeUInt32LE(rate, 24); b.writeUInt32LE(rate * bl, 28);
  b.writeUInt16LE(bl, 32); b.writeUInt16LE(16, 34);
  b.write('data', 36); b.writeUInt32LE(n * bl, 40);
  for (let i = 0; i < n; i++) {
    const v = Math.round(20000 * Math.sin(2 * Math.PI * hz * i / rate));
    b.writeInt16LE(v, 44 + i * 4);
    b.writeInt16LE(Math.round(v * 0.5), 46 + i * 4);
  }
  fs.writeFileSync(datei, b);
  return datei;
}

let fails = 0, checks = 0;
const chk = (ok, what) => { checks++; if (!ok) { fails++; console.log('  FEHLER: ' + what); } };
const grp = n => console.log('- ' + n);

const pegel = p => p.locator('#meterFill').evaluate(e => parseFloat(e.style.width) || 0);
const hoechsterPegel = async (p, ms = 2000) => {
  let max = 0;
  for (let i = 0; i < ms / 100; i++) { max = Math.max(max, await pegel(p)); await p.waitForTimeout(100); }
  return max;
};
const stummMuster = async p => {
  const r = [];
  for (let k = 0; k < 8; k++)
    r.push(await p.locator('.trk').nth(k).evaluate(e => e.classList.contains('muted') ? 1 : 0));
  return r.join('');
};

(async () => {
  const url = process.argv[2] || 'http://localhost:8787/index.html';
  console.log('Oberflaechen-Tests (' + url + ')');
  const b = await chromium.launch({args: ['--autoplay-policy=no-user-gesture-required', '--no-sandbox']});
  const p = await b.newPage({viewport: {width: 420, height: 820}, hasTouch: true});
  const fehler = [];
  p.on('pageerror', e => fehler.push(e.message));
  p.on('console', m => { if (m.type() === 'error') fehler.push(m.text()); });
  await p.goto(url);
  await p.waitForTimeout(1500);

  grp('Die Oberflaeche baut sich aus dem Kern');
  chk(await p.locator('.pad').count() === 16, 'nicht 16 Steps');
  chk(await p.locator('.trk').count() === 8, 'nicht 8 Spuren');
  chk(await p.locator('.pt').count() === 16, 'nicht 16 Patterns');
  chk(await p.locator('.row').count() > 15, 'zu wenige Regler');
  chk((await p.locator('.row .v').first().textContent()).trim().length > 0, 'kein Anzeigetext');

  grp('Mute trifft und schaltet - auch bei schnellen Tipps');
  const m = await p.locator('.trk').first().locator('.m').boundingBox();
  chk(m.height >= 40 && m.width >= 28, 'der Mute-Knopf ist zu klein zum Treffen');
  for (let t = 0; t < 6; t++) await p.locator('.trk').nth(t).locator('.m').tap();   // ohne Pause
  await p.waitForTimeout(300);
  chk(await stummMuster(p) === '11111100', 'ein schneller Tipp ging verloren');
  for (let t = 0; t < 6; t++) await p.locator('.trk').nth(t).locator('.m').tap();
  await p.waitForTimeout(300);
  chk(await stummMuster(p) === '00000000', 'das Aufheben ging verloren');
  chk(!(await p.locator('.trk').nth(2).evaluate(e => e.classList.contains('sel'))),
      'der Mute-Knopf waehlt die Spur mit');

  grp('Stumm heisst still - gemessen am Audio-Kern');
  for (let t = 0; t < 8; t++) { await p.locator('.trk').nth(t).locator('.m').tap(); await p.waitForTimeout(60); }
  await p.locator('#play').click();                 // vor dem Start stummgeschaltet
  await p.waitForTimeout(2000);
  chk(await p.locator('#klang').evaluate(e => e.classList.contains('on')), 'der Klang-Kern meldet sich nicht');
  chk(await hoechsterPegel(p, 1500) === 0, 'vor dem Start gesetzte Mutes erreichen den Klang nicht');
  for (let t = 0; t < 8; t++) { await p.locator('.trk').nth(t).locator('.m').tap(); await p.waitForTimeout(60); }
  await p.waitForTimeout(500);
  chk(await hoechsterPegel(p, 2000) > 5, 'nach dem Aufheben kommt kein Klang');

  grp('Steps und Zeiger');
  const anVorher = await p.locator('.pad.on').count();
  await p.locator('.pad').nth(1).tap();
  await p.waitForTimeout(200);
  chk(await p.locator('.pad.on').count() === anVorher + 1, 'ein Step laesst sich nicht setzen');
  await p.locator('.pad').nth(1).tap();
  await p.waitForTimeout(200);
  chk(await p.locator('.pad.on').count() === anVorher, 'ein Step laesst sich nicht loeschen');
  chk(await p.locator('.pad.now').count() === 1, 'der Zeiger laeuft nicht mit');

  grp('Pattern wechseln im Takt');
  // Der Takt beginnt mit dem Start neu - sonst kann der Wechsel schneller kommen als die
  // Pruefung, und der Test misst nur, wie lange er selbst gebraucht hat.
  await p.locator('#play').click();                 // anhalten
  await p.waitForTimeout(300);
  await p.locator('#play').click();                 // neu starten: der Takt faengt vorn an
  await p.waitForTimeout(200);
  chk((await p.locator('.pt.cur').textContent()).trim() === '1', 'es startet nicht auf Pattern 1');
  await p.locator('.pt').nth(1).tap();
  await p.waitForTimeout(150);
  chk((await p.locator('.pt.cur').textContent()).trim() === '1', 'der Wechsel gilt sofort statt im Takt');
  chk(await p.locator('.pt.next').count() === 1, 'das wartende Pattern wird nicht angezeigt');
  await p.waitForTimeout(2600);                     // ein Takt bei 124 bpm sind 1,94 s
  chk((await p.locator('.pt.cur').textContent()).trim() === '2', 'es wurde nicht gewechselt');
  chk(await p.locator('.pt.next').count() === 0, 'es wartet noch etwas');

  grp('Auswahllisten statt Zahlen');
  const eng = p.locator('.row', {hasText: 'trk.machine'});
  await eng.scrollIntoViewIfNeeded();
  chk(['POLY', 'FM', 'DRUM', 'SMPL'].includes((await eng.locator('.v').textContent()).trim()),
      'ENGINE zeigt keinen Namen');
  await eng.tap();
  await p.waitForTimeout(300);
  const engListe = (await p.locator('#pickList button').allTextContents()).map(t => t.trim());
  chk(engListe.join(',') === 'POLY,FM,DRUM,SMPL', 'die Engine-Liste stimmt nicht');
  await p.locator('#pickList button', {hasText: 'FM'}).tap();
  await p.waitForTimeout(300);
  const a = p.locator('.row', {hasText: 'trk.src.a'});
  chk((await a.locator('.nm').textContent()).includes('RATIO'), 'der Regler heisst nicht wie die Machine');
  chk((await a.locator('.v').textContent()).includes(':'), 'FM zeigt kein Verhaeltnis');

  grp('Samples auswaehlen und laden');
  {
    const feld = p.locator('#smpFile');
    // Auf dem iPhone laesst sich ein mit display:none verstecktes Feld nicht oeffnen,
    // und "audio/*" allein graut .wav-Dateien aus.
    chk(await feld.evaluate(e => getComputedStyle(e).display) !== 'none',
        'das Dateifeld ist mit display:none versteckt');
    const accept = await feld.getAttribute('accept');
    chk(accept.includes('.wav'), 'der Auswaehler nennt .wav nicht ausdruecklich');
    chk(accept.includes('audio/wav'), 'der Auswaehler nennt audio/wav nicht');

    const datei = testWav(path.join(os.tmpdir(), 'sc-test-stereo.wav'));
    await p.locator('#smpBtn').tap();
    await p.waitForTimeout(300);
    await feld.setInputFiles(datei);
    await p.waitForTimeout(1500);
    const eintrag = (await p.locator('#smpList button').first().textContent()).trim();
    chk(eintrag.includes('sc-test-stereo'), 'das Sample steht nicht im Vorrat');
    chk(eintrag.includes('0.50 s'), 'die Laenge stimmt nicht: ' + eintrag);
    chk(eintrag.includes('ST'), 'die Datei wurde nicht als stereo gelesen');
    chk(eintrag.includes('\u25cf'), 'der Klang-Kern hat das Sample nicht bestaetigt');

    await p.locator('#smpUse').tap();
    await p.waitForTimeout(400);
    const eng2 = p.locator('.row', {hasText: 'trk.machine'});
    await eng2.scrollIntoViewIfNeeded();
    chk((await eng2.locator('.v').textContent()).trim() === 'SMPL', 'die Spur spielt nicht SMPL');
    const sr = p.locator('.row', {hasText: 'trk.smp'});
    await sr.scrollIntoViewIfNeeded();
    chk((await sr.locator('.v').textContent()).includes('sc-test-stereo'),
        'der Sample-Platz zeigt den Namen nicht');
  }

  grp('Keine Fehler in der Konsole');
  chk(fehler.length === 0, 'Fehler: ' + fehler.join(' | '));

  await b.close();
  console.log(fails ? `\nFEHLGESCHLAGEN: ${fails} von ${checks} Pruefungen`
                    : `\nOK: ${checks} Pruefungen ok, 0 Fehler`);
  process.exit(fails ? 1 : 0);
})();
