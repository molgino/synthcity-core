// Der Kern laeuft im Audio-Thread. Die Oberflaeche schickt nur Befehle herueber - genau die
// Thread-Grenze aus dem Konzept. Der Bau ist freistehend, er braucht keine Einfuhren.
class SynthCityNode extends AudioWorkletProcessor {
  constructor(opts) {
    super();
    this.ready = false;
    this.lastPost = 0;
    this.pending = [];        // Befehle, die vor dem Fertigwerden ankommen, gehen nicht verloren
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
    if (this.x[m.fn]) this.x[m.fn](...(m.args || []));
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
    for (let i = 0; i < n; i++) { L[i] = f[i * 2]; R[i] = f[i * 2 + 1]; }
    if (currentTime - this.lastPost > 0.04) {     // Zeigerstand hoechstens 25-mal je Sekunde
      this.lastPost = currentTime;
      const steps = [];
      for (let t = 0; t < 8; t++) steps.push(this.x.scPlayStep(t));
      this.port.postMessage({type: 'pos', steps});
    }
    return true;
  }
}
registerProcessor('synthcity', SynthCityNode);
