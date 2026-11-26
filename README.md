# synthcity-core

Der Kern der nächsten Groovebox: **ein Kern, drei Hüllen** (Gerät, Handy, CLAP-Plugin).
Konzept: `../r36-groovebox/docs/konzept-neu.md`.

Kein Framework, keine Abhängigkeiten, keine STL-Container im Kern — C++17 und fertig.

## Stand: Meilenstein 13 — Polyphone Spuren: Akkorde und Stimmenmodus

| Baustein | Datei | Zustand |
| --- | --- | --- |
| Parameter-Tabelle (stabile IDs, Bereich, Kurve, Flags) | `src/param.h` | steht |
| Dokument (ID → Wert, Textdatei, migrationsfrei) | `src/doc.h` | steht |
| Projekt → Pattern → Track → Step, Locks dünn besetzt | `src/model.h` | steht |
| Scheduler in Samples, Conditions, Micro-Timing, Ratchets | `src/seq.h` | steht |
| Lock-freie Befehls-Queue (Oberfläche → Audio) | `src/queue.h` | steht |
| DSP-Bausteine: PolyBLEP, TPT-Filter, Hüllkurve, Skalen | `src/dsp.h` | steht |
| Machines: `POLY`, `FM`, `DRUM`, `SAMPLE` | `src/machine.h` | steht |
| Sample-Vorrat (64 Plätze, mono **und stereo**), Aufnahme | `src/sample.h` | steht |
| 16 Patterns, Wechsel im Takt, kopieren und leeren | `src/engine.h`, `src/model.h` | steht |
| Song: Kette aus Patterns mit Wiederholungen | `src/engine.h`, `src/model.h` | steht |
| Sample-Ausschnitt und Schleife, samplegenau; Editor mit Wellenform | `src/sample.h`, `web/` | steht |
| Skala je Pattern (Geschwindigkeit, Wechsellänge), Tonart, Spurlänge bis 128 | `src/scale.h` | steht |
| Akkorde je Step (NT2–NT4, lockbar, leitereigen), Stimmenmodus POLY/MONO/LEGATO | `src/engine.h` | steht |
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

## Tempo

Das Tempo gehört entweder dem **Projekt** oder einem **Pattern**. Ein Pattern mit eigenem Tempo
bringt es beim Wechsel mit — am Taktende, wie der Wechsel selbst. In der Oberfläche schaltet
**P** neben der Tempoanzeige um; ein Tipp auf die Zahl öffnet das Feinwert-Fenster.

Dabei kam ein alter Fehler ans Licht: **ein Tempowechsel im Lauf feuerte sieben Anschläge auf
einmal.** Die Zeit eines Steps ist Ursprung + k · Steplänge; ändert sich die Steplänge, liegen
alle schon vergangenen k plötzlich woanders, und der Sequencer holt sie nach. `setTempo`
verschiebt deshalb den Ursprung so, dass die *aktuelle* Position im Takt gleich bleibt, und
rechnet alles schon Geplante mit um. Jeder Tempowechsel — Knopf, Pattern, Host — läuft jetzt
darüber; ein Mutationstest ohne das Umrechnen wird rot.

## Skala und Tonart

Zwei Dinge, die beide „Skala" heißen. Die **Skala im Elektron-Sinn** gehört dem Pattern:
eine **Geschwindigkeit** (1/8× bis 2×) und eine **Wechsellänge** — nach wie vielen Steps ein
angewähltes Pattern übernimmt. Je Spur kommen **Länge** (bis 128 Steps, in Seiten zu 16) und
**Teiler** (1/4 bis 1/32, auch triolisch) dazu.

Für den Sequencer ist eine andere Geschwindigkeit dasselbe wie ein anderes Tempo — darum läuft
sie durch dasselbe `setTempo` und springt nicht. Ein Teiler, der im Lauf wechselt, setzt dort
fort, wo die Spur auf dem neuen Raster gerade steht, statt Steps nachzuholen. Das Echo bleibt
am *musikalischen* Tempo, nicht am beschleunigten.

Auch der **Swing** gehört dem Pattern, wenn es einen eigenen hat — sonst gilt der des
Projekts (50 % gerade, 67 % triolisch, bis 80 %). Jeder zweite Step der Zeitachse rückt um
`(2·swing − 1)` Steplängen nach hinten; das gilt am Raster, also swingt auch ein Step-Loop.
Ein Klang-Test misst den Anschlag von Step 2 bei 50, 67 und einem eigenen 75 %.

Die **Tonart** gehört auch dem Pattern: Grundton und Tonleiter (Dur, Moll, Kirchentonarten,
Pentatonik, Blues). Spuren mit **SKALA an** rasten ihre Noten beim Anschlag auf den nächsten Ton
der Leiter ein — bei Gleichstand auf den tieferen. Im Feinwert-Fenster heißt „eine Note höher"
dann **+STUFE**: die nächste Stufe der Leiter, nicht der nächste Halbton. Liegt eine Note nicht
in der Leiter, zeigt die Zeile, wohin sie einrastet (`C#4 → C4`). Ein Test prüft für jede
Leiter in allen zwölf Tonarten, dass Einrasten nie einen Ton außerhalb liefert.

## Polyphone Spuren: Akkorde und Stimmen

Eine polyphone Machine allein macht noch keine polyphone Spur: ein Step spielte genau eine Note.
Jede Spur hat drei **Akkordnoten NT2–NT4** mit Abständen von −24 bis +24 Halbtönen zur NOTE.
**0** bedeutet Unisono, **OFF** schaltet die Zusatzstimme ab. Am R36S führt **X** von OFF
direkt zu +1 und **Y** zu −1. **SELECT + X** öffnet die Auswahl mit OFF, 0 und den Intervallen
in beide Richtungen.
Sie sind gewöhnliche Werte: als Grundwert gelten sie für die ganze Spur, als **Lock** nur für
einen Step — so spielt jeder Step einen anderen Akkord. Mit **SKALA an** rasten auch die
Akkordnoten in die Tonart ein; aus „Dur" wird auf der II. Stufe von C-Dur von selbst ein
Moll-Akkord (D + gr. Terz = F# → F). Ein Slide auf der NOTE zieht alle Akkordnoten parallel mit.

**AKKORD WÄHLEN** setzt die drei Abstände auf einmal (Dur, Moll, Sus2/4, vermindert,
übermäßig, Dur 7, Maj 7, Moll 7, Quinte, Oktave …) — im Lock-Modus nur für den gehaltenen Step.
Im Feinwert-Fenster beginnt eine ausgeschaltete Akkordnote beim Grundton.

Der **Stimmenmodus** je Spur:

- **POLY** — jeder Anschlag startet eigene Stimmen, lange Noten überlappen, Akkorde klingen.
- **MONO** — eine Stimme je Spur; der nächste Anschlag schneidet die vorige in 4 ms ab
  (ohne Knacken). Akkordnoten werden ignoriert.
- **LEGATO** — wie MONO, aber klingt die Spur noch, wird nur umgestimmt (30 ms Gleiten):
  die Hüllkurve läuft weiter, kein neuer Anschlag — für Bass- und Lead-Linien.

Die Klang-Tests hören den Dur-Dreiklang (C, E, G), das Einrasten F# → F, den Akkord-Lock nur
auf seinem Step, das Überlappen in POLY, genau eine Stimme in MONO und in LEGATO die neue
Tonhöhe ohne neuen Anschlag; zwei Mutationen (MONO schneidet nicht ab, LEGATO schlägt neu an)
werden rot.

## Das Projekt speichert sich selbst

Die Web-App lädt nicht mehr bei jedem Öffnen die Demo. Alle 1,5 Sekunden — und wenn die Seite
in den Hintergrund geht — schreibt sie das Projekt-Dokument weg, sobald es sich geändert hat:
in den **Browser-Speicher** (IndexedDB, dazu localStorage als Rückfall) und, wo die Seite es
darf, ins **eigene Konto** (privat, nur für den angemeldeten Nutzer; höchstens alle 5 s). Beim
Start gewinnt der neuere Stand. Ein Update der Seite lässt das Projekt also stehen; der Kern
liest ältere Dokumente weiter, fehlende Werte bekommen ihren Grundwert.

Die Samples liegen im Browser-Speicher **und im Konto** (16 Bit, in Stücken zu 60000 Werten,
weil ein Datenbank-Dokument begrenzt ist) — der Browser-Speicher einer eingebetteten Seite kann
jederzeit leer sein. Fehlt ein Sample im Browser, kommt es aus dem Konto. Das DOK-Fenster zeigt,
was wohin gespeichert wurde; dazu **JETZT SPEICHERN**, **ALS DATEI** und **DATEI ÖFFNEN**.
Jeder Speicher-Zugriff hat eine Frist, damit ein hängender Browser-Speicher das Laden nicht
blockiert. `tests/konto.js` spielt zwei Geräte gegen ein nachgebautes Konto durch. **NEU (DEMO)** im DOK-Fenster (zwei Tipps) fängt von vorn an. Ein Browser-Test ändert das
Tempo, lädt neu und vergleicht das ganze Dokument.

## Notenlänge bis INF

`trk.len` reichte bis 10,6 Steps. Jetzt: 1–96 fein in Zwölfteln eines Steps (bis 8 Steps, wie
bisher), 97–126 grob von 9 bis 256 Steps, 127 = **INF** — die Note klingt, bis die Spur das
nächste Mal anschlägt (in jedem Stimmenmodus). Ein Klang-Test hält eine INF-Note über 11 Steps
und prüft, dass der nächste Trig sie beendet.

## Modulation: zwei LFOs und eine Mod-Hüllkurve je Spur

Reiter **MOD**: **LFO 1** und **LFO 2** (WAVE: sine, triangle, saw, square, S&H · RATE als
Notenwert von 16 Takten bis 1/32, auch punktiert/triolisch · DEPTH bipolar · DEST · MODE: FREE
läuft am Takt, TRIG startet mit jeder Note, ONE SHOT einmal) und eine **Mod-Hüllkurve**
(ATK, DEC, DEPTH, DEST). **Ziel ist jeder lockbare Spurwert** (ausser COND), in zwei Gruppen:
*während die Note klingt* — PITCH (ein DEPTH-Schritt = ein halber Halbton), SRC A/B, CUTOFF,
RESO, FLT ENV, FLT TYPE, AMP ATK/DEC/SUS/REL, VOLUME, PAN, SEND A/B — und *beim Anschlag*
(einmal je Note abgetastet) — VELOCITY, LENGTH, PROB, MICRO, RETRIG, KEY, NT2–NT4, SAMPLE.
Für die Anschlag-Ziele fragt der Sequencer die Engine (`Sink::trigMod`); ein FREE-LFO wird
dafür an der Transport-Position des Trigs ausgewertet. Alles lockbar je Step.

Gerechnet wird je Stimme in der Steuerrate (`src/mod.h`) auf einer Kopie der Werte (`pm`) —
Locks, Scenes und Live-Folgen bleiben unberührt. Klang-Tests: Rechteck auf PITCH springt eine
Oktave, FREE hängt am Takt, Mod-Hüllkurve auf VOLUME, LFO2 auf CUTOFF; zwei Mutationen werden rot.

## Klangerzeugung: 8 Machine-Regler, analoge POLY, 4-Operator-FM, Ladder, Base-Width

Jede Machine hat jetzt **acht** Regler (`trk.src.a` … `trk.src.h`); was sie bedeuten, sagt die
Machine, unbenutzte blendet die Oberfläche aus. Belegt so, dass **alte Projekte gleich klingen**
(die neuen Regler stehen auf Werten, die nichts ändern — die Referenz-Aufnahme stimmt weiter):

- **POLY — eine analoge Stimme:** DETUNE, SHAPE (Säge → Puls), PW (AUTO = folgt SHAPE), OSC2
  (±24 Halbtöne), SUB (Rechteck eine Oktave tiefer), NOISE, DRIFT (freie Startphasen, langsam
  wandernde Stimmung bis ±15 Cent, reproduzierbar je Anschlag), DRIVE (Sättigung vor dem Filter).
- **FM — vier Operatoren** C, A, B1, B2 (B2 = B1 × 2, mit FEEDBACK), **8 Algorithmen** (ALGO),
  RATIO A/B, LEVEL A/B, DETUNE, **MOD DECAY** (die Modulatoren klingen ab: hell im Anschlag,
  weich im Ausklang; OFF = wie die alte 2-Operator-FM).
- **DRUM:** dazu SWEEP T (35–200 ms) und DRIVE.
- **SAMPLE:** Im Play Mode **NORMAL** stehen START, FINE und **LOOP** (SLOT / OFF / FORWARD /
  PING-PONG) zur Verfügung. Im Play Mode **SLICES** wählen **SLICE** und **FOLLOW** (OFF / STEP /
  RANDOM), welcher Abschnitt zwischen den Markern erklingt. SLICE lässt sich pro Step locken.
- Beide Play Modes teilen TUNE (±24 Halbtöne), REVERSE, LENGTH und WARP. **REPITCH** passt das
  Tempo mitsamt der Tonhöhe an, **TIME** nutzt WSOLA und hält die Tonhöhe. **SAMPLE BPM**
  bestimmt das Ausgangstempo. Jeder Slice dauert im Projekttempo den entsprechenden Anteil.
- Regler mit festen Stufen (LOOP, REVERSE, WARP, PLAY MODE, FOLLOW, FM ALGO, RATIO) öffnen eine Liste.
- **Filtertyp LADDER 24:** vier TPT-Einpoler mit Rückkopplung und Sättigung im Eingang — der
  analoge 24-dB-Tiefpass, stabil bis zur Selbstoszillation.
- **Base-Width** auf jeder Spur (Reiter FLTR): BASE = Hochpass, WIDTH = Tiefpass so viele Oktaven
  darüber; beides lockbar und modulierbar.

Die Effekte: der **Hall** ist neu (vier Diffusions-Allpässe, acht Leitungen mit Hadamard-Matrix,
DECAY als RT60 von 0,3 bis 15 s, DAMPING nie ganz zu), beim **Delay** ist TONE jetzt richtig
herum (127 = hell). Vorher machten DAMP 127 bzw. TONE 127 den Effekt stumm — gemeldet als
„Effekte klingen abgeschnitten“; Klang-Tests sichern das jetzt.

## Song: Zeilen wie im Arranger, Clip-Matrix

Jede Song-Zeile hat **Pattern, Wiederholungen, Länge** (AUTO = Wechsellänge des Patterns),
**Start-Step**, **Tempo** (oder das des Patterns), **Mutes je Spur** und eine **Scene-Stellung**
(KEEP, A, 25/50/75 %, B). Dazu Zeilen vom Typ **LOOP** (springe zu Zeile n, x-mal oder für immer)
und **HALT** (der Song hält an, die Stimmen klingen aus). Bedient wird als **Clip-Matrix**:
Zeilen × Spuren, ein Tipp auf eine Zelle schaltet die Spur in dieser Zeile stumm; die
Zeilennummer öffnet den Editor (dort auch Duplizieren, Verschieben, Löschen, Mutes übernehmen).
Song-Mutes wirken zusätzlich zu den eigenen; in der Spurleiste erscheinen song-stumme Spuren
gedimmt mit gestricheltem M. Der Wechsel bleibt samplegenau: eine Zeile mit Start s, Länge l und
r Wiederholungen dauert (s + l·r) Sechzehntel ab ihrem Ursprung. Klang-Tests: Wechselzeitpunkte,
LOOP-Sprung samt Zähler (Mutation wird rot), HALT genau am Ende, Mutes, Scene B, Start-Step,
Zeilentempo, Dokument.

**Song aufnehmen** (im Song-Fenster): **EXPORT WAV** rechnet den Song offline im Kern der
Oberfläche — von Zeile FROM bis zum Ende von Zeile TO (samplegenau, auch wenn eine Zeile sich
selbst folgt), dann TAIL Sekunden Ausklang für Hall und Delay; schneller als Echtzeit, in
30-ms-Häppchen, damit die Seite bedienbar bleibt. **REC LIVE** nimmt auf, was gerade aus dem
Klang-Kern kommt (Mutes, Morph, Pattern-Wechsel von Hand), bis man stoppt (höchstens 15 min).
Beides wird als 24-Bit-Stereo-WAV gespeichert — über den Knopf SAVE WAV, denn iOS und die Claude-App lassen eine Download-Abfrage nur direkt aus einem Tipp zu (vorher ging sie nach dem Rechnen still verloren; dasselbe galt für SAVE FILE beim Projekt). Die Download-Funktion der Claude-Seite erlaubt nur bestimmte Endungen (zip, txt, pdf, Bilder …,
**kein .wav**) — darum kommt die WAV unkomprimiert in einer **ZIP** (24 Bit bleiben 24 Bit;
`tests/konto.js` prüft ZIP-Kopf, CRC-32 und die WAV darin gegen eine nachgebaute
Download-Funktion mit derselben Liste). Wo das Gerät es zulässt, gibt es zusätzlich **SHARE WAV**
über das Teilen-Menü, direkt als WAV. Tests: Länge des Exports auf ±2 Samples
(wasm), WAV-Kopf und Download im Browser, Live-Aufnahme.

## FX-Spur: ein eigener Sequencer für die Effekte

Jedes Pattern hat neben den 8 Spuren eine **FX-Spur** (in der Spurleiste unten: **FX**). Ihre
Steps lösen nichts aus — jeder ist ein Lock-Step für die Projekt-Effekte: DELAY (Time, Feedback,
Tone, Level), REVERB (Size, Decay, Damping, Level) und MASTER. Ein Tipp auf einen Pad öffnet den
Lock; ohne gehaltenen Step zeigt die FX-Spur die Grundwerte (BASE VALUES) — die Effekte
stehen nur hier, nicht zusätzlich als Reiter jeder Spur. Die FX-Spur hat eigene Länge und eigenen
Teiler (Skala-Fenster), wird mit dem Pattern kopiert, geleert und gespeichert
(`pat.N.fx.st.S.fx.a.fb = 80`). Im Kern ist sie Spur 8 der Brücke (`scStepGet(8, …)`); die
Engine liest die Effektwerte je Block über `gNow()`. Ein Klang-Test lockt MASTER auf Steps 5–8
und misst Stille dort und den Grundwert davor und danach.

## Akkorde in der Tonart

Folgt die Spur einer Tonart (Pattern-Tonleiter + KEY an), zählen NT2–NT4 in **Stufen der
Leiter**: +2 ist die Terz, +4 die Quinte — auf D in C-Dur also F und A, auf B D und F. Die
Akkordliste bietet dann TRIAD, SEVENTH, ADD9, SUS2/4, SIXTH, SHELL, FIFTH, OCTAVE …; der
Feinwert hat ±STEP und ±OCT (Oktave = Zahl der Töne der Leiter), die Zeile zeigt Stufe und
Zielton (`+2 3rd · G4`). Ohne Tonart bleiben es Halbtöne.

## Sprache

Die Oberfläche und alle Werte-Texte sind englisch (Tabelle, Machines, Tonleitern, Akkorde,
Bedingungen, Dialoge). Code-Kommentare und diese Notizen bleiben deutsch.

## Kopieren und Einfügen

- **Pattern:** lange auf die Pattern-Nummer drücken → *kopieren*, dann auf dem Ziel → *einfügen*.
- **Spur:** lange auf die Spurnummer drücken → *kopieren*; am Ziel *Trigs einfügen* (Klang der
  Ziel-Spur bleibt) oder *alles einfügen* (mit Klang und Länge). Auch über Patterns hinweg.
- **Step:** im Lock-Modus über **···** → Step samt Locks kopieren und auf einen anderen Step legen.

Die Ablage liegt im Kern, nicht in der Seite — beide Kerne bekommen dieselben Befehle. Kopiert
wird ein Abbild; was danach an der Quelle passiert, ändert die Ablage nicht.

Ein **trigless Step**, auf den man einen Trig legt, behält seine Locks; nur ein Trig, der
ausgeschaltet wird, nimmt sie mit. Und ein Pattern, in das man schreibt, gilt sofort als
benutzt — vorher fehlten Patterns, die nur über Trigs gefüllt wurden, im Dokument und damit
nach dem nächsten Laden. Das Dokument schreibt jetzt zusätzlich jedes Pattern mit Trigs mit,
benutzt oder nicht.

## Reiter statt langer Liste

Die Regler einer Spur stehen auf Reitern — **TRIG · AKKORD · QUELLE · FILTER · HÜLLE ·
MIX** —, dazu **FX** für die Projektwerte (Echo, Hall, Summe; ohne Lock und Scene), sichtbar ist immer nur eine Gruppe. Der Reiter bleibt beim Spurwechsel stehen (und
über das Neuladen hinweg); ein Punkt am Reiter zeigt, dass dort auf dem gehaltenen Step ein Lock
oder in der Scene ein Wert liegt. Im Hochformat sind die Pads dafür etwas flacher.

Die **Pattern-Auswahl** ist zugeklappt; der Knopf links über den Pads sagt, was läuft (`P3`) und
was wartet (`P3 → 5`, grün umrandet). Ein Tipp klappt die 16 Patterns auf, der Zustand bleibt
gespeichert. **SAMPLE** erscheint nur bei einer Engine, die Samples spielt (`usesSample()` der
Machine) — bei POLY, FM und DRUM wäre die Zeile nur ein Platzhalter.

## Genaue Werte per Touch

Ziehen ist für grobe Wege — eine Note höher trifft man damit nur durch Glück. **Antippen**
öffnet ein Feinwert-Fenster mit Schritten: bei NOTE Halbton und Oktave, sonst ±1 und ±10; die
Tasten wiederholen beim Halten, und *Grundwert* (im Lock-Modus: *Lock entfernen*) setzt zurück.

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

### Ausschnitt und Schleife

Start, Ende und Schleifenanfang gehören dem **Sample**, samplegenau, und stehen im Dokument.
START auf der Spur bleibt der lockbare Versatz — *innerhalb* des Ausschnitts. Ein Sample, das
erst später nachgereicht wird (aus dem Browser-Speicher), behält den Ausschnitt, den das
Projekt für seinen Namen verlangt. Unsinnige Punkte rückt der Kern zurecht, statt zu stören.

Im Editor (SMP → BEARBEITEN): Wellenform, Tippen oder Ziehen setzt den gewählten Punkt, die
Tasten rücken ihn in Millisekunden, Zoom bis 256×, und **Nulldurchgang** lässt die Punkte dort
einrasten, wo das Signal von unten durch null geht — dort knackt kein Anfang, Ende oder
Schleifensprung. *Vorhören* spielt den Platz so, wie er ist (offenes Filter, Note 60), auch
bei stehender Uhr, unabhängig von dem, was eine Spur eingestellt hat.

## Machines und Effekte

**Vier Machines hinter denselben Reglern** — das Elektron-Prinzip. Dieselbe Zahl heißt je
Machine etwas anderes, und *die Machine sagt, was sie heißt*:

| Machine | Regler A | Regler B | Wert 64 zeigt |
| --- | --- | --- | --- |
| `POLY` | DTUN | SHAPE | `+0.00 Halbt.` |
| `FM` | RATIO | INDEX | `5.00 : 1` |
| `DRUM` | SWEEP | RAUSCH | `24 Halbt.` |
| `SAMPLE` (NORMAL) | START | FEIN | `50 %` |

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

**Bearbeitet** werden Scenes wie Locks, nur mit eigener Farbe: **A** oder **B** neben dem
Morph-Regler antippen, dann landet jede Regleränderung in dieser Scene, für die gewählte Spur.
Der Morph fährt dabei auf die Seite der Scene, damit man hört, was man einstellt. Lock- und
Scene-Modus schließen einander aus; eine ganze Scene zu leeren braucht zwei Tipps.

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

## Builds auf GitHub

Der Workflow **Build Synth City Core** läuft bei Pull Requests und bei Pushes auf `main`.
Er lässt sich unter **Actions** auch manuell starten. Unter **Artifacts** des Laufs findest
du die beiden R36S-Pakete `synthcity-arkos.zip` und `synthcity-os-update.tgz` sowie den
Linux-CLAP-Build und die WebAssembly-Datei.

## Das Gerät: R36S

Die dritte Hülle liegt in `device/`. Sie bindet die Brücke (`web/bridge.cpp`) direkt ein und
spricht nur über deren `sc*`-Funktionen mit dem Kern: **dieselben** Machines, Locks, Scenes,
Song-Zeilen und dasselbe Projektformat wie im Browser. Eine Projektdatei vom Handy läuft auf dem
Gerät und umgekehrt.

| Datei | Aufgabe |
|---|---|
| `device/main.cpp` | Oberfläche 640×480, Ton (SDL/ALSA), Dateien, Export, Selbsttest |
| `device/drm.cpp` | Bild direkt per DRM (Groovebox-OS), **ohne libdrm**: nur Kernel-ioctls, mit dem Panel-Versatz aus SYNTH CITY |
| `device/input.cpp` | Gamepad, übernommen aus SYNTH CITY (gleiche `input.cfg`, Lernmodus) |
| `device/font.h` | Bitmap-Schrift 8×16 aus SYNTH CITY |
| `device/midi.cpp` | MIDI über ALSA (Sequencer, sonst rawmidi) plus Netzwerk-MIDI, aus SYNTH CITY |
| `device/usbaudio.cpp` | R36S als USB-Audio-Interface am PC (UAC2-Gadget, Taktausgleich), aus SYNTH CITY |
| `device/web.cpp`, `webpage.cpp` | Webserver: Samples/Projekte hoch- und herunterladen, App-Update — neu, schlank |
| `device/dhcpd.cpp` | Mini-DHCP für den PC-Modus (`groovebox --dhcpd usb0`, startet `pc-modus.sh`) |

**Bedienung** (SELECT halten zeigt sie auf dem Gerät):

| Taste | Wirkung |
|---|---|
| L1 / R1 oder B + ▲▼ | Spur 1–8, FX-Spur. B + ▲▼ gilt in der TRACK-Ansicht. |
| L2 / R2 | Ansicht TRACK / SONG / PROJECT |
| ▲▼ | Regler · ◀▶ Step-Cursor · B + ◀▶ Reiter (TRIG CHRD SRC FLTR AMP MIX MOD INS) |
| FN + Pfeile | Step-Bereich markieren. Links/rechts erweitert über Steps, hoch/runter über Audiospuren. Auf der FX-Spur springt hoch/runter in 16er-Schritten. |
| FN + X / Y | Bereich kopieren / ab Cursor einfügen. Ohne Auswahl kopiert FN + X den aktuellen Step. FN + B hebt die Auswahl auf. |
| X / Y | Wert hoch / runter (B gehalten = grob, lange halten = schneller) |
| A antippen | Trig an/aus im normalen Modus |
| A ca. 350 ms halten, dann loslassen | **STEP LOCK** rastet ein. ▲▼ wählt den Parameter, X/Y ändert seinen Lock-Wert und ◀▶ wechselt den Step. Auf einem leeren Step entsteht ein trigless Lock. B beendet den Modus. Beim Spur-, Pattern- oder Ansichtswechsel endet er ebenfalls. |
| A halten + X/Y oder B | Schnellweg vor dem Einrasten: X/Y setzt einen Lock, B löscht die Locks des Steps. |
| B + A | Spur stumm |
| SELECT → Select pattern | Alle 16 Patterns im Raster. Steuerkreuz wählt, A bestätigt und B schließt. Bei Wiedergabe wartet der Wechsel bis zur Pattern-Grenze. |
| B + SELECT | Direkt zu einem Parameterbereich springen, etwa LFO 2, Insert 2 oder Master EQ. Auch im SELECT-Menü unter Parameter sections. |
| SELECT + X | Bei Condition, DEST oder NT2–NT4 die Werteliste öffnen. Links/rechts wechselt die Gruppe, hoch/runter den Wert, L1/R1 die Seite. X wechselt zwischen Grundwert und Step-Lock. A übernimmt, B schließt die Auswahl. Auch unter SELECT → Choose value. |
| START | Play/Stop (in SONG: Song) · B + START: FILL, solange gehalten |
| SELECT + L1 / R1 | Rückgängig / Wiederholen (auch im SELECT-Menü unter Undo / Redo). Erfasst Steps, Locks, Patterns, Song und Projektwerte, egal ob am Gerät, per MIDI oder über die Webseite geändert. Bis zu 64 Schritte; Laden, Neu und Demo beginnen einen neuen Verlauf. |
| SELECT | Menü: Pattern-Auswahl, Parameterbereiche, kopieren/einfügen (Pattern, Spur, Step), Sample laden, Spurlänge, Scene A/B bearbeiten |
| Track length | X/Y ändern die Länge der gewählten Spur um 1, Rechts/Links um 16 Steps. Bereich 1–128. |
| rechter Stick ◀▶ | Scene-Crossfader A ↔ B |

**Ansichten** (L2/R2): TRACK, SONG, PROJ, SYSTEM.

**Rückgängig/Wiederholen:** Das Gerät vergleicht das Projekt alle 250 ms über eine Prüfsumme
(Pattern für Pattern unter kurzer Sperre, der Audio-Faden wartet nie lange). Nach 700 ms Ruhe
wird aus allen Änderungen seither **ein** Schritt; wer eine halbe Sekunde am Regler dreht,
macht also einen Schritt, nicht hundert (nach spätestens 4 s Dauerdrehen beginnt ein neuer).
Ein Schritt hält nur die geänderten Patterns (je ~90 KB) plus Projektwerte und Song; der
Verlauf ist auf 64 Schritte und 256 Pattern-Kopien (~23 MB) gedeckelt. Mutes, Fill und die
Scene-Stellung sind Performance und kommen nicht in den Verlauf, Samples ebenso nicht.

Die Kopfzeile zeigt den Akkustand mit Ladeblitz, sobald der Akku erkannt wird. Die Prozentanzeige
nutzt die geglättete Akkuspannung aus dem ursprünglichen Synth City. Der Ladeblitz berücksichtigt
auch die separate Ladebuchse des R36S. **SYSTEM → BATTERY** zeigt Spannung, Strom und den
Prozentwert des Akku-Treibers zum Vergleich.

* **PROJ:** Projekte haben **freie Namen**. OPEN PROJECT blättert mit X/Y durch alle Dateien in
  `projects/`, A öffnet. SAVE AS und NEW PROJECT fragen den Namen über eine Bildschirmtastatur
  (A tippt, B löscht, X Leerzeichen, Y Groß/Klein, START fertig). Der Name steht oben links,
  ein `*` heißt: noch nicht gesichert.
* **SYSTEM:** AUDIO OUT (Ausgang wählen, „not open!“ heißt: kein Ton-Gerät), VOLUME (Codec-Lautstärke,
  auch per Lautstärketasten; zeigt Lautsprecher/Kopfhörer — der Pfad wird wie im alten SYNTH CITY
  beim Einstecken umgeschaltet), TEST TONE (2 s 440 Hz auf jeden Ausgang, auch USB), USB-Modus (HOST · PC mit MIDI + Netz · PC + Audio-Interface; X/Y wählt, A wendet an,
  bleibt über Neustarts), Web-Adresse, USB-Audio-Status und -Lautstärke, PC-Ton mischen, MIDI IN,
  CLOCK IN, CLOCK OUT, NOTES OUT, verbundene MIDI-Geräte, Panel-Versatz, Tasten lernen, Beenden.

Beim ersten Start (und beim Umstieg von Version 1 der Einstellungsdatei) übernimmt das Gerät
USB-Modus, Lautstärke und Ausgang aus der `settings.cfg` des alten SYNTH CITY. Ohne gespeicherten
Modus bleibt der USB-Modus, wie er gerade läuft — früher schaltete der Start ihn auf HOST und damit
USB-Audio ab.

**MIDI** (USB-MIDI am PC, Geräte am Host-Port, Netzwerk-MIDI):
* MIDI IN: Noten auf Kanal 1–8 spielen Spur 1–8, Program Change wählt das Pattern (vorgemerkt,
  wenn es läuft). Ein grüner Punkt in der Kopfzeile blinkt bei jeder eingehenden Nachricht.
* CLOCK IN: externe Clock führt das Tempo (orange in der Kopfzeile) und Start/Continue/Stop; nach
  2 s ohne Clock gilt wieder das eigene Tempo. Dafür hat die Engine `extBpm` bekommen.
* CLOCK OUT: 24 ppq plus Start/Stop, zeitgestempelt mit einem Puffer Vorlauf.
* NOTES OUT: jeder Trig (auch Akkordnoten) als Note auf Kanal = Spur; das Note-Off folgt nach der
  Notenlänge. Dafür gibt es den Haken `Engine::noteOut`; Browser und Plugin lassen ihn leer.

**Web-Upload:** Browser auf die Adresse aus SYSTEM (PC-Modus: `http://192.168.77.1`, sonst die
WLAN-IP, Port 80 bzw. 8080). Samples als **Ordner-Explorer** (Brotkrumen, Ordner zuerst mit Anzahl,
Vorhören im Browser, Suche über alle Ordner, neuer Ordner, Ordner samt Inhalt löschen); **IMPORT
FOLDER** oder Ziehen lädt ganze Ordner mit allen Unterordnern in den Ordner, in dem man gerade
steht (nur `.wav`, Fortschritt je Datei). Projekte (`.sc`, auch die
`.txt`-Sicherung der Handy-App), Herunterladen, Löschen, Song-Exporte unter `export/`, im
Groovebox-OS das App-Update (`.tgz`). Hochgeladene Samples, die ein Projekt vermisst, lädt das
Gerät sofort nach. Pfade mit `..`, versteckte Dateien und fremde Endungen lehnt der Server ab.

Auf dem Gerät zeigt SELECT → Load sample dieselbe Ordnerstruktur: A öffnet bzw. lädt, ◀ eine Ebene
hoch, ▶ in den Ordner, L1/R1 blättern, X hört den Slot vor; das Sample im Slot ist grün markiert.

Dateien liegen neben dem Programm: `projects/<name>.sc` (das Dokument als Text, wie die
Web-Sicherung), `samples/` (WAV 8–32 Bit, mono/stereo, mit Unterordnern). Ein Projekt merkt
sich seine Samples per Name; beim Laden sucht jeder Slot seine Datei selbst. Gesichert wird
automatisch 4 s nach der letzten Änderung (erst in eine Zwischendatei, dann umbenannt). Der
Song-Export schreibt **24-Bit-WAV** nach `samples/export/` — auf dem Groovebox-OS also direkt
aufs USB-Laufwerk.

**Bauen**

```
make device && make devtest          # Desktop-Build + Selbsttest mit Bildschirmfotos (build/shots)
make devtest-asan devtest-tsan       # derselbe Selbsttest mit ASan/UBSan bzw. ThreadSanitizer (auch in der CI)
./tools/r36s-build.sh                # aarch64, gegen Ubuntu-18.04-Sysroot (glibc 2.27, SDL 2.0.8)
./tools/r36s-package.sh              # dist/synthcity-os-update.tgz + dist/synthcity-arkos.zip
```

Der Cross-Build linkt **ohne libstdc++** (nur SDL2, ALSA, libm, libc) und läuft so unverändert auf
ArkOS und auf dem Groovebox-OS. Der Selbsttest besteht auch als aarch64-Programm (unter
qemu, mit einer SDL-Attrappe): 74 Prüfungen von Trig/Lock/Scene über Song, Speichern/Laden,
Samples und Export bis zu Bildschirmtastatur, MIDI-Clock/Program Change/Noten und dem
Webserver über echtes HTTP.

* **Groovebox-OS:** `synthcity-os-update.tgz` wie ein SYNTH-CITY-Update aufspielen (Upload-Seite
  oder Laufwerk `GROOVEBOX/update/`). Es ersetzt das Programm; die alte Version bleibt als
  `groovebox.bak` und kommt automatisch zurück, wenn die neue dreimal beim Start abstürzt.
* **ArkOS:** Inhalt von `synthcity-arkos.zip` nach `/roms/ports/` — im Ports-Menü erscheint
  „SYNTH CITY CORE“. Beenden: PROJECT → QUIT oder SELECT + START 2 s.

`--bench` misst, wie schnell der Kern auf dem Gerät rechnet; die Kopfzeile zeigt die
Audio-Last laufend (CPU %).

**Projektgröße ohne feste Grenze:** Das Dokument (`src/doc.h`) wächst mit dem Projekt, statt
48 000 feste Einträge zu je 100 Byte vorzuhalten. Vorher ließ sich ein dichtes Projekt
(z. B. 16 Patterns × 8 Spuren × 64 Steps × 8 Locks) nicht mehr speichern. Jetzt passt der
schlimmste Fall, den das Modell halten kann – alle Patterns, Spuren und 128 Steps mit je 12
Locks samt Slides, rund 410 000 Einträge bzw. 12 MB Text –, und kommt beim Laden unverändert
zurück (Tests in `tests/unit.cpp` und `tests/web.js`, auch im Browser-Bau). Nebenbei belegt ein
leeres Dokument keinen Speicher mehr; vorher waren es je Instanz rund 5 MB. Textpuffer beim
Laden (Gerät, Browser, Plugin-Zustand) wachsen ebenso mit. `DOC_MAX` (4 Mio.) ist nur noch eine
Notbremse.

**Last und Knackser (Nov. 2026, gemeldet: 2× SMPL, CHOP, POLY mit Locks bis 80 %, viele Klicks):**

* *Rechnen ohne Klangverlust.* Alles, was sich am Klang nicht ändern darf, ist mit einer
  Prüfsumme über jedes Sample nachgewiesen bitgleich (`tools/bench_device <spur> <sek>` druckt sie):
  Modulationsbereich wurde je Steuertakt per Namenssuche nachgeschlagen; die Modulation lief über
  alle 34 Ziele statt über die belegten; LFO-Welle ohne Ziel wurde gerechnet; Filter-, Pegel- und
  Panorama-Werte (tan, pow, sin/cos) werden nur neu gerechnet, wenn sich ihr Eingang ändert; POLY
  rechnet Säge/Puls nur, wenn sie gemischt werden, und den gemeinsamen BLEP einmal; Sende-Wege nur,
  wenn sie offen sind; WSOLA liest sein Vergleichsstück einmal statt für jeden Kandidaten.
  callgrind über das Szenario: 779 → 645 Mio. Instruktionen (−17 %).
  Nach den neuen Klangfunktionen (zwei POLY-Oszillatoren, erweiterte Modulation, Live-Regler)
  stieg dasselbe Szenario auf 810 Mio.; wieder bitgleich gesenkt auf 704 Mio. (−13 %): jede
  Stimme baut die Liste der Regler, denen sie folgt, einmal beim Anschlag (und nach trigless
  Locks) statt in jedem Steuertakt alle Regler samt Machine- und Lock-Prüfung durchzugehen;
  POLY entscheidet die Wellenform je Block statt je Sample; `modSlots()` ist klein genug zum
  Einbetten. Prüfung: `tools/bench_device` gegen einen Bau des alten Stands, Prüfsummen gleich.
* *Zwei Rechenkerne.* Die Engine teilt die Stimmen fest in gerade/ungerade (`VoiceHelper`,
  `runHalf`); auf dem Gerät rechnet ein zweiter Faden die ungeraden. Beide Hälften haben eigene
  Puffer und werden danach addiert – das Ergebnis hängt nicht vom Helfer ab (Selbsttest:
  bitgleich mit einem Kern; gegenüber der alten Summierreihenfolge höchstens −129 dBFS
  Rundungsrauschen, Referenz-WAV unverändert gültig).
* *Knackser im Klang behoben:* Hüllkurve sprang bei SUSTAIN über 0 dB am Ende der Attack
  schlagartig auf den Haltepegel (bis doppelt so laut); Sampler/CHOP blenden 1 ms ein, wenn mitten
  im Sample gestartet wird (START, Slice, rückwärts), und vor einem Ende aus (Slice, LENGTH) –
  vorher sprang das Signal auf bzw. von null. Stimmenklau nimmt die leiseste ausklingende Stimme.
  Test „Sampler ohne Knacksen“ misst den größten Sprung (vorher 33 % bzw. 18 % der Spitze, jetzt 2 %).
* *Knackser auf dem Gerät behoben:* Autosave baute das Dokument unter der Audio-Sperre, Sample-
  und Projekt-Laden lasen Dateien darunter (der Audio-Faden wartete Millisekunden). Jetzt wird
  Pattern für Pattern kurz kopiert und ohne Sperre geschrieben/gelesen (`NoLock`). Dazu
  Echtzeit-Priorität für Audio- und Helfer-Faden, `mlockall`, Denormals aus (FTZ).
* SYSTEM: AUDIO BUFFER (128–1024), AUDIO CORES (1/2), AUDIO STATS (zu späte Blöcke, schwerster
  Block in % – zu späte Blöcke hört man als Klick).

## Modulation und Mastering

**MOD** zeigt die Namen der gewählten Machine als SRC-Ziele, etwa **SLICE** bei SAMPLE im Slice-Modus.
Die beiden LFOs bieten **Free**, **Trig**, **Hold**, **One** und **Half**, Startphase,
bipolaren Fade und rückwärts laufende Wellenformen. **Speed** mit **Mult** kann ans Tempo
gekoppelt sein oder bei 120 BPM laufen. **Note Rate** im Speed-Regler lässt die bisherigen
Notenwerte weiter gelten. Der Mod-Envelope kann pro Trig starten oder mit **Free Loop**
seine Attack- und Decay-Phasen unabhängig von den Noten wiederholen.
[Digitakt-Handbuch, Abschnitt 11.7](https://www.elektron.se/wp-content/uploads/2025/07/Digitakt-User-Manual_ENG_OS1.52A_250708.pdf)

Auf der **FX-Spur** unter **Master → Chain** lässt sich die Mastering-Kette einschalten.
Sie enthält Trim, einen Achtband-EQ, Stereo-Bus-Kompressor, optionales Soft Clipping
und einen latenzfreien Limiter. **A/B Dry** schaltet beim Abhören RMS-pegelangepasst auf
das unbearbeitete Signal. Der 24-Bit-Song-Export nutzt den bearbeiteten Mix und zeigt
Integrated LUFS und den vierfach interpolierten, geschätzten Spitzenpegel. Auf dem
Gerät liegt der Messbericht zusätzlich neben der WAV-Datei. Der Limiter verwendet
Sicherheitsreserve für diese Interpolation. Ältere Projekte starten mit ausgeschalteter
Mastering-Kette.

Am R36S öffnet **SELECT → Visual Master EQ** die Frequenzkurve mit Live-Spektrum
des Master-Ausgangs. Auf einem EQ-Regler der FX-Spur geht das direkt mit **SELECT + X**.
**Links/Rechts** wechselt zwischen acht Bändern, **X** zwischen Shape, Frequenz, Gain und Q.
**Hoch/Runter** bearbeitet das Feld, **L1/R1** verschiebt direkt die Frequenz des gewählten Bands,
**Y** setzt das Feld zurück, **A** schaltet die Master-Chain und **R2** wechselt A/B.
Pro Band gibt es Bell, Low/High Shelf, Tief- und Hochpass mit 12 oder 24 dB/Oktave,
Notch, Bandpass und Off. Die drei bisherigen EQ-Werte werden als Bänder 1, 4 und 8 geladen.

**SELECT + X** öffnet auch die Detailansichten von Master-Kompressor, Limiter und Clipper,
von Track-Kompressor und Track-Clipper sowie von Filter-, Amp-, Mod- und FM-Operator-Envelopes.
**Links/Rechts** wählt einen Regler, **Hoch/Runter** ändert den Wert, **L1/R1** ändert ihn gröber.
**START** steuert die Wiedergabe, **B** schließt die Ansicht. Kompressor und Limiter zeigen
eine statische Kennlinie und den Verlauf der Gain Reduction. Auf dem Gerät läuft die
Spektrumanalyse nur im EQ-Fenster und die Verlaufsmessung nur im geöffneten Dynamikfenster.

## Send-Effekte auf der FX-Spur

**Delay** bietet Ping Pong, Stereo und Tape. **Linked Sync** nutzt dieselbe Teilung für beide
Kanäle, **Dual Sync** trennt die Notenwerte, **Free L/R** stellt beide Seiten in Millisekunden ein.
**Base** setzt den Hochpass, **Width** die Öffnung des Tiefpasses in Oktaven. Tape bietet Wow,
Flutter und Drive sowie gleitende Zeitänderungen.

**Reverb** bietet Studio, Room, Plate und Hall. Dazu kommen Predelay, die Eingangsfilter HPF/LPF,
Low- und High-Shelf mit einstellbarer Frequenz auf dem Effekt-Return und Stereo-Width.

**B + Rechts** öffnet auf der FX-Spur den **MOD**-Reiter, **B + Links** kehrt zu **FX** zurück.
Beide FX-LFOs haben Wellenform, synchronisierten Notenwert oder freie Rate in Hz, Phase,
bipolare Tiefe und eigene Ziele auf den beiden Sends. Die FX-Spur kann ihre Werte pro Step locken.

## Filter-Envelope, POLY und FM

Neue Sounds starten im **AMP**-Reiter mit **Sustain auf 0 dB**. Beim Laden bleibt das
Sustain-Verhalten gespeicherter Sounds erhalten.

Im **FLTR**-Reiter schaltet **ENV SOURCE** zwischen Amp-Link und eigenem Filter-ADSR um.
**ENV** legt Betrag und Richtung der Cutoff-Modulation fest. Der Filter-ADSR folgt dem Notengate.
Mit **A** lässt sich seine Quelle direkt in der Detailansicht umschalten.

**POLY** hat im **SRC**-Reiter getrennte Wellenform, Lautstärke, Grob- und Feinstimmung und
Pulsbreite für beide Oszillatoren. Die gemeinsame Saw/Pulse-Mischung lässt sich pro Oszillator
durch Saw, Pulse oder Sine ersetzen. **FM** hat pro Operator C, A, B1 und B2 einstellbares
Ratio, Level, Detune und einen eigenen zuschaltbaren ADSR. **B + SELECT** springt zu den
Oszillator- und Operator-Gruppen. Die neuen Regler stehen als Locks und Modulationsziele zur Verfügung.

Falls die FN-Taste nicht als Guide oder Linux-FN erkannt wird, lässt sie sich unter
**SYSTEM → Learn buttons** zuordnen. Auf einer Tastatur entspricht **F** der FN-Taste.

## Insert-Effekte

Auf jeder Audiospur stehen im Reiter **INS** zwei serielle Stereo-Inserts zur Verfügung.
**Crush / Drive** hat getrennte Regler für Abtastrate und Bit-Tiefe, einen optionalen Filter
vor der Abtastratenreduktion, Drive, Tone, Output und Mix. **Compressor** bietet Threshold,
Ratio, Attack, Release, Knee, Peak/RMS-Erkennung, Auto Release, Output, Mix und einen Sidechain-Hochpass. **Sidechain** wählt
SELF oder eine der acht Spuren. **Clipper** bietet Hard, Cubic Soft, Tanh und Arctan mit Drive,
Ceiling, Output und Mix. Die Anzeige GR zeigt die Gain Reduction in dB.
**Chorus** hat die Charaktere **Classic** (ein modulierter Delay-Abgriff pro Seite)
und **Ensemble** (zwei versetzte Abgriffe pro Seite). Rate, Depth, Width, Mix und Output
sind einstellbar. Bei Width = 0 bleibt ein Mono-Signal in der Mitte.

Die Inserts bearbeiten die Summe der Stimmen einer Spur. Eine externe Sidechain greift
vor den Inserts ihrer Quellspur ab. Die Sends bleiben vor den Inserts, damit bestehende
Send-Locks pro Stimme erhalten bleiben. Der Einzelspur-Recorder nimmt hinter den Inserts
und ohne Send-Effekte auf. Einstellungen sind pro Pattern und Spur gespeichert. Beide
Inserts sind beim Laden alter Projekte aus.

## SAMPLE, Sample-Editor und Recorder

Mit **SRC → ENGINE → SAMPLE** spielt die Spur den gewählten **SAMPLE SLOT**. **PLAY MODE**
schaltet zwischen **NORMAL** und **SLICES**. Slice-Playback verwendet die Marker aus dem
Sample-Editor. Ohne Marker ist der ganze Ausschnitt ein Slice.

Auf dem R36S öffnet **SELECT + X** am **SAMPLE SLOT** den Sample Manager für genau diesen Slot.
**SELECT + Y** öffnet auf einer SAMPLE-Spur direkt den Editor. Bei einem leeren Slot öffnet
sich der Manager. Im Manager öffnet **Y** den Editor für das geladene Sample.

Bei **WARP** oder **SAMPLE BPM** öffnet **SELECT + X** die BPM-Eingabe. Links/rechts ändert
den Wert um 1 BPM, hoch/runter um 10 BPM. **X** wählt **AUTO**, **A** übernimmt.
Auto schätzt das Tempo beim Import und verwendet für kurze Loops zusätzlich deren Länge.
**REPITCH** ändert Tempo und Tonhöhe gemeinsam, **TIME** hält die Tonhöhe. Bestehende
Projekte laden ihre früheren Beat-Einstellungen als **LEGACY**.

Links/rechts bewegt den Cursor, L2 verkleinert die Schritte und R2 schaltet auf einzelne
Frames. L1/R1 wählt einen Marker, X hört den Slice am Cursor vor. Mit A lassen sich
ein gleichmäßiges Raster oder Marker aus Transienten erzeugen und Marker manuell setzen,
verschieben oder löschen. Im Browser stehen die gleichen Befehle im Wellenform-Editor.

**Create linear locks** verteilt Slices der Reihe nach auf die vorhandenen Trigs der
Spur im Slice-Modus. **Create random locks** schreibt einmal gewürfelte Slice-Werte in dieselben
Trigs. Beides lässt sich mit **Undo slice locks** rückgängig machen. Trigless Steps bleiben
unverändert. Die Marker werden in der Projektdatei gespeichert und folgen dem Sample-Slot.

**Recorder** im SELECT-Menü nimmt **Master**, die **gewählte Spur ohne Send-Effekte** oder
den **USB-Rückkanal** (SYSTEM → PC + Audio) auf. Die Länge ist frei oder 1, 2, 4 bzw. 8 Takte,
soweit der Sample-Pool Platz bietet. START startet sofort, am nächsten Takt oder beim
nächsten erklingenden Trig am zuvor gewählten Step. Erneutes START stoppt oder bricht die
Armierung ab. Die Aufnahme wird als Stereo-WAV in `samples/` geschrieben. X hört den Take
vor, Y weist ihn der gewählten SAMPLE-Spur zu und öffnet den Editor.

## Als Nächstes

12. Gerät: Sampling vom internen Codec, MIDI-CC auf Regler, Mehrkanal-USB-Audio.
13. Modulation: LFOs und Hüllkurven als Ziele aus der Parametertabelle.

Offen aus Meilenstein 2: das DSP ist **neu geschrieben**, nicht aus SYNTH CITY portiert — ein
Bit-Vergleich gegen den alten Render steht deshalb aus. Die Referenz-WAV sichert ab jetzt den
neuen Klang.
