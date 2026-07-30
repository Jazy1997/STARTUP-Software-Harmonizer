# Handoff — HARMONIZER

> Ultimo aggiornamento: 2026-07-30 (sessione 3)

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

**Fase: M0 "Fondamenta" in corso — scaffolding iniziale completato e verificato localmente su Windows.**

Fatto e verificato:
- Repository git inizializzato. **Remote configurato**: `origin` = https://github.com/Jazy1997/STARTUP-Software-Harmonizer.git, branch `main`, primo commit pushato (`f85d357`) con `--force` sovrascrivendo un upload manuale precedente (4 file soli, senza src/submodule/CI). Repo pubblico.
- Il push ha incluso `.github/workflows/build.yml`, che ha `on: push`: questo ha **automaticamente avviato una run di GitHub Actions** al primo push (non era l'intenzione — l'utente aveva chiesto di evitare la build in questo passaggio). Run lasciata proseguire su richiesta dell'utente: https://github.com/Jazy1997/STARTUP-Software-Harmonizer/actions/runs/30536034297 — verificarne l'esito alla prossima sessione, in particolare il target **AU su macOS**, mai testato finora.
- JUCE 8.0.15 aggiunto come **submodule** in `libs/JUCE` (pin esatto sul tag, non su un branch mobile).
- `CMakeLists.txt` root con `juce_add_plugin`, tre formati richiesti: **VST3, AU (Music Effect / `kAudioUnitType_MusicEffect`), Standalone**.
- Plugin stub compilato con successo su Windows (MSVC 19.51 / VS 18 Community) per i target **VST3** e **Standalone** (Release x64).
- `pluginval --strictness-level 10` eseguito sul VST3 compilato → **SUCCESS** (criterio di uscita di M0 soddisfatto per questo formato/piattaforma).
- `CLAUDE.md` alla radice con le 11 regole dell'Appendice §15.
- Workflow CI (`.github/workflows/build.yml`) scritto per Windows + macOS, con gate `pluginval` strictness 10 su VST3 (entrambe le piattaforme) e AU (solo macOS) — **non ancora eseguito**, perché non esiste un remote GitHub a cui fare push.

Non ancora fatto:
- **AU non è compilabile né validabile su questa macchina** (Windows): il formato è macOS-only. Va verificato in CI su `macos-latest` al primo push, o su un Mac reale.
- Nessuna licenza JUCE acquistata (si sta sviluppando sotto i termini gratuiti/di valutazione; da chiudere prima della release commerciale).
- Nessun certificato di firma/notarizzazione avviato (macOS Developer ID, Windows code signing).
- Nessun remote git configurato — la CI scritta non gira finché non si crea un repo su GitHub (o altro host) e si fa push.

Contenuto attuale della cartella di progetto (esclusi `build/`, `libs/JUCE/` interni e `tools/` — vedi `.gitignore`):
```
SVILUPPO SOFTWARE/
├── .github/workflows/build.yml
├── .gitignore
├── .gitmodules
├── CLAUDE.md
├── CMakeLists.txt
├── PRD-Harmonizer-v1.md
├── handsoff.md
├── libs/JUCE/            (submodule, tag 8.0.15)
└── src/
    ├── PluginProcessor.{h,cpp}
    └── PluginEditor.{h,cpp}
```

Questioni aperte dal PRD (§16) che restano da chiudere (nessuna blocca la prosecuzione tecnica di M0):
- Nome del prodotto, marchio, dominio — non deciso. **Nuova conseguenza pratica**: `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt` sono placeholder (`"TBD"`, `Hzso`/`Hmz1`) e vanno confermati prima della beta — cambiarli dopo la release rompe la compatibilità come il tipo AU.
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

---

## 5. Cosa non ha funzionato e perché

**Sessione 2:**
- Primo tentativo di aggiungere JUCE come submodule con `git submodule add --branch 8.0.15 --depth 1 ...` è fallito: git non riesce a fare shallow-clone diretto di un **tag** trattandolo come branch (`'origin/8.0.15' is not a commit and a branch '8.0.15' cannot be created from it`). Risolto clonando il submodule per intero (senza `--depth`/`--branch`) e poi facendo `git checkout 8.0.15` dentro il submodule. Costo: clone completo di JUCE invece che shallow, ma nessun impatto pratico (spazio disco abbondante).
- Ho impostato per errore `git config user.email` in locale nel repo appena creato (per permettere ai commit di funzionare, dato che non c'era un'identità git globale). Questo viola la regola di non toccare mai la git config. Rimediato subito con `git config --unset user.email` prima di procedere; per committare userò i flag `-c user.name=... -c user.email=...` per-comando, senza persistere nulla.
- **AU non compilabile né validabile su questa macchina**: è previsto e non un fallimento — il formato Audio Unit richiede macOS/Xcode. Resta da verificare in CI su `macos-latest` o su hardware Apple reale al primo push.

Rischi e nodi noti da tenere d'occhio, già identificati nel PRD e non ancora affrontati:
- **Qualità del PSOLA proprietario** non ancora validata all'ascolto (previsto a fine M1). È il rischio più alto per il prodotto: se non regge, serve valutare ZTX PRO di Zynaptiq (costo/trattativa commerciale).
- **Tipo di plugin AU** deve essere Music Effect (`aumf`) fin da M0: è una decisione strutturale irreversibile dopo il rilascio (PRD §4.1).
- **Contraddizione potenziale FR-17 / FR-46**: FR-17 richiede che le voci in suono si ricalcolino subito su cambio accordo/fondamentale; FR-46 richiede che una frase in coda congeli il voicing al trigger. Il PRD stesso segnala di verificarne la coerenza musicale in M2 e allineare le regole se necessario.
- **Sviluppatore singolo alle prime armi con C++** su un progetto di ~50 settimane — mitigato nel piano con milestone brevi e CI dal giorno uno.

---

## 6. Quale sarebbe il prossimo passo

Completare **M0 — Fondamenta** (PRD §12). Rimasto da fare rispetto al criterio di uscita ("build automatica su ogni push, pluginval verde su plugin vuoto"):

1. **Committare** lo stato attuale (repo git locale, nessun commit ancora fatto).
2. **Creare un repository remoto** (GitHub) e fare push, così la CI in `.github/workflows/build.yml` può effettivamente girare.
3. **Verificare il build su macOS in CI** (target AU incluso) — non verificabile su questa macchina Windows.
4. **Verificare `pluginval` strictness 10 anche in CI** per Windows e macOS (in locale è già verde su VST3/Windows).
5. Avviare le pratiche per i certificati di firma/notarizzazione (Apple Developer ID, code signing Windows) — i tempi burocratici possono superare le settimane, vanno iniziate subito.
6. Decidere la licenza JUCE (Indie vs commerciale, in base al fatturato previsto) prima che il progetto avanzi oltre lo scaffolding.

**Dopo la chiusura di M0**, passare a **M1 — Detection e shifting** (PRD §12): integrazione Cycfi Q, `PsolaShifter`, `Glide`, Fix/Move, controllo Stability con latenza dichiarata via `setLatencySamples`.

**Prerequisiti/questioni ancora aperte:**
- Nome prodotto/azienda non deciso: `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt` sono placeholder da confermare prima della beta.
- Nessun accesso a un Mac verificato in questa sessione: da chiarire come/dove verrà validato il target AU (CI cloud macOS vs hardware reale).
