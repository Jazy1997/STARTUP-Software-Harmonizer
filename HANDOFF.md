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

- **M5 (UI): il contenuto di PRD §12 è completo** (FR-59 chiuso in s.33); il **criterio d'uscita**
  (*"un tester esterno usa il plugin senza documentazione"*) sta per essere esercitato per la
  prima volta — vedi § Prossimo passo.
- **Gira in VST3 su Ableton ed è giudicato soddisfacente** (s.29), riferito alla catena
  **Harmonizer**; il passaggio Harmonizer↔Play è confermato all'ascolto in s.35.
- Motore: PSOLA proprietario dietro `PitchShifter` astratto. **Chiuso** da D-19, forma di ogni
  modifica futura vincolata da **D-21**.
- **M6 (licensing) non esiste.** `src/licensing/` contiene `BetaGate.h`, che **non è** il
  `LicenseManager`: cancello temporaneo, da cancellare quando arriverà A-01 (D-26).
- **Nessun sintomo aperto in `BUGS.md`** — ma le sei voci in attesa di conferma (sotto) sono ora
  in mano ai tester.

**Sessione 37 — il pacchetto beta è pronto, A-04 rimandata con criterio** (per esteso in
`LOG/sessione-37.md`). L'obiettivo era **far sentire il plugin a degli artisti**, non rilasciare:
si fa **a costo zero** (D-25). Il rischio irreversibile non era la firma ma l'identità del plugin
— `juce_VST3ModuleInfo.h:61`: l'UID VST3 dipende **solo dai codici a 4 lettere**, quindi
`Hzso`/`Hmz1`/`aumf` sono **congelati**, il nome resta libero, **A-02 non blocca più la beta** (D-24).
**Le build beta scadono a 30 giorni** (D-26): si spegne il **wet**, il dry passa (FR-68
anticipato); una riga accanto al Bypass, sulla rampa anti-click già esistente. **13/13 suite verdi
in 2.2 s** — `beta_expiry_test` copre *quando* scade, `beta_gate_audio_test` *cosa fa all'audio*.

---

## Prossimo passo

**Uno solo: mandare la beta agli artisti.** Procedura in `BETA-consegna.md`. Il repository **è
privato** (lucchetto verificato il 13/08): quel timore è chiuso.

**Prima di spedire**, sulla pagina del run: devono comparire gli artefatti
`Harmonizer-Windows-beta` / `Harmonizer-macOS-beta`, e il passo `Firma ad-hoc dei bundle` deve
stampare `adhoc`. **I passi di pacchettizzazione non hanno mai completato con successo** (D-25):
i percorsi macOS sono scritti dalla struttura di JUCE, non osservati.

⚠️ **Un run costa ~3 ore di macOS** (A-09), e ogni push ne fa partire uno — `[skip ci]` nel
messaggio per i commit che non toccano il codice. **Una sola build per tutti i tester**, non una
per persona: il nome nel binario non vale tre ore.

Da qui in avanti **il collo di bottiglia sono le orecchie degli artisti, non il codice.**

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

- **Di macOS non è verificabile nulla da qui, e ora conta più di prima.** Restano in mano al
  **primo tester**: installazione, quarantena, **l'AU in Logic** (mai caricato in un host vero —
  due tester lo useranno, scelta consapevole), il permesso microfono dello standalone (aggiunto in
  s.37, corretto per costruzione e non provato) e la resa Retina.
- **La fetta x86_64 non è mai stata eseguita da nessuno**: si compila universal ma la CI gira su
  runner arm64. Un tester su Mac Intel sarebbe la prima esecuzione di quel codice — le guide
  glielo chiedono. È anche metà della causa di A-09.
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
- **`[DECISION]` Costo della CI (A-09): job macOS ~3 h contro 15 min di Windows.** Due correzioni
  a copertura invariata sono pronte e non applicate; la terza spetta all'utente — **job macOS solo
  a richiesta**, che taglia quasi tutto ma riduce la copertura appena conquistata con D-23.
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
(C4-D4-E4-C4) e `Test 2 - E-Piano.wav` sono i **dry sorgente**; `DBG Timbro/` ha gli export e le
reference Autoshift. Convenzione dei nomi e impostazioni di export: `LOG/sessione-31.md`.

**Prima di pushare** · `git log --oneline origin/main..HEAD` — in questo repo capita che commit
importanti restino solo in locale: `ctest` in CI (D-23, s.36) è arrivato su GitHub solo il 13/08,
quindi i run verdi fino a #25 **non avevano mai eseguito le suite**. E ogni push fa partire un run
da ~3 h: `[skip ci]` nel messaggio quando il commit non tocca il codice (A-09).

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

**`pluginval` non è nel repo** e non è più su disco: si scarica come fa la CI (v1.0.4, il comando
esatto è in `.github/workflows/build.yml`), poi `pluginval.exe --strictness-level 10 --validate`.
