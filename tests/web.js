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
  chk(str(x.scParamText(idxOf('trk.vol'), 100)).includes('dB'), 'VOL nicht in dB');
  chk(str(x.scParamText(idxOf('trk.note'), 69)) === 'A4', 'Note 69 ist nicht A4');
  chk(str(x.scParamText(idxOf('trk.flt.freq'), 127)).includes('kHz'), 'FREQ nicht in kHz');
  chk(str(x.scParamText(idxOf('trk.amp.dec'), 71)).includes('ms'), 'DEC nicht in ms');

  grp('Die eigene Mathematik rechnet richtig');
  // pTime(71) soll rund 0,17 s sein, pFreq(127) rund 18 kHz - wie im nativen Bau.
  const ms = parseFloat(str(x.scParamText(idxOf('trk.amp.dec'), 71)));
  chk(ms > 150 && ms < 200, 'DEC bei 71 ist nicht rund 170 ms (' + ms + ')');
  const khz = parseFloat(str(x.scParamText(idxOf('trk.flt.freq'), 127)));
  chk(khz > 17 && khz < 19, 'FREQ bei 127 ist nicht rund 18 kHz (' + khz + ')');
  chk(str(x.scParamText(idxOf('trk.vol'), 100)).startsWith('0.0'), 'VOL 100 ist nicht 0 dB');
  // Das eigene snprintf muss dasselbe schreiben wie das der Linux-Bibliothek - Vorzeichen eingeschlossen.
  chk(str(x.scParamText(idxOf('trk.micro'), 24)) === '+0.00 Steps', 'MTIM zeigt kein Pluszeichen');
  chk(str(x.scParamText(idxOf('trk.micro'), 12)) === '-0.50 Steps', 'MTIM nach vorn falsch');
  chk(str(x.scParamText(idxOf('trk.len'), 6)) === '0.50 Steps', 'LEN halber Step falsch');

  grp('Auswahlwerte heissen wie ihre Bedeutung, nicht wie ihr Index');
  chk(str(x.scParamText(idxOf('trk.machine'), 0)) === 'POLY', 'ENGINE zeigt keinen Namen');
  chk(str(x.scParamText(idxOf('trk.flt.type'), 0)) === 'TIEFPASS', 'Filtertyp zeigt keinen Namen');
  chk(str(x.scParamText(idxOf('trk.cond'), 0)) === '--', 'COND 0 ist nicht leer');
  chk(x.scEnumCount(idxOf('trk.cond')) > 50, 'zu wenige Bedingungen zur Auswahl');
  chk(x.scEnumCount(idxOf('trk.machine')) === 1, 'Machine-Liste falsch lang');
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

  grp('Zeigerstand');
  x.scStart();
  render(6000);
  const s1 = x.scPlayStep(0);
  render(6000);
  chk(s1 >= 0 && x.scPlayStep(0) !== s1, 'der Zeiger bewegt sich nicht');

  console.log(fails ? `\nFEHLGESCHLAGEN: ${fails} von ${checks} Pruefungen`
                    : `\nOK: ${checks} Pruefungen ok, 0 Fehler`);
  process.exit(fails ? 1 : 0);
})();
