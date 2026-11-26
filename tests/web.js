// Prueft den WebAssembly-Bau: dieselbe Bruecke, die die Oberflaeche benutzt.
// Der Bau hat eine EIGENE kleine Mathematik (ohne libc), darum wird hier nachgemessen,
// dass Tonhoehe, Textanzeige und Dokument stimmen.
const fs = require('fs');
let fails = 0, checks = 0;
const chk = (ok, what) => { checks++; if (!ok) { fails++; console.log('  FEHLER: ' + what); } };
const grp = n => console.log('- ' + n);

(async () => {
  const path = process.argv[2] || 'web/synthcity.wasm';
  const mod = await WebAssembly.compile(fs.readFileSync(path));
  const inst = await WebAssembly.instantiate(mod, {});
  const x = inst.exports;
  x.__wasm_call_ctors();
  const SR = 48000;
  x.scInit(SR);

  const str = p => {
    const b = new Uint8Array(x.memory.buffer);
    let e = p; while (b[e]) e++;
    return new TextDecoder().decode(b.slice(p, e));
  };
  const slotOf = id => {
    for (let i = 0; i < x.scNParams(); i++) if (str(x.scParamId(i)) === id) return x.scParamSlot(i);
    return -1;
  };
  const idxOf = id => {
    for (let i = 0; i < x.scNParams(); i++) if (str(x.scParamId(i)) === id) return i;
    return -1;
  };

  grp('Parametertabelle kommt heruber');
  chk(x.scNParams() > 20, 'zu wenige Parameter');
  chk(str(x.scParamName(idxOf('trk.flt.freq'))) === 'FREQ', 'Name von trk.flt.freq falsch');
  chk(str(x.scParamText(idxOf('trk.vol'), 100, -1)).includes('dB'), 'VOL nicht in dB');
  chk(str(x.scParamText(idxOf('trk.note'), 69, -1)) === 'A4', 'Note 69 ist nicht A4');
  chk(str(x.scParamText(idxOf('trk.flt.freq'), 127, -1)).includes('kHz'), 'FREQ nicht in kHz');
  chk(str(x.scParamText(idxOf('trk.amp.dec'), 71, -1)).includes('ms'), 'DEC nicht in ms');

  grp('Die eigene Mathematik rechnet richtig');
  // pTime(71) soll rund 0,17 s sein, pFreq(127) rund 18 kHz - wie im nativen Bau.
  const ms = parseFloat(str(x.scParamText(idxOf('trk.amp.dec'), 71, -1)));
  chk(ms > 150 && ms < 200, 'DEC bei 71 ist nicht rund 170 ms (' + ms + ')');
  const khz = parseFloat(str(x.scParamText(idxOf('trk.flt.freq'), 127, -1)));
  chk(khz > 17 && khz < 19, 'FREQ bei 127 ist nicht rund 18 kHz (' + khz + ')');
  chk(str(x.scParamText(idxOf('trk.vol'), 100, -1)).startsWith('0.0'), 'VOL 100 ist nicht 0 dB');
  // Das eigene snprintf muss dasselbe schreiben wie das der Linux-Bibliothek - Vorzeichen eingeschlossen.
  chk(str(x.scParamText(idxOf('trk.micro'), 24, -1)) === '+0.00 Steps', 'MTIM zeigt kein Pluszeichen');
  chk(str(x.scParamText(idxOf('trk.micro'), 12, -1)) === '-0.50 Steps', 'MTIM nach vorn falsch');
  chk(str(x.scParamText(idxOf('trk.len'), 6, -1)) === '0.50 Steps', 'LEN halber Step falsch');

  grp('Auswahlwerte heissen wie ihre Bedeutung, nicht wie ihr Index');
  chk(str(x.scParamText(idxOf('trk.machine'), 0, -1)) === 'POLY', 'ENGINE zeigt keinen Namen');
  chk(str(x.scParamText(idxOf('trk.flt.type'), 0, -1)) === 'LOWPASS', 'Filtertyp zeigt keinen Namen');
  chk(str(x.scParamText(idxOf('trk.cond'), 0, -1)) === '--', 'COND 0 ist nicht leer');
  chk(x.scEnumCount(idxOf('trk.cond')) > 50, 'zu wenige Bedingungen zur Auswahl');
  chk(x.scEnumCount(idxOf('trk.machine')) === 4, 'Machine-Liste falsch lang');
  chk(x.scMachCount() === 4, 'Only one sample machine is selectable');
  chk(str(x.scParamText(idxOf('trk.machine'), 3, -1)) === 'SAMPLE', 'Machine 3 heisst nicht SAMPLE');
  chk(str(x.scParamText(idxOf('trk.machine'), 1, -1)) === 'FM', 'Machine 1 heisst nicht FM');
  chk(str(x.scParamText(idxOf('trk.machine'), 2, -1)) === 'DRUM', 'Machine 2 heisst nicht DRUM');
  // Dieselbe Zahl heisst je Machine etwas anderes - das ist der Sinn des Machine-Prinzips.
  chk(str(x.scParamText(idxOf('trk.src.a'), 24, 1)) === '2.00 : 1', 'FM zeigt kein Verhaeltnis');
  chk(str(x.scParamText(idxOf('trk.src.a'), 64, 0)) === '+0 ct', 'POLY zeigt kein Detune');
  chk(str(x.scParamText(idxOf('trk.src.a'), 64, -1)) === '+0', 'ohne Machine fehlt die nackte Zahl');
  chk(str(x.scParamText(idxOf('trk.src.a'), 64, 2)).includes('semi'), 'DRUM zeigt keinen Sweep');
  chk(str(x.scMachLabel(0, 0)) === 'DETUNE' && str(x.scMachLabel(1, 0)) === 'RATIO A' && str(x.scMachLabel(1, 2)) === 'ALGO' && str(x.scMachLabel(2, 3)) === '' && str(x.scMachLabel(3, 5)) === 'WARP'
      && str(x.scMachLabel(2, 0)) === 'SWEEP', 'Reglernamen je Machine falsch');
  chk(str(x.scParamText(idxOf('fx.a.div'), 4, -1)) === '1/8', 'Echo-Zeit zeigt keinen Notenwert');
  chk(x.scEnumCount(idxOf('fx.a.div')) === 9, 'Echo-Zeiten falsch viele');
  chk(x.scEnumCount(idxOf('trk.vol')) === 0, 'ein stufenloser Regler bekommt eine Liste');
  {
    const ci = idxOf('trk.cond');
    const namen = [];
    for (let i = 0; i < x.scEnumCount(ci); i++) namen.push(str(x.scParamText(ci, x.scEnumValue(ci, i))));
    chk(namen.includes('FILL') && namen.includes('/PRE') && namen.includes('1ST'),
        'feste Bedingungen fehlen in der Liste');
    chk(namen.includes('50 %'), 'Wahrscheinlichkeit fehlt in der Liste');
    chk(namen.includes('1:2') && namen.includes('/1:2') && namen.includes('8:8'),
        'A:B-Bedingungen fehlen in der Liste');
    chk(new Set(namen).size === namen.length, 'ein Eintrag kommt doppelt vor');
  }

  grp('Klang: Tonhoehe stimmt auch im Browser-Bau');
  const N = 24000, buf = x.scAlloc(N * 2 * 4);
  const render = n => {
    let done = 0;
    while (done < n) { const c = Math.min(128, n - done); x.scRender(buf + done * 8, c); done += c; }
    return new Float32Array(x.memory.buffer, buf, n * 2);
  };
  const bin = (f, n, hz) => {              // Goertzel wie in den C++-Tests
    const w = 2 * Math.PI * hz / SR, cw = Math.cos(w), c = 2 * cw;
    let s1 = 0, s2 = 0;
    for (let i = 0; i < n; i++) { const s0 = f[i * 2] + c * s1 - s2; s2 = s1; s1 = s0; }
    const re = s1 - s2 * cw, im = s2 * Math.sin(w);
    return Math.sqrt(re * re + im * im) / n;
  };
  const t0 = slotOf('trk.note');
  x.scStepSet(0, 0, 1);
  x.scSet(0, t0, 69);                      // A4
  x.scSet(0, slotOf('trk.flt.freq'), 127);
  x.scSet(0, slotOf('trk.amp.sus'), 100);
  x.scSet(0, slotOf('trk.amp.dec'), 127);
  x.scSet(0, slotOf('trk.len'), 127);
  x.scStart();
  const f = render(N);
  chk(bin(f, N, 440) > bin(f, N, 523) * 4, '440 Hz ist nicht deutlich staerker als der Nachbarton');
  let peak = 0, sum = 0;
  for (let i = 0; i < N * 2; i++) { peak = Math.max(peak, Math.abs(f[i])); sum += f[i] * f[i]; }
  chk(peak > 0.01 && peak <= 1.0, 'Pegel unplausibel (' + peak.toFixed(3) + ')');
  chk(Math.sqrt(sum / (N * 2)) > 0.01, 'kaum Klang');
  x.scStop();

  grp('Sample-Vorrat');
  {
    const n = 24000;
    const p2 = x.scAlloc(n * 4);
    const f32 = new Float32Array(x.memory.buffer, p2, n);
    for (let i = 0; i < n; i++) f32[i] = Math.sin(2 * Math.PI * 440 * i / 48000);
    const namePtr = x.scAlloc(16);
    new Uint8Array(x.memory.buffer, namePtr, 16).set(new TextEncoder().encode('sinus\0'));
    chk(x.scSmpSet(0, p2, n, 48000, namePtr, 1) === 1, 'Sample laesst sich nicht ablegen');
    chk(x.scSmpFrames(0) === n, 'Laenge stimmt nicht');
    chk(str(x.scSmpName(0)) === 'sinus', 'Name stimmt nicht');
    chk(x.scSmpFrames(5) === 0, 'ein leerer Platz meldet Laenge');
    chk(x.scSmpFree() > 0 && x.scSmpFree() < 1 << 21, 'freier Platz unplausibel');

    x.scSet(0, slotOf('trk.machine'), 3);
    x.scSet(0, slotOf('trk.smp'), 0);
    x.scSet(0, slotOf('trk.note'), 60);
    x.scSet(0, slotOf('trk.flt.freq'), 127);
    x.scStepSet(0, 0, 1);
    x.scStart();
    const f = render(24000);
    chk(bin(f, 24000, 440) > bin(f, 24000, 880) * 6, 'das Sample klingt nicht richtig');
    x.scStop();
    x.scSmpClear();
    chk(x.scSmpFrames(0) === 0, 'der Vorrat laesst sich nicht leeren');
  }

  grp('Dokument: speichern und laden');
  x.scDemo();
  const save = str(x.scSave());
  chk(save.includes('pat.0.used = 1'), 'das Demo-Pattern steht nicht im Dokument');
  chk(save.includes('bpm = 124'), 'das Tempo steht nicht im Dokument');
  const bytes = new TextEncoder().encode(save + '\0');
  const p = x.scAlloc(bytes.length);
  new Uint8Array(x.memory.buffer, p, bytes.length).set(bytes);
  x.scSet(0, t0, 12);                      // etwas kaputtmachen
  x.scLoad(p);
  // Die Reihenfolge der Zeilen darf sich aendern - der Inhalt nicht.
  const sortiert = t => t.trim().split('\n').sort().join('\n');
  chk(sortiert(str(x.scSave())) === sortiert(save), 'nach dem Rundlauf steht etwas anderes da');

  grp('Steps und Locks');
  x.scStepSet(3, 5, 1);
  chk(x.scStepGet(3, 5) === 1, 'Step nicht gesetzt');
  x.scLockSet(3, 5, t0, 72, 0);
  chk(x.scLockGet(3, 5, t0) === 72, 'Lock nicht gesetzt');
  x.scLockClear(3, 5, t0);
  chk(x.scLockGet(3, 5, t0) === -32768, 'Lock nicht entfernt');

  grp('Scenes setzen und entfernen');
  {
    const fs2 = slotOf('trk.flt.freq');
    x.scSceneSet(1, 2, fs2, 99);
    chk(x.scSceneGet(1, 2, fs2) === 99, 'Scene-Wert nicht gesetzt');
    chk(x.scSceneCount(1, 2) === 1, 'Scene-Zaehler falsch');
    x.scSceneClear(1, 2, fs2);
    chk(x.scSceneGet(1, 2, fs2) === -32768, 'Scene-Wert nicht entfernt');
    x.scSceneSet(0, 1, fs2, 10); x.scSceneSet(0, 3, fs2, 20);
    x.scSceneClearAll(0);
    chk(x.scSceneCount(0, 1) === 0 && x.scSceneCount(0, 3) === 0, 'Scene A nicht ganz geleert');
  }

  grp('Zeigerstand');
  x.scStart();
  render(6000);
  const s1 = x.scPlayStep(0);
  render(6000);
  chk(s1 >= 0 && x.scPlayStep(0) !== s1, 'der Zeiger bewegt sich nicht');

  grp('Song als WAV rechnen: genau bis zum Zeilenende, dann der Ausklang');
  {
    x.scStop();
    while (x.scSongN()) x.scSongDel(0);
    x.scSongAdd(0, 1); x.scSongAdd(1, 2);
    x.scSongSetF(0, 3, 16); x.scSongSetF(1, 3, 8);            // Laenge 16 und 8 Steps
    const patVor = x.scPattern();
    const zaehle = (von, bis, tail) => {
      chk(x.scExportBegin(von, bis, tail, 600) === 1, 'der Export beginnt nicht');
      const buf = x.scAlloc(4096 * 2 * 4);
      let n = 0, k, laut = 0;
      while ((k = x.scExportRender(buf, 4096)) > 0) {
        const f = new Float32Array(x.memory.buffer, buf, k * 2);
        for (const v of f) if (Math.abs(v) > laut) laut = Math.abs(v);
        n += k;
      }
      x.scExportEnd();
      return {n, laut};
    };
    const bpm = x.scTempoNow(), sech = SR * 60 / bpm / 4;
    let r = zaehle(0, 0, 1);
    chk(Math.abs(r.n - (Math.round(16 * sech) + SR)) <= 2, 'Zeile 1 + 1 s Ausklang hat nicht die richtige Laenge: ' + r.n);
    chk(r.laut > 0.01, 'der Export ist still');
    r = zaehle(0, 1, 0);
    chk(Math.abs(r.n - Math.round((16 + 16) * sech)) <= 2, 'Zeile 1 bis 2 (16 + 2 x 8 Steps) hat nicht die richtige Laenge: ' + r.n);
    r = zaehle(1, 1, 0);
    chk(Math.abs(r.n - Math.round(16 * sech)) <= 2, 'ab Zeile 2 beginnt der Export nicht dort: ' + r.n);
    chk(x.scPattern() === patVor && !x.scSongOn(), 'nach dem Export ist der Zustand nicht wie vorher');
    // Ein Song aus einer Zeile wiederholt sich selbst - der Export endet trotzdem nach ihr
    x.scSongDel(1);
    r = zaehle(0, 0, 0);
    chk(Math.abs(r.n - Math.round(16 * sech)) <= 2, 'eine Zeile, die sich selbst folgt, wird endlos exportiert: ' + r.n);
    while (x.scSongN()) x.scSongDel(0);
  }

  grp('Grosses Projekt: alle Patterns, Spuren und Steps mit 12 Locks');
  {
    // Frueher war das Dokument bei 48 000 Eintraegen voll; im Browser waechst es jetzt mit.
    const lockSlots = [];
    for (let i = 0; i < x.scNParams() && lockSlots.length < 12; i++)
      if (x.scParamScope(i) === 1 && (x.scParamFlags(i) & 1)) lockSlots.push(x.scParamSlot(i));
    const patterns = x.scNPatterns(), tracks = x.scNTracks(), steps = x.scNSteps();
    for (let pt = 0; pt < patterns; pt++) {
      x.scSetPattern(pt);
      for (let t = 0; t < tracks; t++) {
        x.scSetTrackLen(t, steps);
        for (let s = 0; s < steps; s++) {
          x.scStepSet(t, s, 1);
          lockSlots.forEach((sl, l) => x.scLockSet(t, s, sl, 1 + (s + l + pt) % 100, 1));
        }
      }
    }
    x.scSetPattern(0);
    const p0 = x.scSave();
    chk(p0 !== 0, 'grosses Projekt laesst sich nicht speichern');
    const big = p0 ? str(p0) : '';
    const zeilen = big.split('\n').length;
    console.log(`  ${zeilen} Zeilen, ${(big.length / 1048576).toFixed(1)} MB`);
    chk(zeilen > 400000, 'grosses Projekt unvollstaendig: ' + zeilen + ' Zeilen');
    const b2 = new TextEncoder().encode(big + '\0');
    const q = x.scScratch(b2.length);
    chk(q !== 0, 'kein Speicher fuer das grosse Projekt');
    if (q) {
      new Uint8Array(x.memory.buffer, q, b2.length).set(b2);
      x.scSetPattern(3); x.scLockSet(2, 7, lockSlots[0], 5, 0);          // etwas aendern
      chk(x.scLoad(q) === 1, 'grosses Projekt laesst sich nicht laden');
      const back = str(x.scSave());
      const sortiert = t => t.trim().split('\n').sort().join('\n');
      chk(sortiert(back) === sortiert(big), 'grosses Projekt kommt nicht unveraendert zurueck');
    }
  }

  console.log(fails ? `\nFEHLGESCHLAGEN: ${fails} von ${checks} Pruefungen`
                    : `\nOK: ${checks} Pruefungen ok, 0 Fehler`);
  process.exit(fails ? 1 : 0);
})();
