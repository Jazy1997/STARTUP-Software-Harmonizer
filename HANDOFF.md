# HANDOFF — HARMONIZER

> Ultimo aggiornamento: **2026-08-12**, sessione 36.
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
  alla catena **Harmonizer**; il passaggio Harmonizer↔Play è confermato all'ascolto in s.35.
- Motore: PSOLA proprietario dietro `PitchShifter` astratto. **Chiuso** da D-19, con la forma
  di ogni modifica futura vincolata da **D-21**.
- **Nessun sintomo aperto in `BUGS.md`. Nessun ascolto in coda.**

**Sessione 36 — A-06 chiuso, nessuna riga di `src/` toccata.** La CI eseguiva 3 suite su 11 con
`g++` a mano e non invocava mai `ctest`. Ora esegue `ctest` nel job `build`: **11 su 11, su
Windows e macOS** (D-23), a costo di build zero — quegli eseguibili venivano già compilati e
buttati via senza essere eseguiti. Misurato: 11/11 verdi in **3.2 s**. Il job veloce a `g++`
resta ma è **dichiarato come sottoinsieme**, col divieto di estenderlo; una **guardia** impedisce
a un banco nuovo di scivolare fuori in silenzio (provata in entrambe le direzioni).

**Il Delay non è in ritardo: è fuori da v1.0 per specifica.** Domanda dell'utente, verificata sul
PRD: FR-47..50 sono tutti `[V1.1]`, la §12 li mette fra i post-v1.0 e il deliverable di M3 è
letteralmente *"`PhraseScheduler` (senza editor)"* — l'unico obbligo v1.0 è già assolto.
Anticiparlo sveglierebbe B-11, la semantica vera di Keep Tails e il riscaldamento su una quarta
strada, e richiederebbe A-05 prima. Per esteso in `LOG/sessione-36.md` §1.

---

## Prossimo passo

**Uno solo: A-04 — avviare le pratiche di firma e notarizzazione.** È il **lead time più lungo
del progetto**, era previsto per **M0** e non è **mai stato avviato**: cinque milestone di
ritardo su un elemento che non si recupera scrivendo codice più in fretta. Apple Developer ID
(~24-48 h, a volte molto di più) + certificato di code signing Windows.

Non è codice, ed è deliberato: oggi non c'è nessun sintomo aperto, nessun ascolto in coda e la
rete di regressione è appena stata chiusa. Il collo di bottiglia del progetto non è più tecnico.

*(Se si preferisce comunque scrivere codice: l'item più maturo è **A-07**, i 6 preset di
fabbrica su 7 mai confrontati col prototipo M4L.)*

---

## In attesa di conferma dell'utente

| Cosa | Dove | Stato |
|---|---|---|
| **MS-5: il dry al passaggio di modalità** | `mode_switch_test` | La metà *"senza interruzioni del dry"* di FR-28 è verificata **per calcolo**, non all'ascolto: il percorso dry non dipende dalla modalità, quindi il cancello è una guardia, non una prova. |
| **FR-59: la conferma è stata GENERALE** | s.33 | *"Ok, funziona"*: la scala funziona a vista, **non** che i tre punti seguenti siano stati guardati. **HiDPI**: corretto per costruzione, ma il display della prova poteva essere a 100%; su Retina mai. **Keep Tails al 70%**: 562 px logici contro 528 richiesti, 34 px di margine — **calcolato**, da guardare su Edit. **Persistenza**: da provare riaprendo un progetto salvato, non cambiando scala a plugin aperto. |
| `kSettleFrames = 1.5` | `src/harmony/PitchLatch.h` | Attesa in frame d'analisi. **Misurata, non tarata all'ascolto**: prima manopola se l'armonia sembrasse in ritardo. |
| Default **E2 (Voice Male)** | `PluginProcessor.h` | Scelto misurando: l'impostazione più pronta che resta pulita su tutte e quattro le tabelle. |
| **B-10 con Keep Tails ON** | `PhraseScheduler.cpp` | Confermato in s.30 ma **in generale**, non su questa configurazione (tensione con FR-46). |
| Isteresi cella vuota, `kEmptyCellHoldMs = 80.0f` | `src/voices/EmptyCellHold.h` | Misurata irrilevante sul materiale reale. Mai tarata né ascoltata. |

---

## Limiti noti

Da non scambiare per requisiti soddisfatti.

- **AU non è mai stato caricato in un host vero, e Retina mai guardato.** Precisato in s.36: la
  CI **valida `Harmonizer.component` con `pluginval --strictness-level 10` su macOS ad ogni push,
  e passa** (25 run verdi), quindi "AU non verificato" era impreciso. Manca AU in
  Logic/GarageBand e la resa Retina **a occhio**: serve una macchina macOS, non altra CI.
- **I rami warm di `PhraseScheduler` scaldano col rapporto vecchio** — la causa 2 di B-15 vale
  probabilmente anche per celle vuote (B-04) e late-binding (B-12), che usano ancora
  `processWarmOnly` a 3 argomenti. **Deliberatamente non toccato**: va misurato sul percorso
  Harmonizer per conto proprio (D-21), e quella catena è oggi giudicata soddisfacente.
- **In Play, 8 note rilasciate e ripremute entro 8 ms** non trovano nessuno slot `isSilent()` e
  ricadono sulla scivolata d'intonazione (limite dell'allocazione, non del motore) · **nessun
  `ScopedNoDenormals`** in tutto `src/` (trovato in s.34, non corretto: non era il sintomo).
- **Le voci acute della lista strumenti non sono verificate nel loro registro.** Sopra Eb Alto
  Sax si misurano 1-3 offset di passaggio e nessuna taratura li elimina — ma il file di prova
  suona C4-D4-E4. **Serve un export dedicato.**
- **Il ritardo del cambio d'offset non va a zero** (convergenza del rilevatore + confine di
  blocco; azzerarlo richiede il **lookahead**, rimandato in s.32) · **la quantizzazione al
  blocco resta** (A-05), e in Play si somma al fatto che `metadata.samplePosition` non viene
  letto: fino a ~85 ms di anticipo a 4096. Non produce click, sposta l'attacco.
- **La regola utente di D-15 non va scritta com'è.** *"Compilare tutte le celle"* è
  controproducente sugli attacchi (D-17).
- **Formanti mai tarate**: `k = 0.3` non è mai passato per l'ascolto, e i due setter scrivono
  il float grezzo senza `Glide` — se si sentisse un click girandoli, è un'entry propria.
- **CC mai provato con hardware reale** (il parsing MIDI di `CcRouter` è scoperto; Play sì dalla
  tastiera dell'utente) · **A 200% la finestra è 1800×1320 fisici**, che non entra in un 1080p
  (lettura letterale di FR-59, D-20) · **Preset di fabbrica**: dei 7 solo "Min" è confrontato col
  prototipo Max4Live (A-07) · **B-11**: il tetto voci ha lo stesso difetto di B-10, oggi non
  morde (32 su 32) · **la UI non riflette un override CC attivo** (FR-36/37) · **nessuna
  `LookAndFeel`** (D-10) · **FR-61 non esiste** · **CPU mai profilata** contro il budget ≤15%
  del PRD §1.3 · **Catch2 mai adottato** · **`Harmonizer (1).vst3`** residuo nel bundle,
  probabilmente inerte (s.31).

---

## Questioni aperte

Nessuna di queste è tecnica; nessuna è chiusa. **Sono ora il collo di bottiglia del progetto.**

- **`[DECISION]` Backend di licensing (A-01): la scadenza era M5, che è arrivata.** Blocca M6
  per intero — `src/licensing/` è vuota.
- **Certificati di firma e notarizzazione (A-04)**: il lead time più lungo del progetto, **mai
  avviato**, previsto per M0. → § Prossimo passo.
- **Il criterio d'uscita di M5 non è mai stato esercitato**: *"un tester esterno usa il plugin
  senza documentazione"*. Serve una persona, non del codice. In tensione con la voce sotto.
- **La guida utente non esiste** — nessun README utente, nessuna `docs/`, nessun tooltip.
  Deve ospitare il modello CC posizionale (D-03) e la scelta dello strumento (D-18).
- Nome prodotto, marchio, dominio (A-02) → bloccano `PLUGIN_MANUFACTURER_CODE` (`Hzso`),
  `PLUGIN_CODE` (`Hmz1`), `COMPANY_NAME` (`"TBD"`), `BUNDLE_ID`. **Cambiarli dopo il rilascio
  rompe i progetti salvati**, come il tipo AU.
- Tipo di licenza JUCE (A-03) · prezzi dei tre tier · canale di vendita · consegna licenza nel
  bundle hardware (A-08).

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
`--target Harmonizer_Standalone`, che quel fallimento salta · `ctest --test-dir build -C Release
--output-on-failure` (**11 suite, ~3 s** — è ciò che gira anche in CI, D-23)
· `build/Release/play_mode_input_test.exe` stampa la tabella PM-1..PM-7 di B-15
· `build/Release/mode_switch_test.exe` stampa la tabella MS-1..MS-8 di B-16/B-17
· `build/Release/degree_trace_probe.exe "<dry.wav>" "<12 celle>" <root> <block> <notaMin>`

**`pluginval` non è nel repo** e non è più su disco: si scarica come fa la CI —
`curl -sL -o pluginval.zip https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip`
poi `unzip`, e `pluginval.exe --strictness-level 10 --validate <path.vst3>`.
