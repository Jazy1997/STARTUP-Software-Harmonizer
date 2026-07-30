# Handoff — HARMONIZER

> Ultimo aggiornamento: 2026-07-30 (sessione 7)

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

**Fase: M0 completo dal punto di vista tecnico (restano solo licenza JUCE, certificati, nome prodotto — decisioni non tecniche, vedi §6). Vertical slice DSP M1/M2/M3 in corso su richiesta esplicita dell'utente: PresetLibrary (M2), Fix/Move+Glide+Stability (M1) e ora anche il motore a frasi (M3, FR-43..53) sono completi e funzionali.**

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
└── src/
    ├── PluginProcessor.{h,cpp}, PluginEditor.{h,cpp}
    ├── dsp/PitchDetector.{h,cpp}, PitchShifter.h, SpectralShifter.{h,cpp}, Glide.h, OnsetDetector.{h,cpp}
    ├── harmony/HarmonyPreset.h, HarmonyEngine.{h,cpp}, PresetLibrary.{h,cpp}, CsvIo.{h,cpp}
    └── voices/Voice.{h,cpp}, VoicePool.{h,cpp}, Phrase.h, PhraseScheduler.{h,cpp}
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

Nessuna modifica a `PRD-Harmonizer-v1.md`. Questa tabella va estesa (non sovrascritta) a ogni sessione futura.

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

Rischi e nodi noti da tenere d'occhio, già identificati nel PRD e non ancora affrontati:
- **Qualità del PSOLA proprietario** non ancora validata all'ascolto (previsto a fine M1). È il rischio più alto per il prodotto: se non regge, serve valutare ZTX PRO di Zynaptiq (costo/trattativa commerciale).
- **Tipo di plugin AU** deve essere Music Effect (`aumf`) fin da M0: è una decisione strutturale irreversibile dopo il rilascio (PRD §4.1).
- **FR-17 / FR-46**: implementate entrambe in sessione 7 con una risoluzione esplicita (solo la frase piu' recente segue dal vivo il preset) — **da validare all'ascolto**, come il PRD stesso richiedeva. Non ancora fatto: l'utente non ha ancora potuto testare in Ableton.
- **Sviluppatore singolo alle prime armi con C++** su un progetto di ~50 settimane — mitigato nel piano con milestone brevi e CI dal giorno uno.

---

## 6. Quale sarebbe il prossimo passo

**Immediato — testare in Ableton Live (l'utente non ha ancora potuto, da due sessioni):**
1. Ricaricare "Harmonizer" dalla cartella VST3 custom (`build/Harmonizer_artefacts/Release/VST3`) gia' configurata in precedenza.
2. Provare in particolare il motore a frasi (novita' di questa sessione): suonare una linea veloce di note diverse e verificare che si sentano piu' armonizzazioni sovrapposte (FR-45), che cambiare accordo mentre si tiene una nota aggiorni dal vivo la voce (FR-17), e che cambiare accordo DOPO essere passati a una nuova nota NON alteri retroattivamente la frase precedente (FR-46). Provare anche ad abbassare "Voice Cap" a un valore piccolo (es. 4) e suonare rapidamente per sentire il furto di frase in azione.
3. Continuare a provare anche le novita' di sessione 6 (Fix/Move con vibrato, cambio Stability) se non gia' fatto.
4. Ricordare i limiti noti: latenza minima ~30ms, solo preset Min verificato, pattern ritmico non implementato (tutte le voci di una frase entrano in sync), risoluzione FR-17/FR-46 non ancora validata all'ascolto.

**Prossima area di sviluppo — da ridiscutere con l'utente**, tra le direzioni proposte e non ancora scelte:
- **Controllo MIDI CC (M4)**: router dei 3 CC, modalita' Play, override vs automazione host.
- **Formanti** (FR-39..42): correzione automatica in funzione dello shift, knob Spread, offset per voce.
- **Pattern ritmico (M3, `[V1.1]`)**: griglia piano-roll o modalita' millisecondi per il timing di entrata delle voci — fuori scope v1.0 per il PRD, ma l'architettura di `PhraseScheduler` e' gia' pronta ad accoglierlo.
- Sostituire `SpectralShifter` con `PsolaShifter` proprietario dietro la stessa interfaccia (FR-62) — lavoro DSP sostanzioso a se stante; risolverebbe anche la latenza minima troppo alta.

**A seguire, per chiudere M0 davvero (non urgente per continuare lo sviluppo):**
- Avviare le pratiche per i certificati di firma/notarizzazione (Apple Developer ID, code signing Windows).
- Decidere la licenza JUCE (Indie vs commerciale, in base al fatturato previsto).

**Questioni ancora aperte:**
- Nome prodotto/azienda non deciso: `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt` restano placeholder.
- Solo un preset (Min) e' verificato contro il prototipo reale — gli altri 6 sono standard jazz generici, da correggere quando l'utente fornira' i dati veri (ora importabili via CSV).
- Risoluzione FR-17/FR-46 (sessione 7) da validare all'ascolto appena possibile.
- Commit/push di questa sessione: da confermare con l'utente prima di procedere — verificare `git status` all'inizio della prossima sessione.
