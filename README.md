# synthcity-core

Der Kern der nächsten Groovebox: **ein Kern, drei Hüllen** (Gerät, Handy, CLAP-Plugin).
Konzept: `../r36-groovebox/docs/konzept-neu.md`.

Kein Framework, keine Abhängigkeiten, keine STL-Container im Kern — C++17 und fertig.

## Stand: Meilenstein 3 — der Sequencer ist vollständig

| Baustein | Datei | Zustand |
| --- | --- | --- |
| Parameter-Tabelle (stabile IDs, Bereich, Kurve, Flags) | `src/param.h` | steht |
| Dokument (ID → Wert, Textdatei, migrationsfrei) | `src/doc.h` | steht |
| Projekt → Pattern → Track → Step, Locks dünn besetzt | `src/model.h` | steht |
| Scheduler in Samples, Conditions, Micro-Timing, Ratchets | `src/seq.h` | steht |
| Lock-freie Befehls-Queue (Oberfläche → Audio) | `src/queue.h` | steht |
| DSP-Bausteine: PolyBLEP, TPT-Filter, Hüllkurve, Skalen | `src/dsp.h` | steht |
| Machines (austauschbare Klangquelle), erste: `POLY` | `src/machine.h` | steht |
| Stimmen, Filter, Hüllkurve, Panorama, Summe | `src/engine.h` | steht |
| Slides als Rampen, Scenes + Morph, Mute, Step-Loop | `src/engine.h`, `src/model.h` | steht |
| Effekte, Sends, Sampler | — | Meilenstein 4 |

```
make test        # 60 + 25 Prüfungen
make SAN=1 test  # mit Address-/UB-Sanitizer
make bench       # Last: Faktor gegenüber Echtzeit
make demo        # schreibt demo.wav (16 s)
```

## Wie der Klang entsteht

```
Sequencer ──Event (Sample-genau)──▶ Stimme
                                      │
      Machine (POLY: 2 Osc) ──▶ Filter (TPT-SVF) ──▶ Hüllkurve ──▶ Panorama ──▶ Summe ──▶ weiche Begrenzung
```

Drei Entscheidungen, die den Rest tragen:

- **Der Block wird an jeder Ereignis-Stelle geteilt.** Ein Anschlag beginnt auf die Sample genau,
  egal wie der Host die Blöcke schneidet. Der Test dazu rendert dieselbe Sekunde mit 64er und
  977er Blöcken und vergleicht sie *bitgenau*.
- **Die Steuerrate hängt an der Stimme, nicht am Block.** Filterkoeffizienten werden alle 16
  Samples neu gerechnet (`tanf` ist teuer) — gezählt wird ab dem Anschlag der Stimme. Hinge der
  Zähler am Block, klänge dieselbe Note je nach Blockgröße anders. Das brachte gegenüber
  „jedes Sample neu“ rund 65 % mehr Luft, ohne hörbaren Unterschied.
- **Der P-Lock-Schnappschuss wird beim Anschlag genommen.** Die Stimme trägt ihre eigenen Werte
  mit sich; was danach am Regler passiert, ändert sie nicht mehr — genau wie bei der Hardware.

## Die zweite Werte-Ebene

Der wirksame Wert ist immer dieselbe Kette: **Lock → Morph zwischen Scene A und B → Grundwert.**
Eine Scene hält je Spur eine Handvoll abweichender Werte; wo sie nichts sagt, wird gegen den
Grundwert gefahren (wie der Crossfader des Octatrack). Der Morph-Regler wirkt auch auf einen
schon klingenden Ton, nicht erst auf den nächsten Anschlag.

**Slides** machen aus der Stufe zwischen zwei Locks eine Rampe — und zwar ab dem Wert, an dem
die *Spur* gerade steht, nicht ab einer noch klingenden Stimme. Das war der entscheidende Punkt:
die vorige Stimme ist meistens längst verklungen, wenn der nächste Trig kommt. Weil die Tonhöhe
auf der Steuerrate neu gelesen wird, ist ein Pitch-Slide dieselbe Maschinerie wie ein
Filter-Slide — kein Sonderfall.

**Die Performance-Ebene fasst die Daten nie an.** Mute schweigt die Spur, der Sequencer läuft
weiter (ein Test beweist es: eine Nachbarspur, die per `NEI` dem Würfel der stummen Spur folgt,
klingt bitgleich wie ohne Mute). Step-Loop lenkt nur um, welcher Step gelesen wird — die Uhr
läuft dabei ungerührt weiter.

## Was die Tests beweisen

- **Das Dokument trägt**: speichern, laden, Lock mit Slide, trigless Step — und eine ID, die
  diese Version *nicht kennt*, überlebt den Rundlauf unverändert.
- **Ein Projekt lädt, nachdem Parameter dazugekommen sind**: was fehlt, bekommt seinen
  Standardwert; Standardwerte stehen gar nicht erst in der Datei.
- **Die Blockgröße ändert nichts**: dieselben Events bei 1, 64 und 977 Frames je Block.
- **Micro-Timing nach vorn** ist kein Sonderfall mehr — ein Step kann eine halbe Stepdauer
  *früher* klingen, ohne Vorzieh-Logik, und der Versatz im Block wird nie negativ.
- **Wahrscheinlichkeit ist eine Condition**: `PRE` folgt ihr, Trigs ohne Condition sieht sie
  dabei nicht (der Elektron-Trick, mit dem ein Zufall eine ganze Gruppe steuert).
- **Polymetrik**: 16 gegen 12 Steps laufen sauber auseinander.
- **Ratchets** teilen den Step in gleiche Teile.
- Die **Queue** hält die Reihenfolge und läuft nicht über.

## Was die Klang-Tests beweisen

- **Jede Blockgröße liefert dasselbe Signal** — bitgenau, 64 gegen 977 Frames.
- **Die Tonhöhe stimmt**: Note 69 misst 440 Hz, Note 57 eine Oktave tiefer (gemessen per
  Goertzel, nicht an internen Werten). Dieser Test fand sofort einen echten Fehler: die zweite
  Oszillator-Phase stand auf 0,5 und löschte bei gleicher Frequenz die Grundwelle aus.
- **Das Filter nimmt die Höhen weg**, der Grundton bleibt.
- **P-Locks wirken je Step**: der Grundton der Spur verschwindet auf dem gelockten Step.
- **32 Stimmen gleichzeitig** erzeugen keine Aussetzer; der Ausgang bleibt immer in den Grenzen.
- **Slide**: zu Beginn des Steps klingt noch die alte Note, am Ende die neue.
- **Scenes**: A dumpf, B offen, Morph 0,5 messbar dazwischen — und der Crossfader öffnet auch
  einen Ton, der schon läuft.
- **Mute hält den Lauf nicht an**: stumm und „ganz leise“ liefern bitgleich dasselbe Signal.
- **Step-Loop**: der Step außerhalb des Fensters klingt nicht mehr, das Fenster wiederholt sich.
- **Referenz-Aufnahme**: zwei Sekunden eines Patterns werden gegen `tests/ref/pattern.wav`
  verglichen — jede ungewollte Klangänderung fällt auf.

Last (x86, ein Kern): 1400× Echtzeit mit einer Spur, 36× im schlimmsten Fall mit allen
32 Stimmen und 32stel-Noten auf acht Spuren.

## Als Nächstes

4. CLAP-Plugin aus derselben Parameter-Tabelle — der ehrliche Prüfstein für das Datenmodell.
5. Oberfläche gegen das Modell.
6. Gerät: R36S-Build, Sampler, Dateien.

Offen aus Meilenstein 2: das DSP ist **neu geschrieben**, nicht aus SYNTH CITY portiert — ein
Bit-Vergleich gegen den alten Render steht deshalb aus. Die Referenz-WAV sichert ab jetzt den
neuen Klang.
