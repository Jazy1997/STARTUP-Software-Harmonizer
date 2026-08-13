# HANDOFF — HARMONIZER

> Ultimo aggiornamento: **2026-08-13**, sessione 37.
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
  **criterio d'uscita** (*"un tester esterno usa il plugin senza documentazione"*) sta
  per essere esercitato per la prima volta: vedi § Prossimo passo.
- Il plugin **gira in VST3 su Ableton e l'utente lo giudica soddisfacente** (s.29), riferito
  alla catena **Harmonizer**; il passaggio Harmonizer↔Play è confermato all'ascolto in s.35.
- Motore: PSOLA proprietario dietro `PitchShifter` astratto. **Chiuso** da D-19, con la forma
  di ogni modifica futura vincolata da **D-21**.
- **M6 (licensing) non esiste.** `src/licensing/` contiene ora `BetaGate.h`, che **non è** il
  `LicenseManager`: è un cancello temporaneo da cancellare quando A-01 arriverà (D-26).
- **Nessun sintomo aperto in `BUGS.md`. Nessun ascolto in coda** — ma sei voci in attesa di
  conferma (sotto) sono ora in mano ai tester.

**Sessione 37 — il pacchetto beta è pronto, A-04 rimandata con criterio** (per esteso in
`LOG/sessione-37.md`). L'obiettivo reale era **far sentire il plugin a degli artisti**, non
rilasciare: la beta si può fare **a costo zero** (D-25). Il rischio irreversibile non era la
firma ma l'identità del plugin — `juce_VST3ModuleInfo.h:61` dimostra che l'UID VST3 dipende
**solo dai codici a 4 lettere**, non dal nome: `Hzso`/`Hmz1`/`aumf` sono **congelati adesso**, il
nome resta libero, **A-02 non blocca più la beta** (D-24).

**Le build beta scadono a 30 giorni** (D-26): si spegne il **wet**, il dry continua a passare
(FR-68 anticipato). L'innesto è una riga accanto al Bypass e scende sulla rampa anti-click di
8 ms già esistente. **13/13 suite verdi in 2.2 s** (erano 11): `beta_expiry_test` copre *quando*
scade, `beta_gate_audio_test` *cosa fa all'audio* — senza il secondo, un cancello scollegato
spedirebbe una versione illimitata in silenzio.

---

## Prossimo passo

**Uno solo: mandare la beta agli artisti.** Procedura in `BETA-consegna.md`.

**Prima, una cosa da verificare e che non riguarda la beta:** il remote è
`github.com/Jazy1997/STARTUP-Software-Harmonizer` e **non è stato possibile controllare se è
privato** (`gh` non installato). *Settings → Danger Zone*: deve offrire "Make public". Se invece
offre "Make private", il sorgente è pubblico adesso e viene prima di tutto il resto.

Poi: GitHub → Actions → *Build & validate* → **Run workflow** con `beta=true`, `beta_days=30`,
`tester="Nome Cognome"`; una build per persona, e un link privato a testa.

Da qui in avanti **il collo di bottiglia sono le orecchie degli artisti, non il codice.** Le sei
voci della tabella qui sotto si chiudono con il loro feedback, non con altro lavoro.

---

## In attesa di conferma dell'utente

| Cosa | Dove | Stato |
|---|---|---|
| **MS-5: il dry al passaggio di modalità** | `mode_switch_test` | La metà *"senza interruzioni del dry"* di FR-28 è verificata **per calcolo**, non all'ascolto: il percorso dry non dipende dalla modalità, quindi il cancello è una guardia, non una prova. |
| **FR-59: la conferma è stata GENERALE** | s.33 | *"Ok, funziona"*: la scala funziona a vista, **non** che i tre punti seguenti siano stati guardati. **HiDPI**: corretto per costruzione, ma il display della prova poteva essere a 100%; su Retina mai. **Keep Tails al 70%**: 562 px logici contro 528 richiesti, 34 px di margine — **calcolato**, da guardare su Edit. **Persistenza**: da provare riaprendo un progetto salvato. |
| `kSettleFrames = 1.5` | `src/harmony/PitchLatch.h` | Attesa in frame d'analisi. **Misurata, non tarata all'ascolto**: prima manopola se l'armonia sembrasse in ritardo. → **è la domanda 1 delle guide beta.** |
| Default **E2 (Voice Male)** | `PluginProcessor.h` | Scelto misurando: l'impostazione più pronta che resta pulita su tutte e quattro le tabelle. |
| **B-10 con Keep Tails ON** | `PhraseScheduler.cpp` | Confermato in s.30 ma **in generale**, non su questa configurazione (tensione con FR-46). |
| Isteresi cella vuota, `kEmptyCellHoldMs = 80.0f` | `src/voices/EmptyCellHold.h` | Misurata irrilevante sul materiale reale. Mai tarata né ascoltata. |
| **Il conto alla rovescia nell'editor** | `PluginEditor.cpp` | Corretto per costruzione, **mai visto a schermo**: qui non si apre una GUI. |

---

## Limiti noti

Da non scambiare per requisiti soddisfatti.

- **Di macOS non è verificabile nulla da qui, e ora conta più di prima.** La CI valida
  `Harmonizer.component` con `pluginval -strictness 10` a ogni push e passa (D-23), ma restano in
  mano al **primo tester**: installazione, quarantena, **l'AU in Logic** (mai caricato in un host
  vero — due tester lo useranno, scelta consapevole), il permesso microfono dello standalone
  (aggiunto in s.37, corretto per costruzione e non provato) e la resa Retina.
- **La fetta x86_64 non è mai stata eseguita da nessuno**: la CI compila universal
  (`arm64;x86_64`) ma gira su runner arm64. Un tester su Mac Intel sarebbe la prima esecuzione in
  assoluto di quel codice; le guide gli chiedono esplicitamente di dirlo.
- **La scadenza beta è un deterrente, non un DRM:** chi sposta indietro l'orologio riottiene il
  wet, e "ora prima della data di build" è **deliberatamente** non scaduto (D-26).
- **I rami warm di `PhraseScheduler` scaldano col rapporto vecchio** — la causa 2 di B-15 vale
  probabilmente anche per celle vuote (B-04) e late-binding (B-12), che usano ancora
  `processWarmOnly` a 3 argomenti. **Deliberatamente non toccato** (D-21).
- **In Play, 8 note rilasciate e ripremute entro 8 ms** non trovano nessuno slot `isSilent()` e
  ricadono sulla scivolata d'intonazione · **nessun `ScopedNoDenormals`** in tutto `src/`.
- **Le voci acute della lista strumenti non sono verificate nel loro registro**: sopra Eb Alto Sax
  si misurano 1-3 offset di passaggio, ma il file di prova suona C4-D4-E4. **Serve un export.**
- **Il ritardo del cambio d'offset non va a zero** (convergenza del rilevatore + confine di
  blocco; azzerarlo richiede il **lookahead**, rimandato in s.32) · **la quantizzazione al blocco
  resta** (A-05), e in Play si somma al fatto che `metadata.samplePosition` non viene letto: fino
  a ~85 ms di anticipo a 4096. Non produce click, sposta l'attacco.
- **La regola utente di D-15 non va scritta com'è**: *"compilare tutte le celle"* è
  controproducente sugli attacchi (D-17) · **formanti mai tarate**, `k = 0.3` non è mai passato
  per l'ascolto e i due setter scrivono il float grezzo senza `Glide`.
- **CC mai provato con hardware reale** · **A 200% la finestra è 1800×1320 fisici**, che non entra
  in un 1080p (D-20) · **Preset di fabbrica**: dei 7 solo "Min" è confrontato col prototipo M4L
  (A-07) · **B-11**: il tetto voci ha lo stesso difetto di B-10, oggi non morde · **la UI non
  riflette un override CC attivo** (FR-36/37) · **nessuna `LookAndFeel`** (D-10) · **FR-61 non
  esiste** · **CPU mai profilata** contro il budget ≤15% del PRD §1.3 · **Catch2 mai adottato** ·
  **`Harmonizer (1).vst3`** residuo nel bundle locale (s.31) — **non** negli artefatti della CI,
  che nascono da un albero pulito: è la ragione per cui ai tester si spediscono quelli.

---

## Questioni aperte

Nessuna di queste è tecnica. **Il collo di bottiglia è il feedback degli artisti.**

- **`[DECISION]` Backend di licensing (A-01): la scadenza era M5, che è arrivata.** Blocca M6 per
  intero. `BetaGate.h` **non** è un inizio di M6: va cancellato quando A-01 arriva.
- **A-04 (firma e notarizzazione): rimandata per decisione, non dimenticata** (D-25). Torna
  urgente al **primo** di questi tre: un tester si blocca sulla quarantena · serve un installer
  `.pkg`/`.exe` (FR-71, M8) · qualunque data di rilascio pubblico meno 8 settimane. Vincolo da
  sapere ora: **Apple non accetta ditte individuali** per gli account Organization; un account
  Individual si converte conservando Team ID e certificati.
- **La guida utente non esiste** — `BETA-Windows.md`/`BETA-macOS.md` sono fogli d'installazione,
  non la documentazione di prodotto. Deve ospitare il modello CC posizionale (D-03) e la scelta
  dello strumento (D-18).
- Nome, marchio, dominio (A-02): **non blocca più la beta** (D-24), solo `PRODUCT_NAME` e
  `COMPANY_NAME`, che sono cambiabili.
- Tipo di licenza JUCE (A-03) · prezzi dei tre tier · canale di vendita · consegna licenza nel
  bundle hardware (A-08).

---

## Puntatori

| File | Cosa |
|---|---|
| `PRD-Harmonizer-v1.md` · `PRD-UI.md` | Fonte di verità, FR-01..FR-83 |
| `CLAUDE.md` | Regole non negoziabili + ciclo di vita dei documenti |
| `BUGS.md` · `DECISIONS.md` · `MAPPA.md` | Sintomi · decisioni · moduli |
| `BETA-consegna.md` | **Foglio operativo dell'utente**: checklist, procedura, testo dell'email |
| `BETA-Windows.md` · `BETA-macOS.md` | Per i tester. La CI li mette nello zip come `LEGGIMI.md` |
| `LOG/archivio-s01-s28.md` · `LOG/sessione-NN.md` | Il racconto per esteso |

**Materiale di test** (`SAMPLE TEST/`, non versionato): `Test 1 - Basic Silk Horns.wav`
(C4-D4-E4-C4, ~2 s per nota) e `Test 2 - E-Piano.wav` sono i **dry sorgente**; `DBG Timbro/`
contiene gli export dell'utente e le reference Autoshift (`REF_<nome>`). Convenzione dei nomi
`exp#<N>_<Preset>_<Voce>_<Versione>.wav` e impostazioni di export: `LOG/sessione-31.md`.

**Comandi** · `cmake --build build --config Release` (il fallimento della copia in
`Program Files` è atteso, D-12: filtrare **solo** `error C####`/`error LNK` — aggiungere
`error MSB` fa sembrare rotta una build sana) · **poi** `--target Harmonizer_Standalone`, che
quel fallimento salta · `ctest --test-dir build -C Release --output-on-failure`
(**13 suite, ~2 s** — è ciò che gira anche in CI, D-23)
· `build/Release/play_mode_input_test.exe` stampa la tabella PM-1..PM-7 di B-15
· `build/Release/mode_switch_test.exe` stampa la tabella MS-1..MS-8 di B-16/B-17
· `build/Release/degree_trace_probe.exe "<dry.wav>" "<12 celle>" <root> <block> <notaMin>`
· **build beta locale**: `cmake -B build-beta -DHARMONIZER_BETA=ON -DHARMONIZER_BETA_DAYS=0`
  (`DAYS=0` = scaduta subito, per sentire la dissolvenza del wet senza aspettare 30 giorni)

**`pluginval` non è nel repo** e non è più su disco: si scarica come fa la CI —
`curl -sL -o pluginval.zip https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip`
poi `unzip`, e `pluginval.exe --strictness-level 10 --validate <path.vst3>`.
