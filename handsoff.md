# Handoff — HARMONIZER

> Ultimo aggiornamento: 2026-08-02 (sessione 12)

---

## 1. Goal

Costruire **HARMONIZER**: un plugin armonizzatore (VST3 / AU / Standalone) per strumenti monofonici — sax, tromba, voce — pensato per il palco.

Invece di trasporre a intervalli fissi, calcola la relazione tra la nota suonata e l'accordo corrente:

```
d = (notaMIDI − fondamentale) mod 12
```

e usa `d` per leggere un offset (in semitoni, per ognuna delle 8 voci) da una tabella 12×8 editabile dall'utente (**preset armonico**). L'armonia è dato dell'utente, non teoria imposta dal codice.

Punti chiave del prodotto (dettagli completi in `PRD-Harmonizer-v1.md`):
- Priorità assoluta alla reattività: latenza ≤ 15 ms nella modalità più rapida.
- Fix/Move per voce: le voci possono seguire (Move) o ignorare (Fix) vibrato/bending dell'esecutore.
- Modalità Play: armonizzazione pilotata da note MIDI in ingresso invece che dalla tabella.
- Motore di frasi/pattern ritmico (architettura obbligatoria in v1.0, UI in v1.1).
- Correzione formanti automatica in funzione dello shift.
- Controllo hardware via 3 CC configurabili, con CC del preset = posizione in lista (posizionale, non ID).
- Stack: JUCE 8.x, C++20, CMake (JUCE CMake API), Cycfi Q (pitch detection), PSOLA proprietario dietro interfaccia astratta, Signalsmith Stretch come motore alternativo.

Fonte di verità: `PRD-Harmonizer-v1.md` (v1.0, luglio 2026). In caso di conflitto tra questo file e il PRD, vince il PRD.

---

## 2. Stato attuale

**Fase: M0 completo dal punto di vista tecnico (restano solo licenza JUCE, certificati, nome prodotto — decisioni non tecniche, vedi §6). Vertical slice DSP M1/M2/M3 in corso su richiesta esplicita dell'utente: PresetLibrary (M2), Fix/Move+Glide+Stability (M1) e motore a frasi (M3, FR-43..53) sono completi e funzionali. Sessione 9: il PSOLA proprietario scoperto in sessione 8 e' stato PORTATO E INTEGRATO come motore di default dietro `PitchShifter`. Sessione 10: PRIMO TEST REALE in Ableton di tutto il lavoro di sessione 9 (PSOLA, Formanti, CC, Play) — trovato e corretto un bug reale nel ciclo di vita delle frasi, due bug di UI, aggiunta diagnostica. Sessione 11: canto legato non aggiornava l'armonizzazione — due bug distinti, non uno: isteresi di intonazione mancante (identificato dall'utente, corretto) E `freeAllPhrases()` innescato dalla confidenza del pitch invece che dalla presenza del segnale (la mia ipotesi originale, rivelatasi comunque necessaria dopo il primo fix). Sessione 12: causa delle "note saltate senza una logica precisa" (segnalata a fine sessione 11) confermata a lettura di codice — corsa fra `OnsetDetector` e `PitchDetector`, con una seconda causa concorrente (`pitchDetector` mai resettato al silenzio) — vedi sotto. **CONFERMATO ALL'ASCOLTO dall'utente**: "Active" non resta piu' a zero, armonizza sempre tutte le note, nessuna persa per strada. Sessione 12 (continuazione) — feedback utente sul timbro ("non fedele al segnale sorgente, robotico e granuloso"): trovato e corretto un bug reale in `PsolaShifter::emitGrain` (la correzione formantica automatica di `Voice.cpp`, attiva di default, accorciava i grani di sintesi sotto il minimo necessario alla sovrapposizione) — confermato con un nuovo test numerico (Test 8) che falliva PRIMA del fix e passa dopo, mentre tutti i test preesistenti restano bit-per-bit invariati. Sessione 12 (continuazione) — utente riporta "scricchiolii, click, glitch" e armonizzazione "non stabile al 100%": trovato un bug architetturale — QUALUNQUE voce smetteva di essere processata (fine frase, silenzio totale, cella tornata vuota su una frase viva/FR-17, uscita da Play mode) veniva tagliata di ampiezza piena a zero in un solo blocco, senza dissolvenza. Aggiunta una breve dissolvenza di ampiezza (8ms) per ogni voce, con un rilascio "morbido" invece che istantaneo per le frasi; stesso trattamento per il gain dry/wet/bypass (anch'esso applicato prima come salto istantaneo). NON ANCORA CONFERMATO ALL'ASCOLTO.**

**Novita' sessione 12 — note saltate: corsa fra onset e rilevamento di pitch (FR-43/45/46):**

A fine sessione 11 l'utente aveva segnalato, senza altro dettaglio, che il plugin "continua a saltare alcune note ma senza una logica troppo precisa" (oltre a un problema di timbro separato, vedi sotto). Diagnosticato leggendo il codice, non ipotizzato — la stessa corsa onset/pitch gia' sospettata (mai confermata) per i punti 2/5 del test di sessione 10.

- **Causa 1 (principale)**: `PluginProcessor.cpp` calcola `offsets` (la tabella di offset per voce) solo dentro `if (! playModeEnabled && inputIsStable)`; se il pitch non e' ancora confidente in quel blocco, l'array resta quello di default — 8 celle vuote. `PhraseScheduler::triggerNewPhrase` (chiamato da `PhraseScheduler::process` sul ramo onset) salta l'allocazione dello slot per ogni cella vuota: con offsets tutti vuoti la frase nasce **attiva ma con zero slot fisici**, muta per sempre — il ramo di live-update (FR-17) aggiornava solo `frozenOffsets`, mai gli slot mancanti. Chi vince la corsa: `OnsetDetector` apre il gate in ~10ms su soglia di livello, mentre `cycfi::q::pitch_detector` (BACF, minimo 60Hz) ha bisogno di piu' periodi per dare confidenza — al primo attacco dopo un silenzio l'onset arriva quasi sempre prima.
- **Causa 2 (concorrente)**: `pitchDetector.reset()` non veniva mai chiamato dopo `prepareToPlay`. Nota e confidenza dell'ultima stima sopravvivevano al silenzio finche' il rilevatore non ne calcolava una nuova da solo: un onset poteva quindi trovare `hasStableSignal()` gia' vero ma su una nota STANTIA (quella precedente) — la frase nasceva armonizzata sull'accordo sbagliato invece che in attesa del pitch vero. Spiega il "senza una logica precisa": a seconda di chi vince la corsa, la stessa gestualita' produceva una nota muta, una nota sull'accordo sbagliato, o una nota corretta.
- **`src/voices/PhraseScheduler.{h,cpp}`**: nel ramo di live-update (`inputIsStable`), oltre ad aggiornare `frozenOffsets` come prima, ora si completa l'allocazione degli slot rimasti vuoti al trigger (`slotIndices[v] < 0` ma la cella ha un valore), riusando `allocateFreeSlot()` cosi' com'e'. Nessun timeout, nessuno stato nuovo: se il pitch non arriva mai la frase resta a zero slot e si libera normalmente alla chiusura del gate. Il caso gia' funzionante (onset con pitch gia' confidente) non cambia: il ramo `onsetDetectedThisBlock` ha ancora precedenza nell'`else if`. Nuovo contatore cumulativo `numLateBindingsTotal` (atomico, `getNumLateBindings()`) per rendere l'intervento osservabile.
- **`src/PluginProcessor.{h,cpp}`**: nuovo `signalPresentLastBlock` (solo audio thread); sul fronte di DISCESA di `signalPresent` si chiama anche `pitchDetector.reset()`, accanto al gia' presente `pitchLatch.reset()`. Verificato RT-safe leggendo `PitchDetector::reset()` (due assegnazioni float + `cycfi::q::pitch_detector::reset()`, che e' solo `_frequency = 0.0f` — nessuna allocazione/lock). Ordine importante: questo fix da solo, senza il punto precedente, avrebbe reso `inputIsStable` falso ad ogni attacco (peggiorativo) — i due fix vanno insieme.
- **Diagnostica**: nuovo atomico `lastGateOpen` (stato del gate, distinto da `lastInputStable` che riflette la confidenza del pitch) esposto via `getLastGateOpen()`. La label "Detected" in `PluginEditor.cpp` ora mostra anche `gate open/closed` e `late-bindings N` — permette di confermare all'ascolto che il fix interviene davvero (contatore che sale) invece di limitarsi a sperare che compili (CLAUDE.md regola 12).
- **Non toccato**: `PitchDetector`, `OnsetDetector`, `Voice.cpp`, `PsolaShifter`, `HarmonyEngine`, `PitchLatch` — nessuno di questi era in causa.
- **Verificato**: build VST3 e Standalone riuscite (solo il consueto fallimento di copia post-build per permessi, atteso e documentato dalla sessione 4), `pluginval --strictness-level 10` **SUCCESS** (exit code 0, nessuna occorrenza di fail/error/crash nel log completo), tutte e 3 le suite verdi via `ctest` (`psola_test`, `override_manager_test`, `pitch_latch_test` — nessuna tocca `PhraseScheduler`, riverificate per scrupolo dato che questo e' proprio il file modificato).
- **NON ancora verificato all'ascolto** (CLAUDE.md regola 12): l'utente deve suonare note staccate ripetute in Ableton e controllare che (a) nessuna venga saltata, (b) "Active" non resti a 0 sul primo attacco, (c) `late-bindings` salga davvero, (d) nessuna nota venga armonizzata sull'accordo della nota precedente. Solo dopo questa conferma il fix puo' considerarsi completo, non solo compilato.

**Novita' sessione 12 (continuazione) — timbro "robotico e granuloso": bug reale trovato e corretto in `PsolaShifter::emitGrain`:**

Dopo la conferma all'ascolto del fix precedente, l'utente ha chiesto di passare al feedback di qualita' lasciato in sospeso: "non fedele al segnale sorgente, robotico e granuloso". Dei tre candidati annotati a fine sessione precedente (vedi sotto la versione originale di questa nota), il primo si e' rivelato un bug reale, verificato per calcolo PRIMA di toccare il codice e poi confermato empiricamente con un test scritto apposta.

- **Diagnosi**: la garanzia di sovrapposizione dei grani in `emitGrain` (`2W >= synthPeriod`, commento originale nel codice) e' derivata assumendo implicitamente `beta = 1` — `Lg = 2W` nella derivazione. Il codice pero' calcola `Lg = round(2W/beta)`, per la correzione formantica (FR-39/41), senza mai ricompensare la sovrapposizione quando `beta != 1`. `Voice.cpp` applica la correzione formantica automatica **di default** (`formantSpread = 1.0`, non un caso limite): qualunque voce shiftata verso il basso ha `beta > 1` (schiarimento) in condizioni normali. Calcolo a mano: a -12 semitoni (il confine esatto dove il margine originale e' gia' teso al limite) con `beta` reale prodotto da `Voice.cpp` (k=0.3) il grano risulta ~19% piu' corto del minimo gia' provato appena sufficiente — a scostamenti maggiori (-14.5/-17 semitoni, dove sia `alpha` cala sia `beta` cresce) il deficit peggiora.
- **Verifica PRIMA del fix (coerente con l'abitudine di sessione 11 — scrivere il test che fallisce prima di correggere)**: aggiunto **Test 8** a `tests/psola_test.cpp`, stessa misura del Test 6 (RMS a breve termine minimo/medio) ma con la `beta` REALE che `Voice.cpp` produrrebbe (stessa formula, k=0.3, spread=1.0) invece di `beta=1` fisso. Eseguito sul codice non ancora corretto: **FALLITO su -14.5 e -17 semitoni** (ratio 0.222 e 0.146, sotto la soglia 0.25) — il bug non era solo teorico, era misurabile.
- **Fix**: aggiunto un floor a `Lg` basato su `synthPeriod` (indipendente da `beta`), invece di allargare `W` (la semiampiezza in campioni SORGENTE). Scelta deliberata: allargare `W` invece del floor su `Lg` avrebbe fatto piu' contenuto sorgente entrare in ogni grano, rischiando di reintrodurre esattamente l'artefatto di ottava del Test 1 (sessione 9) che l'attuale margine minimo su `W` evita di proposito. Il floor su `Lg` agisce solo nel dominio di uscita, dove serve davvero (far toccare grani consecutivi), senza toccare quanto segnale sorgente ciascun grano contiene.
- **Verifica DOPO il fix**: Test 8 ora passa (ratio 0.540/0.439/0.379). **Test 1 e Test 6 (il caso `beta=1` gia' validato) restano numericamente identici, cifra per cifra**, a prima del fix — conferma per calcolo (non solo per assenza di regressioni nei test) che il floor e' un no-op quando `beta=1`, esattamente come previsto analiticamente prima di scrivere la modifica.
- **`src/dsp/PsolaShifter.cpp`**: `emitGrain`, floor su `Lg` (vedi sopra).
- **`tests/psola_test.cpp`**: nuovo Test 8. Nota nel codice: la formula di `beta` e' una copia intenzionale di quella in `Voice.cpp` — se `k` cambia la', va aggiornato anche qui.
- **Non toccato**: `Voice.cpp`, `PitchDetector`, `OnsetDetector`, `PhraseScheduler`, `HarmonyEngine` — nessuno di questi era in causa per questo bug specifico.
- **Verificato**: build VST3 e Standalone riuscite, tutte e 3 le suite verdi via `ctest`, `pluginval --strictness-level 10` SUCCESS (exit code 0, log completo senza fail/error/crash).
- **NON ancora verificato all'ascolto** (regola 12): questo e' un fix di un bug numericamente reale e misurabile, ma non e' garanzia che risolva TUTTO il feedback "robotico e granuloso" — restano gli altri due candidati sotto, non ancora esplorati.

**Rimangono FUORI SCOPE — altri due candidati per il timbro, non ancora esplorati:**
1. Individuazione degli epoch come massimo di `|x|` in una finestra ±P/4 (`detectEpochs`): su segnali non impulsivi (synth, fiati) puo' posizionare male gli epoch, incoerenza di fase fra grani = "robotico".
2. Otto istanze PSOLA indipendenti sullo stesso ingresso: artefatti correlati che si sommano invece di mediarsi.

**Novita' sessione 12 (continuazione) — "scricchiolii, click, glitch" e armonizzazione "non stabile al 100%": bug architetturale trovato e corretto, dissolvenza di ampiezza per le voci:**

L'utente ha riportato (dopo aver confermato all'ascolto il fix dell'inviluppo) due sintomi distinti: click/scricchiolii ricorrenti, e un'armonizzazione che "suona sempre" ma non sembra stabile al 100% (senza sapere se dovuto a formanti, ottave riarmonizzate o altro). Diagnosticato leggendo `PhraseScheduler.cpp`, `PlayModeInput.cpp` e `PluginProcessor.cpp` insieme (non isolatamente): un'unica causa architetturale spiega entrambi i sintomi.

- **Causa**: ogni volta che una voce smetteva di essere processata, la sua ampiezza passava da piena a zero in UN SOLO BLOCCO, senza alcuna dissolvenza — un salto di ampiezza discontinuo, la definizione stessa di un click. Questo succedeva in QUATTRO punti distinti, tutti routine, non casi limite:
  1. **`PhraseScheduler::process`, silenzio totale** (`freeAllPhrases()`): TUTTE le frasi attive, con tutte le loro voci potenzialmente a piena ampiezza, smettevano di essere processate nello stesso blocco — un taglio multiplo simultaneo a ogni fine-frase/nota.
  2. **`PhraseScheduler::process`, nuovo onset con MENO voci della frase precedente** (voicing con un numero diverso di celle non vuote): le voci della vecchia frase non reclamate dalla nuova smettevano di essere processate, mentre quelle reclamate continuavano con un semplice glide di intonazione (questo caso, quando il conteggio voci NON cambia, era gia' corretto — nessun click li').
  3. **`PhraseScheduler::process`, cella diventata vuota su una frase ANCORA viva** (FR-17, cambio accordo su nota tenuta): `voice.setMuted(true); continue;` — nessuna chiamata a `processAdd`, taglio istantaneo nel bel mezzo di una nota sostenuta. Probabile causa diretta della sensazione di "non stabile al 100%" segnalata dall'utente, perche' e' esattamente lo scenario "modifiche in corso d'opera" che sospettava.
  4. **`PlayModeInput::process`**: stesso pattern, sia su note-off (`slotNote` torna -1) sia sull'uscita dalla modalita' Play (`!modeActive`, ritorno immediato senza toccare nessuna voce).
- **Fix — dissolvenza di ampiezza per voce (`Voice.h/.cpp`)**: nuovo `Glide ampGlide`, rampa fissa a 8ms (`kDeclickMs`, costante DELIBERATAMENTE indipendente dal `glideTimeMs` musicale di FR-17 — quello e' scelto dall'utente per il movimento armonico, questo e' un tempo anti-click fisso e breve). `setMuted(bool)` non azzera piu' di netto: avvia/ferma la rampa. `processAdd` non esce piu' subito se `muted`, esce solo se `isSilent()` (muted E rampa assestata a zero) — nel mezzo, continua a processare e a moltiplicare l'uscita per l'ampiezza corrente della rampa. Una voce completamente inattiva da tempo non consuma comunque CPU inutile (esce al primo controllo).
- **Fix — rilascio morbido delle frasi (`Phrase.h`, `PhraseScheduler.{h,cpp}`)**: nuovo flag `Phrase::releasing`. La vecchia `freePhrase()` (azzeramento istantaneo) e' rinominata `hardFreePhrase()` e usata SOLO per il furto d'emergenza (FR-52, pool esaurito — li' serve lo slot fisico SUBITO, la continuita' la da' gia' il Glide dell'offset sul nuovo target, nessun click). Il caso normale (fine frase, silenzio, superata da un nuovo onset) usa la nuova `beginRelease()`: la frase resta `active=true` (protetta da riuso) ma tutte le sue voci sfumano; una volta tutte silenziose (`Voice::isSilent()`), la frase si libera per davvero. `allocateFreeSlot()` preferisce rubare una frase gia' in rilascio (meno disruptivo) prima di rubare una frase ancora piena.
- **Fix — FR-17, cella tornata vuota su frase viva**: stesso principio applicato per singola voce dentro il ciclo di mixing (non serve `releasing` sulla frase intera, la frase resta viva, solo quella colonna sfuma).
- **Fix — `PlayModeInput.cpp`**: stesso pattern per note-off e per l'uscita da Play mode (prima: ritorno immediato senza toccare le voci; ora: un ciclo che le muta/sfuma prima di uscire).
- **Fix — dry/wet/bypass (`PluginProcessor.{h,cpp}`)**: `dryLevel`/`wetLevel`/il fronte di `bypass` venivano letti come valore grezzo e applicati di netto per l'intero blocco — un secondo, indipendente meccanismo di click (automazione, CC bypass, il bottone Bypass stesso). Nuovi `dryGlide`/`wetGlide` (stessa rampa fissa 8ms), calcolati una sola volta per blocco (`Glide::process` muta stato interno) prima del ciclo sui canali.
- **`src/dsp/Glide.h`**: nuovo `isSettled() const` (verifica se la rampa ha raggiunto il target senza confronti in virgola mobile fragili dall'esterno).
- **Non toccato**: `PsolaShifter`, `PitchDetector`, `OnsetDetector`, `HarmonyEngine`, `PitchLatch`, `VoicePool` (solo consumato, non modificato) — nessuno di questi centrava con questo bug.
- **Verificato**: build VST3 e Standalone riuscite, tutte e 3 le suite verdi via `ctest` (nessuna tocca questo codice, ma rieseguite per scrupolo), `pluginval --strictness-level 10` SUCCESS (exit code 0, log completo senza fail/error/crash).
- **NON ancora verificato all'ascolto** (regola 12): questo e' un fix architetturale di un meccanismo confermato per lettura di codice (un salto di ampiezza discontinuo E' un click, per definizione — non serve l'ascolto per stabilirlo), ma la durata scelta (8ms) e' un punto di partenza ragionevole, non tarato; e non e' garanzia di risolvere l'INTERO feedback "non stabile al 100%" (restano da escludere formanti/PSOLA come contributo residuo, vedi sopra).
- **Limite noto accettato consapevolmente**: quando una frase `releasing` viene rubata (`allocateFreeSlot`) prima che la sua dissolvenza finisca, la rampa si interrompe bruscamente a meta' — comunque un salto MOLTO piu' piccolo (dalla poca ampiezza residua invece che da piena scala), non un problema nuovo introdotto, un compromesso deliberato per non complicare ulteriormente FR-52 in questa sessione.

**Novita' sessione 11 — isteresi di intonazione per note legate (FR-16/17):**

Dopo le correzioni di sessione 10, l'utente ha ripreso il test: con un sample Rhodes tutto funziona, ma cantando note legate (C→D→E, senza stacco netto) solo la prima nota si armonizzava. Diagnosi iniziale mia (instabilita' del rilevatore -> `freeAllPhrases()` durante lo scivolamento fra due note) — **corretta dall'utente**: ha riconosciuto che la causa reale e' l'assenza di isteresi sulla nota usata per il lookup nella tabella armonica (`quantizedPlayedNote`, un semplice `juce::roundToInt` ricalcolato da zero ogni blocco), e ha specificato lui stesso il comportamento voluto con tolleranza numerica precisa (±25 cent) e verificato a mano che la matematica di Fix/Move in `Voice.cpp` fosse gia' corretta (lo era).

- **`src/harmony/PitchLatch.h`** (nuovo, header-only come `Glide.h`, nessuna dipendenza JUCE come `OverrideManager`): isteresi con aggancio/sgancio a gradino. Entro ±25 cent dalla nota agganciata non cambia nulla; oltre la soglia si sposta di un semitono per volta verso la nota piu' vicina alla stima corrente — **bloccato (min/max) al risultato di un arrotondamento standard**, non un passo incondizionato.
- **Bug trovato e corretto nel MIO stesso primo tentativo, prima di consegnarlo**: un passo incondizionato (`heldNote += 1` ogni volta che si supera la soglia) rimbalza avanti e indietro ad ogni blocco durante uno scivolamento lento — il salto di un intero semitono supera quasi sempre la stima attuale del pitch, quindi la nuova nota agganciata risulta "oltre soglia" nella direzione opposta e la chiamata successiva la disfa immediatamente. Scoperto scrivendo il test 4 (verifica esplicita "nessun rimbalzo") **prima** di integrare nel plugin, non dopo — coerente con CLAUDE.md regola 8 (verificare prima di dichiarare fatto) e con l'abitudine di questa sessione di validare ogni pezzo isolatamente. Risolto bloccando lo scatto al risultato di un arrotondamento standard di `continuousMidiNote` (`std::min`/`std::max`), che non puo' mai superare né disfare la nota di destinazione.
- **`tests/pitch_latch_test.cpp`** (nuovo): 8 gruppi di verifiche — primo aggancio, tolleranza ±25 cent, scatto a gradino nelle due direzioni, assenza di rimbalzo (il test che ha scoperto il bug sopra), `onAttack` forza l'aggancio immediato, `reset()`, un salto ampio che si risolve un semitono a chiamata, e la replica esatta dell'esempio dell'utente (vibrato ±50 cent su C alterna B/C/C#). Un solo intoppo, non nel design ma nel test: `std::lround` a un pareggio esatto di mezzo semitono arrotonda sempre "lontano da zero" (quindi verso l'alto anche scendendo) — corretto usando −51 cent invece di −50 esatti nel test, un pareggio che con dati di pitch reali non si presenta mai.
- **`PluginProcessor.cpp`**: `quantizedPlayedNote = juce::roundToInt(continuousInputMidiNote)` sostituita con `pitchLatch.update(continuousInputMidiNote, onsetDetectedThisBlock)` — `onsetDetectedThisBlock` (un vero attacco) forza l'aggancio immediato senza isteresi, gia' disponibile a quel punto del metodo. `pitchLatch.reset()` nel ramo "segnale non stabile".

**Novita' sessione 11 (continuazione) — secondo bug: `freeAllPhrases()` legato alla confidenza del pitch invece che alla presenza del segnale:**

Dopo il fix dell'isteresi, l'utente ha riprovato: la label "Detected" ora segue correttamente C→D→E (confermando che l'isteresi funziona), ma l'armonizzazione continuava ad aggiornarsi solo sulla prima nota. Questo ha riportato in gioco la MIA ipotesi originale di sessione 11 (poi accantonata quando l'utente aveva indicato l'isteresi come causa) — **si e' rivelata comunque necessaria**, come causa distinta e concorrente, non alternativa.

- **Causa**: `PhraseScheduler::process()` liberava tutte le frasi (`freeAllPhrases()`) quando `!inputIsStable` — cioe' quando la CONFIDENZA del rilevatore di pitch scende sotto soglia. Durante uno scivolamento di intonazione fra due note cantate legato la confidenza puo' calare per pochi blocchi anche se il performer sta chiaramente ancora suonando (la periodicita' e' meno stabile mentre il pitch si muove) — abbastanza per svuotare tutto lo stato armonico. Senza un onset a ribattere (canto legato, per definizione), nulla lo ricostruisce: l'armonizzazione resta muta da li' in poi.
- **Fix, questa volta SENZA una finestra temporale** (esplicitamente scartata dall'utente in questa stessa sessione per l'ipotesi precedente): distinti due segnali che prima erano confusi in uno solo.
  - `signalPresent` (nuovo, da `onsetDetector.isGateOpen()` — il gate a livello di segnale, gia' esistente, non pitch-detection): "il performer sta ancora suonando qualcosa" — governa SOLO la liberazione delle frasi.
  - `inputIsStable` (invariato, da `PitchDetector::hasStableSignal()`): "il pitch di QUESTO blocco e' abbastanza confidente da fidarsene" — governa SOLO l'aggiornamento dal vivo degli offset (FR-17).
  - Durante un calo di confidenza transitorio con segnale ancora presente: non si libera nulla (il gate resta aperto) e non si aggiornano gli offset con un valore inaffidabile — si mantiene l'ultimo voicing valido, semplicemente non facendo nulla quel blocco. Nessuna soglia temporale, nessun ritardo di reazione aggiunto.
- **`PhraseScheduler::process()`** guadagna un parametro (`signalPresent`, distinto da `inputIsStable`); il ramo altrimenti "libera tutto" ora testa `!signalPresent`, il ramo di live-update (FR-17) resta condizionato a `inputIsStable`, con un terzo caso esplicito (segnale presente, nessun onset, pitch non confidente) che non tocca nulla.
- **`PluginProcessor.cpp`**: nuova variabile `signalPresent`; `pitchLatch.reset()` ora legato a `!signalPresent` invece che a `!inputIsStable` (un calo di confidenza transitorio non deve piu' azzerare l'aggancio dell'isteresi); in modalita' Play, sia `signalPresent` sia `inputIsStable` vengono forzati a `false` per la catena Harmonizer (stesso principio di sessione 9, esteso al nuovo segnale).
- **Non toccato**: `PitchDetector`, `OnsetDetector` (il gate esisteva gia', riusato cosi' com'era), `Voice.cpp`, `PsolaShifter`.
- **Verificato**: build VST3/Standalone riuscite, `pluginval --strictness-level 10` verde, tutte e 3 le suite di test ancora verdi (nessuna delle tre tocca `PhraseScheduler`, riverificate per scrupolo).
- **Non ancora verificato all'ascolto**: questo secondo fix, appena scritto — richiede un altro giro di test in Ableton sullo stesso C→D→E legato.
- **Scartata esplicitamente una finestra temporale di tolleranza (150ms)** proposta inizialmente da me per un'ipotesi diversa (instabilita' del rilevatore): l'utente ha correttamente osservato che, anche non toccando la latenza dichiarata del motore, avrebbe comunque introdotto un ritardo di reazione percepibile a fine frase. Non implementata; l'ipotesi "instabilita' -> `freeAllPhrases()`" resta non verificata e non toccata in questa sessione.
- **Verificato**: build VST3/Standalone riuscite, `pluginval --strictness-level 10` verde, tutte e 3 le suite di test verdi via CTest (`psola_test`, `override_manager_test`, `pitch_latch_test` nuovo).
- **Non toccato**: `Voice.cpp` (Fix/Move gia' corretti, verificato per calcolo diretto contro gli esempi in Hz dell'utente), `PhraseScheduler.cpp` (il meccanismo di sessione 10 resta invariato), `HarmonyEngine`, `PitchDetector`, `OnsetDetector`.

**Novita' sessione 10 — primo test reale in Ableton, bug nel ciclo di vita delle frasi (FR-45/46), fix UI, diagnostica pitch:**

Prima sessione in cui l'utente ha effettivamente ascoltato/usato in Ableton il lavoro delle sessioni 8-9 (PSOLA, Formanti, CC MIDI, Play — tutto verificato fino a qui solo con test numerici, build e `pluginval`). Ha riportato 8 osservazioni con uno screenshot dell'editor e un file Excel che illustra il comportamento atteso del futuro Pattern Ritmico. Confermato funzionante senza modifiche: Dry/Wet, Voices, Root/Chord/Name, Up/Down, e il cambio di accordo su nota tenuta che si aggiorna dal vivo (FR-17).

- **Bug principale — le frasi superate da un nuovo onset non si liberavano mai** (`PhraseScheduler::process()`, ramo onset): venivano solo "smontate" (`isLive=false`) ma restavano `active` per sempre, finche' non rubate (pool esaurito) o il segnale taceva. Conseguenza osservata dall'utente: ogni frase "smontata" continuava a chiamare `voice.processAdd()` sul segnale live CORRENTE coi propri offset congelati vecchi — cambiare preset tra una nota e l'altra faceva sentire insieme tutti i preset selezionati fino a quel momento, e le voci attive salivano di `numVoices` ad ogni nuovo attacco (osservato: 0→4→8→12). Cambiare Stability "sembrava" resettare tutto, ma non tocca `phrases[]` — quasi certamente solo l'effetto del silenzio naturale mentre si maneggia la UI.
- **Non risolto con un comportamento fisso**: l'utente ha chiarito (con un file Excel che illustra tre note in corsa scaglionata nel tempo, C→E→D, dove la seconda nota della corsa di E taglia la terza nota — ancora "in coda" — della corsa di C) che il caso reale a cui pensa e' il futuro **Pattern Ritmico** (FR-47..49, `[V1.1]`, voci scaglionate nel tempo dentro una frase, non ancora costruito). Li' "tronca la coda non ancora suonata" vs "lasciala finire, la nuova armonizzazione parte dal prossimo suono" e' una scelta creativa legittima, non un bug. Oggi, senza pattern (tutte le voci di una frase partono insieme, nessuna mai "in coda"), le due scelte collassano nello stesso caso limite. **Risolto con un bottone invece di un'interpretazione fissa**: nuovo parametro APVTS `keepPhraseTails` ("Keep Tails" in UI), **default OFF (tronca)** — deciso con l'utente. `PhraseScheduler::setKeepTails(bool)`: quando `false`, le frasi smontate si liberano subito (`freePhrase`) invece di restare vive; quando `true`, comportamento precedente invariato. Sara' lo stesso bottone a guadagnare il pieno significato descritto nell'Excel quando il Pattern Ritmico esistera' — nessun ritocco di plumbing previsto in quel momento. Aggiornato il commento in `Phrase.h` per riflettere questa risoluzione (era ancora "[DECISION] da validare all'ascolto" dalla sessione 7).
- **Due bug di UI, entrambi da lettura diretta del codice, non ipotizzati**: `fixMoveLabel` (dalla sessione 6) e `voiceFormantLabel` (da questa sessione, stesso pattern replicato) venivano aggiunte con `addAndMakeVisible()` ma **mai posizionate** in `resized()` — nessun `attachToComponent()` ne' `setBounds()` esplicito, quindi bounds vuoti/invisibili. Corretto catturando il rettangolo di `row.removeFromLeft(labelWidth)` e assegnandolo alla label invece di scartarlo. Le 8 slider Fmt/Voice (i "puntini" nello screenshot dell'utente) avevano bounds validi — solo troppo piccole (26px, rotary senza text box): ingrandite a 36px di riga già che si toccava quel layout.
- **Titolo dell'editor obsoleto**: `paint()` diceva ancora "motore Signalsmith interinale", mai aggiornato dal cambio a PSOLA in sessione 9 — attivamente fuorviante durante un test. Sostituito con un testo che non richiede sync manuale ad ogni milestone.
- **Diagnostica "nota rilevata" aggiunta** (soddisfa anche un requisito PRD mai implementato, §8.1 "Display della nota rilevata"): nuovi atomici in `PluginProcessor` (`lastDetectedMidiNote`/`lastDetectedConfidence`/`lastInputStable`), popolati ogni blocco da `PitchDetector` (gia' pubblico, nessuna modifica li'), letti dall'editor nel timer 15Hz esistente. Serve a diagnosticare i punti 2 e 5 del test (mancato riconoscimento con certi synth, primo attacco a "0 voci attive"): l'ipotesi piu' probabile e' una corsa fra `OnsetDetector` (apre il gate in pochi ms) e `PitchDetector` (BACF, serve una finestra piu' lunga per agganciare con confidenza) — **non verificabile senza dati reali, quindi NESSUN fix speculativo sulle costanti del rilevatore** (CLAUDE.md regole 12/13, adottate proprio in sessione 9). Deciso esplicitamente con l'utente: solo diagnostica in questa sessione, il fix vero arrivera' dal prossimo giro di test con la nuova label visibile.
- **Verificato**: build VST3 (bloccata una volta da `LNK1104` — Ableton aveva ancora il plugin caricato, stesso caso gia' visto in sessione 6, risolto chiedendo all'utente di chiuderlo) e Standalone riuscite, `pluginval --strictness-level 10` verde su tutte le sezioni, `psola_test` e `override_manager_test` ancora verdi (nessuno dei due tocca questo codice, riverificati per scrupolo).
- **Non toccato**: `PitchDetector`, `OnsetDetector`, `Voice`, `VoicePool`, `PsolaShifter`, `CcRouter`, `OverrideManager`, `PlayModeInput` — nessuno di questi era in causa nella diagnosi.

**Novita' sessione 9 (continuazione) — Modalita' Play (FR-24..28):**

Dopo il controllo MIDI CC, chiesto di nuovo esplicitamente all'utente quale area (Play / Pattern ritmico / Preset timbrici / altro): scelta la modalita' Play.

- **Decisione architetturale centrale**: `PlayModeInput` (`src/midi/PlayModeInput.{h,cpp}`, nuovo) e' deliberatamente SEPARATO da `PhraseScheduler`, non costruito sopra. Il modello e' fondamentalmente diverso: `PhraseScheduler` esiste per "un onset genera una frase con voicing congelato che vive di vita propria" (FR-43/46); in Play mode non c'e' alcun concetto di frase, solo "nota MIDI premuta -> una voce insegue quella nota assoluta finche' non arriva il note-off". Questo E' esattamente la logica gia' esistente per la modalita' Fix di `Voice` (FR-22: bersaglio = `quantizedPlayedNote + offset`): con `quantizedPlayedNote=0` e `offset=notaMidi`, il bersaglio assoluto e' semplicemente la nota MIDI — **riusato cosi' com'era, zero codice nuovo dentro `Voice`**.
- **`VoicePool` dedicato a 8 slot** (non i 32 della catena Harmonizer): le due modalita' sono mutuamente esclusive in ogni istante (FR-24, "la tabella e' completamente disattivata"), quindi non serve condividere/contendere lo stesso pool. Mapping nota->slot: 1:1 in ordine di arrivo; oltre 8 note simultanee, le eccedenti restano mute (FR-25, coerente con "le voci in eccesso restano mute" applicato anche al caso limite non esplicitamente coperto dal PRD).
- **FR-20 si applica anche qui**: senza un ingresso audio stabile (`pitchDetector.hasStableSignal()`) le voci Play tacciono, indipendentemente da quali note MIDI sono premute — la nota resta "premuta" internamente (`slotNote` invariato), solo l'audio si interrompe finche' l'ingresso non torna stabile. **Confermato dalla lettura del PRD, non solo assunto**: il setup di riferimento (§3.4, "stesso schema di Waves Harmony") descrive una traccia MIDI separata che pilota il plugin residente sulla traccia AUDIO — Play non e' un sintetizzatore, richiede comunque una sorgente audio viva da pitchare.
- **`PlayModeInput::process()` viene chiamato SEMPRE**, anche a modalita' spenta (`modeActive=false`): il tracking note-on/off resta aggiornato (riattivare Play non richiede un nuovo note-on) e lo swap di Stability continua ad applicarsi in modo uniforme indipendentemente dalla modalita' corrente — stesso principio gia' usato per `phraseScheduler`, che a sua volta viene sempre chiamato con `inputIsStable` forzato a `false` mentre Play e' attivo (FR-24: nessuna frase nuova o viva, ma lo swap di Stability del suo pool continua a funzionare).
- **Canale MIDI condiviso col controllo CC** (`ccRouter.getMidiChannel()`): un'unica impostazione di canale per tutto il MIDI in ingresso al plugin (note E CC), non una seconda impostazione ridondante — interpretazione di FR-32, che nel PRD vive nella stessa sezione Impostazioni della configurazione CC.
- **Nuovo parametro APVTS `playModeEnabled`** (bool, automatizzabile FR-34, ma NON uno dei 3 CC di FR-30 — quindi non passa per `OverrideManager`). Le due modalita' non si sommano mai (`voicesMix` sceglie l'uno o l'altro buffer in base al flag, mai entrambi).
- **UI**: `ToggleButton` "Play Mode" (stesso pattern del toggle Bypass). Finestra ri-allargata (910px default).
- **Non fatto**: nessuna gestione velocity (non richiesta dal PRD). Nessuna regola di furto per oltre 8 note (il PRD non la definisce per Play, a differenza di FR-51/52 per Harmonizer — le eccedenti restano semplicemente mute). Nessun test numerico dedicato (a differenza di CC/Formanti/PSOLA): la logica nota->slot e' semplice e deterministica ma coinvolge `juce::MidiBuffer`/`juce::MidiMessage`, stesso limite gia' incontrato per `CcRouter`.
- **Verificato**: build VST3/Standalone riuscite, `pluginval --strictness-level 10` verde, entrambe le suite di test (psola, override_manager) ancora verdi — invariate, nessuna delle due tocca questo codice.
- **Non verificabile in questa sessione**: nessun controller MIDI fisico ne' tastiera collegata — il ciclo reale "traccia MIDI + traccia audio" del setup di riferimento non e' mai stato provato, ne' l'assenza di click nel passaggio Harmonizer<->Play (FR-28) e' stata verificata all'ascolto (il meccanismo scelto, `freeAllPhrases()` forzato ogni blocco mentre Play e' attivo, e' lo stesso gia' usato — e mai verificato all'ascolto nemmeno li' — per il caso "segnale non stabile", quindi non introduce un rischio nuovo, solo lo stesso rischio pre-esistente in un caso d'uso in piu').

**Novita' sessione 9 (continuazione) — Controllo MIDI CC (M4, FR-29..38):**

Dopo il push del lavoro precedente, scelto esplicitamente dall'utente di procedere col controllo MIDI CC, pur non potendo ancora testare in Ableton — l'utente ha notato che questa e' un'area verificabile con test numerici (override) e con pluginval, non richiede ascolto.

- **`src/midi/OverrideManager.{h,cpp}`** (nuovo): logica PURA (nessuna dipendenza JUCE) della regola di precedenza CC/automazione (FR-36/37/38). Tre stati indipendenti (root/preset/bypass), ciascuno "override attivo + valore" o "segui l'host". `resolve()` applica i nuovi eventi CC del blocco e ritorna i valori effettivi; `clearOverrides()` va chiamata sul fronte di stop del transport. FR-38 (l'ultimo CC vince) soddisfatto per costruzione, nessuna logica dedicata necessaria.
- **`tests/override_manager_test.cpp`** (nuovo): 6 test — pass-through senza override, indipendenza tra i tre parametri, persistenza dell'override sui blocchi successivi anche con nuova automazione host in arrivo, `clearOverrides()` che restituisce il controllo, l'ultimo CC che vince, soglia booleana del bypass. Tutti verdi al primo tentativo. Target CMake separato (`override_manager_test`), stesso principio di `psola_test`: **testabile senza un controller MIDI fisico**, perche' la logica di precedenza non dipende dall'hardware — e' l'equivalente, per il MIDI, di "non puoi ascoltare" per il DSP.
- **`src/midi/CcRouter.{h,cpp}`** (nuovo): interpreta i CC grezzi secondo FR-30 (root 1-12, preset 1-N con 0 ignorato, bypass soglia 64 come il sustain) e gestisce il MIDI Learn (FR-33) — durante l'apprendimento il filtro canale viene ignorato di proposito (si vuole imparare da QUALUNQUE controller fisico, prima ancora di aver deciso il canale). Configurazione (numeri CC, canale, target di apprendimento) in `std::atomic`: scritta dal message thread (UI) o dall'audio thread stesso (quando l'apprendimento cattura un numero), letta dall'audio thread ogni blocco — nessun lock, nessuna allocazione.
- **`PluginProcessor`**: nuovo parametro APVTS `bypass` (automatizzabile come tutti gli altri, FR-34). `processBlock` ora nomina il `MidiBuffer` (prima ignorato) e lo passa a `ccRouter.process()`; rileva il fronte di stop del transport (nuovo helper `isTransportPlaying()`, distinto da `canApplyStabilityChangeNow()` perche' la semantica standalone e' diversa: qui lo standalone va escluso del tutto dal rilevamento, mai incluso come "sempre fermo") e chiama `overrideManager.clearOverrides()` solo li'; **mai in standalone** (FR-37). I valori effettivi (`OverrideManager::Effective`) sostituiscono le letture dirette di `rootNote`/`presetIndex`/`bypass` a valle. Bypass implementato come `dryLevel=1, wetLevel=0` per quel blocco — il percorso dry e' gia' il segnale non processato, non serve un secondo percorso audio.
- **Persistenza (FR-31)**: numeri CC e canale salvati in un nodo `MidiCcSettings` dentro lo stato del plugin (sibling di APVTS e PresetLibrary) — sono configurazione di routing, non valori automatizzabili, quindi deliberatamente FUORI dall'APVTS (non avrebbe senso automatizzare "quale CC controlla cosa").
- **UI**: ComboBox canale MIDI (Omni + 1-16), 3 slider CC (0-127) con bottone "Learn" ciascuno — non sono `SliderAttachment` (i numeri CC non sono parametri APVTS), si sincronizzano dal timer 15Hz gia' esistente, con guardia `isMouseButtonDown()` per non "strappare" lo slider da sotto il mouse durante il polling. `ToggleButton` bypass attaccato al parametro APVTS per test senza hardware. Finestra riallargata (880px di default).
- **Non fatto in questo passaggio** (scope FR-30..38, non tutto M4): **Modalita' Play (FR-24..28)** resta esplicitamente fuori — l'utente aveva scelto "punto 1" (CC), non Play, che era un'opzione separata proposta. Nessun indicatore UI di "override attivo" (non richiesto dal criterio di uscita M4, "ciclo hardware -> plugin senza mappature manuali" e' comunque soddisfatto). Il selettore root/preset in UI continua a mostrare il valore del parametro APVTS, non il valore effettivo quando un override CC e' attivo — gap di UX noto, non un errore funzionale (l'audio segue correttamente il CC).
- **Verificato**: `override_manager_test` verde (6/6, anche via CTest), `psola_test` ancora verde (invariato), build VST3 e Standalone riuscite, `pluginval --strictness-level 10` verde su tutte le sezioni.
- **Non verificabile in questa sessione**: il parsing dei CC veri (`CcRouter::process`) non ha un test dedicato — richiede `juce::MidiBuffer`/`juce::MidiMessage`, che avrebbe richiesto linkare `juce_audio_basics` in un target di test separato; rimandato, coperto per ora da lettura attenta del codice + `pluginval` (che include un fuzzing dei parametri, non della porta MIDI). **Il ciclo reale hardware -> plugin non e' mai stato provato**: nessun controller MIDI fisico disponibile in questa sessione, esattamente come per l'ascolto del motore PSOLA.

**Novita' sessione 9 (continuazione) — Formanti (FR-39..42):**

Dopo il commit del motore PSOLA, chiesta di nuovo esplicitamente all'utente la prossima area (Formanti / MIDI CC / Pattern ritmico / aspettare l'ascolto): scelte le Formanti, resa naturale dal fatto che `beta` era gia' implementato e testato nel motore ma non collegato a nulla.

- **Dove si calcola**: in `Voice::processAdd`, subito dopo aver calcolato `semitonesToApply` (lo shift REALMENTE applicato a quella voce in quel blocco, identico in Fix e Move, e identico anche in una futura modalita' Play — FR-42 soddisfatto strutturalmente senza codice dedicato, perche' tutte le modalita' producono lo stesso `semitonesToApply`).
- **Unita' di misura scelta**: "semitoni-equivalenti" invece del rapporto `beta` direttamente. Motivo: FR-41 dice che l'offset manuale per voce va "sommato alla correzione automatica" — lavorando in semitoni quella somma e' letterale (`totalFormantSemitones = autoFormantSemitones + formantOffsetSemitones`), invece di dover decidere se moltiplicare due rapporti o convertire avanti e indietro. Coerente con come il resto del progetto ragiona gia' in semitoni (`setPitchShiftSemitones`, gli offset della tabella armonica). Alla fine si converte una sola volta: `beta = 2^(totalFormantSemitones/12)`.
- **Formula automatica** (FR-39, presa da `psola-spec.md` §3 di `TIPS`, gia' verificata concettualmente nella sessione precedente): `autoFormantSemitones = -k * spread * semitonesToApply`, `k = 0.3`. Per costruzione: shift in giu' (`semitonesToApply < 0`) da' `autoFormantSemitones > 0` (schiarisce, FR-39), shift in su scurisce. Il clamp di `beta` gia' presente dentro `PsolaShifter::setFormantRatio` (`[0.25, 4.0]`, cioe' circa ±24 semitoni-equivalenti) copre gia' i casi estremi, non serve un clamp duplicato in `Voice`.
- **Propagazione**: stesso schema gia' rodato per `ShiftMode` (sessione 6) — `setFormantSpread(float)` globale su tutti gli slot fisici del `VoicePool` (una nuova frase la trova gia' impostata), `setVoiceFormantOffset(int, float)` per colonna armonica (0-7), applicato a qualunque slot fisico la stia interpretando ora, identico a `setVoiceMode`.
- **Parametri APVTS nuovi**: `formantSpread` (float 0..1, default **1.0**, non 0 — FR-39 dice "attiva di default"); `voiceFormantOffset1..8` (float −24..24 semitoni-equivalenti, default 0, range scelto per coincidere esattamente col clamp di `beta` nel motore).
- **UI**: slider "Fmt Spread" accanto agli altri controlli globali; riga di 8 knob rotativi "Fmt/Voice" sotto la riga Fix/Move — stesso pattern (`layoutRowOfButtons`, gia' generico su `Component*`, riusato senza modifiche). Finestra allargata di conseguenza (720px di default, limite minimo 680px).
- **Non toccato**: `PitchDetector`, `HarmonyEngine`, `PresetLibrary`, `VoicePool`, `PhraseScheduler` (solo due metodi aggiunti, nessuna riga esistente modificata), `PsolaShifter`/`SpectralShifter` (l'interfaccia `setFormantRatio` esisteva gia' dalla sessione precedente).
- **Verificato**: build VST3 e Standalone riuscite (solo il consueto fallimento di copia post-build per permessi), `pluginval --strictness-level 10` verde su tutte le sezioni, suite `psola_test` riverificata verde (non toccata da queste modifiche, ma rieseguita per scrupolo).
- **Non verificato**: nessun ascolto, ne' della correzione automatica ne' dell'offset manuale — il motore PSOLA stesso non e' ancora stato provato all'ascolto (vedi sotto), quindi le Formanti lo sono ancora meno. La costante `k=0.3` e' quella di partenza della spec sorgente, mai tarata.

**Novita' sessione 9 — PSOLA proprietario integrato come motore di default (M1):**

Su scelta esplicita dell'utente tra le direzioni proposte (PSOLA / MIDI CC / Formanti / solo processo), si e' portato il motore TD-PSOLA scoperto in sessione 8 dentro il progetto, sostituendolo a Signalsmith Stretch come motore attivo di default dietro l'interfaccia astratta `PitchShifter` (FR-62, CLAUDE.md regola 2). Perimetro concordato con l'utente: solo il motore (Formanti rimandate a una sessione dedicata), selezione a compile-time (nessun parametro APVTS/UI), suite di test numerici portata nel repo e in CI.

- **`src/dsp/PsolaShifter.{h,cpp}`** (nuovo): algoritmo TD-PSOLA (dominio pubblico) split `.h`/`.cpp` in stile progetto, namespace globale (non `harm`), derivato dalla nostra `PitchShifter`. Tre problemi reali trovati leggendo i sorgenti (non noti alla sessione 8) e risolti PRIMA che il motore entrasse in `processBlock`, ciascuno verificato con la suite numerica prima di procedere al successivo:
  1. **`std::deque<long long> epochs` allocava sull'audio thread** (`push_back`/`pop_front` dentro `detectEpochs()`, violazione CLAUDE.md regola 1/PRD §9.4 — su MSVC il blocco del deque e' da soli 16 byte, allocazione quasi ad ogni chiamata). Sostituito con un ring buffer a capacita' fissa (`epochRing`/`epochHead`/`epochCount`), pre-allocato in `prepare()`, dimensionato su `bufSize/2/minPeriod + 4`. **Verificato bit-per-bit identico** al deque originale (stessa suite di test, stessi numeri esatti prima e dopo la sostituzione).
  2. **La latenza sarebbe esplosa** (`latency = 2*maxPeriod + maxBlockSize`, e `PluginProcessor.cpp` passa agli shifter `maxBlockSize = 8192`, limite prudenziale per gli scratch buffer, non il blocco reale dell'host — sarebbero stati ~170ms). Risolto con **chunking interno**: `process()` ora suddivide qualunque blocco ricevuto in fette di `kInternalChunk = 64` campioni e la formula di latenza usa questa costante invece del `maxBlockSize` esterno. **Verificato con un controllo ad-hoc** (fuori dal progetto, scratchpad) che l'uscita e' identica bit-per-bit indipendentemente da come il chiamante spezza le chiamate a `process()` (testato con blocchi da 256, 8192 e 777 campioni sullo stesso segnale).
  3. **Perdita di sovrapposizione sotto circa un'ottava sotto** (bug latente nell'originale, non nella nostra checklist di sessione 8): il commento di `emitGrain()` diceva che la semiampiezza del grano va legata al maggiore fra periodo di analisi e di sintesi "cosi' la sovrapposizione resta sempre garantita", ma il codice usava solo il periodo di analisi. Sotto `alpha <= 0.5` (~-12 semitoni) la spaziatura fra grani di sintesi supera la loro lunghezza: l'inviluppo crolla a vuoti periodici. **Primo tentativo di correzione (margine largo, `W = P/alpha`) ha rotto il test 1** (a -12 semitoni la f0 misurata torna quella originale, errore di un'ottava): un grano troppo lungo, a beta=1, e' una copia diretta e non trasposta del segnale sorgente, e reintroduce direttamente la periodicita' originale al suo interno. **Corretto con il margine minimo analiticamente necessario** (`W = 1.2 * max(P, P/(2*alpha))`, il fattore 1.2 e non 1.0 perche' la finestra di Hann si azzera ai bordi e il contatto esatto lasciava comunque vuoti stretti) — l'intera indagine (formula sbagliata -> test 1 rotto -> formula corretta -> tutti i test verdi) e' stata condotta empiricamente con il compilatore, non "a orecchio" (CLAUDE.md regola 12).
  - Mappatura **Stability -> minF0Hz** (non piu' una finestra STFT come in `SpectralShifter`): tabella `{165, 130, 100, 85, 70}` Hz per i 5 livelli Fast..Accurate, valori di partenza **da tarare all'ascolto**. E' uno scostamento deliberato e documentato dalla lettera di FR-54 ("seleziona la dimensione della finestra di analisi") — il PRD non e' stato modificato, solo annotato qui e nel codice.
  - `setPitchShiftSemitones(float)` converte in `alpha` internamente (`alpha = 2^(semitoni/12)`); il resto del progetto continua a ragionare in semitoni.
- **`src/dsp/PitchShifter.h`** (modificato): due metodi virtuali nuovi con default no-op, cosi' `SpectralShifter` non ha dovuto essere toccato: `setInputF0Hz(double)` (PSOLA ne ha bisogno per gli epoch) e `setFormantRatio(double)` (FR-39..42, implementato nel motore ma non ancora collegato a nulla — sessione futura).
- **`src/voices/Voice.cpp`** (modificato): una riga in `processAdd`, `shifter->setInputF0Hz(440.0 * exp2((continuousInputMidiNote-69.0)/12.0))` — nessuna firma cambiata in `Voice`/`PhraseScheduler`/`PluginProcessor`, perche' la nota MIDI continua era gia' un parametro esistente e la conversione a Hz e' un calcolo esatto (round-trip dello stesso valore che `PitchDetector` ricava da Hz).
- **`src/dsp/PitchShifterFactory.cpp`** (nuovo): `createDefaultPitchShifter()` spostata qui da `SpectralShifter.cpp`. Default: PSOLA. `SpectralShifter` resta compilato e disponibile come fallback dietro `#define HARMONIZER_USE_SPECTRAL_SHIFTER`, nessun parametro APVTS/UI (scelta esplicita dell'utente per questa sessione).
- **`tests/psola_test.cpp`** (nuovo): suite portata da `TIPS` e adattata alla nostra interfaccia (niente JUCE, si compila ed esegue in meno di un secondo). Test 1-5 della suite originale (accuratezza di trasposizione, ortogonalita' pitch/formanti nei due sensi, assenza di discontinuita', monotonia della latenza — quest'ultimo riformulato su `stabilityLevel` invece che su `minF0Hz` diretto) piu' **due test nuovi**: Test 6 (inviluppo minimo di sovrapposizione, RMS a breve termine su finestra scorrevole — e' quello che ha scoperto il problema 3 sopra) e Test 7 (tenuta con f0 variabile nel tempo, mai esercitata dalla suite originale che passa sempre una f0 costante — verifica il ricambio continuo del ring di epoch). **Tutti e 7 verdi**, esito completo riportato durante la sessione, non solo "passa".
- **`CMakeLists.txt`** (modificato): nuovi sorgenti (`PsolaShifter.cpp`, `PitchShifterFactory.cpp`) nel target del plugin; nuovo target `psola_test` (eseguibile separato, senza dipendenze JUCE, `enable_testing()` + `add_test`).
- **`.github/workflows/build.yml`** (modificato): nuovo job `dsp-tests` (ubuntu, compila ed esegue `psola_test` con g++) che gira per primo; i job di build del plugin (Windows/macOS) ora dipendono da esso (`needs: dsp-tests`) — se il DSP e' rotto non si aspettano 20 minuti di build per scoprirlo.
- **`CLAUDE.md`** (modificato): aggiunte le regole 12 ("non puoi ascoltare") e 13 ("un test che fallisce puo' essere il test sbagliato — ma vale anche il contrario"), adottate da `TIPS/CLAUDE.md` dopo che si sono dimostrate utili proprio in questa sessione (problema 3 sopra). Nota di stato milestone aggiornata da M0 a M1.
- **Correzione di un errore della sessione 8**: la nota su `_USE_MATH_DEFINES`/`M_PI` (riga 337 della versione precedente di questo file) attribuiva il problema al motore PSOLA in generale; verificato leggendo `PsolaShifter.h` che **non usa affatto `M_PI`** (costante letterale scritta a mano). Il problema riguardava solo `psola_test.cpp` originale, e nel nostro porting e' stato evitato definendo una `constexpr double kPi` locale invece di includere `<cmath>` con la define — non serve alcun flag di compilazione speciale.

**Verificato in questa sessione (build reale, non solo lettura):**
- Compilazione isolata (MSVC via `vcvarsall.bat x64`, scratchpad, nessun file di progetto toccato) di ogni fase intermedia del porting, con la suite di test rieseguita ad ogni modifica (deque -> ring, poi chunking, poi correzione `emitGrain`) per confermare invarianza o correggere regressioni prima di andare avanti.
- **Build reale del progetto**: `cmake --build` Release, VST3 compilato e linkato con successo (il solo passo di copia post-build in `C:\Program Files\Common Files\VST3` fallisce per permessi, comportamento gia' noto e documentato dalla sessione 4 — non un problema di codice). **Standalone compilato con successo** (target buildato separatamente per bypassare l'interruzione del grafo MSBuild causata dal fallimento del passo di copia del VST3).
- **`pluginval --strictness-level 10` verde sul VST3**, nessun fallimento su nessuna sezione (audio processing, state, automation, parametri, thread safety, bus, fuzz — log completo controllato riga per riga, non solo il codice di uscita).
- **Target CMake `psola_test` verificato anche tramite `ctest`** (non solo la compilazione manuale in scratchpad): `1/1 Test #1: psola ... Passed`.
- **Latenza misurata, target PRD raggiunto**: Stability Fast = 646 campioni = **13.5 ms** @48kHz — sotto la soglia <=15ms di NFR §1.3 per la prima volta nel progetto. Accurate = 1436 campioni = 29.9ms (comunque migliore del minimo di Signalsmith, che partiva da 30ms).

**Novita' sessione 8 — scoperta cartella `TIPS` (PSOLA esterno, NON ANCORA INTEGRATO):**

L'utente ha segnalato una cartella `TIPS` (trovata in `C:\Users\cazza\Downloads\TIPS`, **fuori dal progetto**) prodotta da una sessione Claude web separata, concentrata specificamente sul motore PSOLA proprietario — il rischio più alto del prodotto secondo il PRD (§13). Contenuto:

```
TIPS/
├── CLAUDE.md                          (variante indipendente delle nostre regole, con alcune aggiunte utili — vedi sotto)
├── PsolaShifter.h, psola-spec.md      (copie duplicate di quelle sotto)
└── harmonizer-scaffold/
    ├── CLAUDE.md
    ├── CMakeLists.txt                  (JUCE via FetchContent tag 8.0.4, non submodule; target psola_test)
    ├── docs/prd.md                     (VERIFICATO byte-per-byte identico al nostro PRD-Harmonizer-v1.md)
    ├── docs/psola-spec.md              (specifica tecnica del TD-PSOLA, ben scritta)
    ├── src/dsp/PitchShifter.h          (interfaccia astratta, namespace harm, SIMILE ma non identica alla nostra)
    ├── src/dsp/PsolaShifter.h          (implementazione, ~300 righe, ZERO dipendenze esterne)
    └── tests/psola_test.cpp            (5 test numerici)
```

**Non esistono PluginProcessor/PluginEditor in quella cartella**: è DSP puro, pensato per essere validato da riga di comando prima ancora di toccare JUCE. Il loro `CLAUDE.md` documenta un ordine di lavoro diverso dal nostro: PSOLA prima (validato numericamente) → M0 infrastruttura → CI → verifica AU → **solo dopo** PitchDetector — mentre noi abbiamo fatto M0 prima e costruito in ampiezza con un motore interinale (Signalsmith).

**Verifica fatta io stesso (non solo lettura):** ho compilato `psola_test.cpp` con MSVC in una cartella temporanea fuori dal progetto (nessun file di progetto toccato) ed eseguito i test:
- **TUTTI I TEST SUPERATI (0 fallimenti)**: errore di trasposizione < 0.01 cent su ±12 semitoni; indipendenza pitch/formanti confermata numericamente; nessuna discontinuità; latenza monotona al variare di `minF0`.
- Un solo intoppo, cosmetico e non-mio-problema: mancava `_USE_MATH_DEFINES` per `M_PI` su MSVC (la libreria è stata scritta/testata altrove, probabilmente con g++/clang dove `M_PI` è disponibile di default) — risolto con un flag di compilazione, non è un bug dell'algoritmo.
- I numeri di latenza misurati coincidono ESATTAMENTE con quelli dichiarati nella spec (minF0=70Hz → 33.9ms / 1628 campioni; minF0=120Hz → 22.0ms / 1056 campioni).

**Perché è rilevante per noi:**
- Algoritmo TD-PSOLA di dominio pubblico (anni '90), **zero dipendenze e zero problemi di licenza** (coerente con CLAUDE.md regola 9).
- **Latenza reale misurata (22-34ms secondo `minF0`) già inferiore al nostro motore interinale Signalsmith** (30-180ms secondo Stability) — e ulteriormente riducibile con un blocco più piccolo (con blocco 32-64 campioni si stima si scenda sotto i 15ms target del PRD a `minF0` intorno a 150Hz, cosa che Signalsmith non potrà mai fare per costruzione).
- **`alpha`/`beta` ortogonali per costruzione**: `alpha` controlla SOLO il pitch, `beta` SOLO le formanti — la formula proposta per FR-39 (`beta = alpha^(-k*spread)`) è già scritta nella spec e ricalca quasi alla lettera il requisito. Noi non abbiamo ancora toccato le Formanti (FR-39..42): questo ci darebbe un punto di partenza concreto.
- Loro `CLAUDE.md` ha due regole in più che vale la pena adottare anche nel nostro: **"Non puoi ascoltare"** (mai dichiarare completo un lavoro sul suono perché "compila" — o tradurlo in una misura numerica con un test, o segnalare che serve un ascolto umano) e un promemoria che un test che fallisce potrebbe essere il test sbagliato, non l'algoritmo (successo due volte nella loro sessione, entrambe le volte l'algoritmo era giusto — vedi dettagli su autocorrelazione/sub-armonica e centroide spettrale in `psola-spec.md` §8.1).

**Perché NON è plug-and-play — differenze architetturali reali da colmare prima di integrarlo:**
1. La loro interfaccia `PitchShifter` (namespace `harm`) richiede `setF0(double f0Hz)` esplicito — PSOLA ne ha bisogno per posizionare gli epoch sul periodo reale. La nostra interfaccia attuale (`src/dsp/PitchShifter.h`) espone solo `setPitchShiftSemitones(float)`: va estesa.
2. Loro lavorano in `alpha` (rapporto moltiplicativo, es. 2.0 = ottava sopra) e `beta` (rapporto formanti); noi lavoriamo in semitoni. Conversione banale (`alpha = 2^(semitoni/12)`), ma va scritta.
3. Il nostro `PitchDetector` oggi espone solo `getMidiNote()` (nota MIDI continua); serve anche l'Hz grezzo (`getFrequencyHz()` o simile) per alimentare `setF0`.
4. Loro `prepare()` prende `minF0Hz` direttamente; il nostro prende `stabilityLevel` (0-4, mappato internamente da `SpectralShifter`). Andrebbe deciso come le 5 posizioni di Stability mappano a `minF0Hz` per questo motore (la loro tabella in `psola-spec.md` §6 da' gia' dei valori di riferimento).
5. Nessun collegamento a Formanti/UI/parametri APVTS per `beta` — tutto da costruire (FR-39..42 sono ancora del tutto assenti dal nostro progetto).
6. Va portato nella nostra struttura (`src/dsp/PsolaShifter.h` + eventuale `.cpp`), verificato con la NOSTRA build CMake/pluginval, non solo con la loro suite standalone.

**Nessun file del progetto è stato modificato in questa sessione**, su richiesta esplicita dell'utente: solo lettura, ricerca e una compilazione di verifica fuori dal progetto.

**Novita' sessione 7 — motore a frasi (M3):**
- **Rilevamento onset** (FR-43, nuovo `src/dsp/OnsetDetector.{h,cpp}`): inviluppo di picco (`cycfi::q::peak_envelope_follower`) + `cycfi::q::onset_gate` (soglia di livello O pendenza rapida, per catturare anche attacchi morbidi). Un EVENTO di onset e' il fronte di salita del gate; pattern d'uso copiato esattamente dalla documentazione di Cycfi Q in `noise_gate.hpp`.
- **`Phrase`** (nuovo `src/voices/Phrase.h`): dati di una frase — offset congelati (FR-46) + quali slot fisici occupa + un contatore d'eta' (per FR-52) + un flag `isLive`.
- **`VoicePool` generalizzato**: da 8 slot fissi a un pool generico di N slot fisici (default/tetto tecnico 32), con lo stesso schema di swap Stability di prima ma ora parametrizzato sul numero di slot.
- **`PhraseScheduler`** (nuovo, orchestratore): ad ogni onset crea una nuova frase, congelandone gli offset correnti; alloca fino a 8 slot fisici (uno per voce armonica non muta) dal pool, **rubando per intero la frase piu' vecchia** (FR-52) se il pool e' esaurito. **Risoluzione della tensione FR-17/FR-46** (segnalata dal PRD come `[DECISION]` da validare all'ascolto, mai fatto qui): solo la frase piu' recente resta "viva" e segue in tempo reale i cambi di preset/fondamentale finche' la nota che l'ha generata continua a suonare (FR-17); nel momento in cui arriva un nuovo onset, quella frase smette di essere "viva" e resta congelata per sempre a quello che era il suo ultimo voicing (FR-46). Tutte le frasi si liberano insieme quando il segnale in ingresso torna silenzioso.
- **Furto senza dissolvenza dedicata**: quando uno slot rubato viene riassegnato a una nuova frase, il `Glide` gia' presente in `Voice` fornisce naturalmente la transizione morbida richiesta da FR-52 (>= 20ms, di default 30ms) — nessuna dissolvenza/crossfade separata da costruire.
- Nuovo parametro `maxSimultaneousVoices` (FR-51, 1..32, default 32): tetto REGOLABILE A COSTO ZERO senza riallocare, perche' i 32 slot fisici sono sempre pre-allocati e il parametro si limita a restringere quanti sono utilizzabili.
- Editor: slider "Voice Cap" + label "Active" (FR-53, aggiornata dal timer esistente).

**Novita' sessione 6 — qualita' pitch shifting (M1):**
- **Fix/Move per voce** (FR-21/22/23): `enum class ShiftMode { move, fix }` in `Voice`, settabile indipendentemente per ciascuna delle 8 voci (8 parametri APVTS `voiceFix1..8`, bool). Move (default) = rapporto fisso rispetto all'ingresso, comportamento naturale dello shifter. Fix = la voce insegue una nota ASSOLUTA (nota quantizzata + offset), ricalcolando il rapporto di shift **ogni blocco** sulla base del pitch continuo rilevato — verificato che questo e' sicuro perche' `SignalsmithStretch::setTransposeSemitones` non alloca mai (letto il sorgente: assegna due float e azzera un puntatore a funzione gia' nullo).
- **Glide** (FR-17): nuova classe `src/dsp/Glide.h`, rampa lineare a **durata fissa** (default 30ms, parametro APVTS `glideTimeMs`), applicata all'offset armonico grezzo prima che Fix/Move lo interpretino. Un salto (es. cambio accordo su nota tenuta) impiega sempre lo stesso tempo a completarsi, qualunque sia l'ampiezza.
- **Controllo Stability** (FR-54..58): 5 posizioni discrete (Fast..Accurate, parametro APVTS `stabilityLevel`) che mappano a finestre STFT di Signalsmith Stretch da 30ms a 180ms — **anche la piu' reattiva resta ben oltre il target <=15ms del PRD**, limite noto del motore interinale (vedi sotto). Cambiare Stability richiede riallocare i buffer interni (`configure()` di Signalsmith alloca), quindi non puo' avvenire sull'audio thread: implementato uno schema realtime-safe a due fasi — il message thread (un `juce::Timer` in `HarmonizerAudioProcessor`, 250ms) nota il cambio di parametro e **costruisce** 8 nuovi shifter (`VoicePool::requestStabilityChange`); l'audio thread, dentro `processBlock`, **applica** lo scambio solo quando e' sicuro (`canApplyStabilityChangeNow()`: transport fermo, o sempre in standalone — FR-56/57) tramite `std::unique_ptr::swap` (nessuna allocazione/distruzione), mettendo i vecchi shifter in una lista che lo stesso Timer distrugge poi sul message thread (`VoicePool::collectGarbage`). `setLatencySamples` viene richiamato solo nel blocco in cui lo scambio e' stato effettivamente applicato (FR-55/56).
- Editor: ComboBox Stability, slider Glide, 8 ToggleButton Fix/Move per voce (una riga sopra i bottoni di gestione preset).

Fatto e verificato:
- Repository git con remote **origin** = https://github.com/Jazy1997/STARTUP-Software-Harmonizer.git, branch `main`, storia pushata. Repo pubblico. CI del primo push verde su Windows e macOS incluso AU (run https://github.com/Jazy1997/STARTUP-Software-Harmonizer/actions/runs/30536034297).
- JUCE 8.0.15 + **Cycfi Q** (pitch detection) + **Signalsmith Stretch** (pitch shifting interinale, tag `1.1.0`) come submodule, tutte licenze permissive.
- Catena audio end-to-end: `PluginProcessor` → downmix mono → `PitchDetector` (Cycfi Q) → snapshot di `harmony::PresetLibrary` → `harmony::HarmonyEngine::getOffsets` (puro calcolo, stateless) → `VoicePool`/`Voice` (fino a 8 voci continue, ciascuna con un `PitchShifter` dietro interfaccia astratta) → somma pesata dry/wet.
- **PresetLibrary reale (nuovo, sessione 5)** — `src/harmony/PresetLibrary.{h,cpp}`:
  - Lista ordinata di preset (7 di fabbrica all'avvio), CRUD completo: add/duplicate/rename/remove, `movePreset` per il riordino (FR-06/07: la posizione, 1-based via `getCcValue`, e' gia' concettualmente il futuro valore CC).
  - ID stabile `juce::Uuid` per preset, indipendente dalla posizione (FR-10). Tetto tecnico 128 preset (FR-02).
  - **CSV import/export** (`src/harmony/CsvIo.{h,cpp}`, FR-03): intestazione con i 12 gradi + 8 righe (voci) x 12 colonne, cella vuota = stringa vuota, `0` = zero. Verificato andata/ritorno.
  - **Serializzazione nello stato del plugin** (FR-08): `PresetLibrary::toValueTree()`/`loadFromValueTree()`, incorporata in `getStateInformation`/`setStateInformation` insieme ai parametri APVTS — un progetto salvato porta con se' la propria copia della libreria.
  - **Libreria globale su disco** (FR-09): `PresetLibrary::saveAsGlobal()`/`loadGlobal()`, file XML in `%APPDATA%\Harmonizer\GlobalPresetLibrary.xml` (o equivalente utente su macOS), operazioni esplicite via bottoni UI, mai automatiche.
  - **Swap thread-safe verso l'audio thread** (PRD §9.4): la libreria vive dietro `std::shared_ptr<const PresetLibrary>` scambiato sotto `juce::SpinLock` (`HarmonizerAudioProcessor::getPresetLibrary()`/`editPresetLibrary()`), con un singolo "slot retired" per tenere in vita la versione precedente finche' non arriva la modifica successiva. E' un compromesso pragmatico (non hazard-pointer/epoch-based reclamation rigorosa) — vedi §5 per i limiti noti.
  - `presetIndex` e' ora `AudioParameterInt` 1..128 (non piu' `AudioParameterChoice`): le choices fisse di APVTS non reggono una lista che cambia dimensione a runtime; il valore 1-based coincide gia' col futuro CC posizionale, e valori oltre la libreria attuale vengono ignorati (stesso comportamento di FR-30).
  - Editor: ComboBox preset sincronizzata via polling (`juce::Timer`, 15 Hz, non un `ComboBoxAttachment`), text editor per rinominare, bottoni Add/Duplicate/Delete/Up/Down/Import CSV/Export CSV/Load Global/Save As Global.
- Build locale Windows verificata: **VST3 compila** (incluso tutto il codice sopra) **ed e' verde su `pluginval --strictness-level 10`**. `COPY_PLUGIN_AFTER_BUILD` e' di nuovo `TRUE` su richiesta dell'utente — su questa macchina, senza shell elevata, il solo passo di copia post-build fallisce con "Permission denied" (atteso, documentato in `CMakeLists.txt`); l'artefatto compilato resta comunque valido in `build/Harmonizer_artefacts/Release/VST3`.

Non ancora fatto / semplificazioni consapevoli di questo vertical slice (da NON scambiare per requisiti soddisfatti):
- **Pattern ritmico** (FR-47..50, `[V1.1]`): non implementato, correttamente fuori scope per v1.0. Conseguenza pratica: tutte le voci di una frase entrano "in sync" al trigger (nessun offset temporale tra voci) — questo rende FR-46 osservabile solo nel caso "chord change mentre una frase e' gia' congelata da un onset successivo", MAI nel caso "voce ancora in coda non ancora suonata" (che richiederebbe il pattern con ritardi reali). Quando il pattern editor arrivera', la logica di congelamento in `PhraseScheduler` va rivista per questo caso aggiuntivo.
- **Risoluzione FR-17/FR-46 non validata all'ascolto**: e' un'interpretazione mia (unica frase "viva" = quella piu' recente, tutte le altre congelate), consistente con la lettera del PRD ma esplicitamente segnalata come `[DECISION]` da verificare — l'utente dovrebbe ascoltarla e confermare che il comportamento sia musicalmente sensato.
- **Formanti**: nessuna correzione (FR-39..42).
- **Latenza minima ben oltre il target del PRD**: anche Stability "Fast" (30ms) e' molto piu' della soglia <=15ms richiesta — limite intrinseco del motore STFT interinale (Signalsmith), non raggiungibile prima del PSOLA proprietario.
- **Swap Stability e furto di frase**: sicuri nel caso normale (stesso compromesso pragmatico della PresetLibrary: nessuna garanzia assoluta in ogni intreccio di timing estremo, niente hazard-pointer/epoch-based reclamation rigorosa).
- **MIDI CC, modalita' Play, licensing**: tutti placeholder/non iniziati (M4/M6). Il riordino preset in UI usa bottoni Su/Giu', non drag&drop vero (quello e' UI di M5).
- **Preset armonici**: 7 di fabbrica generati algoritmicamente — solo **Min** e' verificato contro il prototipo (vedi §5). Gli altri 6 sono standard jazz generici, da sostituire via import CSV quando disponibili i dati reali.
- **AU non compilabile ne' testabile su questa macchina** (Windows) — verificato in CI su macOS.
- Nessuna licenza JUCE acquistata, nessun certificato di firma avviato.
- **Non ancora testato per davvero in Ableton** dalla sessione 6 in poi (solo verificato via pluginval) — l'utente non ha ancora potuto riprovare il caricamento del VST3 aggiornato.

Contenuto attuale della cartella di progetto (esclusi `build/`, `libs/*` interni e `tools/` — vedi `.gitignore`):
```
SVILUPPO SOFTWARE/
├── .github/workflows/build.yml
├── .gitignore / .gitmodules
├── CLAUDE.md
├── CMakeLists.txt
├── PRD-Harmonizer-v1.md
├── handsoff.md
├── libs/{JUCE, q, signalsmith-stretch}/   (submodule)
├── src/
│   ├── PluginProcessor.{h,cpp}, PluginEditor.{h,cpp}
│   ├── dsp/PitchDetector.{h,cpp}, PitchShifter.h, PsolaShifter.{h,cpp}, SpectralShifter.{h,cpp},
│   │       PitchShifterFactory.cpp, Glide.h, OnsetDetector.{h,cpp}
│   ├── harmony/HarmonyPreset.h, HarmonyEngine.{h,cpp}, PresetLibrary.{h,cpp}, CsvIo.{h,cpp}
│   └── voices/Voice.{h,cpp}, VoicePool.{h,cpp}, Phrase.h, PhraseScheduler.{h,cpp}
└── tests/psola_test.cpp   (target CMake `psola_test`, niente JUCE, gate in CI)
```

Questioni aperte dal PRD (§16) che restano da chiudere (nessuna blocca la prosecuzione tecnica):
- Nome del prodotto, marchio, dominio — non deciso. `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt` restano placeholder da confermare prima della beta.
- Tipo di licenza JUCE in funzione del fatturato previsto — non deciso.
- Backend di licensing — `[DECISION]` entro M5, non ancora aperta.
- Numero esatto di posizioni del controllo Stability — `[DECISION]` entro M1.
- Coerenza FR-17 vs FR-46 (nota tenuta vs frase in coda) — da verificare all'ascolto entro M2.

---

## 3. File su cui sto lavorando

**Sessione 1:**

| File | Stato | Scopo |
|---|---|---|
| `PRD-Harmonizer-v1.md` | letto, invariato | Specifica di prodotto, fonte di verità |
| `handsoff.md` | creato | Questo documento di handoff |

**Sessione 2 (M0):**

| File | Stato | Scopo |
|---|---|---|
| `.gitignore` | creato | Esclude `build/`, `tools/` (pluginval scaricato), file IDE/OS |
| `.gitmodules` + `libs/JUCE` | creato | Submodule JUCE pinnato al tag `8.0.15` |
| `CMakeLists.txt` | creato | Target `juce_add_plugin`, formati VST3/AU/Standalone |
| `src/PluginProcessor.h` / `.cpp` | creato | Processor stub, dry passthrough, nessuna allocazione in `processBlock` |
| `src/PluginEditor.h` / `.cpp` | creato | Editor placeholder ridimensionabile |
| `CLAUDE.md` | creato | Le 11 regole non negoziabili del PRD §15 + note di stato milestone |
| `.github/workflows/build.yml` | creato | CI Windows+macOS, gate pluginval strictness 10 — non ancora eseguita (nessun remote) |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 3 (push su GitHub):**

| File | Stato | Scopo |
|---|---|---|
| `handsoff.md` | aggiornato | Registrato remote, force-push, run CI avviata inavvertitamente |

**Sessione 4 (vertical slice DSP):**

| File | Stato | Scopo |
|---|---|---|
| `.gitmodules` + `libs/q` | creato | Submodule Cycfi Q (+ submodule annidato `infra`) |
| `.gitmodules` + `libs/signalsmith-stretch` | creato | Submodule Signalsmith Stretch, tag `1.1.0` |
| `src/harmony/HarmonyPreset.h` | creato | Tipi `Cell`/`Table`/`Preset` (12×8, `null` vs `0`) |
| `src/harmony/HarmonyEngine.{h,cpp}` | creato | 7 preset di fabbrica generati algoritmicamente, `degreeOf`/`getOffsets` |
| `src/dsp/PitchDetector.{h,cpp}` | creato | Wrapper Cycfi Q, pimpl con `unique_ptr` |
| `src/dsp/PitchShifter.h` | creato | Interfaccia astratta (FR-62) + factory `createDefaultPitchShifter()` |
| `src/dsp/SpectralShifter.{h,cpp}` | creato | Implementazione interinale su Signalsmith Stretch |
| `src/voices/Voice.{h,cpp}` | creato | Una voce = un `PitchShifter` + scratch buffer |
| `src/voices/VoicePool.{h,cpp}` | creato | Somma fino a 8 voci continue (non a frase) |
| `src/PluginProcessor.{h,cpp}` | modificato | Da dry passthrough a catena completa + APVTS (5 parametri) + save/restore stato |
| `src/PluginEditor.{h,cpp}` | modificato | ComboBox root/preset, slider voci/dry/wet con attachment APVTS |
| `CMakeLists.txt` | modificato | Nuovi sorgenti, include dir per q/infra/signalsmith-stretch, `COPY_PLUGIN_AFTER_BUILD FALSE` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 5 (PresetLibrary):**

| File | Stato | Scopo |
|---|---|---|
| `src/harmony/HarmonyPreset.h` | modificato | `Preset` ora ha `juce::Uuid id` (FR-10); `name` diventato `juce::String` |
| `src/harmony/HarmonyEngine.{h,cpp}` | riscritto | Diventa puro calcolo stateless (namespace di funzioni), non possiede piu' la lista preset |
| `src/harmony/PresetLibrary.{h,cpp}` | creato | Lista ordinata, CRUD, CC posizionale, ValueTree (FR-08), libreria globale su disco (FR-09) |
| `src/harmony/CsvIo.{h,cpp}` | creato | Import/export CSV della tabella 12x8 (FR-03) |
| `src/PluginProcessor.{h,cpp}` | modificato | Swap thread-safe `shared_ptr<const PresetLibrary>`, `presetIndex` da Choice a Int 1..128, stato serializzato include la libreria |
| `src/PluginEditor.{h,cpp}` | modificato | ComboBox preset dinamica (Timer, no ComboBoxAttachment), text editor rinomina, bottoni gestione libreria |
| `CMakeLists.txt` | modificato | Nuovi sorgenti `PresetLibrary.cpp`/`CsvIo.cpp`; `COPY_PLUGIN_AFTER_BUILD` rimesso `TRUE` su richiesta utente |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 6 (Fix/Move, Glide, Stability):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/Glide.h` | creato | Rampa lineare a durata fissa (default 30ms, FR-17) |
| `src/dsp/PitchShifter.h` | modificato | `prepare()` prende uno `stabilityLevel`; namespace `Stability` (5 nomi/livelli, FR-54) |
| `src/dsp/SpectralShifter.{h,cpp}` | modificato | Mappa stabilityLevel -> finestra STFT (30-180ms); rimossa guardia inutile su `setPitchShiftSemitones` |
| `src/voices/Voice.{h,cpp}` | modificato | `ShiftMode` (Fix/Move, FR-21/22/23), `Glide` per voce, `swapShifterNoAlloc` |
| `src/voices/VoicePool.{h,cpp}` | modificato | `requestStabilityChange`/`collectGarbage`/swap differito thread-safe (FR-56/57) |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametri `stabilityLevel`, `glideTimeMs`, `voiceFix1..8`; `juce::Timer` per notare i cambi Stability + garbage collect; `canApplyStabilityChangeNow()` (transport/standalone) |
| `src/PluginEditor.{h,cpp}` | modificato | ComboBox Stability, slider Glide, 8 ToggleButton Fix/Move |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 7 (motore a frasi, M3):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/OnsetDetector.{h,cpp}` | creato | Inviluppo di picco + onset_gate (Cycfi Q); evento = fronte di salita (FR-43) |
| `src/voices/Phrase.h` | creato | Dati di una frase: offset congelati, slot assegnati, eta', flag isLive |
| `src/voices/VoicePool.{h,cpp}` | riscritto | Da 8 slot fissi a pool generico di N slot fisici (default 32) |
| `src/voices/PhraseScheduler.{h,cpp}` | creato | Trigger onset, congelamento (FR-46), live-update solo frase piu' recente (FR-17), furto (FR-51/52) |
| `src/PluginProcessor.{h,cpp}` | modificato | Usa `PhraseScheduler` invece di `VoicePool` diretto; nuovo parametro `maxSimultaneousVoices`; wiring onset detection |
| `src/PluginEditor.{h,cpp}` | modificato | Slider "Voice Cap", label "Active" (FR-53) |
| `CMakeLists.txt` | modificato | Nuovi sorgenti `OnsetDetector.cpp`, `PhraseScheduler.cpp` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 8 (scoperta PSOLA esterno — NESSUN file di progetto modificato):**

| File | Stato | Scopo |
|---|---|---|
| `handsoff.md` | aggiornato | Questo aggiornamento — unico file toccato in questa sessione |

File letti (fuori dal progetto, in `C:\Users\cazza\Downloads\TIPS`, mai modificati): `CLAUDE.md`, `harmonizer-scaffold/CLAUDE.md`, `harmonizer-scaffold/CMakeLists.txt`, `harmonizer-scaffold/docs/prd.md`, `harmonizer-scaffold/docs/psola-spec.md`, `harmonizer-scaffold/src/dsp/PitchShifter.h`, `harmonizer-scaffold/src/dsp/PsolaShifter.h`, `harmonizer-scaffold/tests/psola_test.cpp`.

Nessuna modifica a `PRD-Harmonizer-v1.md`. Questa tabella va estesa (non sovrascritta) a ogni sessione futura.

**Sessione 9 (integrazione PSOLA, M1):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/PsolaShifter.{h,cpp}` | creato | Motore TD-PSOLA portato da `TIPS`, ring di epoch RT-safe, chunking interno, mappa Stability->minF0Hz |
| `src/dsp/PitchShifterFactory.cpp` | creato | `createDefaultPitchShifter()` spostata qui da `SpectralShifter.cpp`; default PSOLA, `#define HARMONIZER_USE_SPECTRAL_SHIFTER` per fallback |
| `tests/psola_test.cpp` | creato | Suite numerica portata (5 test originali + 2 nuovi), niente JUCE |
| `src/dsp/PitchShifter.h` | modificato | `setInputF0Hz`/`setFormantRatio` virtuali con default no-op |
| `src/dsp/SpectralShifter.cpp` | modificato | Rimossa la factory (spostata) |
| `src/voices/Voice.cpp` | modificato | Una riga: `setInputF0Hz` da `continuousInputMidiNote` |
| `CMakeLists.txt` | modificato | Nuovi sorgenti + target `psola_test` + `enable_testing()` |
| `.github/workflows/build.yml` | modificato | Job `dsp-tests` come gate prima della build plugin |
| `CLAUDE.md` | modificato | Regole 12/13 (processo) + nota di stato milestone M0->M1 |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 9 (continuazione — Formanti, FR-39..42):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/Voice.{h,cpp}` | modificato | `setFormantSpread`/`setFormantOffsetSemitones`; calcolo di `beta` in `processAdd` da `semitonesToApply` |
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | `setFormantSpread` (globale) e `setVoiceFormantOffset` (per colonna, come `setVoiceMode`) |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametri `formantSpread`, `voiceFormantOffset1..8`; lettura e propagazione in `processBlock` |
| `src/PluginEditor.{h,cpp}` | modificato | Slider "Fmt Spread", 8 knob rotativi "Fmt/Voice"; finestra allargata |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 9 (continuazione — Controllo MIDI CC, M4, FR-29..38):**

| File | Stato | Scopo |
|---|---|---|
| `src/midi/OverrideManager.{h,cpp}` | creato | Logica pura di precedenza CC/automazione (FR-36/37/38), nessuna dipendenza JUCE |
| `tests/override_manager_test.cpp` | creato | 6 test sulla logica di override, target CMake separato |
| `src/midi/CcRouter.{h,cpp}` | creato | Interpretazione CC (FR-30) + MIDI Learn (FR-33), config in `std::atomic` |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametro `bypass`; `MidiBuffer` nominato e passato a `CcRouter`; fronte di stop transport; valori effettivi a valle; persistenza `MidiCcSettings` |
| `src/PluginEditor.{h,cpp}` | modificato | ComboBox canale, 3 slider CC + bottoni Learn, toggle bypass; finestra allargata |
| `CMakeLists.txt` | modificato | Nuovi sorgenti `midi/*.cpp`; target `override_manager_test` + `add_test` |
| `.github/workflows/build.yml` | modificato | Job `dsp-tests` compila ed esegue anche `override_manager_test` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 9 (continuazione — Modalita' Play, FR-24..28):**

| File | Stato | Scopo |
|---|---|---|
| `src/midi/PlayModeInput.{h,cpp}` | creato | `VoicePool` dedicato 8 slot, mapping nota MIDI -> slot, riusa `ShiftMode::fix` di `Voice` con bersaglio assoluto |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametro `playModeEnabled`; secondo scratch buffer; `phraseScheduler` forzato a `inputIsStable=false` mentre Play e' attivo; scelta del buffer wet in base alla modalita' |
| `src/PluginEditor.{h,cpp}` | modificato | `ToggleButton` "Play Mode"; finestra allargata |
| `CMakeLists.txt` | modificato | Nuovo sorgente `midi/PlayModeInput.cpp` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 10 (primo test reale in Ableton — fix ciclo di vita frasi, UI, diagnostica):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/Phrase.h` | modificato | Commento aggiornato: risoluzione FR-17/FR-46 col bottone Keep Tails |
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | `setKeepTails(bool)`; le frasi smontate si liberano subito quando `keepTails=false` (default) |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametro `keepPhraseTails`; atomici `lastDetectedMidiNote`/`lastDetectedConfidence`/`lastInputStable` + getter |
| `src/PluginEditor.{h,cpp}` | modificato | Fix bounds `fixMoveLabel`/`voiceFormantLabel` (erano invisibili); slider Fmt/Voice ingrandite; titolo aggiornato; toggle "Keep Tails"; label "Detected"; finestra allargata |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 11 (isteresi di intonazione per note legate, FR-16/17):**

| File | Stato | Scopo |
|---|---|---|
| `src/harmony/PitchLatch.h` | creato | Isteresi ±25 cent, aggancio/sgancio a gradino bloccato al nearest-round; pura, nessuna dipendenza JUCE |
| `tests/pitch_latch_test.cpp` | creato | 8 gruppi di verifiche, incluso il test che ha scoperto il bug di rimbalzo prima dell'integrazione |
| `src/PluginProcessor.{h,cpp}` | modificato | Nuovo membro `pitchLatch`; `quantizedPlayedNote` ora da `pitchLatch.update(...)` invece di `roundToInt` |
| `CMakeLists.txt` | modificato | Nuovo target `pitch_latch_test` + `add_test` |
| `.github/workflows/build.yml` | modificato | Job `dsp-tests` compila ed esegue anche `pitch_latch_test` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 11 (continuazione — `freeAllPhrases()` legato al segnale, non al pitch):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | Nuovo parametro `signalPresent` (governa `freeAllPhrases`), distinto da `inputIsStable` (governa il live-update FR-17) |
| `src/PluginProcessor.{h,cpp}` | modificato | `signalPresent` da `onsetDetector.isGateOpen()`; `pitchLatch.reset()` legato a `signalPresent`; Play mode forza entrambi i segnali |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 12 (note saltate — corsa onset/pitch, FR-43/45/46):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | Ramo live-update completa l'allocazione degli slot rimasti vuoti al trigger; nuovo contatore `numLateBindingsTotal`/`getNumLateBindings()` |
| `src/PluginProcessor.{h,cpp}` | modificato | `signalPresentLastBlock`; `pitchDetector.reset()` sul fronte di discesa di `signalPresent`; nuovo atomico `lastGateOpen`/`getLastGateOpen()` |
| `src/PluginEditor.cpp` | modificato | Label "Detected" mostra anche `gate open/closed` e `late-bindings N` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 12 (continuazione — timbro robotico/granuloso, fix `emitGrain`):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/PsolaShifter.cpp` | modificato | `emitGrain`: floor su `Lg` (lunghezza del grano in uscita) basato su `synthPeriod`, indipendente da `beta` |
| `tests/psola_test.cpp` | modificato | Nuovo Test 8: sovrapposizione dei grani con la `beta` reale prodotta dalla correzione formantica di default di `Voice.cpp` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 12 (continuazione — fix click/scricchiolii, dissolvenza di ampiezza per voce):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/Glide.h` | modificato | Nuovo `isSettled()` |
| `src/voices/Voice.{h,cpp}` | modificato | `ampGlide` (rampa fissa 8ms), `setMuted`/`isSilent()`, `processAdd` non esce piu' subito su `muted` |
| `src/voices/Phrase.h` | modificato | Nuovo flag `releasing` |
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | `freePhrase` diviso in `hardFreePhrase` (solo furto d'emergenza) e `beginRelease` (rilascio morbido); ciclo di mixing gestisce frasi in rilascio e celle tornate vuote su frasi vive (FR-17) |
| `src/midi/PlayModeInput.cpp` | modificato | Stesso pattern di dissolvenza su note-off e uscita da Play mode |
| `src/PluginProcessor.{h,cpp}` | modificato | `dryGlide`/`wetGlide`: dry/wet/bypass non piu' un salto istantaneo per blocco |
| `handsoff.md` | aggiornato | Questo aggiornamento |

---

## 4. Cambiamenti in questa sessione

**Sessione 1:**
- Letto integralmente `PRD-Harmonizer-v1.md` per ricostruire il contesto del prodotto.
- Ricognizione della cartella di progetto (era vuota a parte il PRD).
- Creato `handsoff.md` con la struttura di handoff richiesta dall'utente.

**Sessione 2 — M0 avviato:**
- Verificata la toolchain locale: git 2.54, CMake 4.4.0-rc2, Visual Studio 18 Community con MSVC 19.51 (C++ workload presente). Connettività GitHub e spazio disco confermati.
- `git init` nella cartella di progetto; creata la struttura di directory `src/{dsp,harmony,voices,midi,state,licensing,ui}`, `resources/factory_presets`, `tests`, `tools`, `.github/workflows` (PRD §9.3).
- JUCE aggiunto come submodule e pinnato al tag `8.0.15` (ultimo rilascio 8.x disponibile).
- Scritto `CMakeLists.txt`: `juce_add_plugin` con `NEEDS_MIDI_INPUT TRUE`, `IS_MIDI_EFFECT FALSE`, `AU_MAIN_TYPE kAudioUnitType_MusicEffect`, `VST3_CATEGORIES Fx "Pitch Shift"`, `FORMATS VST3 AU Standalone`.
- Scritto uno stub minimo di `PluginProcessor`/`PluginEditor`: dry passthrough, nessuna allocazione/lock/I/O in `processBlock` (rispetta la regola 1 di `CLAUDE.md` fin dal primo commit).
- Scritto `CLAUDE.md` alla radice con le 11 regole del PRD §15.
- Configurato il progetto con CMake (generatore Visual Studio 18 2026, x64) — configurazione riuscita.
- Compilati con successo in locale (Release x64): target **VST3** e target **Standalone**.
- Scaricato `pluginval` v1.0.4 per Windows in `tools/` (ignorato da git) ed eseguito `--strictness-level 10 --validate` sul VST3 → **SUCCESS**, nessun test fallito.
- Scritta la CI GitHub Actions (`build.yml`) per Windows + macOS con gate pluginval strictness 10 su VST3 (entrambe) e AU (solo macOS). Non ancora eseguita: manca un remote.
- Corretto un errore proprio: avevo impostato `user.email` nella git config locale del repo per errore (violazione della regola "mai modificare la git config"); rimosso subito con `git config --unset`. Per i commit di questa sessione uso `-c user.name=/-c user.email=` inline invece di persistere l'identità in config.

Nessuna modifica al PRD.

**Sessione 3 — push su GitHub:**
- Controllato il contenuto della repo remota (4 file caricati manualmente dall'utente via web UI) prima di sovrascrivere.
- Aggiunto remote `origin`, rinominato branch locale `master` → `main`, force-push del commit M0 completo.
- Push del workflow CI ha avviato automaticamente una run di GitHub Actions (effetto collaterale non richiesto, discusso con l'utente e lasciato proseguire).

**Sessione 4 — vertical slice DSP end-to-end:**
- Verificata l'esito della run CI della sessione precedente (Windows + macOS entrambe verdi, incluso AU).
- Aggiunti come submodule: **Cycfi Q** (`libs/q`, con submodule annidato `infra` inizializzato via `git submodule update --init --recursive`) e **Signalsmith Stretch** (`libs/signalsmith-stretch`, tag `1.1.0`). Verificate le licenze (Boost License / MIT) prima di integrarle.
- Studiata l'API pubblica di entrambe le librerie leggendo header e doc/esempi nel repository stesso (non assunta a memoria), in particolare: `cycfi::q::pitch_detector` (costruttore con range di frequenza + soglia in dB, `operator()` per-sample, `get_frequency()`/`periodicity()`), `q::pitch` per la conversione Hz→MIDI, e `SignalsmithStretch::process()` (STFT, richiede buffer in/out distinti, latenza riportata via `inputLatency()`/`outputLatency()`).
- Progettato e implementato un algoritmo di generazione dei preset di fabbrica ("drop voicing" ciclico sui toni dell'accordo) **verificato per calcolo diretto** contro l'unico dato di prototipo presente nel PRD (preset Min, d=2 → [-2,-4,-7,-11,null×4]): l'algoritmo riproduce esattamente quei valori. Gli altri 6 preset (Maj, Dom, Sus, Half Dim, Dim, Aug7) usano tonalita' standard di jazz con lo stesso algoritmo, **non verificate sul prototipo reale** — da sostituire con import CSV (FR-03) quando l'utente avra' i dati originali.
- Implementata la catena completa (vedi §2) e i parametri APVTS.
- Aggiornato `CMakeLists.txt` con i nuovi sorgenti e gli include path delle librerie header-only.
- Compilazione, debug e validazione locale (vedi §5 per gli errori incontrati e come sono stati risolti).

**Sessione 5 — PresetLibrary reale (scelta dall'utente tra 4 direzioni proposte):**
- Chiesto esplicitamente all'utente quale area sviluppare dopo il vertical slice (PresetLibrary / qualita' pitch shifting / motore a frasi / MIDI CC): scelta la PresetLibrary.
- Riattivato `COPY_PLUGIN_AFTER_BUILD TRUE` su richiesta diretta dell'utente, documentando nel commento CMake che serve una shell elevata su Windows perche' funzioni.
- Analizzato il requisito FR-05/FR-35 (CC posizionale + parametro discreto automatizzabile) e riconosciuto che confligge con l'uso di `AudioParameterChoice` se la libreria puo' cambiare dimensione a runtime (le "choices" di un parametro APVTS sono fisse). Risolto passando a `AudioParameterInt` 1..128, che modella gia' correttamente la semantica "posizione = valore" richiesta da FR-05.
- Riconosciuto e affrontato esplicitamente il problema di **thread-safety** descritto (ma non risolto in dettaglio) dal PRD §9.4: mutare la libreria da UI mentre l'audio thread la legge e' una race condition reale. Implementato uno schema a snapshot immutabili (`shared_ptr<const PresetLibrary>`) scambiati sotto `SpinLock`, con un singolo slot "retired" per evitare la distruzione sull'audio thread nel caso normale — vedi §2 per il limite noto di questo compromesso.
- Riprogettata la separazione dei moduli per rispecchiare l'albero di file del PRD §9.3: `HarmonyEngine` (calcolo puro) + `PresetLibrary` (stato/CRUD/CC) + `CsvIo` (I/O) invece di un'unica classe.
- Costruito l'editor con sincronizzazione a polling (Timer 15Hz) invece di un `ComboBoxAttachment`, dato che quest'ultimo assume un parametro con scelte statiche.
- Compilazione, debug (vedi sotto) e validazione pluginval.

**Sessione 6 — qualita' pitch shifting, scelta dall'utente tra 4 direzioni proposte:**
- Chiesto di nuovo esplicitamente all'utente quale area sviluppare (stesse 4 opzioni + Formanti aggiunta): scelta "Qualita' pitch shifting (M1)".
- Prima di implementare Fix, **verificato leggendo il sorgente** di `signalsmith-stretch.h` che `setTransposeSemitones`/`setTransposeFactor` non allocano mai (assegnano due float e azzerano un `std::function` gia' nullo, dato che non usiamo mai `setFreqMap`) — questo smentiva una mia precedente cautela eccessiva (la guardia "chiama solo se cambia" nella sessione 4) e ha sbloccato la modalita' Fix, che richiede il ricalcolo ad ogni blocco.
- Progettato lo schema realtime-safe per il cambio di Stability (che invece ALLOCA davvero, via `configure()`): costruzione sul message thread (Timer) + applicazione sull'audio thread solo quando sicuro (transport fermo o standalone) + smaltimento dei vecchi shifter sul message thread — stesso principio della PresetLibrary (sessione 5) ma con un meccanismo diverso (`unique_ptr::swap` + lista di "retired" invece di `shared_ptr`+`SpinLock`), perche' un `PitchShifter` non e' un value-type economico da copiare come un preset.
- L'utente ha chiuso Ableton Live per errore di sequenza (l'avevamo lasciato aperto con il vecchio VST3 caricato) causando un fallimento di link (`LNK1104`, file bloccato) al primo tentativo di ricompilare: risolto chiudendo Ableton e ricompilando.
- Dopo il fix del link, build e `pluginval --strictness-level 10` verdi con Fix/Move, Glide e Stability inclusi.

**Sessione 7 — motore a frasi, scelta dall'utente tra le direzioni proposte (dopo una domanda di status su M0):**
- Chiesto lo stato di M0 prima di procedere: confermato tecnicamente completo (CMake, 3 target, CI, pluginval), con solo decisioni non tecniche in sospeso (licenza JUCE, certificati, nome prodotto).
- Chiesto di nuovo quale area sviluppare: scelto "Motore a frasi (M3)".
- Studiata l'API di `q::onset_gate`/`q::noise_gate` (Cycfi Q) leggendo la documentazione inline in `noise_gate.hpp`, che mostra esplicitamente il pattern d'uso raccomandato (inviluppo -> gate): seguito alla lettera invece di indovinare i parametri.
- **Decisione di design centrale**: interpretato il modello a frase come "una ricetta di offset congelati applicata in continuo al segnale live in ingresso" (non un frammento audio a durata fissa) — dedotto dall'esempio del PRD sulle 16 frasi simultanee generate da una linea di ottavi su un pattern di 2 misure, che ha senso solo se le frasi restano vive finche' non vengono rubate o il segnale tace, non per una durata fissa breve.
- Risolta esplicitamente (con una scelta motivata, non ancora validata all'ascolto) la tensione FR-17/FR-46 segnalata come `[DECISION]` nel PRD: solo la frase piu' recente resta "viva". Vedi §2 e Phrase.h per il dettaglio.
- Riutilizzato il `Glide` gia' esistente (da sessione 6) per risolvere la "dissolvenza di almeno 20ms" richiesta da FR-52 sul furto di frase, evitando di costruire un sistema di crossfade/slot di riserva separato.
- Generalizzato `VoicePool` da 8 slot fissi a un pool di N slot riutilizzabile sia dalle 8 "colonne armoniche" di ogni frase sia dal meccanismo di furto.
- Build e `pluginval --strictness-level 10` verdi al primo tentativo (nessun errore di compilazione in questa sessione).

**Sessione 8 — scoperta e verifica del PSOLA esterno (richiesta esplicita: nessuna modifica al progetto):**
- Localizzata la cartella `TIPS` (non era nel progetto, ne' menzionata prima): trovata in `C:\Users\cazza\Downloads\TIPS` cercando piu' in profondita' dopo che una ricerca nel progetto non ha dato risultati.
- Letta per intero: `CLAUDE.md` (loro), `docs/prd.md` (confrontato con `diff` contro il nostro — **identico**), `docs/psola-spec.md`, `src/dsp/PitchShifter.h`, `src/dsp/PsolaShifter.h`, `tests/psola_test.cpp`, `CMakeLists.txt`.
- **Compilato ed eseguito io stesso** `psola_test.cpp` con MSVC (`cl.exe` via `vcvarsall.bat x64`), in una cartella temporanea creata sotto lo scratchpad di sessione — non sotto il progetto, non sotto `Downloads/TIPS`. Un solo intoppo (mancava `_USE_MATH_DEFINES` per `M_PI`, aggiunto come flag `/D_USE_MATH_DEFINES` senza toccare i sorgenti), poi tutti i 5 test superati con i numeri riportati nella spec confermati esattamente.
- Confrontata l'interfaccia `harm::PitchShifter` (loro) con la nostra `PitchShifter` per capire il gap di integrazione reale (vedi §2 per il dettaglio: `setF0` esplicito mancante da noi, unita' di misura diverse — alpha/beta vs semitoni, `minF0Hz` vs `stabilityLevel`).
- Nessun file di progetto toccato in questa sessione, ne' prima ne' dopo questo aggiornamento a `handsoff.md`, come esplicitamente richiesto dall'utente.

**Sessione 9 — integrazione PSOLA (scelta dall'utente tra le direzioni proposte):**
- Chiesta esplicitamente all'utente la direzione tra le opzioni segnalate come aperte in sessione 8 (PSOLA / MIDI CC / Formanti / solo processo): scelto "Integrare il PSOLA di TIPS", poi due decisioni di perimetro chieste esplicitamente (selezione motore a compile-time vs UI; solo motore vs anche Formanti; test in CI o solo locali) — vedi §6 vecchia versione per le opzioni presentate.
- **Esplorazione parallela** (due agenti) del nostro DSP attuale e dei sorgenti `TIPS`, poi lettura diretta dei file chiave (`PsolaShifter.h`, `PitchShifter.h` nostro e loro, `Voice.cpp`, `VoicePool.h`) prima di scrivere qualunque riga di piano, per non fidarsi solo dei riassunti degli agenti sui dettagli che contavano di piu' (le firme esatte, dove alloca il codice).
- Lavoro condotto a piccoli passi verificati singolarmente col compilatore (deque->ring, poi chunking, poi correzione `emitGrain`), non come un'unica riscrittura: ha permesso di isolare la regressione del problema 3 (sotto) al passo esatto che l'ha introdotta, invece di dover fare debug su un cambiamento cumulativo.
- **Il primo tentativo di correggere il bug di sovrapposizione (`emitGrain`) ha rotto un test che prima passava** (test 1, accuratezza di trasposizione a -12 semitoni): invece di allentare la soglia del test o di scartare il fix, si e' analizzato perche' l'uscita fosse davvero cambiata (grano troppo lungo = periodicita' originale reintrodotta), trovata la causa esatta e corretta la formula stessa (margine minimo analitico invece del margine largo iniziale). Coerente con la nuova regola 13 di `CLAUDE.md`: un test che fallisce dopo una correzione non e' automaticamente "il test sbagliato" — a volte lo e' davvero il codice.
- Costruito un controllo ad-hoc separato (fuori dalla suite permanente, in scratchpad) per verificare l'invarianza dell'uscita rispetto a come l'host suddivide le chiamate a `process()` — proprieta' non coperta dai 7 test della suite ma cruciale per la correttezza del chunking interno introdotto in questa sessione.
- Build reale (non solo compilazione isolata) e `pluginval --strictness-level 10` eseguiti a fine sessione, con esito riportato per intero (regola 8 di `CLAUDE.md`).

**Sessione 10 — primo test reale in Ableton, diagnosi e correzioni:**
- L'utente ha condiviso uno screenshot dell'editor e 8 osservazioni testuali dal primo uso reale in Ableton. Ogni causa e' stata confermata **leggendo direttamente il codice** (non ipotizzata): `PluginEditor.cpp`, `PhraseScheduler.cpp`, `OnsetDetector.cpp`, `PitchDetector.cpp` riletti integralmente prima di proporre qualunque fix.
- L'utente ha poi condiviso un file Excel (letto estraendo l'XML interno, `.xlsx` e' uno zip) che illustra il comportamento desiderato del futuro Pattern Ritmico con un esempio concreto (tre note in corsa scaglionata, una che taglia la coda dell'altra) — questo ha permesso di **riformulare il fix del bug principale come un bottone** ("Keep Tails") invece di un comportamento fisso, risolvendo il bug oggi E preparando il terreno per la feature futura senza lavoro sprecato.
- Due domande di chiarimento poste esplicitamente prima di scrivere il piano (comportamento del bottone Keep Tails e relativo default; se tentare anche un fix speculativo per la corsa onset/pitch o solo aggiungere diagnostica) — l'utente ha risposto scegliendo le opzioni consigliate per entrambe.
- Il vecchio file di piano (relativo all'integrazione PSOLA, sessione 9, gia' commessa) e' stato **sovrascritto** con il nuovo piano di questa sessione, come da istruzioni del sistema di planning per un task diverso.
- Build VST3 bloccata una volta da `LNK1104` (Ableton aveva ancora il plugin caricato) durante il primo tentativo — stesso caso esatto di sessione 6, risolto chiedendo all'utente di chiudere Ableton e ricompilando con successo.

**Sessione 11 — isteresi di intonazione, diagnosi corretta dall'utente:**
- Riletti `PhraseScheduler.cpp`, `PluginProcessor.cpp`, `HarmonyEngine.cpp`, `PresetLibrary.cpp` per formulare una prima ipotesi (instabilita' del rilevatore -> `freeAllPhrases()`), presentata all'utente in un piano — **l'utente l'ha corretta**, riconoscendo che la causa reale era l'assenza di isteresi sulla nota per il lookup armonico, con una proposta numerica precisa (±25 cent) e la matematica di Fix/Move verificata a mano con esempi in Hz.
- **Scoperto un bug nel MIO stesso design prima di consegnarlo**, non dall'utente: il primo tentativo di implementazione (passo incondizionato di un semitono oltre la soglia) rimbalza avanti e indietro ad ogni blocco durante uno scivolamento lento — provato scrivendo apposta un test "nessun rimbalzo" (test 4) e vedendolo fallire prima ancora di integrare il codice nel plugin. Corretto bloccando lo scatto al risultato di un arrotondamento standard (`std::min`/`std::max`), che non puo' mai superare la nota di destinazione. Lezione per le prossime sessioni: quando un test esplicito di un caso limite fallisce PRIMA dell'integrazione, e' esattamente il momento in cui costa meno correggerlo.
- Il piano di sessione 10 (finestra temporale) e' stato sovrascritto con quello di sessione 11 (isteresi in cent) dopo la correzione dell'utente, come da istruzioni del sistema di planning per un task diverso ma correlato.
- **Dopo aver consegnato il fix dell'isteresi, l'utente ha rifatto il test e il sintomo persisteva** (solo la prima nota si armonizzava, nonostante la label "Detected" seguisse correttamente le note): segno che l'isteresi era necessaria ma non sufficiente. Rianalizzato `PhraseScheduler::process()` e trovata la mia ipotesi originale (accantonata, non scartata come sbagliata) — confermato che `freeAllPhrases()` era ancora legato alla confidenza del pitch (`inputIsStable`), non alla presenza del segnale. Risolto separando i due segnali (vedi §2) **senza** la finestra temporale scartata in precedenza, usando invece un segnale gia' disponibile (`OnsetDetector::isGateOpen()`) che risponde alla domanda giusta ("il performer sta ancora suonando?") invece che a quella sbagliata ("il pitch e' confidente in questo esatto blocco?"). Lezione: due bug distinti possono produrre lo stesso sintomo osservabile — risolverne uno non implica che il sintomo sia sparito per la ragione giusta, va sempre riverificato.

**Sessione 12 — note saltate, causa confermata a lettura di codice (nessuna nuova ipotesi discussa con l'utente, il "perche'" era gia' verificabile nel codice esistente):**
- Rilette `PluginProcessor.cpp`, `PhraseScheduler.{h,cpp}`, `OnsetDetector.cpp`, `PitchDetector.cpp` per formulare la diagnosi (vedi §2) prima di proporre qualunque piano — nessuna riga scritta prima di aver rintracciato la causa esatta nel codice.
- Chiesto esplicitamente all'utente su cosa lavorare tra 4 opzioni proposte (corsa onset/pitch consigliata / taratura PSOLA / preset timbrici / pattern ritmico) e, separatamente, di specificare il feedback di qualita' lasciato in sospeso a fine sessione 11: l'utente ha scelto la corsa onset/pitch e ha aggiunto due dettagli (note saltate "senza una logica troppo precisa"; timbro "non fedele al segnale sorgente, robotico e granuloso").
- Il secondo dettaglio (timbro) e' stato deliberatamente lasciato fuori scope per questa sessione (l'utente ha scelto l'altra area) ma tre candidati concreti in `PsolaShifter.cpp` sono stati annotati per non ripartire da zero — vedi §2.
- Build, ctest, `pluginval` eseguiti ed esito riportato per intero (regola 8); nessun ascolto possibile da questa sessione (regola 12) — il fix resta "da confermare", non "completo".

**Sessione 12 (continuazione) — timbro, bug reale trovato e corretto in `PsolaShifter`:**
- L'utente ha confermato all'ascolto il fix precedente (note saltate) e ha chiesto di passare al feedback sul timbro lasciato in sospeso.
- Letto `PsolaShifter.cpp`/`Voice.cpp` insieme, non isolatamente: il bug e' emerso proprio dall'incrocio fra i due file (la formula di `beta` in `Voice.cpp`, che di per se' e' corretta, interagisce male con un'assunzione implicita in `PsolaShifter::emitGrain` che nessuno dei due file da solo rendeva visibile).
- **Verificato per calcolo A MANO prima di scrivere qualunque codice** (il deficit di ~19% a -12 semitoni), poi **verificato empiricamente scrivendo un test che fallisce sul codice non ancora corretto** (Test 8), poi corretto, poi riverificato che lo stesso test passa E che nulla di preesistente e' cambiato (Test 1/6 identici cifra per cifra) — stessa disciplina di sessione 11 (regola 13: un test che fallisce dopo un fix va capito, non scartato; e viceversa, un test che ora passa va controllato che non sia passato "per caso" riducendo la soglia).
- Deliberatamente NON esplorati in questa sessione gli altri due candidati (posizionamento epoch, correlazione fra le 8 istanze): un bug alla volta, verificato, per poter attribuire correttamente un eventuale miglioramento (o assenza di miglioramento) all'ascolto.

**Sessione 12 (continuazione) — click/scricchiolii, fix architetturale sulla dissolvenza delle voci:**
- L'utente ha riportato due sintomi (click ricorrenti; armonizzazione "non stabile al 100%", sospettando formanti/riarmonizzazioni) senza sapere la causa esatta — non ho chiesto di specificare ulteriormente, perche' un salto di ampiezza discontinuo (ciò che ho trovato) e' per definizione un click, verificabile a lettura di codice senza bisogno di altri dettagli dall'utente.
- Letti insieme `PhraseScheduler.cpp`, `PlayModeInput.cpp`, `PluginProcessor.cpp`: il pattern (voce smette di essere processata → ampiezza da piena a zero in un blocco) si ripeteva identico in quattro punti indipendenti — non un singolo bug isolato ma un gap architetturale sistemico (nessun meccanismo di dissolvenza esisteva per NESSUNA transizione di stato di una voce).
- Progettato un fix con DUE regole di sicurezza esplicite per non introdurre regressioni sul furto di voce (FR-52): (1) il furto d'emergenza resta istantaneo come prima (nessun ritardo nell'ottenere uno slot quando il pool e' esaurito — la continuita' li' viene comunque dal Glide dell'offset gia' esistente); (2) `allocateFreeSlot` preferisce rubare una frase gia' in rilascio piuttosto che una piena, minimizzando il disturbo quando serve rubare comunque.
- Build, ctest, `pluginval` eseguiti ed esito riportato per intero; nessun ascolto possibile in questa sessione — il fix resta "da confermare", non "completo", nonostante la certezza logica che elimina la CLASSE di bug diagnosticata.

---

## 5. Cosa non ha funzionato e perché

**Sessione 2:**
- Primo tentativo di aggiungere JUCE come submodule con `git submodule add --branch 8.0.15 --depth 1 ...` è fallito: git non riesce a fare shallow-clone diretto di un **tag** trattandolo come branch (`'origin/8.0.15' is not a commit and a branch '8.0.15' cannot be created from it`). Risolto clonando il submodule per intero (senza `--depth`/`--branch`) e poi facendo `git checkout 8.0.15` dentro il submodule. Costo: clone completo di JUCE invece che shallow, ma nessun impatto pratico (spazio disco abbondante).
- Ho impostato per errore `git config user.email` in locale nel repo appena creato (per permettere ai commit di funzionare, dato che non c'era un'identità git globale). Questo viola la regola di non toccare mai la git config. Rimediato subito con `git config --unset user.email` prima di procedere; per committare userò i flag `-c user.name=... -c user.email=...` per-comando, senza persistere nulla.
- **AU non compilabile né validabile su questa macchina**: è previsto e non un fallimento — il formato Audio Unit richiede macOS/Xcode. **Aggiornamento sessione 3**: verificato con successo in CI su `macos-latest`, incluso AU.

**Sessione 4:**
- **Errore di compilazione (pimpl con `std::optional<Impl>`)**: `PitchDetector` nascondeva `cycfi::q::pitch_detector` dietro un tipo `Impl` forward-dichiarato, tenuto in `std::optional<Impl>`. MSVC ha fallito con una serie di errori `C2139`/`C2079` (`Impl` incompleto non valido per `__is_trivially_destructible` ecc.) in ogni TU diversa da `PitchDetector.cpp`. Causa: a differenza di `std::unique_ptr<T>`, `std::optional<T>` istanzia i type-trait di `T` ovunque il tipo contenitore venga usato, non solo dove il costruttore/distruttore sono definiti — quindi non supporta tipi incompleti come member, anche con distruttore dichiarato esplicitamente e definito nel `.cpp`. Corretto sostituendo con `std::unique_ptr<Impl>` (il pattern pimpl standard, che invece funziona correttamente con tipi incompleti).
- **Copia automatica del VST3 fallita** (`COPY_PLUGIN_AFTER_BUILD TRUE`): il post-build step di JUCE prova a copiare in `C:\Program Files\Common Files\VST3`, che richiede permessi di amministratore non disponibili in questo ambiente ("Permission denied"). Non e' un bug del codice: e' il comportamento normale su Windows senza privilegi elevati (documentato nella CMake API di JUCE stessa). Risolto disattivando `COPY_PLUGIN_AFTER_BUILD` e documentando in `CMakeLists.txt`/qui come caricare il plugin in un host puntando alla cartella di build (vedi §6).
- Dopo questi due fix, build VST3 e `pluginval --strictness-level 10` sono verdi con la catena DSP reale.

**Sessione 5:**
- **Falso "exit code 0"**: la prima build dopo aver aggiunto `PresetLibrary`/`CsvIo` sembrava riuscita (notifica di completamento con exit code 0), ma conteneva in realta' una ventina di errori di compilazione reali (`juce::ValueTree` non trovato). Causa: avevo incanalato l'output della build in `| tail -N`, e in una pipeline bash l'exit code riportato e' quello dell'**ultimo** comando (`tail`, sempre 0), non quello di `cmake --build` — quindi il fallimento vero passava inosservato. Corretto aggiungendo `set -o pipefail` prima dei comandi di build/validazione successivi, cosi' l'exit code della pipeline riflette il primo comando che fallisce. **Lezione da applicare sempre in questo progetto**: mai fidarsi dell'exit code di un comando incanalato in `tail`/`head`/`grep` senza `pipefail`, controllare comunque il contenuto del log per errori.
- **Errore reale**: `juce::ValueTree` non e' dichiarato includendo solo `<juce_core/juce_core.h>` — `ValueTree` vive nel modulo separato `juce_data_structures`, non in `juce_core`. `PluginProcessor.cpp` aveva gia' funzionato perche' include `juce_audio_processors.h`, che dipende transitivamente da `juce_data_structures`; `PresetLibrary.h`, includendo solo `HarmonyPreset.h` (-> `juce_core`), non ce l'aveva. Risolto aggiungendo `#include <juce_data_structures/juce_data_structures.h>` a `PresetLibrary.h`.
- Dopo questi due fix, build VST3 e `pluginval --strictness-level 10` sono di nuovo verdi, con la `PresetLibrary` completa inclusa.

**Sessione 6:**
- **LNK1104 al primo tentativo di ricompilare**: "impossibile aprire il file Harmonizer.vst3" durante il link. Causa: Ableton Live aveva ancora il VST3 della sessione precedente caricato/aperto, e Windows blocca la sovrascrittura di un file (dll) in uso da un altro processo. Non un bug — chiesto all'utente di chiudere Ableton, poi ricompilato con successo. **Lezione per le prossime sessioni**: se un link fallisce con LNK1104/1103 su un file .vst3/.dll, prima ipotesi da controllare e' "un host ha ancora il plugin caricato", non un errore di codice.

**Sessione 7:**
- Nessun errore incontrato: build e pluginval verdi al primo tentativo, nonostante la riscrittura sostanziale di VoicePool e i due nuovi file (OnsetDetector, PhraseScheduler). Probabilmente dovuto ad aver progettato con cura la sincronizzazione dei thread PRIMA di scrivere codice (stesso schema gia' rodato in sessione 5/6), invece di scoprirla per tentativi.

**Sessione 8:**
- **`M_PI` non dichiarato compilando `psola_test.cpp` con MSVC**: `error C2065: 'M_PI': identificatore non dichiarato`. Non e' un bug dell'algoritmo — MSVC espone `M_PI` solo se `_USE_MATH_DEFINES` e' definito PRIMA di includere `<cmath>`, a differenza di g++/clang dove e' disponibile di default. Risolto passando `/D_USE_MATH_DEFINES` come flag di compilazione (nessuna modifica ai sorgenti, che restano quelli scaricati). **Correzione sessione 9**: la frase seguente (rimossa) attribuiva il problema al motore PSOLA in generale — falso, verificato leggendo `PsolaShifter.h`: non usa affatto `M_PI` (costante letterale scritta a mano), riguardava solo il file di test. Nel nostro porting evitato del tutto con una `constexpr double` locale.

**Sessione 9:**
- **La prima correzione del bug di sovrapposizione in `emitGrain` ha rotto il test 1**: allargando la semiampiezza del grano al margine "largo" (`W = P * max(1, 1/alpha)`, cio' che il commento originale sembrava suggerire), a -12 semitoni la f0 misurata tornava quella originale (errore di un'ottava esatto). Causa: a `beta=1` il grano e' una copia diretta, non trasposta, del segnale sorgente — un grano piu' lungo della spaziatura fra grani reintroduce direttamente la periodicita' ORIGINALE (non shiftata) al suo interno, che l'autocorrelazione del test rileva come dominante sulla periodicita' "strutturale" data dalla spaziatura. Non era un problema del test (la sua stessa logica anti-ottava, controllata, era corretta): l'uscita conteneva davvero energia forte alla frequenza originale. Risolto usando il margine minimo analiticamente necessario a far toccare i grani (`W = 1.2 * max(P, P/(2*alpha))`, derivato dalla condizione `Lg=2W >= synthPeriod`) invece del margine largo — tutti e 7 i test verdi dopo la correzione, confermato che nessun altro test e' peggiorato.
- Nessun errore di compilazione incontrato nel resto della sessione (build VST3/Standalone e `pluginval` verdi al primo tentativo dopo il porting completo).

Rischi e nodi noti da tenere d'occhio, già identificati nel PRD e non ancora affrontati:
- **Qualità del PSOLA proprietario**: rischio piu' alto secondo il PRD. **Aggiornamento sessione 9**: PSOLA e' ora INTEGRATO come motore di default, verificato numericamente (7 test verdi, incluso un test di sovrapposizione che ha scoperto e permesso di correggere un bug reale nell'algoritmo sorgente) e verificato in build reale (`pluginval` verde, latenza Fast misurata a 13.5ms, sotto il target PRD). Resta comunque solo su segnale sintetico (onda a impulsi + risonanza singola), non su registrazioni reali di sax/tromba/voce ne' provato all'ascolto dentro il nostro plugin. Il rischio "suona bene dal vivo" resta aperto finche' l'utente non lo prova in Ableton. Se anche cosi' non dovesse reggere, resta l'opzione ZTX PRO di Zynaptiq (costo/trattativa commerciale).
- **Tipo di plugin AU** deve essere Music Effect (`aumf`) fin da M0: è una decisione strutturale irreversibile dopo il rilascio (PRD §4.1).
- **FR-17 / FR-46**: implementate in sessione 7, **validate all'ascolto in sessione 10**. FR-17 (live-update su nota tenuta) confermata funzionante cosi' com'era. La risoluzione della tensione fra le due (cosa succede a una frase superata da un nuovo onset) e' risultata sbagliata cosi' com'era (bug di accumulo, vedi sopra) ed e' stata sostituita da un bottone utente ("Keep Tails") invece di un comportamento fisso — vedi `Phrase.h` e novita' sessione 10 in §2.
- **Sviluppatore singolo alle prime armi con C++** su un progetto di ~50 settimane — mitigato nel piano con milestone brevi e CI dal giorno uno.

---

## 6. Quale sarebbe il prossimo passo

**CONFERMATO in sessione 11 all'ascolto:** il canto legato (C→D→E) armonizza correttamente ogni nota — isteresi PitchLatch + `signalPresent` separato da `inputIsStable`.

**Sessione 12 — CONFERMATO all'ascolto:** fix della corsa onset/pitch — l'utente ha verificato in Ableton che "Active" non resta piu' a zero sul primo attacco, armonizza sempre tutte le note, nessuna persa per strada. Feedback esplicito ma non dettagliato voce per voce: non e' stato confermato singolarmente ne' il contatore "late-bindings" (se sale davvero) ne' il caso specifico "nota armonizzata sull'accordo della nota precedente" (causa 2 della diagnosi, `pitchDetector` stantio) — nessun segnale che sia ancora un problema, solo non verificato in modo esplicito e separato. Se in futuro dovesse ricomparire un caso limite (es. attacchi molto ravvicinati, staccato molto rapido), ripartire da li'.

**Sessione 12 (continuazione) — DA CONFERMARE ALL'ASCOLTO (priorita' immediata della prossima sessione):** fix del deficit di sovrapposizione dei grani in `PsolaShifter::emitGrain` quando la correzione formantica di default e' attiva (vedi §2). Bug reale confermato per calcolo e con un test che falliva prima del fix — ma non e' garanzia che risolva l'intero feedback "robotico e granuloso": l'utente deve riascoltare in Ableton, in particolare su voci shiftate parecchio verso il basso (ottava o piu'), e confermare se il suono e' piu' pulito o se resta un problema.

Se dopo l'ascolto il problema persiste (in tutto o in parte), i due candidati seguenti restano da esplorare, in ordine di probabilita':
1. Individuazione degli epoch come massimo di `|x|` in una finestra ±P/4 (`detectEpochs` in `PsolaShifter.cpp`): su segnali non impulsivi (synth, fiati, voce) puo' posizionare male gli epoch, incoerenza di fase fra grani percepita come "robotico".
2. Otto istanze PSOLA indipendenti sullo stesso ingresso: artefatti correlati che si sommano invece di mediarsi tra le voci.

**Sessione 12 (continuazione) — DA CONFERMARE ALL'ASCOLTO (priorita' immediata della prossima sessione):** fix dei click/scricchiolii — dissolvenza di ampiezza (8ms) su ogni transizione di voce (fine frase, silenzio, cella tornata vuota su nota tenuta/FR-17, uscita da Play mode), piu' lo stesso trattamento per dry/wet/bypass. L'utente deve riascoltare in Ableton e confermare se i click sono spariti e se la sensazione di "non stabile al 100%" e' migliorata — se persiste ANCHE dopo questo fix, la causa e' altrove (candidati Formanti/PSOLA sopra, o un quarto meccanismo non ancora trovato). La durata di 8ms e' un punto di partenza ragionevole, non tarata: se l'utente sente ancora un "pop" residuo (non un click secco ma percettibile) potrebbe servire allungarla; se sente un "fruscio"/sfarfallio innaturale sulle transizioni rapide potrebbe servire accorciarla.

**Prossimi passi possibili — da ridiscutere con l'utente:**
- **Timbro robotico/granuloso** (vedi sopra): prossimo candidato naturale, ha gia' indizi concreti da cui partire.
- Ascolti ancora non fatti: Formanti (costante `k=0.3` in `Voice.cpp`), controllo MIDI CC con hardware/automazioni reali (FR-36/37), modalita' Play (setup PRD §3.4, verificare FR-27/28).
- **Pattern ritmico (M3, `[V1.1]`)**: griglia piano-roll o modalita' millisecondi per il timing di entrata delle voci — fuori scope v1.0 per il PRD, ma l'architettura di `PhraseScheduler` e' gia' pronta ad accoglierlo.
- **Preset timbrici (FR-11..13)**: sistema di preset separato da quello armonico, non ancora iniziato — conterrebbe anche Formant Spread/offset per voce una volta esistente (oggi sono solo parametri APVTS piatti, come Stability/Dry/Glide).

**Limiti noti dopo questa sessione (da non scambiare per requisiti soddisfatti):**
- **`f0 <= 0` senza fade dedicato**: oggi `PhraseScheduler` smette semplicemente di processare le voci quando il segnale non e' stabile (`freeAllPhrases()`) — comportamento pre-esistente, non peggiorato ne' risolto da questa sessione.
- **Nessun crossfade esplicito sul cambio di Stability** dentro il motore stesso: la transizione si appoggia interamente al `Glide` gia' presente in `Voice` (sessione 6) — da verificare all'ascolto che basti.
- **Validato solo su segnale sintetico** (onda a impulsi + risonanza singola) — nessuna registrazione reale di sax/tromba/voce ancora provata, ne' in `tests/`, ne' all'ascolto.
- **Finestra di Hann ricalcolata per campione** in `emitGrain` (chiamata a `std::cos`): nota ottimizzazione non fatta, rilevante se la profilazione CPU con 8 voci (mai eseguita) rivelasse problemi rispetto al budget ≤15% del PRD §1.3.
- **`SpectralShifter` non piu' usato ma ancora compilato**: se in futuro si rimuove per pulizia, verificare prima che nessuno faccia piu' riferimento a `HARMONIZER_USE_SPECTRAL_SHIFTER`.
- **Formanti (FR-39..42) implementate ma non tarate**: costante `k=0.3` presa cosi' com'era dalla spec sorgente, mai all'ascolto. Nessun test numerico scritto per le Formanti in questa sessione (a differenza del motore PSOLA) — la correzione formantica e' per natura una preferenza timbrica soggettiva, non ha un criterio "giusto/sbagliato" oggettivo come l'accuratezza di trasposizione.
- **Controllo MIDI CC (FR-29..38) implementato ma mai provato con hardware/host reale**: la logica di precedenza e' testata numericamente (`override_manager_test`), ma il parsing dei messaggi MIDI veri (`CcRouter`) no — nessun target di test dedicato (avrebbe richiesto linkare `juce_audio_basics`), verificato solo a lettura e con `pluginval`. UI: il selettore root/preset non riflette visivamente un override CC attivo (l'audio segue comunque correttamente il CC).
- **Modalita' Play (FR-24..28) implementata ma mai provata con hardware/host reale**: stesso limite del controllo CC — nessun test dedicato (coinvolge `juce::MidiBuffer`/`juce::MidiMessage`), verificata solo a lettura e con `pluginval`. Nessuna regola di furto definita per oltre 8 note simultanee (il PRD non la specifica per Play, a differenza di FR-51/52 per Harmonizer): le eccedenti restano semplicemente mute. FR-28 (nessun click nel passaggio di modalita') non verificato all'ascolto.
- **"Keep Tails" (sessione 10) e' binario oggi**: tronca subito o lascia vivere per sempre. Il significato pieno descritto dall'utente (tronca solo la coda non ancora suonata di una frase, lascia finire il resto) richiede il Pattern Ritmico (FR-47..49, `[V1.1]`), non costruito qui — vedi `Phrase.h`.
- **Mancato riconoscimento pitch con alcuni synth / primo attacco a "0 voci" (sessione 10, punti 2/5 del test)**: causa confermata e corretta in sessione 12 (corsa `OnsetDetector`/`PitchDetector`, vedi §2) — **CONFERMATO risolto all'ascolto** dall'utente.
- **Isteresi di intonazione + `signalPresent` (sessione 11): CONFERMATI all'ascolto** — canto legato C→D→E armonizza correttamente ogni nota. Soglia ±25 cent e soglie del gate (-24/-30/-36 dB, ereditate dal rilevamento onset di sessione 7) restano comunque punti da tarare ulteriormente se emergono altri casi limite (es. un portamento su un intervallo molto ampio, o un legato molto piano/debole).
- **Note saltate senza logica precisa (sessione 12)**: causa confermata a lettura di codice (corsa onset/pitch + `pitchDetector` mai resettato), fix scritto, verificato con build/test/pluginval e **CONFERMATO all'ascolto** — nessuna nota persa, "Active" mai a zero al primo attacco. Non verificato singolarmente il caso "accordo della nota precedente" (causa 2) ne' il contatore "late-bindings": nessun segnale che sia un problema, solo non isolato esplicitamente.
- **Qualità del suono — timbro robotico/granuloso (sessione 12)**: un bug reale trovato e corretto in `PsolaShifter::emitGrain` (deficit di sovrapposizione dei grani quando la correzione formantica di default e' attiva), verificato per calcolo e con un test (Test 8) che falliva prima del fix — **NON ancora confermato all'ascolto**. Restano due candidati non esplorati se il problema persiste in tutto o in parte: posizionamento degli epoch su segnali non impulsivi, correlazione fra le 8 istanze PSOLA.
- **Click/scricchiolii + armonizzazione "non stabile al 100%" (sessione 12)**: bug architetturale confermato a lettura di codice — nessuna voce aveva mai una dissolvenza di ampiezza quando smetteva di essere processata (fine frase, silenzio, cella tornata vuota su nota tenuta/FR-17, uscita da Play mode); stesso problema su dry/wet/bypass. Fix scritto (dissolvenza fissa 8ms, rilascio morbido delle frasi con `Phrase::releasing`), verificato con build/test/pluginval — **NON ancora confermato all'ascolto**. Durata 8ms non tarata. Limite noto accettato: una frase in rilascio rubata da FR-52 prima di finire la dissolvenza si interrompe di netto (salto pero' molto piu' piccolo, dalla poca ampiezza residua).

**A seguire, per chiudere M0 davvero (non urgente per continuare lo sviluppo):**
- Avviare le pratiche per i certificati di firma/notarizzazione (Apple Developer ID, code signing Windows).
- Decidere la licenza JUCE (Indie vs commerciale, in base al fatturato previsto).

**Questioni ancora aperte:**
- Nome prodotto/azienda non deciso: `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt` restano placeholder.
- Solo un preset (Min) e' verificato contro il prototipo reale — gli altri 6 sono standard jazz generici, da correggere quando l'utente fornira' i dati veri (ora importabili via CSV).
- Risoluzione FR-17/FR-46: validata all'ascolto e corretta in sessione 10 (bottone Keep Tails) — vedi sopra.
- Valori della tabella Stability->minF0Hz (sessione 9) e della costante `k=0.3` delle Formanti (sessione 9) sono ancora un punto di partenza, non tarati all'ascolto.
- Causa del mancato riconoscimento pitch con alcuni synth (punti 2/5 del test di sessione 10): determinata in sessione 12 (corsa onset/pitch), fix scritto — resta la conferma all'ascolto, vedi sopra.
- **Push**: verificato a inizio sessione 12 — `git status`/`git log origin/main..HEAD` puliti, tutti i commit fino a sessione 11 gia' sul remoto (la nota precedente su 5 commit "solo locali" era obsoleta). I commit di sessione 12 non sono ancora stati creati: richiedere conferma esplicita all'utente prima di committare/pushare, come da istruzioni di sistema (mai commit non richiesti).
