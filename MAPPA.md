# MAPPA — HARMONIZER

> Mappa dei moduli **com'è adesso**, non com'era o come sarà. Si riscrive quando la
> struttura cambia, non a ogni sessione. Sostituisce la vecchia §3 di `handsoff.md`.
> Aggiornata: 2026-08-10 (s.30).

---

## Catena del segnale

```
Audio in (mono)
   │
   ├────────────────────────────────────► Dry path ──────────────┐
   ▼                                                             │
PitchDetector (Cycfi Q)  ──► notaMIDI + confidenza + f0          │
   │                                                             │
   ├──► OnsetDetector ──► trigger di frase                       │
   ▼                                                             │
PitchLatch (isteresi ±25 cent)                                   │
   │                                                             │
   ▼                                                             │
HarmonyEngine   d = (notaMIDI − fondamentale) mod 12             │
   │            offsets[8] = preset.tabella[d]                   │
   ▼                                                             │
PhraseScheduler (frasi indipendenti, tetto voci, furto)          │
   │                                                             │
   ▼                                                             │
Voice ×N:  PitchShifter → formanti → Glide → Gain → Pan          │
   │                                                             │
   ▼                                                             ▼
  Wet sum ──────────────────────────► dryWetMix ──► Out (stereo)
```

In **Play mode** `HarmonyEngine` e la tabella sono scavalcati: `PlayModeInput` pilota fino
a 8 voci dalle note MIDI ricevute. Il dry resta sempre udibile.

---

## `src/` — 45 file

### Radice
| File | Ruolo |
|---|---|
| `PluginProcessor.{h,cpp}` | `processBlock`, **`ParamIDs` + `createParameterLayout()`** (45 parametri), serializzazione dello stato, `computeDryWetGains` |
| `PluginEditor.{h,cpp}` | Le tre schermate, navbar, tutti gli attachment, sync parametro→UI via `Timer` |

> **Nota**: `src/state/ParameterLayout.*` e `StateSerializer.*` previsti dal PRD §9.3
> **non esistono**. Il layout e la serializzazione vivono dentro `PluginProcessor.cpp`.

### `src/dsp/` — 11 file
| File | Ruolo |
|---|---|
| `PitchShifter.h` | **Interfaccia astratta** (FR-62) + `namespace Stability` (5 livelli: Fast, Fast+, Balanced, Accurate−, Accurate; default Balanced) |
| `PitchShifterFactory.cpp` | L'unico punto che sceglie l'implementazione concreta, via `HARMONIZER_USE_SPECTRAL_SHIFTER` |
| `PsolaShifter.{h,cpp}` | **Motore di default.** `detectEpochs()` con `findEpochByCorrelation()`, `synthesise()`, `emitGrain()`, ring di epoch, `processChunk` |
| `SpectralShifter.{h,cpp}` | Signalsmith Stretch. Compilato, non usato — via di fuga (D-02) |
| `PitchDetector.{h,cpp}` | Wrapper Cycfi Q, pimpl con `unique_ptr` (**non `optional`**: MSVC istanzia i type-trait su tipi incompleti) |
| `OnsetDetector.{h,cpp}` | `cycfi::q::onset_gate`. Soglie: onset −24 dB, slope −30 dB, **release −45 dB** (B-01) |
| `Glide.{h,cpp}` | Rampe. `processRamp` è **campione-per-campione**: la versione per blocco era no-op a 4096 campioni (B-03) |

### `src/harmony/` — 7 file
| File | Ruolo |
|---|---|
| `HarmonyPreset.h` | `Cell` / `Table` / `Preset`. `constexpr int numVoices = 8` |
| `PresetLibrary.{h,cpp}` | Lista ordinata, CC posizionale, `movePreset`, copy-on-write. `makeFactoryPresets()` genera i 7 tipi via `generateDropVoicingTable` |
| `HarmonyEngine.{h,cpp}` | `degreeOf` / `getOffsets` |
| `CsvIo.{h,cpp}` | Import/export CSV (FR-03) |
| `PitchLatch.h` | Isteresi di intonazione, tolleranza ±25 cent (B-09). **Candidato + adozione diretta** (s.31, D-17): non passa mai per una nota intermedia, e l'attesa `kNoteSettleMs` è in ms contati sui campioni del blocco. `prepare(sampleRate)` + `update(nota, onAttack, numSamples)` |

### `src/voices/` — 8 file
| File | Ruolo |
|---|---|
| `Voice.{h,cpp}` | Una voce. `processAdd` **stereo**, `processWarmOnly` / `goCold` (B-04), `justReactivated` (B-01), glide di ampiezza/gain/pan/offset, `kDeclickMs = 8 ms` |
| `Phrase.h` | Stato di una frase. `emptyCellSamples` per voce (s.28) |
| `PhraseScheduler.{h,cpp}` | Frasi indipendenti, late-binding degli slot, furto della più vecchia (FR-52), loop di mixing. Il loop di mixing dà un **verdetto per voce ogni blocco**: oltre `numRequestedVoices` la voce sfuma e lo slot torna al pool (FR-19, s.30) |
| `VoicePool.{h,cpp}` | Tetto di slot, `requestStabilityChange`, `swapShifterNoAlloc` |
| `EmptyCellHold.h` | Funzione pura `stepEmptyCellHold`, zero dipendenze |

### `src/midi/` — 6 file
| File | Ruolo |
|---|---|
| `CcRouter.{h,cpp}` | I 3 CC configurabili + MIDI Learn + canale. **I numeri di CC non sono parametri APVTS**: serializzati a parte |
| `OverrideManager.{h,cpp}` | Precedenza CC vs automazione (FR-36/37/38) |
| `PlayModeInput.{h,cpp}` | Modalità Play. Gain/pan **e formanti** (FR-42, s.30) **per indice di slot**, non per colonna armonica |

### `src/ui/` — 8 file
| File | Ruolo |
|---|---|
| `PresetListEditor.{h,cpp}` | Lista con drag&drop, badge sui primi 5, CC accanto al nome |
| `PresetTableEditor.{h,cpp}` | Tabella 12×8 editabile |
| `RootNoteGrid.{h,cpp}` | Griglia cromatica **2 colonne × 6 righe**, indice = `riga·2 + colonna` |
| `CellInputParser.h` | Parsing di una cella: distingue `0` da vuoto, rifiuta il testo spazzatura (D-05) |
| `DegreeNames.h` | Nomi leggibili dei gradi (R, b2, 2, b3, …) |

### Cartelle vuote
`src/state/` · `src/licensing/` · `resources/factory_presets/` (i preset di fabbrica sono
generati in codice, non caricati da file)

---

## Parametri APVTS — 45

Tutti con version hint `1`. **Un ID pubblicato non cambia mai** (`CLAUDE.md` regola 6).

**Scalari (13)**

| ID | Tipo | Range / default |
|---|---|---|
| `rootNote` | Choice | 12 note C..B, def. 0 |
| `presetIndex` | Int | 1..128, def. 1 |
| `numVoices` | Int | 1..8, def. 4 |
| `dryLevel` | Float | 0..1, def. 1.0 — **legacy, non più letto** (D-07) |
| `wetLevel` | Float | 0..1, def. 0.8 — **legacy, non più letto** (D-07) |
| `dryWetMix` | Float | 0..1, def. 0.7 |
| `stabilityLevel` | Choice | 5 posizioni, def. Balanced |
| `glideTimeMs` | Float | 0..200, def. 30 |
| `formantSpread` | Float | 0..1, def. 1.0 |
| `bypass` | Bool | false |
| `playModeEnabled` | Bool | false |
| `keepPhraseTails` | Bool | false |
| `maxSimultaneousVoices` | Int | 1..32, def. 32 |

**Per voce, 1-based, v = 1..8 (32)** — `voiceFix<v>` (Bool) · `voiceFormantOffset<v>`
(Float −24..+24 st) · `voiceGain<v>` (Float −60..+6 dB) · `voicePan<v>` (Float −1..+1)

---

## `tests/` — 14 file

**In `ctest` (8)** — nessun Catch2 (D-11). Le prime 7 sono JUCE-free e compilabili con un
`g++` nudo; `phrase_scheduler` linka `juce_core` e gira **solo** in `ctest` (D-16)

| Nome | Cosa verifica |
|---|---|
| `psola` | 12 test: accuratezza di trasposizione, ortogonalità pitch/formanti, discontinuità, monotonia della latenza, inviluppo di sovrapposizione (con la `beta` reale di `Voice.cpp`), riattivazione di slot. **Test 10/11/12 sono verifiche di trasparenza, non riproducono i bug** — soglia onesta "non degrada" |
| `voice` | Inviluppo alla riattivazione su 5 Stability × block 64/1024 con controllo negativo. H3 (riattivazione), **H5** (`processWarmOnly`: 23.95 ms → 0.00 ms). T-1..T-5: pan, potenza costante, **regressione bit-per-bit al default**, gain a zero, anti-click al confine di blocco. **T-6/T-7** (s.30, FR-40/41): il picco formantico si sposta della quantità prevista con `spread` e con l'offset manuale, che si sommano in semitoni |
| `glide` | 15 verifiche. TEST 1 riproduce il bug originale (salto 88% di fondoscala in un campione con blocco 4096) |
| `pitch_latch` | 11 gruppi, incluso l'anti-rimbalzo che ha scoperto il bug prima dell'integrazione. **TEST 7 riscritto in s.31** (asseriva il passo di un semitono per chiamata, cioè il difetto B-13). **TEST 9/10/11** (s.31): nessuna nota intermedia su salti ±1..12; stessa sequenza a block 128/512/1024/4096; una stima confidente ma sbagliata di 14.5 ms non viene adottata |
| `override_manager` | 6 test sulla precedenza CC/automazione |
| `cell_input_parser` | 19 verifiche sulla distinzione `0` / vuoto / spazzatura |
| `empty_cell_hold` | 12 verifiche |
| `phrase_scheduler` | **Linka `juce_core`** (D-16). P-1..P-4 (s.30, FR-19/B-10): il conteggio voci scende col selettore, l'ampiezza scende al livello giusto, la risalita non regredisce, la voce si spegne sfumando e non tagliata. P-1/P-2 verificati falliti sul codice pre-fix. **P-5/P-6** (B-12): P-5 distingue per costruzione se un click in aggiunta è preesistente o introdotto da B-10; **P-6 isola la sola voce aggiunta per differenza fra due rig** e ne misura il tempo di salita — è la metrica che ha trovato B-12 dove `maxJump` sul mix non vedeva nulla |

**Sonde diagnostiche, NON in ctest** — dipendono da `SAMPLE TEST/`, non versionato (5)

| Nome | Cosa fa |
|---|---|
| `sample_click_finder` | Il coltellino svizzero. `dumpPitchTrace`, `runFixedF0`, `runRetriggered`/`runProduction`, `runLiveHarmony` (tabella custom), `runFaseZeroTrace` (traccia il gate reale), Passate 1–7 |
| `real_export_probe` | Confronto DRY vs WET reale, cadenza dei disturbi, `--trace <inizio> <fine>` |
| `voice_reference_probe` | Confronto contro un riferimento esterno (AutoShift) |
| `envelope_probe` | Dump dell'inviluppo RMS fine di un WAV su una finestra |
| `degree_trace_probe` | **(s.31)** Quale sequenza di offset la catena di controllo chiede al motore. `PitchDetector`/`OnsetDetector`/`PitchLatch`/`stepEmptyCellHold` veri, **nessun `Voice` né PSOLA**: dice se un difetto nasce prima della sintesi o dentro. Conta le "corse di offset applicato" e quelle di passaggio |

**Header condivisi** — `SampleAnalysis.h` (readWav, measureFrame, envelopeRms, fitWindow,
findClicks, HNR) · `TestSignals.h` (generatori, measureF0, formantPeak, goertzelMag)

---

## Build e CI

- **Formati**: VST3, AU (`aumf`), Standalone. macOS 11+ `arm64;x86_64`, Windows 10+ x64.
- **Submodule**: `libs/JUCE` (8.0.15), `libs/q` (Cycfi Q, MIT), `libs/signalsmith-stretch` (MIT).
  Q e Signalsmith sono header-only.
- **CI** (`.github/workflows/build.yml`): job `dsp-tests` su ubuntu come gate rapido, poi
  build su windows-latest e macos-latest con `pluginval --strictness-level 10` su VST3
  (entrambe) e AU (solo macOS).
  ⚠️ Il gate ricompila a mano **3 suite su 8** e non invoca mai `ctest` — vedi A-06.
  `phrase_scheduler` non è nemmeno compilabile con quei `g++` (linka `juce_core`, D-16).
- **Locale**: `tools/pluginval.exe`. Il fallimento di copia post-build è atteso (D-12).

---

## Trappole già pagate

Da non ripagare una seconda volta.

- **`LNK1104` su `Harmonizer.vst3`** → un host ha ancora il plugin caricato. Chiudi Ableton.
- **Verifica UI solo su Debug** → ricompila **anche la Release** prima di chiedere conferma:
  Ableton può scansionare quella.
- **`| tail` maschera l'exit code** → `set -o pipefail`, e leggi comunque il log.
- **`std::optional<T>` con tipo incompleto** → non compila su MSVC. Usa `unique_ptr`.
- **`juce::ValueTree`** vive in `juce_data_structures`, non in `juce_core`.
- **Screenshot della Standalone** → `SetProcessDPIAware()` prima di `GetWindowRect`.
- **`M_PI` su MSVC** → serve `_USE_MATH_DEFINES`, o una `constexpr double` locale.
