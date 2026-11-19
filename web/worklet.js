// Der Kern laeuft im Audio-Thread. Die Oberflaeche schickt nur Befehle herueber - genau die
// Thread-Grenze aus dem Konzept. Der Bau ist freistehend, er braucht keine Einfuhren.
class SynthCityNode extends AudioWorkletProcessor {
  constructor(opts) {
    super();
    this.ready = false;
    this.lastPost = 0;
    this.pending = [];        // Befehle, die vor dem Fertigwerden ankommen, gehen nicht verloren
    this.recSlot = -1;
    this.peak = 0;
    WebAssembly.instantiate(opts.processorOptions.bytes, {}).then(res => {
      const inst = res.instance || res;
      this.x = inst.exports;
      this.x.__wasm_call_ctors();
      this.x.scInit(sampleRate);
      // Im Audio-Thread gibt es kein TextEncoder - der Text kommt fertig als Bytes herueber.
      if (opts.processorOptions.doc) this.load(opts.processorOptions.doc);
      this.buf = this.x.scAlloc(256 * 2 * 4);
      this.ready = true;
      for (const m of this.pending) this.apply(m);
      this.pending.length = 0;
      this.port.postMessage({type: 'ready'});
    }).catch(e => this.port.postMessage({type: 'fehler', text: String(e && e.message || e)}));
    this.port.onmessage = e => {
      if (this.ready) this.apply(e.data);
      else this.pending.push(e.data);
    };
  }
  apply(m) {
    if (m.fn === 'scLoad') { this.load(m.doc); return; }
    if (m.fn === 'smp')    { this.sample(m); return; }
    if (m.fn === 'rec')    { this.record(m); return; }
    if (this.x[m.fn]) this.x[m.fn](...(m.args || []));
  }

  // Ein Sample kommt als fertige Fliesskommazahlen herueber - entpackt wird im Hauptfaden.
  sample(m) {
    const p = this.x.scAlloc(m.data.length * 4);
    new Float32Array(this.x.memory.buffer, p, m.data.length).set(m.data);
    const nb = m.name;
    const np = this.x.scAlloc(nb.length + 1);
    const nm = new Uint8Array(this.x.memory.buffer, np, nb.length + 1);
    nm.set(nb); nm[nb.length] = 0;
    const ch = m.ch || 1;
    this.x.scSmpSet(m.slot, p, m.data.length / ch, m.rate, np, m.loop ? 1 : 0, ch);
    // Rueckmeldung: die Oberflaeche zeigt erst dann an, dass der Klang-Kern es wirklich hat.
    this.port.postMessage({type: 'smp', slot: m.slot, n: this.x.scSmpFrames(m.slot)});
  }

  // Aufgenommen wird dort, wo der Klang entsteht - hier. Danach geht das Ergebnis
  // zurueck an die Oberflaeche, die es ins Modell und in ihren Speicher legt.
  record(m) {
    const nb = m.name;
    const np = this.x.scAlloc(nb.length + 1);
    const nm = new Uint8Array(this.x.memory.buffer, np, nb.length + 1);
    nm.set(nb); nm[nb.length] = 0;
    const n = this.x.scRecStart(m.slot, m.seconds, np, m.ch || 2);
    this.recSlot = n > 0 ? m.slot : -1;
    this.port.postMessage({type: 'recStart', slot: m.slot, frames: n});
  }

  load(bytes) {
    const p = this.x.scAlloc(bytes.length + 1);
    const m = new Uint8Array(this.x.memory.buffer, p, bytes.length + 1);
    m.set(bytes);
    m[bytes.length] = 0;
    this.x.scLoad(p);
  }

  process(_in, out) {
    const L = out[0][0], R = out[0][1] || out[0][0];
    const n = L.length;
    if (!this.ready) { L.fill(0); R.fill(0); return true; }
    this.x.scRender(this.buf, n);
    const f = new Float32Array(this.x.memory.buffer, this.buf, n * 2);
    let peak = 0;
    for (let i = 0; i < n; i++) {
      L[i] = f[i * 2]; R[i] = f[i * 2 + 1];
      const a = Math.abs(L[i]) > Math.abs(R[i]) ? Math.abs(L[i]) : Math.abs(R[i]);
      if (a > peak) peak = a;
    }
    // Gemeldet wird das Maximum SEIT DER LETZTEN MELDUNG - ein Spitzenwert, der zwischen
    // zwei Meldungen abfaellt, zeigt sonst fast immer null.
    if (peak > this.peak) this.peak = peak;
    if (this.recSlot >= 0 && !this.x.scRecOn()) {       // die Aufnahme ist gerade fertig
      const slot = this.recSlot;
      this.recSlot = -1;
    this.peak = 0;
      const frames = this.x.scSmpFrames(slot), ch = this.x.scSmpCh(slot);
      const data = new Float32Array(this.x.memory.buffer, this.x.scSmpData(slot),
                                    frames * ch).slice();
      this.port.postMessage({type: 'rec', slot, data, ch, rate: this.x.scSmpRate(slot)});
    }
    if (currentTime - this.lastPost > 0.04) {     // Zeigerstand hoechstens 25-mal je Sekunde
      this.lastPost = currentTime;
      const steps = [];
      for (let t = 0; t < 8; t++) steps.push(this.x.scPlayStep(t));
      this.port.postMessage({type: 'pos', steps, peak: this.peak,
                             pat: this.x.scPattern(), next: this.x.scPatNext()});
      this.peak = 0;
    }
    return true;
  }
}
registerProcessor('synthcity', SynthCityNode);
