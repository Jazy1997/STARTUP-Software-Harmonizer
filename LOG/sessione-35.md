# Sessione 35 — 2026-08-12

B-15 chiuso all'ascolto. Provando il passaggio fra le due modalità sono usciti due sintomi
nuovi, **B-16** e **B-17**, entrambi diagnosticati, misurati e corretti in giornata. B-17 è poi
stato **chiuso** dal secondo ascolto dell'utente; B-16 aveva una **seconda causa**, trovata solo
grazie a quel secondo ascolto, e resta aperto in attesa del terzo (§7-§8).

> I §5 e §6 riportano i numeri e le conclusioni *del primo giro*, prima che la seconda causa
> fosse nota. Il §7 li aggiorna: si legga in ordine.

---

## 1. B-15 chiuso

L'utente ha ascoltato in Ableton, modalità Play, la build di fine s.34: *"non ci sono più
click"*. È la conferma che la regola 12 richiede, e chiude il sintomo — le misure PM-1..PM-7
descrivevano il difetto giusto.

Annotato in BUGS.md che la conferma riguarda **l'assenza di click**, non l'esito dei singoli
scenari della lista di prova. FR-28 (passaggio con tasti premuti) era in coda a quella lista
solo per opportunità, non perché fosse un sintomo di B-15, e infatti è diventato B-16.

---

## 2. I due sintomi nuovi

Riportati insieme dall'utente:

1. *"Se attivo Play Mode mentre sta suonando sento un click."*
2. *"Se dopo aver attivato Play Mode la disattivo, il plug-in non si riattiva più e non
   armonizza più secondo la matrice; per farlo funzionare ancora devo stoppare l'audio e farlo
   ripartire."*

Due sintomi, due ID (regola 14). Non sono lo stesso difetto: hanno cause diverse in righe
diverse, e si è visto bene alla fine, quando il fix dell'uno **non** ha spostato la misura
dell'altro.

### La diagnosi, prima di scrivere codice

**B-16.** `processBlock` leggeva il mix wet dell'una **o** dell'altra catena secondo
`playModeEnabled`. Entrambe sfumano correttamente quando escono di scena — `freeAllPhrases()`
→ `beginRelease()` di qua, `setMuted(true)` più la coda di `kDeclickMs` di là, con un commento
in `PlayModeInput.cpp` che cita FR-28 fin da s.12 — ma quella dissolvenza finiva in un buffer
che dal blocco del passaggio in poi non leggeva più nessuno.

**B-17.** `triggerNewPhrase()` ha **un solo call site**, dentro `else if
(onsetDetectedThisBlock)`. Attivando Play, la catena Harmonizer riceve `signalPresent = false`
e `freeAllPhrases()` libera tutto. Disattivandolo con la sorgente che suona ancora, il gate di
`OnsetDetector` è già aperto: nessun onset nuovo, nessuna frase, silenzio. L'unico modo di
generare un onset è chiudere e riaprire il gate — fermare l'audio e farlo ripartire, cioè
esattamente il workaround riferito dall'utente. La corrispondenza fra il workaround e il
meccanismo è ciò che ha reso questa diagnosi credibile prima di qualunque misura.

---

## 3. Il banco: `tests/mode_switch_test.cpp`

Il passaggio di modalità **non esiste** dentro `PhraseScheduler` né dentro `PlayModeInput`:
vive interamente in `HarmonizerAudioProcessor::processBlock`. Un banco che ne ricostruisse la
logica misurerebbe la ricostruzione, non il codice (D-09). Quindi si istanzia il **processore
intero** e si muove il vero parametro `playModeEnabled` a un confine di blocco, come farebbe
un host.

È il **terzo livello di D-16**. Costo di costruzione: tutte le sorgenti del plugin più
`juce_audio_utils`. Due attriti tecnici, entrambi piccoli:

- `PluginProcessor.cpp` usa `JucePlugin_Name`, che qui non esiste perché non c'è wrapper: si
  fornisce con una `target_compile_definitions`, invece di trascinarsi dietro
  `juce_audio_plugin_client`;
- `prepareToPlay` chiama `startTimer`, quindi serve un `ScopedJuceInitialiser_GUI` nel test.

Scenari: **A** senza tasti premuti (isola la catena Harmonizer), **B** con una nota Play
premuta e mai rilasciata — che è il caso "con tasti premuti" di FR-28 e l'unico in cui esiste
una coda Play da tagliare. Sorgente tenuta a 220 Hz che **non si interrompe mai**: è il cuore
della misura di B-17, perché senza interruzione non esiste nessun onset dopo il primo.

### La metrica sbagliata (regola 13)

Il primo giro usava PM-3 di s.34 — massimo salto campione-campione rapportato al regime — e,
**prima di qualunque fix**, ha risposto **1.08**: "nessun click". Nello stesso istante il wet
passava da pieno a zero esatto in un campione.

Non è una contraddizione: il taglio vale `|x[b-1]|`, un campione qualsiasi dell'onda, e il mix
wet dell'Harmonizer è granuloso alla cadenza dei grani PSOLA — i suoi salti naturali sono già
di quell'ordine. Il rapporto non distingue "il segnale è stato troncato" da "quel grano era un
po' più ripido".

Ciò che distingue un taglio da una dissolvenza non è il salto: è l'**inviluppo subito dopo**.
Una dissolvenza di `kDeclickMs` lascia una coda di energia decrescente; un taglio lascia zero
esatto. I cancelli sono stati riscritti sull'energia della coda **prima** di toccare il codice.

È lo stesso esito di PM-2 in s.34, e vale la pena notarlo: due sessioni di fila, la metrica
ovvia era quella sbagliata, ed entrambe le volte se n'è accorto il banco girato *prima* del
fix. Se avessi misurato dopo, 1.08 avrebbe detto "risolto" su un difetto intatto.

Il salto resta stampato come **diagnostica** (MS-4), non come cancello — e infatti nello
scenario B, dove la voce Play è pulita e non granulosa, era sensibile: 7.74 prima, 1.17 dopo.

---

## 4. I fix

**B-16** — `src/PluginProcessor.cpp`: i due mix wet si **sommano** invece di sceglierne uno.
Nessuna rampa nuova: la somma si limita a non buttare via quella che ogni catena applica già a
se stessa. Un crossfade esplicito sarebbe stato una seconda rampa sopra la prima, che avrebbe
accorciato la coda — cioè tagliato di nuovo la dissolvenza, solo più gentilmente. Verbalizzato
in **D-22**, incluso il rapporto con FR-24: l'esclusività resta vera in regime, la
sovrapposizione dura al più 8 ms.

**B-17** — `src/PluginProcessor.{h,cpp}`, due bool, nessuna allocazione. Il fronte di discesa
dell'interruttore arma il retrigger e imposta `onsetPendingForLatch`; il fronte di salita
spegne quest'ultimo, che altrimenti sopravvivrebbe alla parentesi senza più significare nulla
(difetto preesistente trovato per strada: un onset avvenuto durante Play restava appeso e
saltava la conferma dell'isteresi all'uscita). Il retrigger si consuma solo quando segnale,
pitch **e latch riagganciato** ci sono tutti: una frase creata prima nascerebbe sul grado
sbagliato. Nessun percorso nuovo dentro `PhraseScheduler`.

---

## 5. Numeri

| | prima | dopo |
|---|---|---|
| MS-1 coda Harmonizer, 8 ms dopo il toggle ON | 0.000 | **0.547** |
| MS-2 coda voce Play, 8 ms dopo il toggle OFF | 0.000 | **0.887** |
| MS-3 ritorno del wet dopo il toggle OFF | **mai** | **26.5 ms** |
| MS-4 salto campione-campione al toggle OFF (diagnostica) | 7.74 | 1.17 |
| MS-5 salto sul percorso dry | 1.00 | 1.00 |

0.547 contro lo 0.577 teorico di una dissolvenza lineare su tutta la finestra: la coda non è
solo presente, ha la forma giusta.

`ctest` **11/11**. `pluginval --strictness-level 10` **SUCCESS** su VST3/Win. Standalone
costruito. AU non verificabile su questa macchina (limite noto).

Nessuna riga del motore toccata: il difetto era interamente nel processore, quindi D-19/D-21
non sono nemmeno entrate in gioco. `voice`, `psola`, `phrase_scheduler` e `play_mode_input`
restano verdi — e la somma è per costruzione un no-op quando Play non è mai stato attivo,
perché il buffer Play è azzerato ogni blocco.

---

## 6. Cosa resta

L'**ascolto**, che è l'unica cosa che chiude B-16 e B-17. Due domande separate: il click al
passaggio è sparito? l'armonizzazione torna da sola senza fermare il transport?

Dopo, **A-06**: la CI compila 3 suite su 11 e non invoca mai `ctest`. Il divario è cresciuto
ancora oggi — `mode_switch` linka l'intero plugin, quindi è ancora meno compilabile a `g++`
nudo di `play_mode_input`.

---

## 7. Secondo giro d'ascolto — B-17 chiuso, B-16 aveva una seconda causa

L'utente ha provato la build: *"Ora si attiva di nuovo la matrice quando disattivo Play Mode.
Non c'è più il click quando attivo Play Mode ma c'è quando la disattivo e ricomincia a suonare
la matrice."*

**B-17 chiuso.** Il retrigger fa quello che doveva.

**B-16 no**, e la metà rimasta non era dove guardavano i cancelli. Nello stesso istante dello
spegnimento convivono due eventi: la coda della voce Play che se ne va (MS-2, già a posto) e
l'**attacco della matrice che rientra** — che non era misurato da nessuno.

### La seconda causa

`triggerNewPhrase()` faceva `warmupSamples.fill (0)`, con tanto di commento: *"attacco di nota:
nessun riscaldamento, vedi B-12"*. Per un onset vero l'assunzione è corretta — prima c'era
silenzio, un motore che parte freddo non si sente. Per il rientro da Play è falsa: la sorgente
sta già suonando a pieno livello e il motore dello slot appena preso è freddo, quindi gli 8 ms
di `ampGlide` si consumano nel suo silenzio e la voce entra di netto quando il motore comincia
a produrre.

È **B-12 alla lettera, mai portato su questo percorso** — la stessa forma della causa 1 di
B-15, che era B-12 mai portato su Play. Terza volta che quel meccanismo ricompare su una strada
nuova.

### La metrica che l'ha visto (e quella che no)

MS-6 — il salto d'ampiezza all'attacco, cioè PM-3 di s.34 — ha dato **0.95**: pulito. È MS-7,
il **tempo di salita**, ad aver nominato il difetto: **1.66 ms** invece di `kDeclickMs`.

È l'inverso esatto di s.34, dove PM-3 era il cancello buono e PM-2 quello cieco. Messe insieme
le due sessioni, la regola che se ne ricava non è "PM-3 sì, PM-2 no" ma: **la metrica giusta
dipende dalla forma del difetto**. Un troncamento sposta l'inviluppo di colpo ma non
necessariamente due campioni consecutivi; un attacco troppo rapido non fa gradini fra campioni
ma comprime la salita. Servono due lenti diverse, e questa sessione ha avuto bisogno di
entrambe nello stesso file.

### Il fix

Parametro `triggerNeedsWarmup` su `PhraseScheduler::process`, **default `false`**: il percorso
dell'onset vero non cambia di una riga. Quando è vero, le voci della frase nuova ricevono lo
stesso riscaldamento del late-binding di B-12. `PluginProcessor` lo passa esattamente quando
consuma il retrigger. Nessun meccanismo nuovo — un chiamante nuovo per una strada che c'era
già, che è la forma prescritta da D-21.

| | prima | dopo |
|---|---|---|
| MS-7 salita al rientro | 1.66 ms | **7.80 ms** |
| MS-6 salto sullo stesso attacco | 0.95 | 0.93 |
| MS-3 ritorno del wet | 26.5 ms | 31.3 ms |

I ~5 ms in più su MS-3 sono la latenza del riscaldamento, pagata apposta: il wet arriva più
tardi ma **sfumato**. Stesso baratto di B-15 (PM-1 0.70 → 24.4 ms).

`ctest` 11/11, `pluginval --strictness-level 10` SUCCESS su VST3/Win.

### Cosa resta

Il secondo ascolto di B-16, che è l'unica cosa che lo chiude.

---

## 8. Il verso simmetrico, e un cancello che accusava il codice sbagliato

Richiesta esplicita dell'utente: *"controlla e sistema che le misure anti-click siano
implementate sia in entrata che in uscita da Play Mode"*.

Mancava davvero uno scenario: il tasto **già premuto quando Play si accende**. Lì la voce Play
entra, ed è l'altra metà del passaggio; gli scenari A e B non la coprivano (in A non c'erano
tasti, in B il note-on arrivava a Play già acceso). Per costruzione doveva essere già coperto
dal fix di B-15 — in `PlayModeInput` il riscaldamento è *derivato dallo stato del motore*, non
registrato al note-on, quindi vale anche per una nota premuta da prima — ma "per costruzione"
non è una misura. Scenario C aggiunto.

**MS-8 ha fallito: 4.56 ms contro una soglia di 5.** E qui la sessione ha rischiato di
prendere la strada sbagliata: c'era un cancello rosso e una tentazione di scrivere un terzo
fix.

Invece si è misurato il **controllo**, nel senso di PM-7: la stessa voce Play che entra da un
note-on normale, cioè il percorso che l'utente aveva appena confermato all'ascolto chiudendo
B-15. Ha dato **4.69** — lo stesso numero. Il percorso del passaggio era sano; era la soglia a
essere sbagliata.

Due errori sommati nel fissarla, entrambi da "leggere la costante nel codice invece di misurare
un riferimento":

1. una rampa **lineare** di 8 ms attraversa 10%→90% in **6.4** ms, non in 8;
2. un inviluppo **RMS causale** accorcia ancora un po' la salita misurata.

MS-8 è diventata un **rapporto contro il controllo** — 0.97, soglia ≥ 0.70 — che è immune a
entrambe le distorsioni: qualunque cosa faccia lo stimatore, la fa uguale al numeratore e al
denominatore. Per coerenza è stata annotata anche MS-7: la sua soglia (5 ms) è tarata
sull'intervallo misurato fra rotto (1.66) e sano (7.80), non su `kDeclickMs`.

È la **seconda metà della regola 13**, quella che si cita meno: *"quando un test fallisce,
considera anche l'ipotesi che sia sbagliato il test"*. In una sessione in cui il primo giro
aveva già dovuto correggere una metrica cieca, questo è il caso opposto — una metrica troppo
severa — ed è costato un solo esperimento riconoscerlo.

### Copertura finale del passaggio

| verso | chi esce di scena | chi entra |
|---|---|---|
| Harmonizer → Play | coda Harmonizer — MS-1 = 0.547 | voce Play (tasto già premuto) — MS-8 = 0.97 del controllo |
| Play → Harmonizer | coda voce Play — MS-2 = 0.887 | matrice che rientra — MS-6 = 0.93, MS-7 = 7.80 ms |

Otto metriche, sei cancelli, tre scenari. `ctest` 11/11.

---

## 9. Chiusura

Terzo giro d'ascolto in Ableton: *"tutto corretto, ho verificato ascoltando su Ableton"*.
**B-16 CHIUSO.** Con lui si chiude anche **FR-28** `[MUST]`, che era fra i limiti noti come
"mai verificato all'ascolto" dal s.9 — nel verso migliore possibile, cioè verificato e
soddisfatto invece che semplicemente dimenticato.

Bilancio della sessione: tre entry chiuse all'ascolto (B-15, B-16, B-17), un requisito `[MUST]`
uscito dai limiti noti, una decisione nuova (D-22), un banco nuovo in `ctest` (11 suite), zero
entry aperte in coda di conferma.

Il conto degli ascolti: **tre giri** per B-16. Non è uno spreco, è il costo di un sintomo che
sembrava uno ed erano due, in due punti diversi della stessa riga di tempo. Ogni giro ha
prodotto una misura nuova che il giro prima non aveva: MS-1/MS-2 (la coda), MS-6/MS-7
(l'attacco), MS-8 (il verso simmetrico). Nessuna delle tre era prevedibile dalla precedente
senza l'orecchio dell'utente in mezzo — che è esattamente ciò che la regola 12 dice.

**Prossimo passo: A-06**, la CI allineata a `ctest`. Con tutti i sintomi chiusi, il rischio più
grosso è che 8 suite su 11 non le guardi nessuno automaticamente.
