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
  previsto (sotto-blocchi in `processBlock`) **mai scritto** — vedi **A-05** fra le decisioni
  aperte. *(Fino a s.30 questa riga rimandava a "D-13", che è tutt'altro: la numerazione delle
  decisioni è cambiata quando i documenti sono stati divisi.)*
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
**Aggiornamento s.31**: una parte di quel residuo aveva una causa propria, misurata e
corretta — vedi **B-13**. Non chiude questa entry (B-02 è il timbro a nota tenuta, chiuso in
s.26) e non svuota i candidati qui sotto: dice solo che "sporco all'attacco" era un'etichetta
per almeno due fenomeni diversi.

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

**Stato: SOSPESO** (decisione dell'utente, s.30) · Ultimo tocco: s.30 · Confermato
all'ascolto: sì (è il sintomo)

**Decisione s.30** — L'utente sospende la ricerca: il sintomo si aggira **compilando tutte
le celle** delle voci che si vogliono attivare, e la regola va scritta nella guida utente.
Il bounce offline/freeze richiesto sotto (§ "Prossima azione") **non è mai stato fatto**:
in `SAMPLE TEST/` non è comparso alcun riesporto, e `Fix ribattuto V1.wav` non è più su
disco. L'ipotesi "underrun real-time del DAW" resta **non verificata**.

> **Discrepanza da non perdere.** La mitigazione scelta non combacia con le misure di
> questa entry: nell'export reale su cui B-05 è stato misurato **nessuna cella vuota veniva
> attraversata** (preset con solo R/2/3 compilati, melodia Do4→Re4→Mi4→Do4, gradi 0/2/4 —
> vedi "Cosa è stato ESCLUSO" più sotto). "Compilare tutte le celle" è il rimedio di
> **B-04** (chiuso) e della famiglia cella-vuota, non di questo sintomo. Se il ribattuto
> tornasse su un preset a celle tutte piene, l'aggiramento non reggerà e questa entry si
> riapre da qui — non da zero.

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

**Stato: SOSPESO** (decisione dell'utente, s.30) · Ultimo tocco: s.30 · Confermato
all'ascolto: no

**Decisione s.30** — Sospeso insieme a B-05, stessa motivazione: i sintomi dei buchi escono
dallo scope, la mitigazione è documentale. Nessuna delle due strade qui sotto è stata
imboccata. La strada 2 resta comunque interessante **a prescindere dal buco**: è il
vantaggio di CPU che il PRD §9.2 attribuisce a PSOLA e che oggi non viene sfruttato.

Uno slot appena allocato dopo silenzio totale parte comunque da **motore freddo** →
stesso buco di B-04, ma solo sulla primissima nota. Escluso deliberatamente dallo scope
di s.27.

**Due strade annotate**
1. Tenere caldi anche gli slot **liberi** (costo CPU: rapporto `processWarmOnly`/`processAdd`
   misurato **0.987**, cioè quasi il pieno costo di una voce).
2. **Condividere l'analisi fra le voci**: un solo ring e una sola `detectEpochs()` per tutte
   e 8 — è anche il vantaggio di CPU che il PRD §9.2 attribuisce a PSOLA e che oggi
   non viene sfruttato.

**Aggiornamento s.34 — risolto sul percorso Play, resta sospeso sull'Harmonizer.** Indagando
B-15 (click a ogni attacco in modalità Play) questo meccanismo è stato misurato lì e corretto,
con una **terza strada** che in s.30 non era stata vista e che costa molto meno della strada 1:
non si tengono caldi gli slot liberi, si riscalda **su richiesta** al note-on
(`processWarmOnly` per la latenza dichiarata, a voce muta, prima di far partire la
dissolvenza — lo schema di `Phrase::warmupSamples`/B-12). Il costo è quello di una voce per
~20 ms dopo ogni note-on invece che permanente, e il rapporto 0.987 misurato qui sopra resta
valido: è proprio perché è così alto che tenere caldi 8 slot liberi in permanenza non
conviene. Misurato sul percorso Play: salto d'ampiezza alla prima nota da 2.07–2.12 a
0.94–1.02 volte il regime.

Sul percorso **Harmonizer** nulla è cambiato: `PhraseScheduler` scalda solo al late-binding
(B-12) e sulle celle vuote (B-04), non alla prima nota dopo silenzio totale. Questa entry
resta quindi `SOSPESO` per quella metà, e la strada 2 resta interessante a prescindere.

---

## B-07 — FR-42 non arriva in Play mode

**Stato: CHIUSO** · Ultimo tocco: s.30 · Confermato all'ascolto: **sì** (s.30, *"ora funziona
tutto come dovrebbe"*) · Commit `ab79ae7`

**Sintomo** — `PlayModeInput` non riceve `setFormantSpread` / `setVoiceFormantOffset`.
La correzione delle formanti **deve** applicarsi anche in modalità Play (FR-42, `[MUST]`).

**Storia**
- s.23 — Annotato, non affrontato. Rimandato a "quando si torna sui formanti".
- s.30 — **Il perimetro scritto sopra era sbagliato, e va letto con questa correzione.**
  La correzione formantica **automatica** (FR-39) in Play ha **sempre funzionato**: le
  `Voice` del pool di Play partono dal default membro `formantSpread = 1.0f`
  (`Voice.h:181`) e in `ShiftMode::fix` `semitonesToApply` è lo shift reale
  (`Voice.cpp:109`, `targetAbsoluteMidi − continuousInputMidiNote`), quindi la formula di
  `Voice.cpp:129` produceva già una correzione sensata.
  Il buco vero: il knob globale **Fmt Spread** (FR-40) e gli 8 knob **Fmt/Voice** (FR-41)
  erano **inerti** in Play. `PluginProcessor.cpp` propagava le formanti solo a
  `phraseScheduler`, mentre nello stesso loop gain e pan andavano a **entrambi** i percorsi.
  Fix: `PlayModeInput::setFormantSpread` (tutti gli slot) e `setVoiceFormantOffset`
  (per indice di slot) + le due righe gemelle in `processBlock`, più uno
  `static_assert (PlayModeInput::maxNotes == harmony::numVoices)` — il loop usa un solo
  indice per due costanti indipendenti, e gain/pan correvano lo stesso rischio senza guardia.
  Misurato in `voice_test.cpp` blocchi **T-6/T-7** (5 verifiche, tutte verdi): a −12 st la
  separazione fra `spread=0` e `spread=1` è **+3.44 st misurati contro +3.60 attesi**, e un
  offset manuale di −3.6 st **annulla esattamente** la correzione automatica (picco
  formantico di ritorno sull'asciutto, scarto 0.0%). `pluginval --strictness-level 10`
  SUCCESS su VST3 (Win), `ctest` 7/7 verde.

**Perché non è `CHIUSO`** — i test coprono l'anello `Voice` (i due setter producono
l'effetto previsto), **non** il cablaggio: `PlayModeInput.h` include `juce_audio_basics`
per `juce::MidiBuffer` e le 7 suite sono deliberatamente prive di JUCE (D-11). Che i knob
arrivino fin lì è verificabile solo all'ascolto.

**Prossima azione (utente)** — in Play mode, con note MIDI tenute: girare **Fmt Spread**
da 0 a 1 e un knob **Fmt/Voice**. Il timbro della voce corrispondente deve cambiare; oggi
non succedeva nulla.
- **Da non confondere con questa verifica**: `kFormantSpreadK = 0.3` non è mai stato tarato
  all'ascolto. Qui si conferma che il knob *arriva*, non che il valore *sia giusto*.
- **Attenzione all'indice**: in Play "voce N" è l'**N-esimo slot allocato**, non un ruolo
  musicale — il knob agisce su qualunque nota sia finita in quello slot. È la stessa
  asimmetria che gain e pan hanno già da s.23, non una semantica introdotta ora.

**Limite noto emerso, non corretto** — `Voice::setFormantSpread` /
`setFormantOffsetSemitones` (`Voice.h:96-97`) scrivono il float **grezzo**, senza `Glide`,
a differenza di gain e pan che passano da `kDeclickMs`. Girare il knob produce un gradino
nel rapporto formantico. Vale già così in Harmonizer: se si sentisse un click girando i
knob formanti, il sintomo è questo e va aperto come entry propria.

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

---

## B-10 — Il selettore "Voices" non scende dal vivo

**Stato: CHIUSO** · Ultimo tocco: s.30 · Confermato all'ascolto: **sì** (s.30, *"ora posso
togliere o aumentare voci in live"*, poi *"ora funziona tutto come dovrebbe"*) · Commit `5c985e3`

**Sintomo** (utente, s.30) — Ad audio che suona, **alzare** il selettore aggiunge voci in
tempo reale (se ne sente una, poi 2, poi 3). **Abbassarlo no**: da 4 non torna a 3 e poi a 2.
Per risentirne una sola bisogna fermare l'audio, aspettare il silenzio e far ripartire.

**Requisito violato** — **FR-19** `[MUST]`, testuale: *"Il numero di voci attive è
selezionabile da 1 a 8. Le voci oltre il numero selezionato sono mute anche se il preset
contiene offset per loro."* Il workaround trovato dall'utente (ribattere la nota) è
precisamente ciò che **FR-17** stabilisce non debba servire.

**Meccanismo** — `numRequestedVoices` arriva a `PhraseScheduler::process()` **ogni blocco**
(`PluginProcessor.cpp:365` → `:503`): il problema non era a monte. Dentro lo scheduler
compariva in due soli punti, `:201` (trigger) e `:286` (late-binding), entrambi **puramente
allocativi** — sanno solo *aggiungere* slot. Il loop di rendering (`:362-424`) itera sempre
su tutte e 8 le colonne e processa qualunque slot legato: `numRequestedVoices` **non
compariva mai** lì, quindi `:420` continuava a chiamare `setMuted(false)` sulle colonne in
eccesso. L'unico percorso che le spegneva era `freeAllPhrases()` (`:234`), che gira **solo**
su `! signalPresent` — cioè esattamente "ferma l'audio, aspetta il silenzio". Su nota tenuta
o canto legato non arriva alcun onset, quindi non c'era altra via d'uscita.

**Storia**
- s.30 — Fix: nuovo ramo nel loop di rendering, prima del controllo sulla cella. Ricalca il
  ramo `releasing` già presente nello stesso file: `setMuted(true)` (dissolvenza
  `kDeclickMs`, anti-click di s.12/13), `processAdd` finché non è silenziosa, e solo a
  silenzio **confermato** `goCold()` + `slotIndices[v] = -1` — lo slot torna davvero al pool
  (`isSlotInUse()` scansiona `slotIndices`), quindi abbassare le voci abbassa davvero la CPU.
  Risalendo, il late-binding a `:291` rilega lo slot al blocco successivo.
  Scelta dell'utente fra restituire lo slot e tenerlo caldo: **restituirlo**; tenerlo caldo
  sarebbe costato quasi una voce piena (rapporto `processWarmOnly`/`processAdd` = 0.987, B-06).

**Misure** — nuova suite `tests/phrase_scheduler_test.cpp` (P-1..P-4), **primo test in
assoluto su `PhraseScheduler`**. Verificato che P-1 e P-2 **falliscono sul codice pre-fix**
prima di scrivere il fix:

| | Pre-fix | Post-fix |
|---|---|---|
| P-1 voci attive con selettore 4 → 2 → 1 | 4 / 4 / 4 | **4 / 2 / 1** |
| P-2 RMS dopo 4→2, scarto dal riferimento a 2 voci | +16.9% | **−3.1%** |
| P-2 stesso RMS, scarto dal riferimento a 4 voci | −1.8% | **−18.7%** |
| P-3 risalita 1→4 (regressione) | OK | OK, RMS a −3.7% dal riferimento |
| P-4 salto d'ampiezza al passaggio / a regime | 0.86 | **0.86** (nessun click introdotto) |

`ctest` 8/8 verde · `pluginval --strictness-level 10` SUCCESS su VST3 (Win).

**Conferma all'ascolto (s.30)** — *"Va decisamente meglio, ora posso togliere o aumentare voci
in live"*, con le transizioni in **discesa** giudicate fluide. La stessa conferma ha però
aperto **B-12** (click in aggiunta): sintomo diverso, entry propria, chiusa anch'essa dopo
il fix. **Non verificato singolarmente**: il comportamento con **Keep Tails ON**, dove il
ramo si applica anche alle code (vedi sotto).

**Scelta di progetto da conoscere** — il nuovo ramo si applica a **tutte** le frasi attive
non in rilascio, non solo a quella `isLive`. Il knob significa "quante voci sento", e
limitarlo alla frase viva avrebbe riprodotto una versione attenuata dello stesso reclamo con
Keep Tails ON. Tensione con FR-46 (voicing congelato al trigger) **annotata e non risolta**:
senza il pattern ritmico (FR-47..49, `[V1.1]`) tutte le voci di una frase entrano insieme, e
`Phrase.h:30-36` osserva già che oggi i due casi collassano.

**Nota** — è il **secondo** difetto di questa famiglia: il primo, corretto in s.12, è
documentato in `Phrase.h:37-45` (*"una frase superata da un nuovo onset con MENO voci della
precedente — le voci non riprese smettevano di essere processate, non solo mutate"*).
`PhraseScheduler` era l'unico modulo del progetto senza test, ed è il motivo per cui nessuno
dei due è stato intercettato prima dell'ascolto. Vedi D-16.

---

## B-11 — Abbassare il tetto voci simultanee non spegne gli slot già assegnati

**Stato: APERTO** · Ultimo tocco: s.30 (annotato, non affrontato) · Confermato all'ascolto: no

Gemello di B-10, stessa forma, modulo diverso. `setVoiceCap` / `maxSimultaneousVoices`
(FR-51) è usato in **un solo punto**, `PhraseScheduler.cpp:144`:
`const int numSlots = juce::jmin (currentVoiceCap, voicePool.getNumSlots());` — dentro
`allocateFreeSlot()`. È quindi anch'esso **puramente allocativo**: abbassare il tetto
impedisce di prendere slot nuovi, ma non spegne quelli già assegnati.

**Oggi non morde**: il default è 32 su 32 slot fisici, quindi il tetto non si raggiunge mai
in uso normale (servirebbero più di 4 frasi contemporanee a 8 voci).

**Perché non è stato riparato insieme a B-10** — non è un fix meccanico. `numVoices` è
per-frase e ha un ordine naturale (le colonne oltre l'N-esima), mentre il tetto FR-51 è
**globale fra frasi**: spegnere l'eccesso richiede di decidere **quale frase perde slot**
(la più vecchia? l'ultima allocata? proporzionalmente?). È una decisione di design, da
prendere con l'utente.

---

## B-12 — Click aggiungendo una voce dal vivo

**Stato: CHIUSO** · Ultimo tocco: s.30 · Confermato all'ascolto: **sì** (s.30, *"ora funziona
tutto come dovrebbe"*) · Commit `5c985e3`

**Sintomo** (utente, s.30) — Confermando il fix di B-10: *"se tolgo voci le transizioni sono
fluide, mentre se ne aggiungo sento un piccolo click ogni volta che aggiungo una nuova voce"*.

**Non introdotto da B-10** — verificato per costruzione, non per ragionamento. `P-5` misura
due scenari: una **salita pura** da una frase nata con 1 voce (dove il ramo aggiunto per
B-10 non scatta mai, perché le colonne oltre il selettore non hanno slot ed escono su
`slotIndex < 0`) e una **discesa seguita da risalita** (slot riciclati). Il difetto era
presente in entrambi. È preesistente.

**Meccanismo** — misurato da `P-6`, che isola il contributo della **sola voce aggiunta** per
differenza fra due rig identici (uno aggiunge la voce, l'altro no; tutto il resto è
deterministico e si cancella):

- **ritardo prima che la voce parta: 20.09 ms** — la latenza dichiarata del motore, che al
  binding è freddo. Lo stesso numero di B-04 (21 ms a Balanced).
- **tempo di salita 10%→90%: 3.56 ms**, contro gli **8 ms** di `kDeclickMs`.

La dissolvenza anti-click di `Voice` parte al `setMuted(false)` e si esaurisce **dentro** i
20 ms di silenzio del motore: quando il segnale vero arriva, `ampGlide` è già a guadagno
pieno e la voce entra di netto, con la sola rampa intrinseca del PSOLA. È il meccanismo di
B-04/s.27 (*"`ampGlide` consumato dentro il buco"*) su un evento diverso — lì la cella vuota,
qui l'aggiunta di una voce.

**Perché nessun test lo vedeva** — `P-4` e `P-5` usano `maxJump` **sul mix**, dove il salto
di una voce che entra è diluito fra le 2-3 già in suono; per giunta il riferimento "a regime"
cresce col numero di voci, quindi il rapporto saliva (1.06 → 1.80) per il solo fatto che il
segnale è più grande. `H1` in `voice_test.cpp` misurava il **rapporto di slew**, non il
**tempo di salita**, e aveva concluso "salita liscia, solo ritardata". Nessuna delle due
misure era sbagliata: erano inadatte a questa domanda (`CLAUDE.md` regola 13).

**Storia**
- s.30 — Fix: `Phrase::warmupSamples[v]`, un conto di campioni di riscaldamento impostato al
  **late-binding** alla latenza dello slot. Finché è > 0 la voce resta muta (`ampGlide` fermo
  a zero, nessuna dissolvenza sprecata) e viene alimentata con `processWarmOnly` — lo stesso
  meccanismo di s.27/B-04. A zero, il blocco successivo trova il motore pronto e gli 8 ms di
  dissolvenza coincidono con segnale vero.
  Misurato: **salita da 3.56 ms a 6.92 ms** (una rampa lineare di 8 ms misurata 10%→90% dà
  6.4 ms teorici, quindi la dissolvenza si spende ormai quasi tutta su segnale vero). Il
  ritardo passa da 20.09 a 24.67 ms: un blocco in più (il contatore scala di `numSamples`),
  impercettibile su una manopola.

**Deliberatamente NON toccato** — il ramo di `triggerNewPhrase()`, cioè le voci che nascono
con una frase nuova. Quello è **l'attacco di nota**, un evento diverso e già validato
all'ascolto (B-04 chiuso, e il residuo di B-02 accettato). Estendervi il riscaldamento
cambierebbe il suono di **ogni nota**: blast radius sproporzionato rispetto al sintomo
riportato. Se in futuro si volesse, la misura per deciderlo è già scritta (`P-6`).

**Residuo non verificato singolarmente** — il comportamento con **Keep Tails ON**, dove il
ramo di B-10 si applica anche alle code (tensione con FR-46, vedi B-10). La conferma
dell'utente è stata generale, non su quella configurazione specifica.

---

## B-13 — L'attacco viene sporcato dagli offset dei gradi intermedi

**Stato: CHIUSO** · Ultimo tocco: s.32 · Confermato all'ascolto: **sì** (s.32, *"test#3 e
test#2 ora suonano uguali; il problema delle celle vuote e delle voci intermedie è stato
risolto"*)

**Sintomo** (utente, s.31) — Attacchi di nota "sporchi" su tutti i preset **tranne** quelli in
cui l'offset non cambia mai. Quattro export a voce singola con reference (Autoshift):
`exp#1_Test#2_V1_00` (R/2/3 tutti −7, b2/b3 **vuote**) è *"l'unico praticamente perfetto"*;
`exp#1_Test#3_V1_00` ha gli stessi offset sui gradi eseguiti ma le celle intermedie a −2, ed
è sporco. Formulazione dell'utente, che era già la diagnosi: *"l'attacco è perfetto solo se
non cambiano gli offset tra una nota e l'altra e anche in tutte quelle intermedie"*.

**Meccanismo** — `PitchLatch::update()` si spostava di **un semitono per chiamata**, ed è
chiamata **una volta per blocco**. Un salto C→E non arrivava in un colpo: l'aggancio passava
per C#, D, D#, un blocco ciascuno, e ad ogni passo `HarmonyEngine::getOffsets` leggeva la
colonna di quel grado, che `PhraseScheduler` applicava come target reale sulla frase viva.
Con Glide 0 ms ogni gradino è un salto d'intonazione istantaneo. Test#2 era immune perché le
celle vuote sono già protette da `stepEmptyCellHold` (s.28): quella protezione esisteva solo
per la cella vuota, non per una cella piena con valore diverso.

Il passo incrementale (s.11) nasceva da un problema reale — un passo *incondizionato* rimbalza
durante uno scivolamento lento — e lo risolveva. Il prezzo non era stato visto.

**Misure** (tre percorsi indipendenti, tutti in `LOG/sessione-31.md`)
- **Sull'export reale** (`real_export_probe`, REF contro plugin): Test#3, transizione C4→D4,
  il wet sta a **262.8 Hz per ~30 ms** dove il riferimento sta a 196 — cioè D4 trasposto di
  **−2**, la cella b2 del preset — a RMS pieno e con periodicità che crolla a 0.48. Test#1,
  discesa E4→C4: **248 Hz per ~50 ms** dove il target è 175, cioè **−1**, la cella del grado 2.
  Test#2 (controllo): nessuna escursione.
- **Nel codice** (`degree_trace_probe`, sonda nuova, moduli veri senza `Voice` né PSOLA):
  tabella Test#3 a block 1024, **10 corse di offset applicato invece di 4**, di cui 5 di
  passaggio per 116.1 ms, ciascuna lunga **esattamente un blocco**. Test#2: 2 corse, 0 di
  passaggio.
- **Dipendenza dal buffer**: il numero di corse spurie resta 5 a ogni block size, la durata
  scala — 20.3 ms a 128, 58.0 a 512, 116.1 a 1024, **464.4 a 4096**.
- **La stima del rilevatore NON attraversa**: a 128 campioni salta pulita da 59.969 a 62.233
  (C→D) e da 61.978 a 64.232 (D→E). La spazzata era fabbricata dentro `PitchLatch`.
- **Secondo fenomeno, solo sulla discesa E→C**: riacquistata confidenza dopo il transiente, il
  rilevatore riporta **60.696** (70 cent crescente, arrotonda a 61 = b2) per **14.5 ms** prima
  di assestarsi su 60.4. Il solo salto diretto adotterebbe quel 61.

**Storia**
- s.31 — Fix in `src/harmony/PitchLatch.h` (più la riga di chiamata e il `prepare()` in
  `PluginProcessor.cpp`). Passo di un semitono → **candidato + adozione diretta**: oltre i
  ±25 cent, e solo se l'arrotondamento è diverso dalla nota agganciata, quell'arrotondamento
  diventa candidato e viene adottato **di colpo** dopo `kNoteSettleMs` = 25 ms di candidato
  costante. Soglia in **millisecondi contati sui campioni del blocco**, non in numero di
  chiamate. I 25 ms sono scelti più lunghi dei 14.5 ms di stima sbagliata misurati, con
  margine — **non tarati all'ascolto**.
  Il motore **non è stato toccato**: `PsolaShifter`, `Voice`, `PhraseScheduler` invariati, e
  `psola`/`voice` restano verdi identiche.

**Post-fix, misurato** — corse di passaggio **0** su tutte e quattro le tabelle (Test#1/#2/#3,
Maj) e a tutti i block size 128→4096. A 1024 l'offset di destinazione è adottato allo stesso
blocco di prima sulle salite, e **un blocco prima** sulla discesa. A 128 la discesa arriva
~24 ms più tardi (è l'attesa), ma pulita. `ctest` 8/8, `pluginval --strictness-level 10`
SUCCESS su VST3.

**Conferma (s.32)** — la predizione scritta prima dell'ascolto (*"`Test#3_01` deve suonare
come `Test#2_00`"*) ha retto: *"test#3 e test#2 ora suonano uguali"*. Entry chiusa.

**La stessa conferma ha aperto B-14** — *"per una frazione di secondo la nota suona con
l'offset di quella precedente, poi salta a quello corretto"*. È il residuo che questa entry
dichiarava di non risolvere (l'istante in cui l'unico cambio atterra), non una ricaduta:
sintomo diverso, entry propria.

**Contraddice la mitigazione di D-15** — *"vanno compilate tutte le celle delle voci che si
vogliono attivare"* è **controproducente** su questo sintomo: Test#3 ha le celle compilate ed
è peggio di Test#2 che le ha vuote. Vedi D-17.

**Non risolve** — la quantizzazione al blocco dell'istante in cui il cambio atterra (A-05),
deliberatamente fuori scope.

---

## B-14 — L'offset corretto arriva dopo l'attacco: la nota parte con quello della precedente

**Stato: CHIUSO** · Ultimo tocco: s.32 · Confermato all'ascolto: **sì** (s.32, *"il timbro è
corretto e gli attacchi pure"*)

> **Natura della conferma, da non confondere in futuro.** È stata data **dal vivo in Ableton**
> sulla build delle 12:40, **non** su un export confrontato con la REF: in `SAMPLE TEST/`
> non esiste alcun `_02`. Quindi i numeri di questa entry (79.1 → 44.3 ms) restano
> **verificati per calcolo**, e ciò che l'utente ha confermato è che *all'ascolto la flem non
> dà più fastidio*. Se il sintomo tornasse, il primo passo è l'export `_02` che qui manca.

**Sintomo** (utente, s.32, confermando il fix di B-13) — *"per una frazione di secondo, quando
comincia a suonare la nota, suona l'offset della nota precedente, poi salta a quello corretto.
In Test#1 la seconda nota parte a −7 e si corregge a −1, la terza parte a −1 e si corregge a
−7"*. Domanda posta insieme al sintomo: è migliorabile o è fisiologico?

**Non è un difetto nuovo**: è il residuo che B-13 dichiarava esplicitamente di non risolvere
(l'istante in cui l'unico cambio atterra). Entry propria perché il meccanismo è un altro.

**Misure**
- **Sull'export reale** `exp#1_Test#1_V1_01` contro la sua REF, transizione C4→D4: la REF sale
  a 283 Hz a t=2.000; il plugin resta a **192→198 Hz fino a 2.070** — cioè la nota nuova con
  l'offset **vecchio** — e arriva a 279 Hz solo a 2.090. **Ritardo ≈ 85 ms.**
- **Da dove vengono**: `cycfi::q` ricava la finestra d'analisi dalla frequenza minima
  (`bacf_period_detector`: `_zc(hysteresis, lowest_freq.period() * 2 * sps)`) e produce una
  stima ogni mezza finestra; sopra c'è un `median3` che su un cambio di nota chiede due frame
  concordi. Con i **60 Hz** cablati fino a s.31: finestra **33.4 ms**, una stima ogni
  **16.7 ms**, e il pitch nuovo compare **87 ms** dopo l'attacco. Il resto era la nostra
  attesa fissa da 25 ms.
- **60 Hz è B1**, molto sotto quanto serve a voce/sax/tromba (FR-14) — e sotto il **Trombone
  (Ab1 = 51.9 Hz)**, che quindi nelle note gravi non veniva rilevato affatto.

**Storia**
- s.32 — Tre interventi, nessuno dei quali tocca il motore.
  1. **La nota più grave d'analisi diventa un parametro utente** (`analysisLowestNote`, D-18),
     presentato come scelta dello strumento fra 10 voci ordinate dal più acuto al più grave.
  2. **L'attesa di `PitchLatch` si misura in frame d'analisi** invece che in millisecondi
     fissi (`settleSamplesForFrame`, 1.5 frame): segue da sola la finestra scelta. A 60 Hz
     vale esattamente i 25 ms di prima, quindi su quella configurazione non cambia nulla.
  3. **L'aggancio si aggiorna a ogni stima nuova del rilevatore**, non una volta per blocco.
     Era un difetto vero introdotto in s.31: con un blocco più lungo dell'attesa, una stima
     vista **una sola volta** si vedeva accreditare un blocco intero e l'attesa diventava un
     no-op. Misurato: a C4 e blocco 1024 un singolo frame sbagliato (61.771 dove il vero è
     63.984) veniva adottato — **B-13 che rientrava dalla finestra**. Ora il tempo passato
     all'attesa è quello vero fra due stime, e non dipende più dal buffer.

**Post-fix, misurato** (ritardo medio del cambio d'offset rispetto all'attacco):

| buffer | prima (B1) | dopo (E2, default) |
|---|---|---|
| 128 | 93.6 ms | **55.9 ms** |
| 512 | 90.7 ms | **50.1 ms** |
| 1024 | 79.1 ms | **44.3 ms** |

Matrice di non-regressione B-13 al default: **24 configurazioni** (4 tabelle × 6 block size
da 128 a 4096), **0 corse di passaggio**. `ctest` 8/8, `pluginval --strictness-level 10`
SUCCESS su VST3.

**Il compromesso ha due facce, ed è la scoperta importante** — una finestra corta è più pronta
ma anche più **rumorosa**: il rilevatore sbanda più spesso, e su un preset con tutti i gradi
compilati ogni sbandata è un offset sbagliato udibile. Misurato a blocco 1024:

| nota minima | finestra | Test#1 | Test#2 | Test#3 | Maj |
|---|---|---|---|---|---|
| C4 | 8.7 ms | 0 | 0 | 2 | 2 |
| Ab3 | 10.2 ms | 0 | 0 | 2 | 3 |
| E3 | 13.1 ms | 0 | 0 | 0 | 2 |
| Db3 | 14.5 ms | 0 | 0 | 1 | 2 |
| Ab2 | 20.3 ms | 0 | 0 | 1 | 1 |
| **E2 (default)** | 24.7 ms | **0** | **0** | **0** | **0** |
| B1 (era) | 33.4 ms | 0 | 0 | 0 | 0 |

**Nessuna taratura dell'attesa salva le finestre corte**: provato fino a 25 ms di attesa
assoluta (5.7 frame a C4), C4/Ab3/E3/Db3 restano con 1 corsa spuria. Non è l'attesa a essere
breve, è il rilevatore a sbagliare. Da qui il default a **E2** invece del **Ab2** scelto
inizialmente dall'utente: 12 ms di ritardo in più non valgono il rientro di B-13.

**Limite da verificare, non da dare per buono** — quella tabella viene da **un solo file**, che
suona C4-D4-E4: il registro **grave** per un flauto o un soprano sax. Quando quegli strumenti
suonano nel proprio registro la finestra corta potrebbe comportarsi benissimo. Le voci acute
restano quindi in lista, per decisione dell'utente, e **serve un export dedicato** (flauto o
tromba nel loro registro) per misurarle davvero.

**Conferma (s.32)** — data dal vivo, insieme alla chiusura dell'intero fronte del motore di
pitch shifting (D-19): *"il timbro è corretto e gli attacchi pure"*. **Non verificato
singolarmente**: che Test#2 e Test#3 restino uguali fra loro con la build nuova (la rete
anti-regressione di B-13) è stato controllato **solo per calcolo**, sulla matrice di 24
configurazioni.

**Cosa resta comunque** — il ritardo non va a zero. Restano la convergenza del rilevatore e il
confine di blocco. Azzerarlo richiede il **lookahead** (ritardare l'audio e dichiarare la
latenza all'host), rimandato per scelta dell'utente in s.32: contrasta con PRD §1.3
(≤ 15 ms nel modo più reattivo) e va aperto come decisione a sé.

---

## B-15 — Click a ogni attacco di nota in modalità Play

**Stato: APERTO** (fix in codice, misurato) · Ultimo tocco: s.34 · Confermato
all'ascolto: **non ancora** — serve la conferma dell'utente prima di `CHIUSO`
(`CLAUDE.md` regola 12)

**Sintomo** (utente, s.34) — *"Quando clicco una nota sulla tastiera midi il plug in la
armonizza correttamente, ma sento un click all'inizio."* Precisato su domanda:
**ogni volta che si preme un tasto**, non solo alla prima nota dopo un silenzio.

**Perché è entry propria e non B-06** — B-06 descrive lo slot freddo alla *primissima*
nota, sul percorso di allocazione dell'Harmonizer. Questo è un sintomo diverso (ogni
note-on, in Play) e la sua causa dominante è risultata un'altra: il rapporto di
trasposizione, non la temperatura del motore. B-06 resta aperto per la sua metà
Harmonizer, vedi la nota aggiunta lì.

**Banco di misura** — `tests/play_mode_input_test.cpp`, nuovo, in `ctest` (secondo livello
D-16: linka `juce_audio_basics` per `juce::MidiBuffer`). Pilota il **vero** `PlayModeInput`
con veri `MidiBuffer`: è l'unico livello a cui esistono i note-on e l'allocazione degli slot.
Sorgente sintetica stazionaria a 220 Hz, `inputIsStable` sempre vero, così l'unica variabile
è il tasto premuto. Metriche PM-1 (ritardo), PM-2 (salita 10→90%), PM-3 (salto di ampiezza
all'attacco / salto a regime), PM-4 (intonazione nella dissolvenza), PM-5/PM-6/PM-7 (scenari).

**PM-2 non è servita, ed è una lezione** — era la metrica ovvia (con quella fu chiuso B-12),
ma su questo percorso misura male: l'inviluppo RMS dell'uscita PSOLA è grumoso alla cadenza
dei grani, e la stessa "prima nota" dava 4.9–10.1 ms a MIDI 64 e 2.2 ms a MIDI 52 — cambiava
con la profondità dello shift, non con la presenza del difetto. Il cancello è su **PM-3**, che
è la definizione di click usata in questo progetto da s.12 (un salto di ampiezza discontinuo).

**Due cause, trovate in quest'ordine**

1. **Nessun riscaldamento del motore, e nessun `goCold()`.** Uno slot senza nota premuta non
   riceve campioni (`PlayModeInput.cpp` saltava `processAdd`, e `Voice::processAdd` esce
   comunque su `isSilent()`), quindi il `PitchShifter` restava **affamato**: `absWrite`/
   `absRead`/`lastEpoch`/`synthPos` congelati e ring pieno dell'audio della nota precedente.
   `goCold()` non era chiamato da nessuna parte in Play (grep su tutti i call site: zero).
   Al note-on i primi `latency` campioni in uscita venivano da quel contenuto vecchio, e gli
   8 ms di `ampGlide` si consumavano lì dentro. È B-12 alla lettera, mai portato su Play.
   Misurato: la seconda nota diventava udibile **0.70 ms** dopo il note-on (a Balanced) —
   cioè il motore sputava subito la coda della nota precedente.

2. **Il riscaldamento da solo non bastava, ed è la causa vera del sintomo riportato.**
   `processWarmOnly` alimentava il motore ma non gli passava il nuovo rapporto di
   trasposizione, che arrivava solo al primo `processAdd`. `PsolaShifter::synthesise()`
   riempie `outBuf` **in anticipo** (fino a `absWrite - maxPeriod`): al momento in cui la
   dissolvenza partiva c'erano già ~10 ms sintetizzati all'intonazione della nota
   **precedente**, e la giunzione con quella giusta cadeva a guadagno pieno.

**L'esperimento che ha separato le due** (PM-7, scritto apposta prima di credere all'ipotesi —
D-09, *"verificare col codice vero"*): ripetere lo scenario con la seconda nota **alla stessa
altezza** della prima. Stesso rapporto, nessuna intonazione sbagliata da sintetizzare. PM-3
risulta **0.98–1.03 prima di qualunque fix** e 0.96–1.00 dopo: non è un caso corretto dal fix,
è il **controllo** che isola il cambio di rapporto come causa unica del residuo.

**PM-3 — salto d'ampiezza all'attacco, rapportato al regime** (5 livelli di Stability)

| scenario | prima | dopo il solo riscaldamento | dopo entrambi i fix |
|---|---|---|---|
| prima nota dopo il silenzio | 2.07–2.12 | 1.00–1.05 | **0.94–1.02** |
| 2ª nota, altezza **diversa** | 3.30–5.08 | 2.32–5.11 | **0.99–1.03** |
| 2ª nota, **stessa** altezza (controllo) | 0.98–1.03 | — | 0.96–1.00 |

**Altre misure, prima → dopo**
- **PM-2**, seconda nota: 2.15–5.24 ms → **7.71–8.59 ms**, cioè `kDeclickMs` (8 ms). La
  dissolvenza cade ora su segnale vero invece di consumarsi nel silenzio del motore.
- **PM-1**, seconda nota a Balanced: 0.70 ms → **24.4 ms**. Non è un peggioramento: prima
  quel suono immediato *era* la coda della nota precedente. Ora l'attacco udibile arriva alla
  latenza dichiarata del motore più la rampa, come per la prima nota.
- **PM-4**: prima, ad Accurate, l'autocorrelazione nella finestra di dissolvenza leggeva
  **164.7 Hz** — esattamente la nota precedente (164.81 Hz) invece dei 392.0 richiesti. Dopo,
  l'energia armonica alla nota richiesta supera quella alla precedente di **+10.1…+10.6 dB**.
  (La metrica è cambiata in corsa: l'autocorrelazione su una finestra a cavallo di una
  transizione di intonazione sbaglia ottava e restituisce numeri che non corrispondono a
  nessuna delle due note. Il confronto diretto di energia armonica non stima nulla.)
- **PM-6**, nota ribattuta entro la dissolvenza: a +30 ms dal ri-attacco l'intonazione era a
  **−1219.8 cent** dal bersaglio, ora **+2.3 cent**; a +10 ms è già in bersaglio (+0.2 cent).

**Fix (s.34)** — tre pezzi, tutti citati nei commenti al codice:
- `src/midi/PlayModeInput.cpp` — `warmupSamples` + `engineIsCold` per slot. Slot fermo e
  silenzioso → `goCold()` **una volta sola** (`PitchShifter::reset()` azzera buffer interi,
  non può girare a ogni blocco su 8 slot, PRD §9.4). Slot con nota premuta e motore freddo →
  `processWarmOnly` per la latenza dichiarata, muto, prima di far partire la dissolvenza. Il
  conto è **derivato** dallo stato del motore, non registrato al note-on: così vale anche
  quando la nota resta premuta ma l'ingresso perde stabilità (FR-20). I tre rami "non deve
  farsi sentire" (modalità spenta, nessuna nota, ingresso instabile), prima copiati identici,
  sono ora uno solo.
- `src/voices/Voice.{h,cpp}` — **estrazione pura** di `runShifter` dal corpo di `processAdd`
  (aggancio di `justReactivated`, `offsetGlide`, shift, formanti, corsa del motore), più un
  overload `processWarmOnly` a 4 argomenti che lo riusa scartando l'uscita. La versione a 3
  argomenti **resta identica** e continua a servire `PhraseScheduler`.
- `src/midi/PlayModeInput.cpp` — `setMuted(false)` va chiamato **all'inizio** del
  riscaldamento, non alla fine: è lui ad armare `justReactivated`, ed è `justReactivated` ad
  agganciare l'intonazione al bersaglio. `ampGlide` non avanza durante il riscaldamento
  (`Glide::processRamp` vive solo in `processAdd`), quindi la rampa degli 8 ms comincia
  comunque dopo, sul segnale vero.

**Trovato e corretto per strada, sintomo diverso** — al note-off lo slot tornava libero subito
ma la voce impiega 8 ms a spegnersi, e `std::find(..., -1)` restituiva **lo stesso** slot
ancora in dissolvenza. Lì `justReactivated` non si arma (si arma solo su una voce
`isSilent()`), quindi l'intonazione scivolava dalla nota vecchia alla nuova in `glideTimeMs`.
Ora fra gli slot liberi si preferisce uno già `isSilent()`. Limite residuo: con 8 note
rilasciate e ripremute entro 8 ms non ce ne sono, e si ricade sulla scivolata.

**Non tocca** (verificato, non assunto) — `voice`, `psola` e `phrase_scheduler` restano
**bit-identiche** all'uscita salvata prima di iniziare (D-19: sono la rete di regressione del
motore). La catena Harmonizer non passa da nessuna delle righe nuove.

**Post-fix** — `ctest` 10/10, `pluginval --strictness-level 10` SUCCESS su VST3/Win,
Standalone costruito. AU non verificabile su questa macchina (limite già noto).

**Prossima azione** — ascolto dell'utente in Ableton, modalità Play, sorgente audio sostenuta:
nota singola dopo un silenzio lungo, note ripetute sulla stessa altezza, note a altezze
diverse in sequenza, accordo di 4–8 note insieme. Finché non arriva, questa entry resta
`APERTO`.

**Limite noto che questo lavoro NON tocca** — lo stesso miglioramento del punto 2 è
probabilmente disponibile anche per i rami di riscaldamento di `PhraseScheduler` (celle vuote
B-04, late-binding B-12), che continuano a usare la `processWarmOnly` a 3 argomenti e quindi
scaldano il motore col rapporto vecchio. Deliberatamente fuori scope: va **misurato** sul
percorso Harmonizer prima di cambiare qualcosa lì, e la catena Harmonizer è oggi giudicata
soddisfacente all'ascolto (D-19).
