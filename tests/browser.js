// Prueft die Oberflaeche in einem echten Browser: laden, tippen, hoeren.
// Gemessen wird am ERGEBNIS - am Pegel des Audio-Kerns, nicht an der Anzeige.
// Start:  node tests/browser.js [http://localhost:8787/index.html]
const {chromium, devices} = require('playwright');
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

// Die Regler stehen auf Reitern: erst den Reiter der gesuchten Zeile waehlen.
async function R(p, id) {
  await p.evaluate(id => {
    const r = [...document.querySelectorAll('.row')].find(r => r.querySelector('small').textContent === id);
    if (r && r.hidden) document.querySelector('.ptab[data-g="' + r.dataset.g + '"]').click();
  }, id);
  return p.locator('.row', {hasText: id});
}

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
  // Pattern-Auswahl: zugeklappt, der Knopf sagt, was laeuft; aufgeklappt 16 Patterns
  chk(!(await p.locator('#pats').isVisible()), 'die Pattern-Auswahl ist nicht zugeklappt');
  chk((await p.locator('#patToggle').textContent()).startsWith('P1'), 'der Knopf nennt das Pattern nicht');
  await p.locator('#patToggle').click(); await p.waitForTimeout(150);
  chk(await p.locator('#pats').isVisible(), 'die Pattern-Auswahl klappt nicht auf');
  // SAMPLE nur bei einer Engine, die Samples spielt
  const smpSichtbar = async () => p.evaluate(() => !document.querySelector('.row[data-id="trk.smp"]').hidden);
  await p.locator('.ptab', {hasText: 'QUELLE'}).click(); await p.waitForTimeout(100);
  const mach = (await p.locator('.row[data-id="trk.machine"] .v').textContent()).trim();
  chk((await smpSichtbar()) === (mach === 'SMPL'), 'SAMPLE ist bei ' + mach + ' falsch ein- oder ausgeblendet');
  // Reiter: es ist immer nur eine Gruppe zu sehen
  chk(await p.locator('.ptab').count() === 7, 'nicht sieben Reiter: ' + await p.locator('.ptab').count());
  // FX: Echo, Hall und Summe sind Projektwerte und lassen sich verstellen
  await p.locator('.ptab', {hasText: 'FX'}).click(); await p.waitForTimeout(100);
  const fxIds = await p.evaluate(() => [...document.querySelectorAll('.row')].filter(r => !r.hidden).map(r => r.dataset.id));
  chk(['fx.a.div', 'fx.a.fb', 'fx.b.size', 'fx.b.dec', 'master.vol'].every(i => fxIds.includes(i)), 'der FX-Reiter zeigt nicht alle Effekte: ' + fxIds.join(','));
  const hz = p.locator('.row[data-id="fx.b.dec"]');
  const hzVor = (await hz.locator('.v').textContent()).trim();
  await hz.click(); await p.waitForTimeout(200);
  await p.locator('#fineBtns button').nth(3).click(); await p.waitForTimeout(100);       // +10
  await p.locator('#fineClose').click(); await p.waitForTimeout(150);
  chk((await hz.locator('.v').textContent()).trim() !== hzVor, 'die Hallzeit laesst sich nicht verstellen');
  await p.locator('#docBtn').click(); await p.waitForTimeout(150);
  chk(/^fx\.b\.dec = 102$/m.test(await p.locator('#docText').inputValue()), 'die Hallzeit steht nicht im Dokument');
  await p.locator('#docClose').click();
  await p.locator('.row[data-id="fx.a.div"]').click(); await p.waitForTimeout(200);
  chk(await p.locator('#pick').isVisible() && await p.locator('#pickList button').count() === 9, 'ECHO hat keine Auswahlliste der Notenwerte');
  await p.locator('#pickClose').click(); await p.waitForTimeout(100);
  await p.locator('.ptab', {hasText: 'FILTER'}).click(); await p.waitForTimeout(100);
  const sichtbar = await p.evaluate(() => [...document.querySelectorAll('.row')].filter(r => !r.hidden).map(r => r.querySelector('small').textContent));
  chk(sichtbar.length > 0 && sichtbar.every(id => id.startsWith('trk.flt.')), 'der Reiter FILTER zeigt nicht nur Filter: ' + sichtbar.join(','));
  chk(await p.locator('.ptab', {hasText: 'FILTER'}).evaluate(e => e.classList.contains('cur')), 'der gewaehlte Reiter ist nicht markiert');
  chk((await p.evaluate(() => [...document.querySelectorAll('.row')].filter(r => !r.hidden && r.dataset.g === 'Quelle').length)) === 0, 'QUELLE ist trotzdem zu sehen');
  // SAMPLE steht bei QUELLE, nicht als eigene Gruppe
  chk((await p.evaluate(() => document.querySelector('.row[data-g]') && [...document.querySelectorAll('.row')].find(r => r.querySelector('small').textContent === 'trk.smp').dataset.g)) === 'Quelle', 'SAMPLE steht nicht bei QUELLE');
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
  const eng = (await R(p, 'trk.machine'));
  await eng.scrollIntoViewIfNeeded();
  chk(['POLY', 'FM', 'DRUM', 'SMPL'].includes((await eng.locator('.v').textContent()).trim()),
      'ENGINE zeigt keinen Namen');
  await eng.tap();
  await p.waitForTimeout(300);
  const engListe = (await p.locator('#pickList button').allTextContents()).map(t => t.trim());
  chk(engListe.join(',') === 'POLY,FM,DRUM,SMPL', 'die Engine-Liste stimmt nicht');
  await p.locator('#pickList button', {hasText: 'FM'}).tap();
  await p.waitForTimeout(300);
  const a = (await R(p, 'trk.src.a'));
  chk((await a.locator('.nm').textContent()).includes('RATIO'), 'der Regler heisst nicht wie die Machine');
  chk((await a.locator('.v').textContent()).includes(':'), 'FM zeigt kein Verhaeltnis');
  chk(await p.evaluate(() => document.querySelector('.row[data-id="trk.smp"]').hidden), 'bei FM steht SAMPLE noch da');
  await (await R(p, 'trk.machine')).tap(); await p.waitForTimeout(300);
  await p.locator('#pickList button', {hasText: 'SMPL'}).tap(); await p.waitForTimeout(300);
  chk(await p.evaluate(() => !document.querySelector('.row[data-id="trk.smp"]').hidden), 'bei SMPL fehlt SAMPLE');
  await (await R(p, 'trk.machine')).tap(); await p.waitForTimeout(300);
  await p.locator('#pickList button', {hasText: 'FM'}).tap(); await p.waitForTimeout(300);

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
    const eng2 = (await R(p, 'trk.machine'));
    await eng2.scrollIntoViewIfNeeded();
    chk((await eng2.locator('.v').textContent()).trim() === 'SMPL', 'die Spur spielt nicht SMPL');
    const sr = (await R(p, 'trk.smp'));
    await sr.scrollIntoViewIfNeeded();
    chk((await sr.locator('.v').textContent()).includes('sc-test-stereo'),
        'der Sample-Platz zeigt den Namen nicht');
  }

  grp('Sample-Editor: Wellenform, Ausschnitt, Schleife');
  {
    // Der Vorrat hat das Stereo-Sample aus der Gruppe davor in Platz 1.
    // Vorher anhalten - das Vorhoeren soll bei stehender Uhr klingen.
    if ((await p.locator('#play').textContent()).trim() === 'STOP') await p.locator('#play').click();
    await p.waitForTimeout(300);
    await p.locator('#smpBtn').tap(); await p.waitForTimeout(250);
    await p.locator('#smpList button').first().tap(); await p.waitForTimeout(150);
    await p.locator('#smpEditBtn').tap(); await p.waitForTimeout(400);
    chk(await p.locator('#smpEdit').isVisible(), 'der Editor oeffnet nicht');
    // Ist eine Wellenform gezeichnet? Pixel in der Mitte des Bildes zaehlen, die nicht Hintergrund sind.
    const gezeichnet = await p.locator('#seWave').evaluate(c => {
      const g = c.getContext('2d'), d = g.getImageData(0, 0, c.width, c.height).data;
      const bg = [d[0], d[1], d[2]]; let n = 0;
      for (let i = 0; i < d.length; i += 4) if (Math.abs(d[i] - bg[0]) + Math.abs(d[i+1] - bg[1]) + Math.abs(d[i+2] - bg[2]) > 40) n++;
      return n / (c.width * c.height);
    });
    chk(gezeichnet > 0.05, 'keine Wellenform zu sehen');
    const info0 = (await p.locator('#seInfo').textContent()).trim();
    chk(info0.includes('Start 0.000 s') && info0.includes('Ende 0.500 s'), 'der Ausschnitt steht nicht in der Anzeige: ' + info0);
    for (let i = 0; i < 1; i++) await p.locator('#smpEdit .finebtns button[data-d="100"]').tap();
    await p.waitForTimeout(150);
    chk((await p.locator('#seInfo').textContent()).includes('Start 0.100 s'), 'START laesst sich nicht verschieben');
    await p.locator('.semark button[data-m=b]').tap();
    await p.locator('#smpEdit .finebtns button[data-d="-100"]').tap();
    await p.waitForTimeout(150);
    chk((await p.locator('#seInfo').textContent()).includes('Ende 0.400 s'), 'ENDE laesst sich nicht verschieben');
    await p.locator('#seLoop').tap(); await p.waitForTimeout(150);
    chk((await p.locator('#seInfo').textContent()).includes('Loop ab'), 'LOOP laesst sich nicht einschalten');
    // Ziehen im Bild setzt den gewaehlten Punkt (Nulldurchgang an: er rastet ein)
    await p.locator('.semark button[data-m=a]').tap();
    const box = await p.locator('#seWave').boundingBox();
    await p.locator('#seWave').dispatchEvent('pointerdown', {clientX: box.x + box.width * 0.5, clientY: box.y + 40, pointerId: 7});
    await p.locator('#seWave').dispatchEvent('pointerup', {pointerId: 7});
    await p.waitForTimeout(150);
    const startNeu = (await p.locator('#seInfo').textContent()).match(/Start ([\d.]+) s/);
    chk(startNeu && Math.abs(+startNeu[1] - 0.25) < 0.01, 'ein Tipp ins Bild setzt START nicht dorthin');
    // Vorhoeren bei stehender Uhr: der Pegel bewegt sich
    await p.locator('#sePlay').tap();
    chk(await hoechsterPegel(p, 800) > 3, 'Vorhoeren klingt nicht');
    await p.locator('#seClose').tap(); await p.waitForTimeout(200);
    await p.locator('#smpClose').tap(); await p.waitForTimeout(150);
    // Der Ausschnitt steht im Dokument
    await p.locator('#docBtn').tap(); await p.waitForTimeout(200);
    const doc = await p.locator('#docText').inputValue();
    chk(/smp\.0\.a = \d+/.test(doc) && /smp\.0\.b = 19200/.test(doc) && /smp\.0\.loop = 1/.test(doc),
        'der Ausschnitt steht nicht im Dokument');
    await p.locator('#docClose').tap();
  }

  grp('Genaue Werte: antippen statt ziehen');
  {
    // Spur 1 waehlen, NOTE antippen: das Feinwert-Fenster oeffnet sich
    await p.locator('.trk').nth(0).locator('.nm').tap(); await p.waitForTimeout(200);
    const note = (await R(p, 'trk.note'));
    await note.scrollIntoViewIfNeeded();
    await note.tap(); await p.waitForTimeout(250);
    chk(await p.locator('#fine').isVisible(), 'antippen oeffnet kein Feinwert-Fenster');
    const knoepfe = (await p.locator('#fineBtns button').allTextContents()).map(t => t.trim());
    chk(knoepfe.join(' ') === '\u2212OKT \u22121 +1 +OKT', 'NOTE hat keine Halbton- und Oktavschritte: ' + knoepfe.join(' '));
    const vorher = (await p.locator('#fineVal').textContent()).trim();
    await p.locator('#fineBtns button').nth(2).tap(); await p.waitForTimeout(150);      // +1
    const halbton = (await p.locator('#fineVal').textContent()).trim();
    await p.locator('#fineBtns button').nth(0).tap(); await p.waitForTimeout(150);      // -OKT
    const okt = (await p.locator('#fineVal').textContent()).trim();
    const name = n => { const m = n.match(/^([A-G]#?)(-?\d+)$/); return m ? [m[1], +m[2]] : null; };
    const reihe = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
    const num = n => { const x = name(n); return x ? (x[1] + 1) * 12 + reihe.indexOf(x[0]) : NaN; };
    chk(num(halbton) - num(vorher) === 1, '+1 ist kein Halbton (' + vorher + ' -> ' + halbton + ')');
    chk(num(halbton) - num(okt) === 12, '-OKT ist keine Oktave (' + halbton + ' -> ' + okt + ')');
    await p.locator('#fineReset').tap(); await p.waitForTimeout(150);
    chk((await p.locator('#fineVal').textContent()).trim() === 'C4', 'GRUNDWERT setzt nicht auf C4 zurueck');
    await p.locator('#fineClose').tap(); await p.waitForTimeout(150);
    chk((await note.locator('.v').textContent()).trim() === 'C4', 'die Zeile zeigt den neuen Wert nicht');
    // Ziehen bleibt fuer grobe Wege: ein Tipp darf nichts verstellen
    const vol = (await R(p, 'trk.vol'));
    await vol.scrollIntoViewIfNeeded();
    const v0 = (await vol.locator('.v').textContent()).trim();
    await vol.tap(); await p.waitForTimeout(200);
    chk((await p.locator('#fineVal').textContent()).trim() === v0, 'das Oeffnen verstellt den Wert');
    await p.locator('#fineClose').tap();
  }

  grp('Scenes bearbeiten');
  {
    if ((await p.locator('#play').textContent()).trim() === 'STOP') await p.locator('#play').click();
    await p.locator('.pt').nth(0).tap(); await p.waitForTimeout(150);
    await p.locator('.trk').nth(3).locator('.nm').tap(); await p.waitForTimeout(150);   // Spur 4 (Bass)
    await p.locator('#scnB').tap(); await p.waitForTimeout(200);
    chk(await p.locator('#scenebar').isVisible(), 'das Scene-Band erscheint nicht');
    chk(+(await p.locator('#morph').inputValue()) === 100, 'der Morph faehrt nicht auf Scene B');
    const fr = (await R(p, 'trk.flt.reso'));
    await fr.scrollIntoViewIfNeeded();
    const grund = (await fr.locator('.v').textContent()).trim();
    await fr.tap(); await p.waitForTimeout(200);
    for (let i = 0; i < 3; i++) await p.locator('#fineBtns button').nth(3).tap();         // +10 dreimal
    await p.locator('#fineClose').tap(); await p.waitForTimeout(200);
    chk(await fr.evaluate(e => e.classList.contains('inscene')), 'der Scene-Wert ist nicht als solcher zu sehen');
    const sceneWert = (await fr.locator('.v').textContent()).trim();
    chk(sceneWert !== grund, 'der Scene-Wert wurde nicht gesetzt');
    // Ausserhalb des Scene-Modus steht der Grundwert unveraendert da
    await p.locator('#sceneDone').tap(); await p.waitForTimeout(200);
    chk((await fr.locator('.v').textContent()).trim() === grund, 'die Scene hat den Grundwert ueberschrieben');
    chk(await p.locator('#scnB').evaluate(e => e.classList.contains('has')), 'B zeigt nicht, dass es etwas enthaelt');
    // Im Dokument
    await p.locator('#docBtn').tap(); await p.waitForTimeout(200);
    chk(/pat\.0\.sc\.1\.trk\.3\.flt\.reso = \d+/.test(await p.locator('#docText').inputValue()),
        'die Scene steht nicht im Dokument');
    await p.locator('#docClose').tap();
    // Lock-Modus und Scene-Modus schliessen einander aus
    await p.locator('#scnA').tap(); await p.waitForTimeout(150);
    await p.locator('.pad').first().dispatchEvent('pointerdown'); await p.waitForTimeout(450);
    await p.locator('.pad').first().dispatchEvent('pointerup'); await p.waitForTimeout(150);
    chk(await p.locator('#lockbar').isVisible() && !(await p.locator('#scenebar').isVisible()),
        'Lock- und Scene-Modus sind gleichzeitig offen');
    await p.locator('#lockDone').tap();
    // Scene leeren braucht zwei Tipps
    await p.locator('#scnB').tap(); await p.waitForTimeout(150);
    await p.locator('#sceneClear').tap(); await p.waitForTimeout(100);
    chk(await p.locator('#scnB').evaluate(e => e.classList.contains('has')), 'ein einziger Tipp leert die Scene');
    await p.locator('#sceneClear').tap(); await p.waitForTimeout(200);
    chk(!(await p.locator('#scnB').evaluate(e => e.classList.contains('has'))), 'zwei Tipps leeren die Scene nicht');
    await p.locator('#sceneDone').tap();
  }

  grp('Skala: Geschwindigkeit, Laenge, Seiten, Tonart');
  {
    if ((await p.locator('#play').textContent()).trim() === 'STOP') await p.locator('#play').click();
    await p.locator('.pt').nth(0).tap(); await p.waitForTimeout(150);
    await p.locator('.trk').nth(0).locator('.nm').tap(); await p.waitForTimeout(150);
    await p.locator('#padTitle').tap(); await p.waitForTimeout(250);
    chk(await p.locator('#skala').isVisible(), 'ein Tipp auf den Titel oeffnet die Skala nicht');
    const chip = (titel, text) => p.locator('#skBody h4', {hasText: titel}).locator('xpath=following-sibling::div[1]').locator('button', {hasText: text}).first();
    // Swing: eigener des Patterns, sonst der des Projekts
    await chip('SWING', '67%').tap(); await p.waitForTimeout(150);
    chk(await chip('SWING', '67%').evaluate(e => e.classList.contains('cur')), 'Swing 67 % ist nicht gewaehlt');
    chk((await p.locator('#skBody h4', {hasText: 'SWING'}).textContent()).includes('EIGENER'), 'der Swing gilt nicht als eigener des Patterns');
    await p.locator('#skBody h4', {hasText: 'SWING'}).locator('xpath=following-sibling::div[2]').locator('button', {hasText: '+1'}).tap();
    await p.waitForTimeout(150);
    chk((await p.locator('#skBody h4', {hasText: 'SWING'}).locator('xpath=following-sibling::div[2]').textContent()).includes('68 %'),
        'der Swing laesst sich nicht fein verstellen');
    await chip('SWING', 'PROJEKT').tap(); await p.waitForTimeout(150);
    chk((await p.locator('#skBody h4', {hasText: 'SWING'}).textContent()).includes('VOM PROJEKT'), 'PROJEKT nimmt den eigenen Swing nicht zurueck');
    await chip('SWING', '58%').tap(); await p.waitForTimeout(150);
    await chip('GESCHWINDIGKEIT', '2x').tap(); await p.waitForTimeout(150);
    chk(await chip('GESCHWINDIGKEIT', '2x').evaluate(e => e.classList.contains('cur')), '2x ist nicht gewaehlt');
    await chip('GESCHWINDIGKEIT', '1x').tap();
    await chip('TONLEITER', 'DUR').tap(); await p.waitForTimeout(150);
    // Spur 1 auf 32 Steps: +16
    const lang = p.locator('#skBody h4', {hasText: 'LAENGE'}).locator('xpath=following-sibling::div[1]').locator('button', {hasText: '+16'});
    await lang.tap(); await p.waitForTimeout(150);
    await chip('TEILER', '1/8').tap(); await p.waitForTimeout(150);
    chk(await chip('TEILER', '1/8').evaluate(e => e.classList.contains('cur')), 'der Teiler ist nicht gewaehlt');
    await p.locator('#skClose').tap(); await p.waitForTimeout(250);
    chk((await p.locator('#padTitle').textContent()).includes('32 Steps'), 'die Spur ist nicht 32 Steps lang');
    chk((await p.locator('#padTitle').textContent()).toLowerCase().includes('c dur'), 'der Titel nennt die Tonart nicht');
    chk(await p.locator('#pages').isVisible() && await p.locator('#pages button').count() === 2, 'keine zwei Seiten');
    await p.locator('#pages button').nth(1).tap(); await p.waitForTimeout(150);
    chk((await p.locator('.pad').first().textContent()).trim().startsWith('17'), 'Seite 2 zeigt nicht Step 17');
    await p.locator('#pages button').nth(0).tap();
    // NOTE in Tonleiterstufen: von C4 eine Stufe hoch ist D4
    const note = (await R(p, 'trk.note'));
    await note.scrollIntoViewIfNeeded(); await note.tap(); await p.waitForTimeout(200);
    await p.locator('#fineReset').tap(); await p.waitForTimeout(100);              // C4
    chk((await p.locator('#fineBtns button').nth(2).textContent()).trim() === '+STUFE', 'in einer Tonart heisst die Taste nicht +STUFE');
    await p.locator('#fineBtns button').nth(2).tap(); await p.waitForTimeout(150);
    chk((await p.locator('#fineVal').textContent()).trim() === 'D4', 'eine Stufe ueber C4 ist nicht D4');
    await p.locator('#fineBtns button').nth(2).tap(); await p.waitForTimeout(150);
    chk((await p.locator('#fineVal').textContent()).trim() === 'E4', 'zwei Stufen ueber C4 ist nicht E4');
    await p.locator('#fineClose').tap(); await p.waitForTimeout(150);
    // Zuruecksetzen fuer die folgenden Gruppen
    await p.locator('#padTitle').tap(); await p.waitForTimeout(200);
    await chip('TONLEITER', 'CHROMATISCH').tap();
    await p.locator('#skBody h4', {hasText: 'LAENGE'}).locator('xpath=following-sibling::div[1]').locator('button', {hasText: '\u221216'}).tap();
    await chip('TEILER', '1/16').tap();
    await p.locator('#skClose').tap(); await p.waitForTimeout(200);
    await p.locator('#docBtn').tap(); await p.waitForTimeout(200);
    chk(/pat\.0\.swing = 58/.test(await p.locator('#docText').inputValue()), 'der Swing des Patterns steht nicht im Dokument');
    await p.locator('#docClose').tap();
    await p.locator('#padTitle').tap(); await p.waitForTimeout(200);
    await chip('SWING', 'PROJEKT').tap();
    await p.locator('#skClose').tap(); await p.waitForTimeout(200);
  }

  grp('Akkorde und Stimmen je Spur');
  {
    if ((await p.locator('#play').textContent()).trim() === 'STOP') await p.locator('#play').click();
    await p.locator('.pt').nth(0).tap(); await p.waitForTimeout(150);
    await p.locator('.trk').nth(0).locator('.nm').tap(); await p.waitForTimeout(150);
    await R(p, 'trk.nt2');
    const nt = i => p.locator('.row', {hasText: 'trk.nt' + i}).locator('.v');
    await p.locator('#chordBtn').scrollIntoViewIfNeeded();
    await p.locator('#chordBtn').tap(); await p.waitForTimeout(200);
    chk(await p.locator('#pick').isVisible(), 'AKKORD WAEHLEN oeffnet keine Liste');
    await p.locator('#pickList button', {hasText: 'MAJ 7'}).tap(); await p.waitForTimeout(200);
    chk((await nt(2).textContent()).includes('+4') && (await nt(3).textContent()).includes('+7')
        && (await nt(4).textContent()).includes('+11'), 'MAJ 7 setzt nicht +4 +7 +11');
    // Lock: Step 1 bekommt einen Moll-Akkord, der Grundwert bleibt
    await p.locator('.pad').first().dispatchEvent('pointerdown'); await p.waitForTimeout(450);
    await p.locator('.pad').first().dispatchEvent('pointerup'); await p.waitForTimeout(150);
    await p.locator('#chordBtn').tap(); await p.waitForTimeout(200);
    chk((await p.locator('#pickTitle').textContent()).includes('LOCK STEP 1'), 'die Akkordliste sagt nicht, dass sie lockt');
    await p.locator('#pickList button', {hasText: 'MOLL'}).first().tap(); await p.waitForTimeout(200);
    chk((await nt(2).textContent()).includes('+3') && (await nt(4).textContent()).includes('AUS'),
        'der Moll-Lock steht nicht im Step');
    await p.locator('#lockDone').tap(); await p.waitForTimeout(150);
    chk((await nt(2).textContent()).includes('+4'), 'der Lock hat den Grundakkord ueberschrieben');
    // Feinwert: eine ausgeschaltete Akkordnote beginnt beim Grundton
    await p.locator('#chordBtn').tap(); await p.waitForTimeout(200);
    await p.locator('#pickList button', {hasText: 'AUS'}).tap(); await p.waitForTimeout(200);
    await (await R(p, 'trk.nt2')).tap(); await p.waitForTimeout(200);
    await p.locator('#fineBtns button').nth(3).tap(); await p.waitForTimeout(150);          // +OKT
    chk((await p.locator('#fineVal').textContent()).includes('+12 Oktave'), 'von AUS aus ist +OKT nicht +12');
    await p.locator('#fineReset').tap(); await p.waitForTimeout(100);
    chk((await p.locator('#fineVal').textContent()).trim() === 'AUS', 'AUS schaltet die Akkordnote nicht ab');
    await p.locator('#fineClose').tap(); await p.waitForTimeout(150);
    // Stimmenmodus als Liste
    const vm = (await R(p, 'trk.vmode'));
    await vm.tap(); await p.waitForTimeout(200);
    await p.locator('#pickList button', {hasText: 'LEGATO'}).tap(); await p.waitForTimeout(200);
    chk((await vm.locator('.v').textContent()).trim() === 'LEGATO', 'der Stimmenmodus laesst sich nicht waehlen');
    await p.locator('#docBtn').tap(); await p.waitForTimeout(200);
    const doc = await p.locator('#docText').inputValue();
    chk(/pat\.0\.trk\.0\.vmode = 2/.test(doc) && /pat\.0\.trk\.0\.st\.0\.nt2 = 28/.test(doc),
        'Stimmenmodus und Akkord-Lock stehen nicht im Dokument');
    await p.locator('#docClose').tap();
    await vm.tap(); await p.waitForTimeout(200);
    await p.locator('#pickList button', {hasText: 'POLY'}).tap(); await p.waitForTimeout(150);
  }

  grp('Tempo je Pattern');
  {
    const bpm = async () => +(await p.locator('#bpm').textContent()).trim();
    if ((await p.locator('#play').textContent()).trim() === 'STOP') await p.locator('#play').click();
    await p.waitForTimeout(200);
    await p.locator('.pt').nth(0).tap(); await p.waitForTimeout(200);
    const projekt = await bpm();
    chk(!(await p.locator('#bpmPat').evaluate(e => e.classList.contains('on'))), 'Pattern 1 hat schon ein eigenes Tempo');
    await p.locator('.pt').nth(1).tap(); await p.waitForTimeout(200);
    await p.locator('#bpmPat').tap(); await p.waitForTimeout(150);        // Pattern 2 bekommt eigenes Tempo
    chk(await p.locator('#bpmPat').evaluate(e => e.classList.contains('on')), 'P schaltet das eigene Tempo nicht ein');
    for (let i = 0; i < 10; i++) await p.locator('#bpmUp').tap();
    await p.waitForTimeout(200);
    chk(await bpm() === projekt + 10, 'das Pattern-Tempo laesst sich nicht verstellen');
    await p.locator('.pt').nth(0).tap(); await p.waitForTimeout(200);
    chk(await bpm() === projekt, 'Pattern 1 hat das Tempo von Pattern 2 uebernommen');
    await p.locator('.pt').nth(1).tap(); await p.waitForTimeout(200);
    chk(await bpm() === projekt + 10, 'Pattern 2 hat sein Tempo vergessen');
    // Feinwert auch fuer das Tempo
    await p.locator('#bpm').tap(); await p.waitForTimeout(200);
    chk((await p.locator('#fineTitle').textContent()).includes('PATTERN 2'), 'das Fenster sagt nicht, wessen Tempo es ist');
    await p.locator('#fineReset').tap(); await p.waitForTimeout(150);      // zurueck zum Projekttempo
    await p.locator('#fineClose').tap(); await p.waitForTimeout(150);
    chk(await bpm() === projekt && !(await p.locator('#bpmPat').evaluate(e => e.classList.contains('on'))),
        'PROJEKTTEMPO nimmt das eigene Tempo nicht zurueck');
  }

  grp('Song: Patterns nacheinander');
  {
    // Anhalten, damit die Wahl sofort gilt; dann die Kette bauen: P1 einmal, P2 einmal.
    if ((await p.locator('#play').textContent()).trim() === 'STOP') await p.locator('#play').click();
    await p.waitForTimeout(300);
    await p.locator('#songBtn').tap(); await p.waitForTimeout(250);
    chk((await p.locator('#songList').textContent()).includes('Noch leer'), 'der leere Song sagt nicht, was zu tun ist');
    chk(await p.locator('#songPlay').isDisabled(), 'ein leerer Song laesst sich einschalten');
    await p.locator('#songClose').tap();
    await p.locator('.pt').nth(0).tap(); await p.waitForTimeout(200);
    await p.locator('#songBtn').tap(); await p.waitForTimeout(200);
    await p.locator('#songAdd').tap();
    await p.locator('#songClose').tap();
    await p.locator('.pt').nth(1).tap(); await p.waitForTimeout(200);
    await p.locator('#songBtn').tap(); await p.waitForTimeout(200);
    await p.locator('#songAdd').tap();
    const zeilen = (await p.locator('.songrow .wo').allTextContents()).map(t => t.replace(/\s+/g, ' ').trim());
    chk(zeilen.join(' | ') === '1. P1 \u00d7 1 | 2. P2 \u00d7 1', 'die Kette stimmt nicht: ' + zeilen.join(' | '));

    await p.locator('#songPlay').tap(); await p.waitForTimeout(200);
    chk((await p.locator('#songPlay').textContent()).trim() === 'SONG AUS', 'der Song laesst sich nicht einschalten');
    await p.locator('#songClose').tap();
    await p.locator('#play').click();                  // der Song beginnt vorn: P1
    await p.waitForTimeout(500);
    const t0 = (await p.locator('.pt.cur').textContent()).trim();
    await p.waitForTimeout(1950);                      // ein Takt bei 124 bpm sind 1,94 s
    const t1 = (await p.locator('.pt.cur').textContent()).trim();
    await p.waitForTimeout(1950);
    const t2 = (await p.locator('.pt.cur').textContent()).trim();
    chk(t0 === '1', 'der Song beginnt nicht mit P1 (' + t0 + ')');
    chk(t1 === '2', 'nach einem Takt kommt nicht P2 (' + t1 + ')');
    chk(t2 === '1', 'am Ende geht es nicht vorn weiter (' + t2 + ')');
    chk(await p.locator('#songBtn').evaluate(e => e.classList.contains('on')), 'der Song-Knopf zeigt nicht, dass er laeuft');

    // Ein Tipp auf ein Pattern verlaesst den Song.
    await p.locator('.pt').nth(2).tap(); await p.waitForTimeout(300);
    chk(!(await p.locator('#songBtn').evaluate(e => e.classList.contains('on'))),
        'ein Tipp auf ein Pattern laesst den Song weiterlaufen');
  }

  grp('Kopieren, Einfuegen, Trigless wird Trig, benutzte Patterns');
  {
    if ((await p.locator('#play').textContent()).trim() === 'STOP') await p.locator('#play').click();
    await p.waitForTimeout(200);
    const halten = async (loc, ms = 600) => { await loc.dispatchEvent('pointerdown'); await p.waitForTimeout(ms); await loc.dispatchEvent('pointerup'); await p.waitForTimeout(200); };
    const ons = async () => p.evaluate(() => [...document.querySelectorAll('.pad')].map(e => e.classList.contains('on') ? 1 : 0).join(''));
    const docText = async () => { await p.locator('#docBtn').tap(); await p.waitForTimeout(150); const t = await p.locator('#docText').inputValue(); await p.locator('#docClose').tap(); return t; };

    // Ein leeres Pattern bekommt einen Trig: danach gilt es als benutzt und steht im Dokument
    await p.locator('.pt').nth(12).tap(); await p.waitForTimeout(250);
    await p.locator('.trk').nth(0).locator('.nm').tap(); await p.waitForTimeout(150);
    chk(!(await p.locator('.pt').nth(12).evaluate(e => e.classList.contains('used'))), 'Pattern 13 ist schon vorher benutzt');
    await p.locator('.pad').nth(0).tap(); await p.waitForTimeout(200);
    chk(await p.locator('.pt').nth(12).evaluate(e => e.classList.contains('used')), 'Pattern 13 zeigt nach einem Trig nicht, dass es benutzt ist');
    chk(/pat\.12\.trk\.0\.st\.0 = 1/.test(await docText()), 'der Trig in Pattern 13 fehlt im Dokument');

    // Trigless Lock, dann Trig drauf: die Locks bleiben
    const pad = p.locator('.pad').nth(2);
    await halten(pad);
    const fr = (await R(p, 'trk.flt.freq'));
    await fr.scrollIntoViewIfNeeded(); await fr.tap(); await p.waitForTimeout(200);
    await p.locator('#fineBtns button').nth(0).tap(); await p.waitForTimeout(100);       // -10
    await p.locator('#fineClose').tap(); await p.waitForTimeout(150);
    await p.locator('#lockDone').tap(); await p.waitForTimeout(150);
    chk(/pat\.12\.trk\.0\.st\.2 = 2/.test(await docText()), 'der trigless Lock ist nicht angelegt');
    await pad.tap(); await p.waitForTimeout(200);
    let d = await docText();
    chk(/pat\.12\.trk\.0\.st\.2 = 1/.test(d), 'der trigless Step wird kein Trig');
    chk(/pat\.12\.trk\.0\.st\.2\.flt\.freq = \d+/.test(d), 'der Trig hat den trigless Lock geloescht');
    await pad.tap(); await p.waitForTimeout(200);                                   // Trig aus: Locks gehen mit
    chk(!/pat\.12\.trk\.0\.st\.2\./.test(await docText()), 'ein ausgeschalteter Trig behaelt seine Locks');

    // Step kopieren und einfuegen (im Lock-Modus ueber ...)
    await halten(p.locator('.pad').nth(0));
    await p.locator('#lockMore').tap(); await p.waitForTimeout(200);
    await p.locator('#pickList button', {hasText: 'kopieren'}).tap(); await p.waitForTimeout(150);
    await p.locator('.pad').nth(5).tap(); await p.waitForTimeout(150);                 // im Lock-Modus: Step 6 waehlen
    await p.locator('#lockMore').tap(); await p.waitForTimeout(200);
    await p.locator('#pickList button', {hasText: 'einf'}).tap(); await p.waitForTimeout(150);
    await p.locator('#lockDone').tap(); await p.waitForTimeout(150);
    chk((await ons()).slice(0, 6) === '100001', 'der Step wurde nicht eingefuegt: ' + await ons());

    // Spur kopieren: Spur 1 -> Spur 5, nur Trigs
    const muster = await ons();
    await halten(p.locator('.trk').nth(0).locator('.nm'));
    chk(await p.locator('#pick').isVisible(), 'langes Druecken auf die Spur oeffnet kein Menue');
    await p.locator('#pickList button', {hasText: 'kopieren'}).tap(); await p.waitForTimeout(150);
    await halten(p.locator('.trk').nth(4).locator('.nm'));
    await p.locator('#pickList button', {hasText: 'Trigs einf'}).tap(); await p.waitForTimeout(150);
    await p.locator('.trk').nth(4).locator('.nm').tap(); await p.waitForTimeout(200);
    chk(await ons() === muster, 'die Trigs von Spur 1 sind nicht in Spur 5');

    // Pattern kopieren: 13 -> 14
    await halten(p.locator('.pt').nth(12));
    await p.locator('#pickList button', {hasText: 'kopieren'}).tap(); await p.waitForTimeout(150);
    await halten(p.locator('.pt').nth(13));
    await p.locator('#pickList button', {hasText: 'einf'}).tap(); await p.waitForTimeout(200);
    chk(await p.locator('.pt').nth(13).evaluate(e => e.classList.contains('used')), 'Pattern 14 ist nach dem Einfuegen nicht benutzt');
    await p.locator('.pt').nth(13).tap(); await p.waitForTimeout(250);
    await p.locator('.trk').nth(4).locator('.nm').tap(); await p.waitForTimeout(200);
    chk(await ons() === muster, 'Pattern 14 hat nicht die Trigs von Pattern 13');
    await p.locator('.trk').nth(0).locator('.nm').tap();
    await p.locator('.pt').nth(0).tap(); await p.waitForTimeout(250);
  }

  grp('Das Projekt ueberlebt ein Neuladen (Update der Seite)');
  {
    if ((await p.locator('#play').textContent()).trim() === 'STOP') await p.locator('#play').click();
    const bpm = async () => +(await p.locator('#bpm').textContent()).trim();
    await p.locator('.pt').nth(0).tap(); await p.waitForTimeout(200);
    const vorher = await bpm();
    for (let i = 0; i < 7; i++) await p.locator('#bpmUp').tap();
    await p.waitForTimeout(2200);                                      // Autosave alle 1,5 s
    const doc = await p.evaluate(() => document.getElementById('docBtn').click() || document.getElementById('docText').value);
    await p.locator('#docClose').tap();
    await p.reload(); await p.waitForTimeout(1800);
    chk(await bpm() === vorher + 7, 'nach dem Neuladen ist das Tempo wieder ' + await bpm() + ' statt ' + (vorher + 7));
    await p.locator('#docBtn').tap(); await p.waitForTimeout(200);
    const sortiert = t => t.split('\n').sort().join('\n');
    chk(sortiert(await p.locator('#docText').inputValue()) === sortiert(doc), 'nach dem Neuladen ist das Projekt nicht dasselbe');
    chk((await p.locator('#saveState').textContent()).includes('geladen'), 'die Anzeige sagt nicht, woher das Projekt kommt');
    // NEU braucht zwei Tipps und bringt die Demo zurueck - auch nach dem naechsten Neuladen
    await p.locator('#docNew').tap(); await p.waitForTimeout(150);
    chk(await bpm() === vorher + 7, 'ein einziger Tipp auf NEU setzt schon zurueck');
    await p.locator('#docNew').tap(); await p.waitForTimeout(2200);
    chk(await bpm() === vorher, 'NEU bringt die Demo nicht zurueck');
    await p.reload(); await p.waitForTimeout(1800);
    chk(await bpm() === vorher, 'nach NEU und Neuladen ist das alte Projekt wieder da');
  }

  grp('Auf echten Handygroessen ist alles zu treffen');
  // Gemeldet am iPhone: M-Knoepfe verdeckt, Seite breiter als der Schirm. Geprueft wird,
  // ob in der Mitte jedes Bedienelements auch wirklich DIESES Element liegt.
  for (const name of ['iPhone SE', 'iPhone 13', 'iPhone 15 Pro Max', 'Pixel 7',
                      'iPhone SE landscape', 'iPhone 13 landscape', 'iPhone 15 Pro Max landscape']) {
    const ctx = await b.newContext({...devices[name]});
    const q = await ctx.newPage();
    await q.goto(url);
    await q.waitForTimeout(1000);
    await q.locator('#patToggle').click(); await q.waitForTimeout(150);   // aufgeklappt: der engere Fall
    const r = await q.evaluate(() => {
      const W = document.documentElement.clientWidth;
      const fehlt = [];
      for (const s of ['.trk .m', '#play', '#fill', '#songBtn', '#smpBtn', '#docBtn',
                       '#bpmUp', '#bpmDn', '.pt', '#patToggle', '#morph', '.pad'])
        for (const el of document.querySelectorAll(s)) {
          const b = el.getBoundingClientRect();
          const t = document.elementFromPoint(b.left + b.width / 2, b.top + b.height / 2);
          if (!(b.width > 0 && (t === el || el.contains(t)))) fehlt.push(s);
        }
      const reihen = document.querySelectorAll('.row');
      let sichtbar = 0;
      for (const row of reihen) { const b = row.getBoundingClientRect(); if (b.top < innerHeight - 60 && b.bottom > 0) sichtbar++; }
      return {W, scroll: document.documentElement.scrollWidth, fehlt: [...new Set(fehlt)], sichtbar};
    });
    chk(r.scroll <= r.W, name + ': die Seite ist breiter als der Schirm (' + r.scroll + ' > ' + r.W + ')');
    chk(r.fehlt.length === 0, name + ': nicht zu treffen: ' + r.fehlt.join(', '));
    chk(r.sichtbar >= 2, name + ': fuer die Regler bleibt kein Platz');
    // Auch jedes Fenster muss auf den Schirm passen, samt allem, was darin ist.
    const dlg = await q.evaluate(() => {
      const W = document.documentElement.clientWidth, out = [];
      for (const d of document.querySelectorAll('dialog')) {
        d.showModal();
        const br = d.getBoundingClientRect();
        if (br.right > W + 1 || br.left < -1) out.push(d.id + ' breiter als der Schirm');
        if (d.scrollWidth > d.clientWidth + 1) out.push(d.id + ' laeuft quer ueber');
        for (const el of d.querySelectorAll('button, canvas, label')) {
          const b = el.getBoundingClientRect();
          if (b.width > 0 && (b.right > br.right + 1 || b.left < br.left - 1)) out.push(d.id + ': ' + (el.id || el.textContent.trim().slice(0, 14)));
        }
        d.close();
      }
      // Keine Escape-Schreibweise als sichtbarer Text
      if (/\\u[0-9a-f]{4}/i.test(document.body.innerText + [...document.querySelectorAll('dialog')].map(d => d.textContent).join(' ')))
        out.push('\\u... steht als Text da');
      return out;
    });
    chk(dlg.length === 0, name + ': ' + dlg.slice(0, 4).join(' | '));
    if (!name.includes('landscape')) { await ctx.close(); continue; }
    chk(await q.locator('.trk').nth(7).locator('.m').isVisible(), name + ': Spur 8 ist nicht zu sehen');
    await ctx.close();
  }

  grp('Keine Fehler in der Konsole');
  chk(fehler.length === 0, 'Fehler: ' + fehler.join(' | '));

  await b.close();
  console.log(fails ? `\nFEHLGESCHLAGEN: ${fails} von ${checks} Pruefungen`
                    : `\nOK: ${checks} Pruefungen ok, 0 Fehler`);
  process.exit(fails ? 1 : 0);
})();
