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
