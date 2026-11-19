# synthcity-core

Der Kern der nächsten Groovebox: **ein Kern, drei Hüllen** (Gerät, Handy, CLAP-Plugin).
Konzept: `../r36-groovebox/docs/konzept-neu.md`.

Kein Framework, keine Abhängigkeiten, keine STL-Container im Kern — C++17 und fertig.

## Stand: Meilenstein 10 — Song-Modus

| Baustein | Datei | Zustand |
| --- | --- | --- |
| Parameter-Tabelle (stabile IDs, Bereich, Kurve, Flags) | `src/param.h` | steht |
| Dokument (ID → Wert, Textdatei, migrationsfrei) | `src/doc.h` | steht |
| Projekt → Pattern → Track → Step, Locks dünn besetzt | `src/model.h` | steht |
| Scheduler in Samples, Conditions, Micro-Timing, Ratchets | `src/seq.h` | steht |
| Lock-freie Befehls-Queue (Oberfläche → Audio) | `src/queue.h` | steht |
| DSP-Bausteine: PolyBLEP, TPT-Filter, Hüllkurve, Skalen | `src/dsp.h` | steht |
| Machines: `POLY`, `FM`, `DRUM`, `SMPL` — hinter denselben Reglern | `src/machine.h` | steht |
| Sample-Vorrat (64 Plätze, mono **und stereo**), Aufnahme | `src/sample.h` | steht |
| 16 Patterns, Wechsel im Takt, kopieren und leeren | `src/engine.h`, `src/model.h` | steht |
| Song: Kette aus Patterns mit Wiederholungen | `src/engine.h`, `src/model.h` | steht |
| Dokument mit Namen (nicht nur Zahlen) | `src/doc.h` | steht |
| Sende-Effekte: Delay im Takt, Hall (FDN) | `src/fx.h` | steht |
| Stimmen, Filter, Hüllkurve, Panorama, Summe | `src/engine.h` | steht |
| Slides als Rampen, Scenes + Morph, Mute, Step-Loop | `src/engine.h`, `src/model.h` | steht |
| CLAP-Plugin (Parameter, Zustand, Noten, Transport) | `plugin/clap_plugin.cpp` | steht |
| Oberfläche (WebAssembly + Touch), baut sich aus der Tabelle | `web/` | steht |
| Song-Modus, Gerät, Sampler-Aufnahme | — | später |

```
make test        # 60 + 25 + 73 + 19 Prüfungen
make web         # baut web/synthcity.wasm (clang, ohne Werkzeugkette von außen)
make plugin      # baut build/synthcity.clap
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
- **Der Anschlag wird eingefroren, der Regler nicht.** Note, Velocity, Länge und Bedingung
  gehören zum Trig und stehen danach fest. Alles, was die Tabelle als *modulierbar* führt —
  Filter, Pegel, Panorama, Sends, die beiden Regler der Machine — **folgt der Spur weiter**,
  solange dieser Step es nicht gelockt hat und gerade kein Slide darauf läuft.
  Das war zuerst falsch: der Schnappschuss fror alles ein, und ein gedrehter Filterregler
  blieb am klingenden Ton wirkungslos.

## Song

Ein Song ist eine Kette: Pattern, wie oft, dann das nächste; am Ende geht es vorn weiter. Er
benutzt **denselben Wechsel** wie ein Tipp auf ein Pattern — nur entscheidet er selbst, wann.
Darum ist auch er sample-genau und von der Blockgröße unabhängig (ein Test vergleicht 64er und
977er Blöcke über drei Wechsel).

Die eine Feinheit, die man leicht falsch baut: Zwischen den **Wiederholungen** wird das Pattern
**nicht** neu begonnen. Gewechselt wird erst am Ende der letzten. Sonst bräche eine Spur mit
12 Steps bei jeder Wiederholung ab, statt weiter gegen die 16 zu laufen. Ein Mutationstest
(„Wiederholungen ignorieren") macht drei Prüfungen rot — der Test misst also wirklich das.

Einschalten, während es läuft, beginnt am **nächsten Taktende**, nicht mittendrin. Wer von Hand
ein Pattern antippt, verlässt den Song.

## Die Oberfläche wird im Browser geprüft

`tests/browser.js` (`make uitest`) startet einen echten Browser und bedient die Seite wie ein
Finger: tippen, halten, ziehen. Gemessen wird dabei am **Ergebnis** — am Pegel, den der
Audio-Kern meldet, nicht an der Anzeige. Nur so fällt auf, wenn die Anzeige stimmt und der Klang
nicht.

Dazu läuft die Seite auf **sieben echten Handygrößen** (iPhone SE bis 15 Pro Max, hoch und
quer, ein Pixel): für jedes Bedienelement wird geprüft, ob in seiner Mitte auch wirklich *es
selbst* liegt — nicht ein Nachbar, der darüber ragt. Das fing erst an, als ein Bildschirmfoto vom
iPhone zeigte, dass die M-Knöpfe verdeckt waren und die Seite 397 px breit war, auf jedem Gerät.
Meine früheren „Handy"-Tests liefen ohne Viewport-Angabe — also gar nicht in Handybreite.

Dafür gibt es in der Kopfzeile eine **Pegelanzeige**. Sie ist nicht Zierde: sie war das
Messgerät, mit dem sich „Mute geht nicht" überhaupt beantworten ließ.

## Pattern wechseln

Gewählt wird sofort, **gewechselt wird im Takt** — alles andere klingt wie ein Versehen. Der
Wechselpunkt wird beim Antippen als Sample-Position ausgerechnet, und der Audio-Block wird an
genau dieser Stelle geteilt. Darum fällt der Wechsel bei 64er und bei 977er Blöcken **bitgleich**
aus; ein Test vergleicht beide Läufe.

Damit ein neues Pattern bei Step 0 beginnt, hat der Sequencer jetzt einen **Ursprung**: die
Sample-Position, an der das laufende Pattern anfing. Die Zeit läuft durch, der Zähler nicht —
dieselbe Trennung wie beim Step-Loop. Was vom alten Pattern noch in der Warteschlange lag, gilt
nicht mehr.

In der Oberfläche steht die Pattern-Reihe über den Steps: antippen wählt (das wartende blinkt
grün), lange drücken öffnet *kopieren* und *leeren*. **Die Anzeige folgt dem Klang**, nicht
umgekehrt: während es läuft, entscheidet der Audio-Kern, wann gewechselt wird, und das Modell
zieht nach — sonst würde man schon das neue Pattern bearbeiten, während das alte noch spielt.

### Was beim Start mitgehen muss

Der Audio-Kern entsteht erst beim ersten **PLAY**. Alles, was vorher eingestellt wurde, kennt er
nicht — und das ist dreimal schiefgegangen, bevor es eine feste Regel wurde: erst bei den
**Samples**, dann beim **Pattern**, dann bei den **Mutes**. Seitdem schickt `audioOn()` den
ganzen Zustand nach: Pattern, Mutes, Morph und jedes geladene Sample. Ein Browser-Test schaltet
deshalb absichtlich **vor** dem Start stumm und misst danach den Pegel.

## Der Sampler

Der Vorrat ist **ein** Speicherblock mit 64 Plätzen (rund 44 Sekunden). Geladen wird außerhalb
des Audio-Threads, gelesen wird dort nur — die Regel aus Meilenstein 1 gilt auch hier.

Entscheidend ist, dass der Platz ein ganz normaler **lockbarer** Spurparameter ist (`trk.smp`):
jeder Step kann ein anderes Sample anschlagen, ohne dass der Sampler dafür etwas Eigenes braucht.
Die Tonhöhe entsteht aus der Abspielgeschwindigkeit; Note 60 spielt in Aufnahmegeschwindigkeit.

**WAV liest die Oberfläche selbst** — PCM 8/16/24/32 Bit und Fließkomma, mono und stereo. Das
wichtigste Format soll nicht davon abhängen, was der Browser gerade entpacken will; auf dem
iPhone will er nicht immer. Alles andere (MP3, M4A, FLAC …) geht weiterhin an den Browser.
Zwei Dinge, die auf dem iPhone den Dateiwähler blockieren und die man nicht ahnt: ein mit
`display:none` verstecktes Dateifeld öffnet dort oft gar nichts, und `accept="audio/*"` allein
gräut `.wav` aus — die Endungen müssen ausdrücklich dastehen. Zwei Fälle, die ein Test aufgedeckt hat und die leicht zu übersehen
sind: Samples, die **vor** dem ersten PLAY geladen werden, erreichen den Audio-Faden sonst nie —
sie werden beim Start nachgereicht; und ein Punkt in der Liste zeigt, dass der Klang-Kern das
Sample wirklich hat, nicht nur die Anzeige.

### Das Dokument merkt sich Namen, nicht Klänge

Eine Projektdatei bleibt klein und lesbar: sie schreibt `smp.0.name = kick 909`, nicht 200 kB
Audio. Wer sie öffnet, *sieht*, welche Samples sie verlangt. Ein Platz, dessen Name bekannt ist
und dessen Klang fehlt, ist ein **Wunsch** — ihn zu erfüllen ist Sache der Hülle: im Browser der
eigene Speicher (IndexedDB), am Gerät später die Speicherkarte.

Dafür musste das Dokument zum ersten Mal **Text** können. Es bleibt beim Format `id = wert`:
sieht der Wert wie eine Zahl aus, ist es eine; sonst ein Name. Alte Dateien lesen sich unverändert.

### Aufnehmen, was herauskommt

Aufgenommen wird dort, wo der Klang entsteht — im Audio-Thread, direkt in einen reservierten
Platz des Vorrats, ohne Umweg und ohne Speicheranforderung. Danach geht das Ergebnis an die
Oberfläche zurück, die es ins Modell und in ihren Speicher legt. Die Länge steht vorher fest
(1, 2 oder 4 Takte am Tempo), die Aufnahme endet von selbst.

Das ist dieselbe Mechanik, die am Gerät später den Eingang oder das Mikro aufnimmt. **Im
veröffentlichten Browser-Fenster ist der Mikrofonzugriff gesperrt** — deshalb nimmt die Weboberfläche
die Summe auf, nicht das Mikro.

Was noch fehlt: Trimmen und Loop-Punkte setzen, und ein Blick auf die Wellenform.

## Machines und Effekte

**Drei Machines hinter denselben zwei Reglern** — das Elektron-Prinzip. Dieselbe Zahl heißt je
Machine etwas anderes, und *die Machine sagt, was sie heißt*:

| Machine | Regler A | Regler B | Wert 64 zeigt |
| --- | --- | --- | --- |
| `POLY` | DTUN | SHAPE | `+0.00 Halbt.` |
| `FM` | RATIO | INDEX | `5.00 : 1` |
| `DRUM` | SWEEP | RAUSCH | `24 Halbt.` |
| `SMPL` | START | FEIN | `50 %` |

Eine andere Machine zu wählen setzt **ihre** beiden Grundwerte — sonst stünde der Sampler beim
Umschalten mitten im Sample statt an seinem Anfang. (Derselbe Fehler wie in SYNTH CITY, dort
Aufgabe 90.)

Die Parametertabelle nennt die beiden nur noch `QUELLE A` und `QUELLE B`; den echten Namen
liefert `Machine::labelA/labelB`, den Anzeigetext `Machine::text`. Damit stimmt die Beschriftung
automatisch in allen drei Hüllen — Oberfläche, Plugin, später das Gerät.

**Zwei Sende-Busse statt Effekten je Spur**: Hall kostet acht Spuren einmal, nicht achtmal — der
Grund, warum Hardware es genauso macht. Bus A ist ein Delay, dessen Zeit ein **Notenwert** ist
(1/32 bis 1/1, am Tempo), mit kreuzweiser Rückkopplung; Bus B ein Hall als Feedback-Delay-Netzwerk
(vier Leitungen, Householder-Matrix, Dämpfung je Leitung). Der Sende-Anteil gehört zur **Stimme** —
ein P-Lock auf `S.A` schickt wirklich nur diesen einen Anschlag ins Echo.

Die Referenz-Aufnahme blieb dabei **unverändert**: mit Sends auf null klingt nichts anders.

## Die Oberfläche

`web/index.html` ist die dritte Hülle: derselbe Kern als WebAssembly, einmal im Hauptfaden
(Modell, Texte, Speichern) und einmal im Audio-Faden (Klang) — das ist die Thread-Grenze aus dem
Konzept, hier als `postMessage` statt als Queue. Jeder Griff geht an beide.

**Die Reglerliste wird nicht geschrieben, sie entsteht.** Das JavaScript fragt den Kern nach
`scNParams()` und baut daraus Zeilen mit Name, ID, Bereich und Anzeigetext. Ein neuer Parameter in
`src/param.h` erscheint ohne eine Zeile JavaScript — in der Liste, mit Einheit, lockbar.

**Eine Geste für das Häufigste**: Step lange drücken → Lock-Modus (rosa Band, ◀ ▶ springen nur zu
Steps mit Trig), dann einen Regler ziehen = P-Lock auf diesem Step. Beim Ziehen entscheidet die
erste Richtung, ob gescrollt oder verstellt wird — sonst verstellt man beim Blättern Werte.

**Ohne Werkzeugkette von außen.** Emscripten ließ sich hier nicht installieren, also baut `clang`
direkt nach `wasm32`; die fehlende libc (Mathematik, `snprintf`, Speicher) steht in
`web/freestanding.*`, rund 200 Zeilen. Der Browser-Bau hat damit seine **eigene** Mathematik —
`tests/web.js` misst deshalb nach, dass Tonhöhe (440 Hz), dB-, Hz- und ms-Anzeige dort dasselbe
ergeben wie nativ.

### Was dabei schiefging

Drei Fehler, die nur die laufende Oberfläche zeigt:

- **Der Audio-Kern startete stumm.** Im Audio-Thread gibt es kein `TextEncoder`; das Laden des
  Projekts warf, und die Ausnahme verschwand in einer Promise. Jetzt meldet der Worklet Fehler
  über seinen Port, und der Text kommt fertig als Bytes herüber.
- **Befehle gingen verloren**, die ankamen, bevor der Kern im Audio-Faden fertig geladen war —
  darunter `scStart`. Sie werden jetzt gepuffert.
- **`.pad.play` traf die Regel des PLAY-Knopfs** (`.play.on`) und färbte den Zeiger grün. Der
  Zustand heißt jetzt `.pad.now`.

Dazu zwei Anzeigefehler, die erst im Bild auffielen: LEN wurde als Millisekunden angezeigt
(es sind Zwölftel eines Steps), und bipolare Regler rechneten die Mitte immer als 64 statt als
Mitte ihres eigenen Bereichs — MTIM stand dadurch auf „-40“ statt „±0.00 Steps“.

## Der Prüfstein: was das Plugin gekostet hat

Das Konzept sagt: *wenn das Plugin mühsam wird, stimmt etwas am Datenmodell nicht.* Es wurde
nicht mühsam — **392 Zeilen**, davon ist die Hälfte Anzeigetext und C-Schnittstelle.

- **Die Parameterliste wird erzeugt, nicht gepflegt.** Sie entsteht aus `PARAMS`; die ID ist der
  FNV-Hash der Text-ID, also über Versionen hinweg stabil. Ein neuer Regler erscheint im Host,
  ohne dass im Plugin eine Zeile dazukommt.
- **Der Zustand *ist* das Dokument.** `state.save` schreibt dieselben Zeilen `id = wert`, die
  auch auf die Karte gehen. Ein Test liest im gespeicherten Plugin-Zustand nach, ob
  `pat.0.trk.0.note = 45` darin steht — Projektdatei und Plugin-State sind dasselbe Format.
- **Nicht alle Spuren auf einmal.** Ableton Live blendet oberhalb von 32 Parametern nicht mehr
  automatisch ein und zeigt höchstens 128. Darum sieht der Host wie an der Hardware: die
  globalen Werte, die Performance-Ebene (Morph, Fill, 8 Mutes) und die Regler der **gewählten**
  Spur — 35 Parameter. Beim Spurwechsel bittet das Plugin den Host, die Werte neu zu lesen.

### Getestet wie von einem Host

`tests/plugin.cpp` lädt die `.clap` mit `dlopen`, sucht `clap_entry` und benutzt sie wie eine DAW:
73 Prüfungen über Anschlüsse, Parameter, Automation, Noten, Transport und Blockgrößen von 1 bis
4096. Weil 73 grüne Prüfungen im ersten Anlauf verdächtig sind, habe ich drei **Mutationen**
eingebaut und geprüft, dass die Tests rot werden: Transport startet den Sequencer nicht (gefangen),
Spurwahl wird ignoriert (gefangen), gewählte Spur fehlt im Zustand — **nicht** gefangen. Diese
Lücke ist jetzt mit zwei Prüfungen geschlossen.

Noch offen: `clap-validator` in der CI. Der Bau wurde hier abgelehnt, das Werkzeug fehlt also
bisher.

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
- **Ein gedrehter Regler wirkt sofort**, auch mitten im Ton (4 kHz fallen um das Achtzehnfache) —
  **aber** ein P-Lock auf diesem Step bleibt stehen, ein trigless gesetzter Wert auch, und die
  Note springt nicht um. Vier Prüfungen, die zusammen erst die Regel ergeben.
- **Slide**: zu Beginn des Steps klingt noch die alte Note, am Ende die neue.
- **Scenes**: A dumpf, B offen, Morph 0,5 messbar dazwischen — und der Crossfader öffnet auch
  einen Ton, der schon läuft.
- **Mute hält den Lauf nicht an**: stumm und „ganz leise“ liefern bitgleich dasselbe Signal.
- **Step-Loop**: der Step außerhalb des Fensters klingt nicht mehr, das Fenster wiederholt sich.
- **Referenz-Aufnahme**: zwei Sekunden eines Patterns werden gegen `tests/ref/pattern.wav`
  verglichen — jede ungewollte Klangänderung fällt auf.

Last (x86, ein Kern): 580× Echtzeit mit einer Spur, 33× im schlimmsten Fall mit allen
32 Stimmen und 32stel-Noten auf acht Spuren — die Effekte laufen dabei mit.

## Als Nächstes

11. Sample trimmen und loopen, Wellenform zeichnen.
12. Gerät: R36S-Build, echter Eingang, Gamepad-Bedienung.

Offen aus Meilenstein 2: das DSP ist **neu geschrieben**, nicht aus SYNTH CITY portiert — ein
Bit-Vergleich gegen den alten Render steht deshalb aus. Die Referenz-WAV sichert ab jetzt den
neuen Klang.
