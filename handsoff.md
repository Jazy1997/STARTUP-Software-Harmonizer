# Handoff — HARMONIZER

> Ultimo aggiornamento: 2026-07-31 (sessione 9)

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

**Fase: M0 completo dal punto di vista tecnico (restano solo licenza JUCE, certificati, nome prodotto — decisioni non tecniche, vedi §6). Vertical slice DSP M1/M2/M3 in corso su richiesta esplicita dell'utente: PresetLibrary (M2), Fix/Move+Glide+Stability (M1) e motore a frasi (M3, FR-43..53) sono completi e funzionali. Sessione 9: il PSOLA proprietario scoperto in sessione 8 e' stato PORTATO E INTEGRATO come motore di default dietro `PitchShifter` — vedi sotto.**

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
- **FR-17 / FR-46**: implementate entrambe in sessione 7 con una risoluzione esplicita (solo la frase piu' recente segue dal vivo il preset) — **da validare all'ascolto**, come il PRD stesso richiedeva. Non ancora fatto: l'utente non ha ancora potuto testare in Ableton.
- **Sviluppatore singolo alle prime armi con C++** su un progetto di ~50 settimane — mitigato nel piano con milestone brevi e CI dal giorno uno.

---

## 6. Quale sarebbe il prossimo passo

**Immediato — testare in Ableton Live (l'utente non ha ancora potuto, da tre sessioni):**
1. Ricaricare "Harmonizer" dalla cartella VST3 custom (`build/Harmonizer_artefacts/Release/VST3`) gia' configurata in precedenza — ricompilato in questa sessione, contiene sia il motore PSOLA sia le Formanti.
2. **Priorita' assoluta: ascoltare il motore PSOLA per la prima volta**, dato che finora e' stato validato solo su segnale sintetico e in build (mai all'ascolto — regola 12 di `CLAUDE.md`). In particolare:
   - Confronto con la sensazione di reattivita' di prima (Signalsmith): la latenza dichiarata e' scesa da ~30ms a 13.5ms (Fast) / 29.9ms (Accurate) — dovrebbe sentirsi.
   - **Voicing a -12 semitoni e sotto**, il caso specifico su cui questa sessione ha trovato e corretto un bug reale (vedi §2/§5): verificare che non ci siano vuoti/artefatti percepibili scendendo di un'ottava o piu'.
   - Fix/Move con vibrato, cambio Stability, motore a frasi — tutte le funzionalita' di sessione 6/7, ora sopra un motore diverso.
   - Se qualcosa non convince all'ascolto: la mappatura Stability->minF0Hz (`{165,130,100,85,70}` Hz, in `PsolaShifter.cpp`) e' un singolo array con valori esplicitamente segnalati come "di partenza, da tarare" — e' il primo posto dove intervenire.
3. **Poi ascoltare le Formanti** (slider "Fmt Spread" + 8 knob "Fmt/Voice" nell'editor): verificare che shift verso il basso schiarisca davvero (non impastato) e verso l'alto scurisca (non "chipmunk"), a Spread pieno (default) e a zero (deve essere impercettibile/nullo). Se l'effetto e' troppo debole o troppo marcato, la costante `k=0.3` in `Voice.cpp` (`kFormantSpreadK`) e' il primo posto dove intervenire — mai tarata, presa cosi' com'era nella spec sorgente.
4. **Provare il controllo MIDI CC con un controller fisico o le automazioni dell'host** (mai fatto, vedi §2): mandare CC sui 3 numeri di default (Root=20, Preset=21, Bypass=22, canale Omni) o usare "Learn"; verificare in particolare FR-36 (automazione host in scrittura + CC in arrivo -> vince il CC; stop del transport -> torna l'automazione) e FR-37 in standalone (nessuna revoca).
5. Ricordare i limiti noti (aggiornati, vedi sotto): `f0<=0` senza fade dedicato, validato solo su segnale sintetico, risoluzione FR-17/FR-46 (sessione 7) ancora da validare all'ascolto, selettore root/preset in UI che non riflette un override CC attivo.

**Prossima area di sviluppo — da ridiscutere con l'utente** una volta chiuso il giro di prova sopra:
- **Modalita' Play (FR-24..28)**: rimasta esplicitamente fuori da questo passaggio (l'utente aveva scelto CC, non Play). Riusa la stessa `VoicePool`, ma richiede una decisione su come si intreccia col `PhraseScheduler` guidato da onset (oggi pensato per la modalita' Harmonizer).
- **Pattern ritmico (M3, `[V1.1]`)**: griglia piano-roll o modalita' millisecondi per il timing di entrata delle voci — fuori scope v1.0 per il PRD, ma l'architettura di `PhraseScheduler` e' gia' pronta ad accoglierlo.
- **Preset timbrici (FR-11..13)**: sistema di preset separato da quello armonico, non ancora iniziato — conterrebbe anche Formant Spread/offset per voce una volta esistente (oggi sono solo parametri APVTS piatti, come Stability/Dry/Glide).

**Limiti noti dopo questa sessione (da non scambiare per requisiti soddisfatti):**
- **`f0 <= 0` senza fade dedicato**: oggi `PhraseScheduler` smette semplicemente di processare le voci quando il segnale non e' stabile (`freeAllPhrases()`) — comportamento pre-esistente, non peggiorato ne' risolto da questa sessione.
- **Nessun crossfade esplicito sul cambio di Stability** dentro il motore stesso: la transizione si appoggia interamente al `Glide` gia' presente in `Voice` (sessione 6) — da verificare all'ascolto che basti.
- **Validato solo su segnale sintetico** (onda a impulsi + risonanza singola) — nessuna registrazione reale di sax/tromba/voce ancora provata, ne' in `tests/`, ne' all'ascolto.
- **Finestra di Hann ricalcolata per campione** in `emitGrain` (chiamata a `std::cos`): nota ottimizzazione non fatta, rilevante se la profilazione CPU con 8 voci (mai eseguita) rivelasse problemi rispetto al budget ≤15% del PRD §1.3.
- **`SpectralShifter` non piu' usato ma ancora compilato**: se in futuro si rimuove per pulizia, verificare prima che nessuno faccia piu' riferimento a `HARMONIZER_USE_SPECTRAL_SHIFTER`.
- **Formanti (FR-39..42) implementate ma non tarate**: costante `k=0.3` presa cosi' com'era dalla spec sorgente, mai all'ascolto. Nessun test numerico scritto per le Formanti in questa sessione (a differenza del motore PSOLA) — la correzione formantica e' per natura una preferenza timbrica soggettiva, non ha un criterio "giusto/sbagliato" oggettivo come l'accuratezza di trasposizione.
- **Controllo MIDI CC (FR-29..38) implementato ma mai provato con hardware/host reale**: la logica di precedenza e' testata numericamente (`override_manager_test`), ma il parsing dei messaggi MIDI veri (`CcRouter`) no — nessun target di test dedicato (avrebbe richiesto linkare `juce_audio_basics`), verificato solo a lettura e con `pluginval`. Modalita' Play (FR-24..28) non implementata. UI: il selettore root/preset non riflette visivamente un override CC attivo (l'audio segue comunque correttamente il CC).

**A seguire, per chiudere M0 davvero (non urgente per continuare lo sviluppo):**
- Avviare le pratiche per i certificati di firma/notarizzazione (Apple Developer ID, code signing Windows).
- Decidere la licenza JUCE (Indie vs commerciale, in base al fatturato previsto).

**Questioni ancora aperte:**
- Nome prodotto/azienda non deciso: `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt` restano placeholder.
- Solo un preset (Min) e' verificato contro il prototipo reale — gli altri 6 sono standard jazz generici, da correggere quando l'utente fornira' i dati veri (ora importabili via CSV).
- Risoluzione FR-17/FR-46 (sessione 7) da validare all'ascolto appena possibile.
- **Nuova**: valori della tabella Stability->minF0Hz (sessione 9) sono un punto di partenza, non tarati all'ascolto.
- Commit/push di questa sessione: **fatto** — due commit (`40ef069` motore PSOLA, `6e22c78` Formanti) pushati su `origin/main` (`065ba4b..6e22c78`) su richiesta esplicita dell'utente. Verificare l'esito della CI (job `dsp-tests` nuovo + build Windows/macOS) all'inizio della prossima sessione.
