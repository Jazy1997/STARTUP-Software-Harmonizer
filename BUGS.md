# BUGS — HARMONIZER

> Un'entry per **sintomo**, non per fix. L'ID è stabile e non si riusa mai.
> Si aggiorna **in loco**: la storia si allunga, non si riapre un'entry nuova.
> Se un sintomo torna dopo essere stato chiuso, si riapre **la stessa entry**.
>
> Stati: `APERTO` · `APERTO (parziale)` · `CHIUSO` · `CHIUSO (residuo accettato)` · `SOSPESO`
>
> **`CHIUSO` richiede la conferma all'ascolto dell'utente** (`CLAUDE.md` regola 12).
> Verificato per calcolo ≠ chiuso.

---

## B-01 — Click a inizio nota

**Stato: CHIUSO** · Ultimo tocco: s.18 · Confermato all'ascolto: sì (s.18)

**Storia**
- s.14 — Ipotesi "PSOLA mai resettato fra una nota e la successiva". Fix su
  `Voice::setMuted` (reset dello shifter alla riattivazione). Meccanismo **reale**
  (Test 9: scostamento 0.214 senza reset, 0.0 con reset) ma **smentito all'ascolto**:
  il click persisteva identico. Trattato come causa insufficiente, non irrilevante.
  Il fix è rimasto nel codice.
- s.16 — `Voice::justReactivated` + `Glide::getTarget()`: alla riattivazione
  `offsetGlide.reset(offsetGlide.getTarget())`, zero rampa sul nuovo attacco.
  Misurato ~4 cent contro ~195 cent prima. All'ascolto: **migliorato, non risolto**.
- s.17/18 — Ripartenza da zero. Causa vera trovata: il **gate dell'onset** si chiudeva
  mentre la nota decadeva naturalmente. `OnsetDetector.cpp`, `release_threshold`
  da −36 dB a −45 dB (`onset_threshold` −24 dB e `slope_threshold` −30 dB non toccati).
  Verificato su due percorsi indipendenti (export reale + riproduzione offline) e
  **confermato all'ascolto**.

**Nota** — il percorso FR-17 vero (cambio accordo su nota già attiva) resta glideato:
non è mai stato toccato da nessuno di questi fix.

---

## B-02 — Wobbling / timbro granuloso a nota tenuta

**Stato: CHIUSO (residuo accettato)** · Ultimo tocco: s.26 · Confermato all'ascolto: sì (s.26)

**Storia**
- s.12 — Segnalato dall'utente come "timbro robotico e granuloso" + "un po' di
  wobbeling nelle voci". Fix del deficit di sovrapposizione dei grani in
  `PsolaShifter::emitGrain`: bug reale, ma non risolutivo.
- s.13 — Diagnosticato, non corretto: la frequenza di controllo dell'intero motore è
  il reciproco del block size dell'host (~10.8 Hz a 4096 campioni/44.1 kHz). Fix
  previsto (sotto-blocchi in `processBlock`) **mai scritto** — vedi D-13.
- s.19 — Due tentativi su `detectEpochs` (peso sulla ricerca del picco, poi
  interpolazione sub-campione): misurati inefficaci sul file reale e **ritirati**
  (`git checkout b67a741`).
- s.20 — Causa trovata nella **sintesi**, non nell'analisi: `synthesise()` avanzava
  `synthPos` del periodo quantizzato globale invece del periodo di analisi **locale**
  (`epochAfter(epoch) - epoch`). Confermato all'ascolto ("ora va molto meglio"):
  8/792 finestre instabili (1.0%) contro 35/792 (4.4%). Assorbe l'intervento D1
  pianificato in s.17/18/19, che non serve più fare a parte.
- s.26 — Secondo meccanismo: gli epoch venivano scelti per **ampiezza assoluta**.
  Nuovo `findEpochByCorrelation()` in `detectEpochs()` — dal secondo epoch in poi la
  scelta è per **similarità di forma d'onda** (cross-correlazione normalizzata).
  Jitter 4.16 → 0.084 campioni; 0.0% di instabilità su tutto lo sweep −1..−10.
  **Confermato all'ascolto**: "il timbro ora è stabile per tutta la nota".

**Residuo accettato** — sporco all'attacco di nota, giudicato accettabile dall'utente.

**Candidati mai esplorati** (se il residuo dovesse riemergere)
- Forma/ampiezza della finestra di Hann del grano (`emitGrain`, calcolo di `Lg`/`W`).
- Normalizzazione d'inviluppo asimmetrica in `processChunk`
  (`out[i] = outBuf[idx] / max(e, 1.0f)`, si divide solo sopra 1.0) — ipotesi W-B di s.20.
- Un generatore sintetico che riproduca davvero il fenomeno: serve un timbro
  **armonicamente più ricco** di una singola risonanza (Test 10/11 non ci sono
  riusciti). Due o più risonanze comparabili, o un impulso non ideale.

---

## B-03 — Click residui occasionali

**Stato: APERTO (parziale)** · Ultimo tocco: s.13 · Confermato all'ascolto: **no**

**Storia**
- s.12 — Fix architetturale: dissolvenza di ampiezza 8 ms (`kDeclickMs`) su ogni
  transizione di voce + `Phrase::releasing`. All'ascolto: **parzialmente** risolto,
  "continuo a sentire qualche click ogni tanto", meno frequenti.
- s.13 — Causa dei residui trovata per calcolo: con il block size di Ableton
  (4096 campioni, MME/DirectX) la dissolvenza era **no-op**. Fix `Glide::processRamp`,
  guadagno campione-per-campione. `glide_test` 15/15 verde — TEST 1 riproduce e misura
  il bug originale (salto dell'88%+ di fondoscala in un campione).
  **Mai riascoltato nella stessa configurazione.**

**Prossima azione** — riascolto nella configurazione identica di s.12/13 (buffer 4096,
MME/DirectX) per dire se i click occasionali sono spariti.

**Candidato non esplorato** — lo **swap di Stability senza dissolvenza**
(`VoicePool::applyPendingStabilityChangeIfSafe` → `Voice::swapShifterNoAlloc`): sostituisce
l'intero `PitchShifter` con uno nuovo senza rampa, ed è "sempre applicato in standalone".
È oggi il salto di ampiezza più probabile rimasto scoperto. Trovato in s.13, mai corretto.

**Nota** — il furto d'emergenza (FR-52, `hardFreePhrase`) resta istantaneo **per design**:
fonte residua legittima se il pool si esaurisce spesso.

---

## B-04 — Buco a inizio nota su preset a gradi parziali

**Stato: CHIUSO** · Ultimo tocco: s.27 · Confermato all'ascolto: sì · Commit `06048b8`

**Storia**
- s.27 — Sintomo: buchi e click a inizio nota **solo** su preset con celle vuote
  ("Custom" a offset fissi −7/−8/−10), mentre "Maj" non li mostrava mai.
  Causa: nel ramo cella-vuota-su-frase-viva di `PhraseScheduler` il `PitchShifter`
  restava **affamato**, e `Voice::setMuted` chiamava incondizionatamente
  `shifter->reset()` → buco pari alla latenza dichiarata (21 ms a Balanced), con
  `ampGlide` (8 ms) consumato dentro il buco.
  Fix: `Voice::processWarmOnly()` (alimenta il motore e scarta l'uscita, senza toccare
  i glide) + `Voice::goCold()` (contiene il `reset()` prima dentro `setMuted`).
  Misurato in `voice_test.cpp` blocco H5: **23.95 ms senza fix, 0.00 ms con**.
  Confermato dall'utente: "il buco è sparito".

**Attenzione** — la stessa conferma ha aperto **B-05**: "ma ora c'è nota nota".

**Deliberatamente non toccato** — il furto d'emergenza in `allocateFreeSlot()` (FR-52).

---

## B-05 — "Ribattuto": buco di ~15 ms all'attacco di nota

**Stato: APERTO** · Ultimo tocco: s.28 · Confermato all'ascolto: sì (è il sintomo)

**Sintomo** — segnalato dopo aver ascoltato la build di s.27. Screenshot DAW dell'utente
(una sola voce): sulla 2ª nota il plugin parte normale, si spegne ~50 ms dopo l'attacco,
~20 ms di silenzio, riparte poco prima di 70 ms; 3ª nota stesso schema a 70–90 ms;
4ª nota **due** silenzi (65–85 ms e 115–125 ms).

**Misure** (`tests/envelope_probe.cpp`, scritto apposta in s.28)
- Su `Fix ribattuto V1.wav`: **silenzio digitale vero (~−98/−101 dB) da t=2.0518 s a
  t=2.0668 s, ~15 ms**.
- Sul DRY (`Test 1 - Basic Silk Horns.wav`) nello stesso intervallo: **mai sotto −32 dB**.
  → il buco è **introdotto dal plugin**, non presente nella sorgente.
- Correlazione: `dumpPitchTrace` (block=64) mostra la confidenza di `PitchDetector`
  a **esattamente 0.000** (`hasStableSignal()=false`) per ~32 ms (t=2.036–2.068 s),
  quasi sovrapposta al buco. Crollo genuino del rilevatore BACF durante il transiente
  d'attacco (Do4→Re4), non un artefatto di misura.

**Cosa è stato ESCLUSO col codice vero, non con ricostruzioni**
- **Meccanismo A / slot freddo per round-robin** — `runFaseZeroTrace` traccia
  sample-per-sample l'`OnsetDetector` **reale** + un modello a 4 slot con `Voice` **reali**:
  il gate apre **una sola volta in 8 s e non si richiude mai**, e la colonna tracciata
  lega uno slot fisico **una sola volta** per l'intero file.
- **L'invariante F** (riserva calda di slot), che era il fix principale del piano di s.28:
  non ha nulla da correggere su questo materiale. **Non costruita**, d'accordo con l'utente.
- **Il debounce del gate (L1)**: nessuna chiusura spuria da debounciare. **Non scritto.**
- **FR-17 / celle vuote**: il preset reale dell'export ha solo R/2/3 compilati e la melodia
  (Do4→Re4→Mi4→Do4) tocca solo i gradi 0/2/4 — **nessuna cella vuota attraversata**.
- **Quattro riproduzioni offline con la catena DSP vera**: Passata 3 (voce singola,
  `kMajV1Table`) mai sotto −47 dB; **Passata 7** (stessa catena con la tabella **vera**
  dell'utente, shift fino a −10 semitoni) minimo −58 dB per un solo campione.
  **La stessa catena offline non riproduce il buco.**

**Sospetto attuale** — **underrun audio real-time** (CPU/buffer) durante
registrazione/export nel DAW, non un bug deterministico del DSP. Nessun percorso in
`PhraseScheduler` / `Voice` / `PsolaShifter` spiega un silenzio digitale di 15 ms.
**Non confermato.**

**Prossima azione** — l'utente riesporta lo stesso materiale con **bounce offline** o
dopo **freeze** della traccia.
- Buco sparito → underrun confermato, niente da correggere.
- Buco identico → riapre la ricerca di un meccanismo deterministico. Prossimo candidato:
  instrumentare `PsolaShifter::detectEpochs()` / `synthesise()` sul file reale.

**Nota** — l'isteresi cella vuota scritta in s.28 (`EmptyCellHold.h`) **non è il fix di
questo bug** e non va presentata come tale: le attese misurate durano 2136 ms e 279 ms,
quindi soglia 0 e soglia 80 ms producono output **identico**.

---

## B-06 — Slot freddo alla primissima nota dopo silenzio totale

**Stato: APERTO** · Ultimo tocco: s.27 (annotato, non affrontato) · Confermato all'ascolto: no

Uno slot appena allocato dopo silenzio totale parte comunque da **motore freddo** →
stesso buco di B-04, ma solo sulla primissima nota. Escluso deliberatamente dallo scope
di s.27.

**Due strade annotate**
1. Tenere caldi anche gli slot **liberi** (costo CPU: rapporto `processWarmOnly`/`processAdd`
   misurato **0.987**, cioè quasi il pieno costo di una voce).
2. **Condividere l'analisi fra le voci**: un solo ring e una sola `detectEpochs()` per tutte
   e 8 — è anche il vantaggio di CPU che il PRD §9.2 attribuisce a PSOLA e che oggi
   non viene sfruttato.

---

## B-07 — FR-42 non arriva in Play mode

**Stato: APERTO** · Ultimo tocco: s.23 (annotato, non affrontato)

`PlayModeInput` non riceve `setFormantSpread` / `setVoiceFormantOffset`. La correzione
delle formanti **deve** applicarsi anche in modalità Play (FR-42, `[MUST]`): oggi non
si applica. Da chiudere "quando si torna sui formanti".

---

## B-08 — Note saltate (corsa onset/pitch)

**Stato: CHIUSO** · Ultimo tocco: s.12 · Confermato all'ascolto: sì (s.12)

Al primo attacco "Active" restava a zero e la nota non veniva armonizzata. Causa: corsa
fra il rilevamento dell'onset e quello del pitch. Fix con allocazione **differita** degli
slot (FR-43/45/46). Confermato: armonizza tutte le note.

**Non verificati singolarmente** (nessun segnale che siano ancora un problema): il
contatore "late-bindings" e il caso "nota armonizzata sull'accordo della nota precedente"
(`pitchDetector` stantio). Se ricomparisse un caso limite su attacchi molto ravvicinati
o staccato rapido, ripartire da lì.

---

## B-09 — Canto legato non armonizzato / pitch non riconosciuto

**Stato: CHIUSO** · Ultimo tocco: s.11 · Confermato all'ascolto: sì (s.11)

Isteresi di intonazione (`PitchLatch`, tolleranza ±25 cent) + `signalPresent` separato da
`inputIsStable`. Il canto legato C→D→E armonizza correttamente ogni nota.
`pitch_latch_test` copre 8 gruppi, incluso il test anti-rimbalzo che ha scoperto il bug
prima dell'integrazione.

**Da tarare su altri casi limite** — la soglia ±25 cent e le soglie del gate
(−24 / −30 / −45 dB) su portamento ampio e legato molto piano.
