# Sessione 30 — 2026-08-10

> Racconto per esteso. Archivio: non si rilegge, si consulta.
> Stato di oggi in `HANDOFF.md`, sintomi in `BUGS.md`, decisioni in `DECISIONS.md`.

---

## 1. Punto di partenza e primo esito: l'archivio non era il problema, `HANDOFF` sì

La sessione si apre con un bivio: se l'utente avesse fatto il bounce offline per B-05,
misurarlo; altrimenti, proporre su cosa lavorare fra A-06, B-07, FR-59 e A-07.

`SAMPLE TEST/` conteneva tre soli file. L'unico nuovo (di oggi, 11:01) era
`DBG Timbro/REFERENCE Empty cell DBG V1.wav`, che dal nome riguarda la **cella vuota**, non
il ribattuto; `Fix ribattuto V1.wav`, il file su cui B-05 era stato misurato in s.28, non è
più su disco. Nessun bounce. Secondo ramo.

Prima di consigliare, verifica del codice reale invece che dei documenti — e **tre voci di
`HANDOFF.md` si sono rivelate false**:

1. *"FR-59 assente: finestra a dimensione fissa `setSize(900,660)`, nessun ridimensionamento"*
   → falso. `PluginEditor.cpp:392` `setResizable(true,true)` e `:399`
   `setResizeLimits(520,620,1200,900)` esistono da s.25. Quello che manca è la **scala
   percentuale** 70–200%: zero occorrenze di `setScaleFactor`/`AffineTransform` in `src/`,
   tutte le costanti di layout in pixel hardcoded. Oggi è *reflow*, non *scaling*.
2. *"Colonne tagliate su griglia fondamentale e tabella 12×8"* → falso su entrambe.
   `RootNoteGrid` è 2×6 **per requisito** (FR-75, reshape in `39c3282`).
   `PresetTableEditor.cpp:120,129` divide la larghezza disponibile per 12: le colonne ci
   sono sempre tutte, si **comprimono** (69 px a 900, 38 px a finestra minima).
3. *"Working tree sporco: il lavoro di s.28 non è mai stato committato"* → falso, `be9a40f`
   lo aveva committato.

Conseguenza pratica: la mitigazione poi scelta dall'utente ("compila tutte le celle") era
già eseguibile nella UI — tutte e 96 le celle sono raggiungibili. Se avessi creduto a
`HANDOFF` avrei proposto un lavoro di UI che non serviva.

**Lezione di metodo, gemella di D-09**: la Fase 0 di s.28 aveva insegnato a non fidarsi di
una ricostruzione in sola lettura contro il codice vero. Qui la stessa lezione si applica ai
*documenti di stato*: un limite noto scritto una volta sopravvive alle sessioni che lo
risolvono. Le voci "limiti noti" vanno riverificate contro il codice, non ricopiate.

## 2. La decisione dell'utente: i buchi escono dallo scope

L'utente sospende B-05 e B-06: *"dimentica i problemi di audio… se le celle sono tutte
compilate il problema non sussiste, e sarà da aggiornare nella guida che affinché il plug-in
funzioni bene devono essere compilate tutte le celle delle voci che si vogliono attivare"*.

Registrata come **D-15**. Due cose annotate insieme alla decisione, per non perderle:

- **La mitigazione non combacia con le misure di B-05.** Nell'export su cui il sintomo è
  stato misurato in s.28, il preset aveva solo R/2/3 compilati e la melodia (Do4→Re4→Mi4→Do4)
  toccava i gradi 0/2/4: **nessuna cella vuota attraversata**. "Compilare tutte le celle" è
  il rimedio di B-04 e della famiglia cella-vuota, non di questo sintomo. Se il ribattuto
  tornasse su un preset a celle piene, l'aggiramento non regge — e l'entry si riapre da lì.
- **La guida utente non esiste.** Nessun README utente, nessuna `docs/`, nessun tooltip
  in-app: i 7 `.md` alla radice sono tutti documenti di processo. La regola ha ora un posto
  designato che va costruito, ed è in tensione col criterio d'uscita di M5 (*"un tester
  esterno usa il plugin senza documentazione"*).

## 3. B-07 — il perimetro era descritto male

Scelto B-07 come prossimo passo. Prima di scrivere, lettura del percorso reale — e anche qui
la descrizione in `BUGS.md`/`HANDOFF.md` (*"FR-42 non arriva in Play mode"*) era imprecisa.

**Non era vero che in Play le formanti non si applicano.** La correzione **automatica**
(FR-39) funzionava già a piena forza: le `Voice` del pool di Play partono dal default membro
`formantSpread = 1.0f` (`Voice.h:181`), e in `ShiftMode::fix` `semitonesToApply` è lo shift
reale (`Voice.cpp:109`, `targetAbsoluteMidi − continuousInputMidiNote`), quindi
`autoFormantSemitones = −0.3 · spread · semitonesToApply` produceva un valore sensato.

Il buco vero: i due **controlli** erano inerti. `PluginProcessor.cpp` propagava
`setFormantSpread` (FR-40) e `setVoiceFormantOffset` (FR-41) **solo** a `phraseScheduler`,
mentre nello stesso loop gain e pan andavano a **entrambi** i percorsi. Mancavano due righe
gemelle. Il commento a `Voice.cpp:118-122` prevedeva già questo lavoro: *"restera' identica
anche in modalita' Play (FR-42) quando esistera'"*.

### Il fix

- `PlayModeInput.h`: `setFormantSpread` (loop su tutti gli slot, come
  `PhraseScheduler::setFormantSpread`) e `setVoiceFormantOffset` (scrittura diretta sullo
  slot — qui non esiste il concetto di frase né di colonna armonica).
- `PluginProcessor.cpp`: le due righe gemelle nel loop esistente.
- `static_assert (PlayModeInput::maxNotes == harmony::numVoices)`: il loop usa un solo
  indice per due costanti **indipendenti** che oggi valgono entrambe 8. Gain e pan correvano
  lo stesso rischio dal s.23 senza alcuna guardia; ora sono coperti tutti e tre.

Nessuna decisione di design nuova: in Play "voce N" è l'N-esimo **slot allocato**, non un
ruolo musicale — esattamente l'asimmetria che gain e pan hanno già. Seguirla era la scelta
coerente; inventare una mappatura diversa avrebbe creato una semantica nuova per un solo
parametro.

Threading (regola 1): quattro setter `noexcept` che scrivono `float` membri, nessuna
allocazione, nessun lock. Percorso identico a `setVoiceGainLinear`/`setVoicePan`.

### La misura (regola 12)

`voice_test.cpp`, blocchi **T-6/T-7**. Due scelte di progetto del test valgono la pena di
essere ricordate:

- **Shift di prova −12 st, non −5.** La correzione automatica vale `−k·spread·shift`, quindi
  uno shift piccolo produce uno spostamento formantico dentro la tolleranza di misura: il
  test sarebbe passato **anche col knob inerte**, cioè avrebbe mancato esattamente il bug da
  scoprire. A −12 st l'effetto atteso è +3.6 semitoni-equivalenti, ben separato dallo zero.
- **Il riferimento asciutto si misura, non si assume.** `makeVowel` ha una risonanza nominale
  a 1100 Hz, ma `formantPeak` sull'asciutto ne misura **1000** — bias sistematico del proxy
  (lisciatura a ±6 bin). Misurandolo con la stessa funzione, il bias si cancella nel rapporto.

Risultati, tutti verdi:

| Verifica | Misurato | Atteso | Scarto |
|---|---|---|---|
| `spread=0` (correzione spenta) | 1000 Hz | 1000 Hz | +0.0% |
| `spread=1` (correzione piena) | 1220 Hz | 1231 Hz | −0.9% |
| separazione `spread=0`→`spread=1` | **+3.44 st** | **+3.60 st** | −0.16 st |
| `spread=0`, offset manuale +5 st | 1440 Hz | 1335 Hz | +7.9% |
| `spread=1`, offset manuale −3.6 st (annulla) | 1000 Hz | 1000 Hz | +0.0% |

La riga "separazione" è il controllo **discriminante**: con il knob inerte darebbe 0.00 st
contro 3.60 attesi, fallendo di 3.6 volte la soglia. Il test di annullamento è il più
stringente sulla forma della formula: solo una somma in semitoni riporta il picco
esattamente sull'asciutto.

Lo scarto +7.9% sull'offset manuale è la misura peggiore e resta dentro la tolleranza del
12% ereditata da `psola_test` TEST 3 (dove lo stesso proxy era già stato tarato contro un
beta noto). **Non è stata stretta la soglia per far sembrare il risultato migliore**: è il
comportamento noto di `formantPeak` a beta grandi.

### Perché B-07 non è chiuso

I test coprono l'anello `Voice` — che i due setter producano l'effetto previsto. **Non**
coprono il cablaggio: `PlayModeInput.h` include `juce_audio_basics` per `juce::MidiBuffer` e
le 7 suite sono deliberatamente prive di JUCE (D-11). Che i knob arrivino fin lì è
verificabile solo all'ascolto. `BUGS.md` § B-07 resta `APERTO (fix scritto, attende l'ascolto)`.

## 4. Verifiche

- `ctest -C Release`: **7/7 verdi**.
- `cmake --build build --config Release`: exit code 1, ma solo `MSB3073` su `copyDir.cmake`
  (D-12). Filtrato `error C####` / `error LNK`: **nessuna occorrenza**, build valida.
- `pluginval --strictness-level 10` su VST3 Release (Win): **SUCCESS**, exit 0.

## 5. Limite noto emerso e non corretto

Due cose trovate leggendo, entrambe registrate e nessuna affrontata:

- **Keep Tails irraggiungibile a finestra minima**: a 620 px `layoutEdit()` chiede ~528 px di
  contenuto su 522 disponibili, e `keepTailsToggle` (`PluginEditor.cpp:592`) collassa ad
  altezza ~0. Nessun viewport verticale sulle pagine.
- **Knob formanti senza rampa**: `Voice::setFormantSpread`/`setFormantOffsetSemitones`
  scrivono il float grezzo, a differenza di gain e pan che passano da `kDeclickMs`. Girare
  il knob è un gradino nel rapporto formantico. Vale già in Harmonizer, quindi questo lavoro
  non peggiora nulla — ma se si sentisse un click girando i knob formanti, la causa è questa.

## 6. Scostamento dal piano approvato

Il piano diceva *"`DECISIONS.md`: nessuna entry nuova"*. Aggiunta invece **D-15**: sospendere
due sintomi aperti dietro un aggiramento documentale è precisamente il tipo di scelta che
`DECISIONS.md` esiste per non far ridiscutere due volte, e genera un deliverable nuovo (la
guida). Il resto del piano è stato eseguito com'era scritto.

## 7. B-10 — il selettore "Voices" non scende dal vivo

Segnalato dall'utente a lavoro su B-07 concluso: ad audio che suona, **alzare** le voci le fa
entrare in tempo reale, **abbassarle** no; per risentirne una sola bisogna fermare l'audio,
aspettare il silenzio e ripartire.

Requisito violato, testuale — **FR-19** `[MUST]`: *"Il numero di voci attive è selezionabile
da 1 a 8. Le voci oltre il numero selezionato sono mute anche se il preset contiene offset
per loro."* E il workaround dell'utente (ribattere la nota) è esattamente ciò che **FR-17**
stabilisce non debba servire.

### Il meccanismo, e perché era asimmetrico

`numRequestedVoices` arriva a `PhraseScheduler::process()` **ogni blocco**
(`PluginProcessor.cpp:365` → `:503`): il problema non era a monte. Dentro lo scheduler
compariva in **due soli punti** — `:201` (trigger) e `:286` (late-binding) — ed **entrambi
sono puramente allocativi**: sanno solo *aggiungere* slot.

Il loop che rende l'audio (`:362-424`) itera sempre su tutte e 8 le colonne e processa
qualunque slot legato. `numRequestedVoices` **non compariva mai** lì. Da cui:

| Azione | Effetto |
|---|---|
| Alzo 2→4 | `:286` smette di saltare v=2,3 → `allocateFreeSlot()` lega gli slot → si sentono |
| Abbasso 4→2 | Nulla deassegna `slotIndices[2..3]` → `:420` continua a `setMuted(false)` |
| Fermo l'audio | `freeAllPhrases()` (`:234`), che gira **solo** su `! signalPresent` |

Il terzo caso è letteralmente il workaround dell'utente. Su nota tenuta o canto legato non
arriva alcun onset, quindi non esisteva altra via d'uscita.

**È il secondo difetto di questa famiglia.** Il primo, corretto in s.12, sta scritto in
`Phrase.h:37-45`: *"una frase superata da un nuovo onset con MENO voci della precedente — le
voci non riprese smettevano di essere processate, non solo mutate"*. Stessa forma: qualcuno
smette di essere richiesto e nessuno lo mette in mute.

### Il fix

Nuovo ramo nel loop di rendering, prima del controllo sulla cella, ricalcato sul ramo
`releasing` già presente nello stesso file: `setMuted(true)` (dissolvenza `kDeclickMs`,
anti-click di s.12/13), `processAdd` finché non è silenziosa, e solo a silenzio
**confermato** `goCold()` + `slotIndices[v] = -1`.

Scelta dell'utente fra restituire lo slot e tenerlo caldo: **restituirlo**. `isSlotInUse()`
scansiona `slotIndices`, quindi `= -1` lo libera davvero — abbassare le voci abbassa davvero
la CPU, coerente col budget ≤15% del PRD §1.3. Tenerlo caldo sarebbe costato quasi una voce
piena (rapporto `processWarmOnly`/`processAdd` = 0.987, misurato in B-06).

Una scelta di progetto vale la pena di essere ricordata: il ramo si applica a **tutte** le
frasi attive non in rilascio, non solo a quella `isLive`. Il knob significa "quante voci
sento", e limitarlo alla frase viva avrebbe riprodotto una versione attenuata dello stesso
reclamo con Keep Tails ON. La tensione con FR-46 (voicing congelato al trigger) è annotata e
non risolta: senza il pattern ritmico tutte le voci di una frase entrano insieme, e
`Phrase.h:30-36` osserva già che oggi i due casi collassano.

### `phrase_scheduler_test` — il primo test su questo modulo

`PhraseScheduler` era l'**unico modulo del progetto senza alcun test**, e non per svista: la
barriera era già registrata in `empty_cell_hold_test.cpp:12-14` (dipende da `juce_core`,
non linkabile nel livello JUCE-free). Il prezzo di quella barriera è stato B-10.

Scelta dell'utente fra tre opzioni: **nuova suite che linka `juce_core`** — registrata come
**D-16**, che emenda D-11 senza superarla. Scartato togliere `juce_core` dal percorso
(`SpinLock` → `std::atomic_flag`): tocca lo schema di swap realtime-safe di Stability, che
oggi funziona, e modificare codice in `processBlock` per rendere testabile un altro modulo è
il verso sbagliato.

**Il test è stato scritto ed eseguito PRIMA del fix**, per vederlo fallire — un test che
passa anche su codice rotto non è un test:

| | Pre-fix | Post-fix |
|---|---|---|
| P-1 voci attive, selettore 4 → 2 → 1 | **4 / 4 / 4** | **4 / 2 / 1** |
| P-2 RMS dopo 4→2, scarto dal riferimento a 2 voci | +16.9% | **−3.1%** |
| P-2 stesso RMS, scarto dal riferimento a 4 voci | −1.8% | **−18.7%** |
| P-3 risalita 1→4 (regressione) | OK | OK, −3.7% dal riferimento |
| P-4 salto d'ampiezza al passaggio / a regime | 0.86 | **0.86** |

Due dettagli di metodo: P-2 usa un criterio **comparativo** (deve somigliare al riferimento
a 2 voci molto più che a quello a 4) invece di una soglia assoluta, perché il rapporto fra
4 voci e 2 voci è solo 1.19 — le voci sommano in modo incoerente e quelle in più stanno su
offset alti con meno energia. E la suite verifica per prima cosa che i tre riferimenti (1, 2,
4 voci) siano **monotoni e separati**: se non lo fossero, P-2 "passerebbe" per un motivo
sbagliato. P-4 riusa la stessa convenzione di `voice_test` T-5/H1/H4.

Trovato scrivendo il test e corretto: un `printf` di P-3 aveva un `%d` in meno degli
argomenti passati, quindi stampava un intero attraverso `%.5f`.

### Verifiche

`ctest -C Release`: **8/8 verdi** · build Release senza `error C####`/`error LNK` (exit 1
solo da `MSB3073`, D-12) · `pluginval --strictness-level 10` su VST3 Release: **SUCCESS**.

### Gemello lasciato aperto — B-11

`setVoiceCap` / `maxSimultaneousVoices` (FR-51) ha lo stesso difetto a `:144`: è anch'esso
solo allocativo, abbassare il tetto non spegne gli slot già assegnati. Oggi non morde
(default 32 su 32). **Non riparato insieme**, e non per fretta: `numVoices` è per-frase e ha
un ordine naturale (le colonne oltre l'N-esima), mentre il tetto FR-51 è **globale fra
frasi** — spegnere l'eccesso richiede di decidere quale frase perde slot. È una decisione di
design, non un fix meccanico.

## 8. B-12 — il click in aggiunta, e una lezione sulle misure

L'utente conferma B-10 all'ascolto (*"ora posso togliere o aumentare voci in live"*, discesa
fluida) e nella stessa frase apre un sintomo nuovo: *"se ne aggiungo sento un piccolo click
ogni volta che aggiungo una nuova voce"*.

### Prima domanda: l'ho introdotto io?

Risolta **per costruzione, non ragionando**. `P-5` misura due scenari: una salita pura da una
frase nata con 1 voce — dove il ramo aggiunto per B-10 non scatta mai, perché le colonne
oltre il selettore non hanno slot ed escono su `slotIndex < 0` — e una discesa seguita da
risalita, con slot riciclati. Il difetto c'era in entrambi: **preesistente**.

### Due misure inadatte, non sbagliate

`P-5` (e `P-4`) usano `maxJump` **sul mix**. Non hanno visto niente: rapporti 1.06–1.80,
sotto la soglia di 3.0. Ma il rapporto **cresceva col numero di voci**, il che ha fatto
sospettare che stesse misurando "più voci = segnale più grande" e non un artefatto. Due
ragioni per cui quella metrica non poteva funzionare: il salto della singola voce che entra è
diluito fra le 2-3 già in suono, e il riferimento "a regime" viene preso a 1 voce mentre i
confini stanno a 2, 3, 4.

Stessa storia un livello più in là: `H1` in `voice_test.cpp` aveva concluso "salita liscia,
solo ritardata" — ma misurava il **rapporto di slew**, non il **tempo di salita**. Nessuna
delle due misure era sbagliata: erano inadatte a questa domanda. È regola 13 letta al
contrario — non "il test è sbagliato", ma "il test sta misurando un'altra cosa".

### La misura giusta

`P-6` isola il contributo della **sola voce aggiunta** per differenza fra due rig identici,
uno che aggiunge la voce e uno che non la aggiunge: tutto il resto è deterministico e si
cancella. Sul residuo si misura la forma dell'attacco, che è la grandezza giusta:

- **ritardo prima che parta: 20.09 ms** — la latenza dichiarata del motore, freddo al binding.
  Lo stesso numero di B-04.
- **salita 10%→90%: 3.56 ms** contro gli **8 ms** di `kDeclickMs`.

Il meccanismo era quindi già noto al progetto, su un evento diverso: la dissolvenza
anti-click parte al `setMuted(false)` e si esaurisce **dentro** il silenzio del motore, così
quando il segnale vero arriva la voce è già a guadagno pieno. È la frase di B-04/s.27,
*"`ampGlide` consumato dentro il buco"*, applicata all'aggiunta di una voce invece che alla
cella vuota.

### Il fix, e il suo confine

`Phrase::warmupSamples[v]`: al **late-binding** si imposta alla latenza dello slot; finché è
> 0 la voce resta muta e viene alimentata con `processWarmOnly` (stesso meccanismo di s.27).
A zero, il blocco successivo trova il motore pronto e gli 8 ms di dissolvenza coincidono con
segnale vero. Misurato: **salita da 3.56 a 6.92 ms** — una rampa lineare di 8 ms misurata
10%→90% dà 6.4 ms teorici, quindi la dissolvenza si spende ormai quasi tutta su segnale vero.
Il ritardo passa da 20.09 a 24.67 ms, un blocco in più, impercettibile su una manopola.

**Il confine è la parte importante**: il riscaldamento **non** viene esteso a
`triggerNewPhrase()`, cioè alle voci che nascono con una frase nuova. Quello è l'attacco di
nota, un evento diverso e già validato all'ascolto (B-04 chiuso, residuo di B-02 accettato).
Estendervelo cambierebbe il suono di **ogni nota** — blast radius sproporzionato rispetto al
sintomo riportato. Se un giorno lo si volesse, la misura per deciderlo è già scritta.

## 9. Cosa non è stato fatto

**Confermato all'ascolto e committato** a fine sessione, su richiesta esplicita: *"ora
funziona tutto come dovrebbe"*. B-07, B-10 e B-12 passano a `CHIUSO`. Tre commit —
`ab79ae7` (FR-42), `5c985e3` (FR-19, che porta B-10 e B-12 perché i due fix vivono negli
stessi file e non erano separabili senza staging interattivo) e il commit di documentazione.
Resta non verificato singolarmente il comportamento con **Keep Tails ON**: la conferma è
stata generale, non su quella configurazione. Fuori scope per scelta: FR-59
(scala 70–200%), la guida utente, A-06 (CI/`ctest` — quindi **tutto il lavoro di s.30 nasce
fuori dalla CI**, e `phrase_scheduler` non è nemmeno compilabile lì), A-07 (preset di
fabbrica), FR-51/B-11, la taratura di `kFormantSpreadK`, il `Glide` sui parametri formantici.
