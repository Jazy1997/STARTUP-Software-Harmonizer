# Sessione 31 — 2026-08-11

> Gli attacchi delle note. Il timbro a nota tenuta è chiuso da s.26; l'utente chiede di
> occuparsi **solo** dell'attacco, e porta quattro export a voce singola con le rispettive
> reference. Esito: causa trovata, misurata su tre percorsi indipendenti, corretta in un
> solo file. **Manca la conferma all'ascolto.**

---

## Il materiale, e perché era già una diagnosi

L'utente introduce un codice di nome per gli export (annotato in `HANDOFF.md`):
`exp#<N>_<Preset>_<Voce>_<Versione>.wav`, con `exp#1` = Dry/Wet 1 (solo wet), Balanced,
Fmt Spread 0, Glide 0 ms, Voices 4, tutti i Gain/Voice a 0 tranne la voce isolata. Per ogni
export c'è un `REF_...` prodotto automatizzando il Pitch di *Autoshift* di Ableton. Dry
sorgente: `Test 1 - Basic Silk Horns.wav` (C4-D4-E4-C4, ~2 s per nota), Root C, buffer 1024
con Focusrite USB ASIO.

| export | colonna V1 | esito riferito |
|---|---|---|
| `exp#1_Test#2_V1_00` | R −7, 2 −7, 3 −7 · b2/b3 **vuote** | **perfetto** |
| `exp#1_Test#3_V1_00` | R −7, 2 −7, 3 −7 · b2/b3 = **−2** | sporco |
| `exp#1_Test#1_V1_00` | R −7, 2 −1, 3 −7 · b2/b3 vuote | sporco |
| `exp#1_Maj_V1_00` | tutti i gradi pieni e diversi | sporco |

L'osservazione dell'utente era già la diagnosi: *"l'armonizzazione ha un attacco perfetto solo
se non cambiano gli offset tra una nota e l'altra **e anche in tutte quelle intermedie**"*.
Test#2 e Test#3 producono lo stesso voicing su tutti i gradi realmente eseguiti (R, 2, 3 →
−7): se suonano diversi, qualcuno sta leggendo i gradi che la melodia non tocca.

---

## Misura A — sugli export reali, senza scrivere codice

`real_export_probe` ha già una traccia f0 per frame da 10 ms. Passandogli la **REF** nello
slot "dry" e l'export del plugin nello slot "wet" si legge l'escursione d'intonazione dentro
l'attacco. *(Solo la sezione `TRACCIA FINE` è significativa in questo uso: le §1–§5 presumono
dry↔wet e una predizione da tabella. Se qualcuno rilegge quegli output, li ignori.)*

**Test#3, transizione C4→D4** — target ~196 Hz:

| t | REF | plugin | periodicità wet |
|---|---|---|---|
| 2.030 | 198.7 | 200.0 | 0.965 |
| **2.040–2.070** | ~196 | **262.8 / 264.2** | **0.48–0.76** |
| 2.090 | 195.6 | 196.6 | 0.999 |

262.8 Hz = D4 (293.7) trasposto di **−2 semitoni**: esattamente la cella b2/b3 di Test#3.
Durata ~30 ms, a RMS 0.10–0.12, cioè al livello pieno dell'attacco.

**Test#1, discesa E4→C4** — target ~175 Hz: dal 6.030 al 6.080 il wet sta a **248–254 Hz**,
cioè C4 trasposto di **−1**, la cella del grado 2 di Test#1. ~50 ms, a RMS 0.15–0.17.

**Test#2, stessa transizione (controllo)**: nessuna escursione, f0 monotona verso il target,
periodicità sempre ≥ 0.95 tranne un frame a 0.654 a livello bassissimo.

---

## Misura B — il meccanismo, con i moduli veri

Nuova sonda `tests/degree_trace_probe.cpp` (+ target CMake, **non** in `ctest`). Fa girare
`PitchDetector`, `OnsetDetector`, `PitchLatch`, `degreeOf` e `stepEmptyCellHold` — **nessun
`Voice`, nessun `PsolaShifter`**: serve a dire se il difetto nasce prima della sintesi o
dentro. La tabella si passa da CLI. Il numero che conta è quante **corse distinte di offset
applicato** esistono nel file: 4 note dovrebbero darne 4.

Pre-fix, tabella Test#3, block 1024:

```
0.0697  2.0666  1996.9ms   -7   R
2.0666  2.0898    23.2ms   -2   b2      <- un blocco esatto
2.0898  4.0867  1996.9ms   -7   2
4.0867  4.1099    23.2ms   -2   b3      <- un blocco esatto
4.1099  6.0604  1950.5ms   -7   3
6.0604  6.0836    23.2ms   -2   b3   \
6.0836  6.1068    23.2ms   -7   2     >  la spazzata completa E->C
6.1068  6.1301    23.2ms   -2   b2   /
6.1301  7.9877  1857.6ms   -7   R
```

**10 corse invece di 4**, 5 di passaggio per 116.1 ms complessivi, ciascuna lunga
**esattamente un blocco**. Test#2: **2 corse, 0 di passaggio** — il caso che l'utente giudica
perfetto. Test#1: 6 corse, 1 di passaggio da 46.4 ms sul grado 2 = −1, e coincide col 248 Hz
misurato nell'export reale fra 6.030 e 6.080.

Spazzando il block size, il **numero** di corse spurie resta 5 e la **durata** scala:
20.3 ms a 128, 29.0 a 256, 58.0 a 512, 116.1 a 1024, 464.4 a 4096. Firma inequivocabile di
una soglia contata in blocchi invece che in tempo.

---

## La causa

`PitchLatch::update()` si spostava di **un semitono per chiamata**, ed è chiamata **una volta
per blocco** (`PluginProcessor.cpp`). Un salto C→E non arrivava in un colpo: l'aggancio
passava per C#, D, D#, un blocco ciascuno. Ad ogni passo `HarmonyEngine::getOffsets` leggeva
la colonna di quel grado e `PhraseScheduler` la applicava come target reale sulla frase viva.
Con Glide 0 ms ogni gradino è un salto d'intonazione istantaneo.

Il passo incrementale era stato scritto in s.11 per un motivo corretto — un passo
*incondizionato* rimbalza durante uno scivolamento lento — e il bloccaggio all'arrotondamento
risolveva **quel** caso. Il prezzo non era stato visto: su un salto vero, ogni passo
intermedio è una colonna diversa del preset, suonata davvero.

**La traccia fine a 128 campioni dice che la stima del rilevatore NON attraversa**: salta
pulita da 59.969 a 62.233 (C→D) e da 61.978 a 64.232 (D→E). La spazzata era fabbricata
interamente dentro `PitchLatch`.

Un secondo fenomeno, solo sulla terza transizione: appena riacquistata confidenza dopo il
transiente, il rilevatore riporta **60.696** — 70 cent crescente, che arrotonda a 61 = b2 —
per **14.5 ms**, prima di assestarsi su 60.4. Il salto diretto da solo adotterebbe quel 61.
È questo, e solo questo, che giustifica un'attesa.

---

## Il fix

Un solo file di produzione: `src/harmony/PitchLatch.h`, più la riga di chiamata in
`PluginProcessor.cpp` e il `prepare()` in `prepareToPlay`. **Il motore non è stato toccato**
(vincolo esplicito dell'utente: *"non è mia intenzione rovinare il motore PSOLA, sono
contento del timbro ora"*). `PsolaShifter`, `Voice`, `PhraseScheduler`: invariati.

Passo di un semitono → **candidato + adozione diretta**. Oltre i ±25 cent, e solo se
l'arrotondamento è davvero diverso dalla nota agganciata, quell'arrotondamento diventa
candidato; dopo `kNoteSettleMs` = 25 ms di candidato costante viene adottato di colpo, per
quanto lontano sia. La soglia è in **millisecondi contati sui campioni del blocco**, non in
numero di chiamate.

`kNoteSettleMs = 25` non è un numero di comodo: dev'essere più lungo dei 14.5 ms di stima
sbagliata ma confidente misurati sopra, con margine. **Non è passato per l'ascolto.**

Il rimbalzo di s.11 non torna: adottando esattamente l'arrotondamento della stima, lo scarto
residuo è ≤ mezzo semitono per costruzione, non c'è overshoot da disfare. Durante uno
scivolamento lento il candidato è comunque sempre il semitono adiacente.

---

## Post-fix

Corse di passaggio: **0**, su tutte e quattro le tabelle (Test#1/#2/#3 e Maj) e a **tutti** i
block size 128/256/512/1024/4096. Le corse tornano 4 su 4 note.

Il costo in ritardo è più basso del previsto. A **1024** (il buffer dell'utente) l'offset di
destinazione viene adottato allo **stesso blocco di prima** sulle salite (2.0898), e sulla
discesa E→C addirittura **un blocco prima** (6.1068 contro 6.1301), perché i tre blocchi di
spazzata sparisco. A **128** la discesa arriva ~24 ms più tardi — è l'attesa — ma pulita.

```
transizione E->C, block 1024, post-fix:
  6.0372  midi 63.988  held 64  ->  -7
  6.0604  midi 60.696  held 64  ->  -7     <- la stima sbagliata non viene adottata
  6.0836  midi 60.397  held 64  ->  -7
  6.1068  midi 60.224  held 60  ->  -7     <- 64 -> 60 diretto, nessun 63/62/61
```

---

## Test

`tests/pitch_latch_test.cpp`. I TEST 1–6 e 8 restano, col nuovo argomento `numSamples`.

**TEST 7 riscritto di proposito** (`CLAUDE.md` regola 13). Asseriva *"esattamente 5 passi da
60 a 65, un semitono a chiamata"*: non era sbagliato per distrazione, era la specifica di
allora messa per iscritto — ed è esattamente il difetto. Ora asserisce l'opposto: da 60 a 65
in un colpo, e a blocchi corti non restituisce mai 61/62/63/64.

Tre gruppi nuovi:
- **TEST 9** — salti da −12 a +12 semitoni: mai un valore diverso da partenza o arrivo.
- **TEST 10** — stessa traiettoria in *tempo* servita a 128/512/1024/4096: sequenza
  identica (60 → 64, nessun intermedio) a ogni block size.
- **TEST 11** — il caso E→C misurato: 14.5 ms a 60.696 non spostano l'aggancio da 64; appena
  la stima si assesta su 60.4 va a 60, mai a 61.

**Il primo limite del TEST 10 era sbagliato, non il codice.** Asseriva l'adozione entro
"attesa + un blocco": a 1024 i 25 ms non entrano in un blocco da 23.2 e ne servono due
(55.4 ms misurati). Corretto in due limiti diversi fra loro di proposito — in basso l'attesa,
garantita a qualunque buffer; in alto l'attesa **arrotondata per eccesso a blocchi interi**
più un blocco, che è la quantizzazione al blocco (A-05) che questo lavoro non toglie.

---

## Verifica

- `ctest -C Release`: **8/8**. `psola` e `voice` verdi **identiche** — la prova per calcolo
  che il motore non è stato disturbato.
- `pluginval --strictness-level 10` su VST3 (Win): **SUCCESS**.
- Standalone ricompilato (regola 11). Il fallimento della copia in `Program Files` è atteso
  (D-12); `C:\Program Files\Common Files\VST3\` è **vuota**, quindi Ableton legge da
  `build/Harmonizer_artefacts/Release/VST3/`, che è aggiornata.
- `sample_click_finder` adeguato alla nuova firma in 6 punti (+ 5 `prepare`): una sonda che
  non usa lo stesso `PitchLatch` del plugin misurerebbe un altro programma.

---

## Cosa resta

1. **La conferma all'ascolto** (regola 12). Riesportare `exp#1_*_V1_01`. Predizione
   falsificabile: `Test#3_01` ≈ `Test#2_00`, e Test#1/Maj con un solo gradino netto.
2. `kNoteSettleMs = 25` **non è tarato all'ascolto**, solo misurato.
3. La quantizzazione al blocco resta (A-05, deliberatamente fuori scope: è il cuore del
   motore).
4. Trovato per strada, non toccato: dentro il bundle VST3 c'è un
   `Contents/x86_64-win/Harmonizer (1).vst3` del 30/07, residuo di una copia vecchia.
   Probabilmente inerte, ma è sporcizia nell'artefatto.
