# HANDOFF — HARMONIZER

> Ultimo aggiornamento: **2026-08-11**, sessione 34.
> Questo file descrive **solo lo stato di oggi**. La storia sta in `LOG/`, i sintomi
> aperti in `BUGS.md`, le decisioni durature in `DECISIONS.md`, la mappa dei moduli
> in `MAPPA.md`. Fonte di verità del prodotto: `PRD-Harmonizer-v1.md` + `PRD-UI.md`.
> **Tetto: ~150 righe.** Se cresce, va potato — non è un archivio.

---

## Stato

Plugin armonizzatore per strumenti monofonici (VST3 / AU / Standalone). Calcola
`d = (notaMIDI − fondamentale) mod 12` e legge gli offset delle 8 voci da una tabella
12×8 editabile dall'utente.

- **M5 (UI): il contenuto elencato in PRD §12 è completo** (FR-59 chiuso in s.33). Il
  **criterio d'uscita** (*"un tester esterno usa il plugin senza documentazione"*) è
  un'azione dell'utente, mai eseguita. **M6 (licensing) non esiste**: cartella vuota.
- Il plugin **gira in VST3 su Ableton e l'utente lo giudica soddisfacente** (s.29), riferito
  alla catena **Harmonizer**.
- Motore: PSOLA proprietario dietro `PitchShifter` astratto. **Chiuso** da D-19 (s.32).
  Riaperto in s.34 dall'unica condizione che D-19 prevede — un sintomo nuovo riportato
  all'ascolto — e con la forma della modifica ora vincolata da **D-21**.
- **10** suite `ctest` verdi. `pluginval --strictness-level 10` SUCCESS su VST3 (Win).

**Il lavoro di oggi (B-15)** — *"quando clicco una nota sulla tastiera midi ... sento un click
all'inizio"*, in modalità **Play**, **a ogni tasto premuto**. Nuovo banco di misura
(`tests/play_mode_input_test.cpp`, in `ctest`) che pilota il vero `PlayModeInput` con veri
`MidiBuffer`. Due cause distinte, separate da un esperimento di controllo (PM-7, seconda nota
alla stessa altezza — pulito già prima del fix, quindi il colpevole è il cambio di rapporto):

1. Il motore di uno slot senza nota premuta restava **affamato**, e `goCold()` non era mai
   chiamato in Play. Al note-on i primi ~20 ms uscivano dalla coda della nota precedente e gli
   8 ms di `ampGlide` si consumavano lì dentro (è B-12, mai portato su Play).
2. **La causa del sintomo riportato**: il riscaldamento alimentava il motore col rapporto di
   trasposizione **vecchio**, e `synthesise()` riempie `outBuf` in anticipo — la dissolvenza
   cadeva su ~10 ms già sintetizzati all'intonazione della nota precedente.

Salto d'ampiezza all'attacco, rapportato al regime: **2.07–5.08 → 0.94–1.03**. Tempo di salita
sulla seconda nota: 2.15–5.24 ms → **7.71–8.59 ms**, cioè `kDeclickMs`. Corretta per strada
anche la scivolata d'intonazione sulla nota ribattuta (−1219.8 → +2.3 cent a +30 ms).

---

## Prossimo passo

**Uno solo: l'ascolto di B-15.** Tutto il resto è misurato, ma nessuna misura può chiudere un
sintomo udito (regola 12) — e s.14 ha già insegnato quanto costi crederci prima. In Ableton,
Play, con una sorgente audio sostenuta: nota singola dopo un silenzio lungo; **note a altezze
diverse in sequenza** (il caso che era rotto, e quello che il solo riscaldamento non
risolveva); note ripetute sulla stessa altezza e ribattute veloci; accordo di 4–8 note;
passaggio Harmonizer↔Play con tasti premuti (FR-28, **mai verificato all'ascolto**). Se torna
pulito, B-15 si chiude; se no, il banco di misura è in piedi e il prossimo giro parte da lì.

**Il prossimo passo di ingegneria, dopo**, resta **A-06: la CI allineata a `ctest`.** Il
workflow compila 3 suite su 10 con `g++` nudo e non invoca mai `ctest`; il divario è cresciuto
ancora oggi (`play_mode_input` linka JUCE, quindi come `phrase_scheduler` non è compilabile in
quel modo — D-16).

---

## In attesa di conferma dell'utente

| Cosa | Dove | Stato |
|---|---|---|
| **B-15 — il click in Play** | vedi § Prossimo passo | Fix in codice e misurato su 5 livelli di Stability, **mai ascoltato**. L'entry resta `APERTO`. |
| **FR-59: la conferma è stata GENERALE** | s.33 | *"Ok, funziona"*: la scala funziona a vista, **non** che i tre punti seguenti siano stati guardati. **HiDPI**: corretto per costruzione, ma il display della prova poteva essere a 100%; su Retina mai (serve macOS). **Keep Tails al 70%**: 562 px logici contro 528 richiesti, 34 px di margine — **calcolato**, da guardare su Edit. **Persistenza**: da provare riaprendo un progetto salvato, non cambiando scala a plugin aperto. |
| **La conferma di B-14 fu dal vivo, non su export** | — | In `SAMPLE TEST/` non esiste alcun `_02`. I numeri (79.1 → 44.3 ms) restano verificati per calcolo. |
| `kSettleFrames = 1.5` | `src/harmony/PitchLatch.h` | Attesa in frame d'analisi. **Misurata, non tarata all'ascolto**: prima manopola se l'armonia sembrasse in ritardo. |
| Default **E2 (Voice Male)** | `PluginProcessor.h` | Scelto misurando: l'impostazione più pronta che resta pulita su tutte e quattro le tabelle. |
| **B-10 con Keep Tails ON** | `PhraseScheduler.cpp` | Confermato in s.30 ma **in generale**, non su questa configurazione (tensione con FR-46). |
| Isteresi cella vuota, `kEmptyCellHoldMs = 80.0f` | `src/voices/EmptyCellHold.h` | Misurata irrilevante sul materiale reale. Mai tarata né ascoltata. |

---

## Limiti noti

Da non scambiare per requisiti soddisfatti.

- **AU e Retina non sono mai stati verificati.** Richiedono macOS: `pluginval` è sempre girato
  solo su VST3/Windows. Buco che cresce a ogni milestone.
- **I rami warm di `PhraseScheduler` scaldano col rapporto vecchio** — la causa 2 di B-15 vale
  probabilmente anche per celle vuote (B-04) e late-binding (B-12), che usano ancora
  `processWarmOnly` a 3 argomenti. **Deliberatamente non toccato**: va misurato sul percorso
  Harmonizer per conto proprio (D-21), e quella catena è oggi giudicata soddisfacente.
- **In Play, 8 note rilasciate e ripremute entro 8 ms** non trovano nessuno slot `isSilent()`
  e ricadono sulla scivolata d'intonazione. Limite dell'allocazione, non del motore.
- **Nessun `ScopedNoDenormals`** in tutto `src/`. Trovato in s.34 esplorando, non corretto: non
  è il sintomo riportato.
- **Le voci acute della lista strumenti non sono verificate nel loro registro.** Sopra Eb Alto
  Sax si misurano 1-3 offset di passaggio e nessuna taratura li elimina — ma il file di prova
  suona C4-D4-E4. **Serve un export dedicato.**
- **Il ritardo del cambio d'offset non va a zero.** Restano la convergenza del rilevatore e il
  confine di blocco. Azzerarlo richiede il **lookahead**, rimandato in s.32.
- **La quantizzazione al blocco resta** (A-05), e in Play si somma al fatto che
  `metadata.samplePosition` non viene letto: il note-on vale dal campione 0 del blocco, fino a
  ~85 ms di anticipo a 4096. Non produce click, sposta l'attacco.
- **La regola utente di D-15 non va scritta com'è.** *"Compilare tutte le celle"* è
  controproducente sugli attacchi (D-17).
- **Formanti mai tarate**: `k = 0.3` non è mai passato per l'ascolto, e i due setter scrivono
  il float grezzo senza `Glide` — se si sentisse un click girandoli, è un'entry propria.
- **CC mai provato con hardware reale**: il parsing MIDI di `CcRouter` è scoperto. Play sì, ora
  (s.34), ma solo dalla tastiera dell'utente.
- **A 200% la finestra è 1800×1320 fisici**, che non entra in un 1080p (lettura letterale di
  FR-59, D-20) · **Preset di fabbrica**: dei 7 solo "Min" è confrontato col prototipo Max4Live
  (A-07) · **CI copre 3 delle 10 suite** e non invoca mai `ctest` (A-06) · **B-11**: il tetto
  voci ha lo stesso difetto di B-10, oggi non morde (32 su 32) · **la UI non riflette un
  override CC attivo** (FR-36/37) · **nessuna `LookAndFeel`** (D-10) · **FR-61 non esiste** ·
  **CPU mai profilata** contro il budget ≤15% del PRD §1.3 · **Catch2 mai adottato** ·
  **`Harmonizer (1).vst3`** residuo nel bundle, probabilmente inerte (s.31).

---

## Questioni aperte

Nessuna di queste è tecnica; nessuna è chiusa.

- **Il criterio d'uscita di M5 non è mai stato esercitato**: *"un tester esterno usa il plugin
  senza documentazione"*. Serve una persona, non del codice. In tensione con la voce sotto.
- **La guida utente non esiste** — nessun README utente, nessuna `docs/`, nessun tooltip.
  Deve ospitare il modello CC posizionale (D-03) e la scelta dello strumento (D-18).
- **`[DECISION]` Backend di licensing: la scadenza era M5, che è arrivata.** Blocca M6.
- Nome prodotto, marchio, dominio → bloccano `PLUGIN_MANUFACTURER_CODE` (`Hzso`),
  `PLUGIN_CODE` (`Hmz1`), `COMPANY_NAME` (`"TBD"`), `BUNDLE_ID`. **Cambiarli dopo il rilascio
  rompe i progetti salvati**, come il tipo AU.
- **Certificati di firma e notarizzazione**: il lead time più lungo del progetto, **mai
  avviato** · tipo di licenza JUCE (Indie vs commerciale) · prezzi dei tre tier · canale di
  vendita · consegna licenza nel bundle hardware.

---

## Puntatori

| File | Cosa |
|---|---|
| `PRD-Harmonizer-v1.md` | Fonte di verità, FR-01..FR-72 |
| `PRD-UI.md` | Elabora §8, FR-73..FR-83 |
| `CLAUDE.md` | Regole non negoziabili + ciclo di vita dei documenti |
| `BUGS.md` | Un'entry per sintomo, con ID stabile e storia |
| `DECISIONS.md` | Decisioni durature e perché |
| `MAPPA.md` | Mappa dei moduli com'è adesso |
| `LOG/archivio-s01-s28.md` · `LOG/sessione-NN.md` | Il racconto per esteso |

**Materiale di test** (`SAMPLE TEST/`, non versionato): `Test 1 - Basic Silk Horns.wav`
(C4-D4-E4-C4, ~2 s per nota) e `Test 2 - E-Piano.wav` sono i **dry sorgente**;
`DBG Timbro/` contiene gli export dell'utente e le reference Autoshift.
**Nome degli export** (s.31): `exp#<N>_<Preset>_<Voce>_<Versione>.wav`, con `exp#1` =
Dry/Wet 1 (solo wet), Balanced, Fmt Spread 0, Glide 0 ms, Voices 4, Gain/Voice a 0 tranne la
voce isolata; `REF_<nome>` è il riferimento fatto con Autoshift. Buffer 1024, Focusrite ASIO.

**Comandi** · `cmake --build build --config Release` (il fallimento della copia in
`Program Files` è atteso, D-12: filtrare `error C####`/`error LNK`) · **poi**
`--target Harmonizer_Standalone`, che quel fallimento salta · `ctest --test-dir build -C Release`
· `build/Release/play_mode_input_test.exe` stampa la tabella PM-1..PM-7 di B-15
· `build/Release/degree_trace_probe.exe "<dry.wav>" "<12 celle>" <root> <block> <notaMin>`

**`pluginval` non è nel repo** e non è più su disco: si scarica come fa la CI —
`curl -sL -o pluginval.zip https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip`
poi `unzip`, e `pluginval.exe --strictness-level 10 --validate <path.vst3>`.
