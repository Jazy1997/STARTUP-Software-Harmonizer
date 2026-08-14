# DECISIONS — HARMONIZER

> Registro delle decisioni durature, stile ADR. **Append-only**: una entry per decisione,
> l'ID non si riusa. Una decisione superata non si cancella — si marca `SUPERATA da D-NN`
> e si scrive la nuova entry. Serve a non ridiscutere due volte la stessa cosa e a non
> reinventare quello che è già stato scartato con una ragione.
>
> Qui vanno le decisioni **con una conseguenza duratura**. Le scelte di implementazione
> ordinarie stanno nel codice e in `LOG/`.

---

## D-01 — Il tipo AU è Music Effect (`aumf`)

**Contesto** — In Audio Unit un plugin `aufx` (Effect) non può ricevere MIDI. Il prodotto
riceve MIDI su un bus dedicato (FR-29).

**Decisione** — `AU_MAIN_TYPE kAudioUnitType_MusicEffect`, deciso in M0.

**Conseguenze** — **Irreversibile dopo il rilascio**: cambiarlo rende irrecuperabili i
progetti già salvati dagli utenti. In Logic il plugin compare in una categoria diversa
dagli effetti audio ordinari, da documentare per l'utente.

**Stato** — Attiva. `CLAUDE.md` regola 7.

---

## D-02 — PSOLA proprietario come motore di default, dietro interfaccia astratta

**Contesto** — Il caso d'uso è monofonico con pitch già noto: il caso facile. Le librerie
spettrali sono progettate per materiale polifonico sconosciuto e pagano quella generalità
in latenza e CPU. Con 8 voci il vantaggio è decisivo perché tutte condividono la stessa
analisi di pitch. Zynaptiq ZTX LE/STUDIO non supportano il cambio dinamico dei parametri
di pitch — cioè esattamente ciò che un harmonizer fa a ogni nota. aubio è GPL.

**Decisione** — PSOLA proprietario come default, **dietro `PitchShifter` astratto** (FR-62).
`SpectralShifter` (Signalsmith Stretch, MIT) resta compilato e selezionabile con
`HARMONIZER_USE_SPECTRAL_SHIFTER`.

**Conseguenze** — Nessun modulo fuori da `src/dsp/` conosce l'implementazione concreta.
Passare a un motore commerciale costa una settimana, non una riscrittura.
Latenza Fast misurata **13.5 ms @48 kHz**, sotto il target ≤15 ms del PRD §1.3.
**Signalsmith non si rimuove** anche se inutilizzato: 979 KB per non perdere la via di fuga.

**Stato** — Attiva. `CLAUDE.md` regola 2.

---

## D-03 — Il valore CC di un preset è la sua posizione in lista

**Contesto** — Il controller dedicato ha un navigation button a 5 direzioni che seleziona
sempre i primi 5 preset della lista. L'utente riorganizza la lista per decidere cosa avere
sotto le dita.

**Decisione** — Posizione 1-based **è** il valore CC. Il riordino **rinumera** (FR-05/07).
Ogni preset ha comunque un UUID stabile (FR-10), ma non è quello a selezionarlo.

**Conseguenze** — Riordinare cambierebbe il significato delle automazioni già scritte →
risolto da D-04. Rischio "confonde gli utenti non hardware", mitigato mostrando sempre il
CC accanto al preset nella lista.

**Stato** — Attiva. `CLAUDE.md` regola 4.

---

## D-04 — La libreria di preset si serializza dentro lo stato del plugin

**Contesto** — Conseguenza diretta di D-03 (FR-08).

**Decisione** — La libreria vive nello stato del plugin, quindi nella sessione dell'host.
In parallelo esiste una libreria globale su disco (FR-09), usata per popolare le nuove
istanze, con caricamento e salvataggio **sempre espliciti, mai automatici**.

**Conseguenze** — Ogni progetto porta con sé la propria copia della libreria e del suo
ordine: riordinare oggi non altera i progetti salvati ieri.

**Stato** — Attiva. `CLAUDE.md` regola 5.

---

## D-05 — `0` e cella vuota sono stati distinti

**Contesto** — `0` significa unisono (utile nelle frasi ritmiche); vuoto significa voce
muta su quel grado. Confonderli rompe la semantica delle voci mute.

**Decisione** — `null` per la cella vuota nella serializzazione, **mai** `0`. Nel CSV la
cella vuota è la stringa vuota. `CellInputParser` rifiuta il testo spazzatura invece di
coercerlo a 0 — il contrario della trappola in cui cadeva `CsvIo`.

**Conseguenze** — `cell_input_parser_test` copre 19 verifiche su questa distinzione.

**Stato** — Attiva. `CLAUDE.md` regola 3.

---

## D-06 — Tre schermate con barra di navigazione sempre visibile

**Contesto** — Il PRD §8.1 letto alla lettera chiedeva una Main molto densa. Discutendo la
struttura reale, riferimento esplicito a **Portal di Output**: Main non è una versione
compressa di tutto, è un **sottoinsieme curato** — quello che si tocca mentre si suona.

**Decisione** — Main / Edit / Impostazioni, con navbar a 3 pulsanti visibile su **tutte e
tre** (FR-73). Pan e gain per voce si spostano su Edit; la lista preset vive **solo** su
Main. Cambiare schermata non ridimensiona la finestra.

**Conseguenze** — Supera deliberatamente la lettera di §8.1 ("Accesso alle altre due
schermate"), rendendola simmetrica. Coerente con FR-60. Beneficio collaterale: il pannello
piatto era alto 1428 px al minimo, oggi la finestra è 900×660.

**Stato** — Attiva. Documentata per esteso in `PRD-UI.md`.

---

## D-07 — `dryWetMix` sostituisce `dryLevel`/`wetLevel` nella lettura, non nella dichiarazione

**Contesto** — Due slider indipendenti possono stare entrambi al massimo (somma libera).
Un vero crossfade no. È un cambiamento del comportamento **sonoro**, non solo della UI.

**Decisione** — Nuovo `dryWetMix` (`AudioParameterFloat` 0..1, default 0.7), crossfade a
potenza costante `dry=cos(mix·π/2)`, `wet=sin(mix·π/2)` in `computeDryWetGains`.
`dryLevel` e `wetLevel` **restano dichiarati per sempre** ma smettono di essere letti in
`processBlock`.

**Conseguenze** — Un ID di parametro pubblicato non si rimuove mai (NFR-07). Il default 0.7
era dichiarato "punto di partenza da verificare", ed è stato **confermato all'ascolto** in
s.26.

**Stato** — Attiva. `CLAUDE.md` regola 6.

---

## D-08 — "Keep Tails" è un bottone utente, non un comportamento fisso

**Contesto** — FR-17 (cambio accordo su nota tenuta → ricalcolo immediato) e FR-46 (ogni
frase congela il proprio voicing al trigger) descrivono comportamenti diversi per casi
diversi. Il PRD segnalava la tensione come `[DECISION]` da risolvere all'ascolto entro M2.

**Decisione** — La prima risoluzione scritta in s.7 si è rivelata sbagliata all'ascolto
(bug di accumulo, s.10). Sostituita da un **controllo utente** invece che da una regola
fissa: `keepPhraseTails`.

**Conseguenze** — Chiude la `[DECISION]` del PRD §6.1. Il significato pieno del controllo
richiede però il Pattern Ritmico (FR-47..49, `[V1.1]`): oggi è binario.

**Stato** — Attiva.

---

## D-09 — Niente invariante F: al suo posto l'isteresi sulla cella vuota

**Contesto** — Il piano di s.28 prevedeva una "riserva calda" di `numRequestedVoices` slot
per spiegare il ribattuto. La **Fase 0 obbligatoria** (traccia col codice vero, non con una
ricostruzione a mano) ha smentito la premessa: nessun ri-attacco, nessun furto, nessuno slot
mai freddo per round-robin.

**Decisione** — Invariante F **non costruita** (non ha nulla da correggere su quel
materiale: uno slot in uso da una frase attiva è già incondizionatamente caldo). Debounce
del gate **non scritto**. Al loro posto, scelta dall'utente, l'isteresi cella vuota
(`EmptyCellHold.h`).

**Conseguenze** — Lezione di metodo: la Fase 0 ha risparmiato due interventi inutili su
codice real-time. **Verificare col codice vero prima di fidarsi di una ricostruzione in
sola lettura** — vale per la prossima indagine.

**Stato** — Attiva. Vedi `BUGS.md` § B-05.

---

## D-10 — Nessuna passata estetica finché la struttura non è ferma

**Contesto** — Chiesto esplicitamente dall'utente a fine s.21.

**Decisione** — Nessuna `LookAndFeel` custom. Forma dei knob, colori, font: lavoro non
iniziato e volutamente rimandato.

**Conseguenze** — Il PRD congela la feature list della UI a fine M4; l'estetica viene dopo.

**Stato** — Attiva.

---

## D-11 — Test scritti a mano, non Catch2

**Contesto** — Il PRD §9.1 indica Catch2 + pluginval.

**Decisione** — Le suite sono `int main()` con assert propri, senza dipendenze e senza JUCE.
Girano in pochi secondi su un runner minimo.

**Conseguenze** — **Discostamento dal PRD mai formalizzato prima di qui.** Il vantaggio è
che il job `dsp-tests` in CI fa da gate rapido prima delle build lunghe. Lo svantaggio è che
ogni suite nuova va aggiunta a mano in due posti (CMake e CI) — ed è **esattamente il motivo
per cui 4 delle 7 suite oggi non sono in CI**.

**Stato** — Attiva, ma da rivedere: vedi "Decisioni aperte".

---

## D-12 — `COPY_PLUGIN_AFTER_BUILD` resta attivo, e il suo fallimento è atteso

**Contesto** — Il post-build di JUCE copia in `C:\Program Files\Common Files\VST3`, che su
Windows richiede privilegi di amministratore. In s.4 il flag era stato **disattivato** per
aggirare il problema; è stato poi **rimesso a `TRUE`** (stato attuale, `CMakeLists.txt:31`).

**Decisione** — `COPY_PLUGIN_AFTER_BUILD TRUE`. Da una shell elevata la copia funziona e il
plugin finisce dove l'host lo cerca; da una shell normale il passo fallisce e **basta
puntare l'host a `build/Harmonizer_artefacts/<config>/VST3/Harmonizer.vst3`**.

**Conseguenze** — La build termina con **exit code 1** e una cascata di `MSB3073` su
`copyDir.cmake` a ogni compilazione non elevata. **È atteso, non è un errore di
compilazione**: l'artefatto è valido. Per distinguere un fallimento vero, filtrare
`error C####` / `error LNK` invece di fidarsi dell'exit code.

**Trappola correlata** — se la verifica riguarda la UI, ricompilare **anche la Release**:
Ableton può scansionare quella (s.22).

**Stato** — Attiva. *(Corregge una descrizione errata scritta in s.29, che riportava il flag
come disattivato basandosi su `LOG/archivio-s01-s28.md` sessione 4, superata.)*

---

## D-13 — Documenti divisi per ciclo di vita

**Contesto** — `handsoff.md` aveva raggiunto 2623 righe (276 KB) mescolando stato corrente,
storia, bug, decisioni e mappa dei file. Un file solo non può essere insieme stato e archivio.

**Decisione** (s.29) — `HANDOFF.md` (stato di oggi, max ~150 righe, riscritto ogni sessione)
· `BUGS.md` (una entry per sintomo, ID stabile, aggiornata in loco) · `DECISIONS.md`
(questo file, append) · `MAPPA.md` (moduli com'è adesso) · `LOG/` (racconto per esteso,
archivio).

**Conseguenze** — L'archivio s.01–s.28 è conservato **integro e verbatim** in
`LOG/archivio-s01-s28.md`: le sessioni erano sparse su 5 sezioni, fuori ordine e con
"(continuazione)" spezzate, quindi uno split automatico per sessione avrebbe perso o
misattribuito contenuto. I file per-sessione partono dalla 29.
Ciclo di vita codificato in `CLAUDE.md` regola 14.

**Stato** — Attiva.

---

## D-14 — Cosa non si elimina dal repository

**Contesto** (s.29) — Richiesta di eliminare "tutto quello che non concorre alla
compilazione della build per Ableton".

**Decisione** — Eliminati gli **output rigenerabili**: `SAMPLE TEST/scratch/` (130 WAV
intermedi, 84 MB) e `tools/pluginval_Windows.zip`. **Non** eliminati:

| Cosa | Perché |
|---|---|
| `tests/` | 284 KB. Cancellarlo non libera nulla e toglie l'unico sostituto dell'ascolto (`CLAUDE.md` regola 12) |
| Target AU e Standalone | Regola 11 (lo standalone è lo strumento di iterazione sul DSP) e regola 7 (l'AU è irreversibile). Non pesano sulla build VST3 |
| `SpectralShifter` + Signalsmith | 979 KB, è la via di fuga di FR-62 — vedi D-02 |
| `SAMPLE TEST/Test 1`, `Test 2`, `DBG Timbro/` | Sono gli **input** delle sonde, non output |
| `libs/` | 208 MB di submodule necessari a compilare |

**Conseguenze** — Il peso vero del repository è `build/` (2 GB), rigenerabile e già
gitignored. Un futuro "alleggerimento" parta da lì, non da `tests/`.

**Stato** — Attiva.

---

## D-15 — I buchi all'attacco si aggirano per documentazione, non si inseguono

**Contesto** (s.30) — B-05 ("ribattuto", ~15 ms di silenzio digitale) è aperto da s.28 dopo
quattro riproduzioni offline che **non lo riproducono**, e la sua prossima azione richiedeva
un bounce offline che l'utente non ha fatto. B-06 (slot freddo alla primissima nota) è
annotato da s.27 e mai affrontato. Entrambi bloccavano il fronte DSP.

**Decisione** — L'utente sospende entrambi. La condizione d'uso diventa una **regola
documentale**: *"perché il plugin funzioni bene vanno compilate tutte le celle delle voci
che si vogliono attivare"*, da scrivere nella guida utente. Nessun ulteriore lavoro di
indagine sul DSP finché il sintomo non si ripresenta.

**Conseguenze**
- B-05 e B-06 passano a `SOSPESO`, non a `CHIUSO`: non c'è un fix e non c'è una conferma
  all'ascolto (`CLAUDE.md` regola 12). Le entry conservano tutte le misure.
- **La mitigazione non combacia con le misure di B-05**, ed è annotato dentro l'entry:
  nell'export su cui il sintomo è stato misurato nessuna cella vuota veniva attraversata.
  "Compilare tutte le celle" è il rimedio di B-04 e della famiglia cella-vuota. Se il
  ribattuto tornasse su un preset a celle tutte piene, l'aggiramento non regge.
- **Genera un obbligo nuovo**: la guida utente **non esiste** (nessun README utente, nessuna
  `docs/`, nessun tooltip in-app). Diventa un deliverable, ed è in tensione col criterio
  d'uscita di M5 (PRD §12: *"un tester esterno usa il plugin senza documentazione"*) — se
  la regola serve, andrebbe resa evidente anche dalla UI, non solo scritta.
- L'ipotesi "underrun real-time del DAW" resta **non verificata**: il bounce offline che
  l'avrebbe decisa in un colpo solo non è stato fatto, e resta la via più economica se il
  sintomo torna.

**Stato** — Attiva. Vedi `BUGS.md` § B-05 e § B-06.

---

## D-16 — Secondo livello di test, che può linkare JUCE (emenda D-11)

**Contesto** (s.30) — B-10 ha reso evidente il costo di D-11. `PhraseScheduler` era l'**unico
modulo del progetto senza alcun test**, non per svista ma per una barriera tecnica: dipende
da `juce_core` (`juce::SpinLock` in `VoicePool.h`, `juce::jmin`, `juce::Uuid`/`String` via
`HarmonyPreset.h`), quindi non entrava nel livello "nessuna dipendenza, runner minimo".
La barriera era già registrata in `tests/empty_cell_hold_test.cpp:12-14`, che dichiara
l'integrazione con `PhraseScheduler` "verificata a lettura" perché non linkabile.
Il prezzo: B-10 è il **secondo** difetto della stessa famiglia arrivato fino all'ascolto
dopo quello di s.12 documentato in `Phrase.h:37-45`.

**Decisione** — Le suite di test si dividono in due livelli:
1. **JUCE-free** (le 7 esistenti): compilabili con un `g++` nudo, sono il gate rapido della CI.
2. **JUCE-linked** (`phrase_scheduler_test`, la prima): linkano `juce::juce_core` via CMake,
   girano in `ctest` insieme alle altre, **non** nel gate a `g++`.

Un modulo va nel livello 2 **solo** se la sua dipendenza da JUCE è reale e non rimovibile a
costo ragionevole. L'alternativa scartata era togliere `juce_core` dal percorso
(`SpinLock` → `std::atomic_flag`, `jmin` → `std::min`): rifiutata perché tocca lo schema di
swap realtime-safe di Stability (FR-54/56), che oggi funziona ed è provato — modificare
codice in `processBlock` per rendere testabile un altro modulo è il verso sbagliato.

**Conseguenze**
- `ctest` passa da 7 a **8** suite; il tempo totale resta sotto i 2 secondi.
- **A-06 diventa più urgente**: le suite fuori dal gate della CI passano da 4 su 7 a **5 su
  8**, e la nuova non sarebbe comunque compilabile con i tre `g++` a mano. Il passaggio a
  `ctest` in CI (già la sostanza di A-06) è ora l'unico modo di coprirla.
- D-11 **non è superata**: il livello 1 resta il default e resta il gate. Questa entry la
  emenda, non la sostituisce.

**Stato** — Attiva. Vedi `BUGS.md` § B-10 e A-06 fra le decisioni aperte.

---

## D-17 — L'aggancio va al grado d'arrivo, mai per gradi intermedi; le soglie si contano in millisecondi

**Contesto** (s.31) — La nota agganciata da `PitchLatch` **indicizza la tabella armonica**:
ogni valore che quella classe restituisce è una colonna del preset che il motore suona
davvero. Fino a s.30 l'aggancio si spostava di **un semitono per chiamata**, ed è chiamata una
volta per blocco: un salto C→E veniva servito attraversando C#, D, D#, un blocco ciascuno, e
ogni passo applicava sul serio l'offset di un grado che il performer non aveva eseguito.
Misurato in s.31 sul materiale dell'utente: **10 corse di offset invece di 4**, 116 ms di
offset spuri a 1024 campioni, e sull'export reale il wet a 262.8 Hz (la cella b2, −2) dove il
riferimento stava a 196. Vedi `BUGS.md` § B-13.

**Decisione** — Due regole, che valgono da qui in avanti per qualunque isteresi della catena
di controllo:

1. **L'aggancio non passa mai per un valore intermedio.** Va dove il pitch è realmente
   arrivato, o resta dov'era. Un'isteresi che *attraversa* i valori intermedi non è
   un'isteresi: è un generatore di eventi che nessuno ha chiesto. Il rimbalzo che il passo
   incrementale evitava si evita meglio adottando **esattamente** l'arrotondamento della
   stima, che per costruzione non produce overshoot.
2. **Le soglie di attesa si esprimono in millisecondi, contati sui campioni del blocco, mai
   in numero di chiamate.** Contarle in chiamate lega il comportamento al buffer size
   dell'host: lo stesso difetto durava 20 ms a 128 campioni e 464 ms a 4096. Vale già per
   `EmptyCellHold` (s.28) e ora per `PitchLatch`; vale per qualunque soglia futura.

**Conseguenze**
- `PitchLatch::update()` prende `numSamples` e la classe ha un `prepare(sampleRate)`.
  `kNoteSettleMs = 25` è scelto più lungo dei **14.5 ms** di stima confidente ma sbagliata
  misurati a un cambio di nota reale, con margine. **Non è tarato all'ascolto.**
- `tests/pitch_latch_test.cpp` TEST 7 è stato **riscritto**: asseriva *"esattamente 5 passi da
  60 a 65, un semitono a chiamata"*, cioè metteva per iscritto il difetto. Sostituzione
  deliberata e motivata (`CLAUDE.md` regola 13), non un adeguamento silenzioso al nuovo codice.
- **Qualifica la mitigazione di D-15.** La regola utente *"vanno compilate tutte le celle delle
  voci che si vogliono attivare"* è **controproducente** su questo sintomo: una cella
  intermedia compilata con un valore diverso dalla destinazione veniva suonata, mentre una
  cella vuota era già protetta da `EmptyCellHold`. D-15 non è superata (riguarda i buchi di
  ampiezza di B-05/B-06), ma la sua regola **non va scritta nella guida utente in quella
  forma**: prima serve capire se, corretto B-13, serva ancora.
- Ciò che questa decisione **non** tocca: la quantizzazione al blocco dell'istante in cui il
  cambio atterra (A-05) e il motore PSOLA, escluso su richiesta esplicita dell'utente
  (*"sono contento del timbro ora"*). `psola` e `voice` restano verdi identiche.

**Stato** — Attiva. B-13 **confermato all'ascolto** in s.32.

---

## D-18 — La gamma d'analisi la sceglie l'utente, e si serializza la nota (non la posizione)

**Contesto** (s.32) — Chiuso B-13, resta che l'offset giusto arriva **~85 ms dopo l'attacco**:
la nota parte con l'offset di quella precedente. Il termine dominante non è nostro — Cycfi Q
ricava la finestra d'analisi dalla frequenza minima che gli passiamo, e i **60 Hz** cablati
fino a s.31 danno una finestra di 33.4 ms e una stima ogni 16.7 ms. 60 Hz è **B1**: molto più
in basso di quanto serva a voce/sax/tromba (FR-14), e più in basso del **Trombone (Ab1 =
51.9 Hz)**, che quindi nelle note gravi non veniva rilevato affatto.

**Decisione**

1. **La nota più grave d'analisi diventa un parametro utente**, presentato come scelta dello
   strumento fra 10 voci (`src/ui/InstrumentRanges.h`) ordinate **dal più acuto al più grave**:
   l'ordine è esso stesso l'informazione, scendendo cresce il ritardo. Note fornite
   dall'utente, non da una tabella generica.
2. **Si serializza la NOTA MIDI, non la posizione in lista** (`analysisLowestNote`,
   `AudioParameterInt` 28..72). È l'opposto di **D-03**, dove il CC *è* la posizione, ed è
   deliberato: la lista è ordinata per altezza, quindi uno strumento nuovo va **inserito** in
   mezzo — con un `Choice` posizionale quell'ordine resterebbe congelato per sempre (regola 6).
   Prezzo: due strumenti che condividono la nota (Voice Female e Bb Trumpet, entrambi E3) non
   si distinguono nello stato salvato, e la UI evidenzia il primo. Accettato: per il suono
   conta la nota, non l'etichetta.
3. **Le attese della catena di controllo si esprimono in frame d'analisi del rilevatore**, non
   in millisecondi fissi (estende D-17, che le voleva in ms invece che in blocchi). Il
   transiente da cui ci si difende vive sulla scala del rilevatore: `kSettleFrames = 1.5`
   riproduce a 60 Hz esattamente i 25 ms di s.31 e scala da solo con lo strumento scelto.
4. **Il cambio si applica subito, non allo stop del transport.** Ricostruire il rilevatore
   alloca, quindi si riusa lo schema di Stability (costruzione sul message thread → scambio di
   puntatore in `processBlock` → distruzione in `collectGarbage`), ma **senza**
   `canApplyStabilityChangeNow()`: qui la latenza dichiarata non cambia, FR-56 non si applica.
5. **Default: E2 (Voice Male, 82.4 Hz), scelto misurando.** L'utente aveva indicato Bb Tenor
   Sax (Ab2); la misura ha mostrato che Ab2 riapre in piccolo B-13, e l'utente ha scelto E2
   alla luce del dato.

**Conseguenze**
- Ritardo del cambio d'offset, su materiale reale: **79.1 → 44.3 ms** a 1024 campioni,
  93.6 → 55.9 a 128. Zero corse di passaggio su 24 configurazioni (4 tabelle × 6 block size).
- **Il compromesso ha due facce**, ed è la scoperta che ha cambiato il default: una finestra
  corta è più pronta ma anche più **rumorosa**, e su un preset con tutti i gradi compilati ogni
  sbandata del rilevatore diventa un offset sbagliato udibile. Sopra Eb Alto Sax compaiono 1-3
  corse spurie, e **nessuna taratura dell'attesa le elimina** (provato fino a 25 ms assoluti).
  Le voci acute restano in lista per scelta dell'utente, con il limite annotato: la misura
  viene da un solo file che suona C4-D4-E4, cioè il registro grave per quegli strumenti.
- **Corretto un difetto di D-17**: l'attesa contava i campioni del *blocco*, quindi con un
  blocco più lungo dell'attesa una stima vista una sola volta si vedeva accreditare un blocco
  intero e l'attesa era un no-op. Ora l'aggancio si aggiorna a ogni stima nuova del rilevatore.
- Parametri APVTS: **45 → 46**.
- **Il lookahead resta fuori** (ritardare l'audio e dichiarare la latenza): è l'unica strada che
  azzererebbe il ritardo, ma contrasta con PRD §1.3 (≤ 15 ms nel modo più reattivo) e con
  *"progettato per il palco"*. Rimandato per scelta dell'utente, da aprire come decisione a sé.

**Stato** — Attiva. B-14 **confermato all'ascolto** in s.32.

---

## D-19 — Il motore di pitch shifting è chiuso

**Contesto** (s.32) — Con B-14 confermato all'ascolto si chiude la catena che ha occupato le
sessioni 12→32: timbro a nota tenuta (B-02, s.26), buco a inizio nota su preset a gradi
parziali (B-04, s.27), attacco sporcato dai gradi intermedi (B-13, s.31), offset in ritardo
sull'attacco (B-14, s.32).

**Decisione** — L'utente dichiara **chiuso il fronte del pitch shifting**: *"adesso possiamo
definire chiuso il motore audio per il pitch shifting. Il timbro è corretto e gli attacchi
pure"*.

**Conseguenze**
- `PsolaShifter`, `Voice` e la catena di sintesi non si toccano più senza un sintomo nuovo
  **riportato all'ascolto**. Un'ottimizzazione o una rifinitura che non nasca da un difetto
  udito non è motivo sufficiente: la stabilità raggiunta vale più del guadagno teorico.
- Le suite `psola` e `voice` diventano la rete di regressione di questa chiusura: devono
  restare verdi **identiche**, ed è il modo di dimostrare per calcolo che il timbro è intatto
  quando si lavora a monte (come in s.31 e s.32).
- **Non chiude i sintomi che restano aperti altrove**: B-03 (click residui occasionali, mai
  riascoltato nella configurazione di s.12/13) e B-11 (tetto voci) non sono coperti da questa
  decisione, e nemmeno B-05/B-06 che restano `SOSPESO` per D-15.
- Le voci acute della lista strumenti (D-18) restano **non verificate nel loro registro**:
  è l'unico lavoro di misura ancora dovuto su questo fronte, e serve materiale nuovo.
- Il fronte si sposta su **M5/UI**: FR-59 è l'ultima voce in PRD §12.

**Stato** — Attiva.

---

## D-20 — La finestra si scala, non si ridispone: un layout logico fisso e un solo transform

**Contesto** (s.33) — FR-59 `[MUST]` chiede *"ridimensionamento 70%–200% con resa corretta su
display HiDPI e Retina"*. Il codice aveva il **reflow** (la finestra era ridimensionabile dalla
s.25) ma non la **scala**: tutte le costanti di layout in pixel, nessun `AffineTransform`,
dimensione mai serializzata.

**Decisione** — La forma del layout non cambia mai. Le tre schermate vengono disposte sempre su
uno spazio logico di **900×660**, e un solo `AffineTransform::scale` porta quello spazio alla
dimensione fisica richiesta. Il rapporto d'aspetto della finestra è **bloccato**: trascinare
l'angolo non fa reflow, cambia la scala.

Scartata l'alternativa "due gradi di libertà separati" (scala da menu **più** reflow libero
dentro la dimensione logica risultante): avrebbe richiesto, nello stesso lavoro, un viewport
verticale o un'altezza logica minima per non riprodurre il difetto Keep Tails.

**Il transform sta su un COMPONENTE FIGLIO, non sull'`AudioProcessorEditor`.** Non è una
preferenza di stile, è l'unica via che JUCE consente —
`juce_AudioProcessorEditor.cpp:193` contiene

```
jassert (getTransform() == hostScaleTransform);
// "applying your own transform will obliterate it! ... consider putting the component
//  you want to transform in a child of the editor and transform that instead"
```

e `setScaleFactor()` (`:227`) **sovrascrive** il transform dell'editor con quello dell'host. Un
transform nostro sull'editor sarebbe un assert in Debug e verrebbe cancellato alla prima
notifica di DPI. Tenuti separati, i due si **compongono**: l'host scala l'editor (HiDPI), noi
scaliamo il contenuto. FR-59 chiede scala e HiDPI insieme, e così un solo meccanismo li dà
entrambi.

**La percentuale non è un valore memorizzato**: la fonte di verità è la larghezza attuale della
finestra, e `resized()` la ricava da lì. Menu e trascinamento sono quindi lo stesso percorso e
non possono divergere.

**Si serializza nello stato del plugin, non nell'APVTS** — è una preferenza di visualizzazione,
non un valore musicale: automatizzare "quanto è grande la finestra" non vorrebbe dire niente.
Nodo `UiSettings` accanto a `MidiCcSettings`, che è il precedente esatto (configurazione da
salvare ma non da automatizzare). Nessun ID di parametro nuovo, quindi la regola 6 non è in
gioco.

**Conseguenze**
- Le costanti di layout in pixel (`rowHeight = 26`, `labelWidth = 60`, l'altezza della tabella
  preset, i corpi dei font) **restano com'erano e vanno lasciate lì**: ora vivono in uno spazio
  che non cambia mai dimensione. Chi in futuro fosse tentato di moltiplicarle per un fattore
  sta rifacendo a mano ciò che il transform fa già.
- **Il difetto Keep Tails sparisce come effetto collaterale.** Con l'altezza logica fissa a 660,
  `layoutEdit()` dispone di 562 px e ne chiede 528: **34 px di margine** a qualunque scala.
  Prima, all'altezza minima di 620 px, i disponibili erano 522 contro 528 richiesti — deficit di
  6 px, ed è per questo che `keepTailsToggle` collassava (s.30). *Verificato per calcolo: la
  stessa aritmetica riproduce esattamente i 522/528 documentati allora.*
- Al 100% **nulla si muove di un pixel** rispetto a prima: 900×660 era già la dimensione
  d'apertura.
- Le sessioni salvate prima della s.33 non hanno il nodo `UiSettings` e aprono al 100%.

**Non deciso qui** — a 200% la finestra è 1800×1320 fisici, che non entra in un display 1080p.
È la lettura letterale di FR-59 ed è lasciata così; limitare il massimo allo schermo sarebbe una
decisione a sé.

**Stato** — Attiva.

---


## D-21 — Come si tocca il motore chiuso: overload nuovo, mai modifica al percorso esistente

**Contesto** (s.34) — B-15: click a ogni attacco in modalità Play, riportato all'ascolto
dall'utente. È esattamente la condizione che **D-19** pone per riaprire il motore (*"non si
toccano più senza un sintomo nuovo riportato all'ascolto"*), quindi D-19 non è stata violata —
ma la condizione, da sola, non dice **quanto** si può toccare, e la risposta sbagliata a
quella domanda rimetterebbe in gioco tutto il lavoro delle sessioni 12→32.

Il lavoro è stato pianificato con un **punto di arresto esplicito**: solo `PlayModeInput`, e se
la misura avesse mostrato che serve `Voice`, fermarsi e riportare i numeri invece di decidere
da soli. La misura lo ha mostrato (il riscaldamento senza il nuovo rapporto di trasposizione
non toglieva il click), ci si è fermati, e l'utente ha autorizzato la modifica minima.

**Decisione** — Quando un sintomo nuovo obbliga a toccare `Voice`, `PsolaShifter` o la catena
di sintesi, la forma della modifica è vincolata:

1. **Il percorso esistente non cambia.** Si aggiunge un metodo o un overload nuovo, usato solo
   dal chiamante che ha il difetto. Qui: `processWarmOnly` a 4 argomenti per `PlayModeInput`;
   la versione a 3 argomenti resta identica e continua a servire `PhraseScheduler`.
2. **Il codice condiviso si ottiene per estrazione pura**, mai riscrivendolo. Qui: `runShifter`
   estratto dal corpo di `processAdd`, stessa matematica, stesse righe.
3. **L'identità si dimostra, non si argomenta.** Le uscite di `voice`, `psola` e
   `phrase_scheduler` si salvano **prima** di cominciare e devono risultare **bit-identiche**
   dopo. In s.34 lo sono state, e questo ha coperto anche un dettaglio che il ragionamento da
   solo avrebbe lasciato in dubbio: l'estrazione ha spostato `ampGlide.processRamp` prima
   dell'aggancio di `justReactivated`, che nell'originale veniva dopo.

**Perché non basta "stare attenti"** — la lezione di s.14 (fix plausibile, meccanismo reale,
smentito all'ascolto) e di D-09 (*"verificare col codice vero prima di fidarsi di una
ricostruzione in sola lettura"*) valgono a maggior ragione su un motore che l'utente ha già
approvato: qui l'errore non si paga con un fix inutile, si paga con una regressione su
qualcosa che funzionava.

**Non copre** — un cambiamento che debba per forza alterare il percorso condiviso (per esempio
portare lo stesso miglioramento ai rami di riscaldamento di `PhraseScheduler`, vedi il limite
noto in fondo a B-15). Quello non è una modifica minima: va misurato sul percorso Harmonizer
per conto proprio e deciso con l'utente, non fatto di rimbalzo.

**Stato** — Attiva. Emenda D-19 senza superarla: D-19 dice *quando*, questa dice *come*.

---

## D-22 — I due mix wet si sommano, non si sceglie

**Contesto** (s.35) — B-16: attivando Play mentre la sorgente suona si sente un click.
`processBlock` leggeva il mix wet **dell'una o dell'altra** catena secondo `playModeEnabled`.
Entrambe, quando escono di scena, sfumano già per conto proprio — `freeAllPhrases()` →
`beginRelease()` da un lato, `setMuted(true)` più la coda di `kDeclickMs` dall'altro — ma
quella dissolvenza finiva in un buffer che dal blocco del passaggio in poi non leggeva più
nessuno. Il wet andava a zero in **un campione**. Misurato prima del fix: coda 0.000 in
entrambe le direzioni (MS-1/MS-2 di `tests/mode_switch_test.cpp`).

**Decisione** — I due buffer si **sommano** sempre, in ogni modalità. Nessuna scelta, nessun
crossfade esplicito.

**Perché non un crossfade** — sarebbe una seconda rampa sopra quella che ogni catena già
applica a se stessa: la coda uscente verrebbe attenuata due volte e quindi accorciata, cioè si
tornerebbe a tagliare la dissolvenza, solo più gentilmente. La somma non aggiunge nessuna
rampa: si limita a non buttare via quella che c'è.

**Rapporto con FR-24** (*"le due modalità sono mutuamente esclusive"*) — resta soddisfatta.
L'esclusività vale **in regime**, ed è garantita a monte: mentre Play è attivo la catena
Harmonizer riceve `signalPresent = false` e non genera nulla di nuovo; mentre Play è spento
`PlayModeInput` mette in muto tutti gli slot. La sovrapposizione udibile esiste solo durante la
dissolvenza e dura al più `kDeclickMs` = 8 ms. È una deviazione dalla **lettera** di FR-24
scelta per soddisfare FR-28 (`[MUST]`, *"il passaggio avviene senza click"*), che sulla lettera
di FR-24 era impossibile da soddisfare.

**Sicurezza della somma** (verificato, non assunto) — entrambi i `process()` sono chiamati
incondizionatamente ogni blocco e ognuno comincia azzerando il proprio buffer
(`PhraseScheduler.cpp`, `PlayModeInput.cpp`): non esiste il caso "sommo un buffer con dentro
contenuto vecchio". `FloatVectorOperations::add` non alloca e non prende lock (PRD §9.4).

**Stato** — Attiva. Da rivedere solo se comparisse un caso in cui le due catene producono
suono insieme **oltre** la finestra di dissolvenza: sarebbe un difetto a monte, non di questa
somma.

---

## D-23 — La CI verifica con `ctest`; il gate a `g++` resta, ma è una corsia veloce

**Contesto** (s.36) — Chiude A-06. `.github/workflows/build.yml` compilava a mano tre suite
con `g++` nudo e non invocava mai `ctest`, mentre `CMakeLists.txt` ne registra **11**: fuori
dal gate `glide`, `cell_input_parser`, `voice`, `empty_cell_hold`, `phrase_scheduler`,
`play_mode_input`, `ui_scale`, `mode_switch` — **8 su 11**, fra cui i due banchi che in s.34/35
hanno chiuso B-15, B-16 e B-17. Il divario si allargava da solo: per D-16 ogni banco nuovo
tende a linkare JUCE (`mode_switch_test` linka il plugin intero) e quindi **non è compilabile**
con un `g++` a mano.

**Decisione** — La verifica è `ctest --test-dir build -C Release --output-on-failure`, eseguito
nel job `build` **dopo** il passo di build e **prima** di `pluginval`. Gira sulla matrice già
esistente, quindi copre le 11 suite su Windows **e** macOS — prima copertura automatica di
macOS oltre a `pluginval`.

**Perché il job `dsp-tests` non viene rimosso né convertito** — il suo valore non erano i test,
era il **tempo**: gira su `ubuntu-latest` senza JUCE e fallisce in secondi invece che dopo la
build completa su due piattaforme. Convertirlo a CMake richiederebbe le dipendenze Linux di
JUCE (X11, freetype, ALSA) e ucciderebbe esattamente quel vantaggio. Resta quindi com'è, ma
**dichiarato per quello che è**: un sottoinsieme, non la verifica. Il commento in testa al job
lo dice, e vieta esplicitamente di estenderlo aggiungendo altri `g++`.

**Nessun costo di build** — il passo *Build* gira senza `--target`, quindi gli eseguibili di
test erano **già compilati** e buttati via senza essere eseguiti. Misurato in locale: 11/11
verdi in **3.2 s** su un totale di build di parecchi minuti. Il rischio "la CI si allunga" era
infondato; se un giorno mordesse, la leva è `ctest --parallel`, non togliere suite.

**La guardia** — la causa di A-06 non era una CI sbagliata: era che un target nuovo restava
fuori dal gate **da solo, in silenzio**. Un passo nel job `dsp-tests` confronta l'insieme dei
target `add_executable(<nome>_test` con quello registrato da `add_test(` e fallisce se non
coincidono, nominando il colpevole. Provato in entrambe le direzioni prima di committare: passa
sugli 11 attuali, e togliendo la riga `add_test` di `mode_switch` scatta indicando
`mode_switch_test`. I probe (`*_probe`, `sample_click_finder`, `voice_bench`) sono esclusi per
costruzione dal suffisso `_test`: vogliono file WAV non versionati e non sono `add_test`.

**Stato** — Attiva. Da rivedere se un banco futuro avesse bisogno di file di test non
versionati: dovrebbe nascere `_probe`, non `_test`, oppure la guardia va estesa con una lista
di eccezioni esplicita.

---

## D-24 — L'identità del plugin dipende solo dai codici a 4 lettere: `Hzso`/`Hmz1`/`aumf` sono congelati

**Contesto** (s.37) — `CMakeLists.txt` marcava `PLUGIN_MANUFACTURER_CODE Hzso` e
`PLUGIN_CODE Hmz1` come *"placeholder … da confermare prima della beta pubblica"*, in attesa di
A-02 (nome, marchio, dominio). Con la decisione di dare il plugin ad artisti beta quella formula
diventa pericolosa: **dal primo progetto salvato da un tester quei codici sono pubblici di
fatto**, e cambiarli fa perdere il plugin da *dentro* i progetti degli artisti — la stessa classe
di rottura del tipo AU (regola 7, D-01).

**Il fatto verificato** — l'UID VST3 è derivato **solo** dai due codici a 4 lettere, non dal
nome: `libs/JUCE/modules/juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h:61` →
`VST3Interface::jucePluginId (JucePlugin_ManufacturerCode, JucePlugin_PluginCode, …)`. Il ramo
che usa `JucePlugin_Name` (`:58`) è dietro `JUCE_VST3_CAN_REPLACE_VST2`, che teniamo a `0`. Per
l'AU, `JUCEUtils.cmake:1612` → `JucePlugin_AUSubType = JucePlugin_PluginCode`.

**Decisione** — Due gruppi con regole opposte, scritte nel commento in `CMakeLists.txt`:

| | Nei progetti salvati | Cambiabile |
|---|---|---|
| `PLUGIN_MANUFACTURER_CODE`, `PLUGIN_CODE`, `AU_MAIN_TYPE` | **sì** | **mai** |
| `PRODUCT_NAME`, `COMPANY_NAME`, `BUNDLE_ID` | no | sì |
| Certificato di firma / Developer ID / Team ID | no | sì |

**Conseguenza: A-02 non blocca più la beta.** I codici sono opachi — l'utente non li vede mai e
non devono somigliare al nome commerciale — quindi congelarli non anticipa nessuna decisione di
marchio. `COMPANY_NAME` è passato da `"TBD"` (che i tester leggerebbero come nome del produttore
nel browser dell'host) a `"Giacomo Cazzaro"`, e `BUNDLE_ID` da `com.tbd.harmonizer` a
`com.giacomocazzaro.harmonizer`: entrambi si cambiano di nuovo a costo zero.

**Stato** — Attiva. I tre valori congelati non si toccano più. Da rivedere solo se si volesse
attivare `JUCE_VST3_CAN_REPLACE_VST2`, che tirerebbe `JucePlugin_Name` dentro l'identità VST3 e
congelerebbe anche il nome — motivo in più per lasciarlo a `0`.

---

## D-25 — Beta a costo zero: VST3+AU, firma ad-hoc, A-04 rimandato con un grilletto

**Contesto** (s.37) — `HANDOFF.md` indicava A-04 (firma e notarizzazione) come unico prossimo
passo. Studiandolo, la premessa si è rivelata troppo stretta: l'intento reale è **raccogliere
feedback all'ascolto da artisti beta**, non rilasciare. L'utente ha obiettato che €99/anno
ricorrenti sono presto per un prodotto senza nome né data di rilascio. Obiezione accolta.

**Decisione** — Beta gratuita:

- **VST3 + AU**, nessuno standalone (è un'altra copia dello stesso codice da proteggere, e su
  macOS il permesso microfono non è mai stato verificato: sembrerebbe rotto).
- **macOS: firma ad-hoc** (`codesign -s -`, nessun account). Su Apple Silicon serve *una* firma
  perché il codice giri, e ad-hoc basta. Resta la quarantena: una riga di Terminale a carico del
  tester, scritta in `BETA-macOS.md`.
- **Windows: nessun certificato.** SmartScreen gate gli `.exe` scaricati, non una cartella
  `.vst3` copiata a mano. In più, dal **15 febbraio 2026 i certificati di code signing durano al
  massimo 1 anno**: comprarne uno adesso ne brucerebbe la vita su una beta.
- La CI **carica gli artefatti** (prima li buttava via: nessun `upload-artifact`), così le build
  macOS si producono **senza possedere un Mac** — il runner macOS compila già universal.

**L'AU entra pur non essendo mai stato caricato in un host vero** (`HANDOFF.md`): due tester
usano Logic, e l'utente ha deciso esplicitamente che servono a questo. Registrato come scelta
consapevole, non come svista.

**Il grilletto che riapre A-04** — FR-72 chiede di avviare le pratiche in M0 proprio perché il
lead time morde tardi, e rimandare senza una condizione di risveglio ripeterebbe il ritardo di
cinque milestone. A-04 torna urgente al **primo** di questi:

1. un tester si blocca sulla quarantena e non riesce a caricare il plugin;
2. serve un **installer** (`.pkg`/`.exe`) invece di uno `.zip` — cioè FR-71, M8;
3. **qualunque data di rilascio pubblico meno 8 settimane**.

**Da sapere fin d'ora** — Apple **non accetta ditte individuali né DBA** per gli account
Organization: quando si costituirà dovrà essere una società di capitali. Un account Individual
aperto ora si converte più tardi **conservando Team ID e certificati**, quindi iscriversi come
persona fisica non sarà lavoro buttato.

**Verificato in run #27** (14/08/2026, dai log): `codesign -dv` restituisce `Signature=adhoc` su
entrambi i bundle, `pkg/` contiene i bundle più `LEGGIMI.md`, gli artefatti sono stati caricati
col nome del tester ripulito. `ctest` **13/13 su Windows e macOS**. La pacchettizzazione non ha
più niente di non verificato.

⚠️ **Da non ripetere quando A-04 si riaprirà:** il passo di firma usa `codesign --force --deep`.
Per una firma ad-hoc è innocuo, ma **Apple ha deprecato `--deep` per firmare** (resta valido per
verificare): con un Developer ID vero il codice annidato va firmato singolarmente dall'interno
verso l'esterno, altrimenti la notarizzazione può rifiutare o produrre firme fragili. Da
ricontrollare sulla documentazione Apple al momento di usarlo davvero, non da dare per scontato.

**Stato** — Attiva. Scade al primo dei tre grilletti.

---

## D-26 — Le build beta scadono: si spegne il wet, non il plugin

**Contesto** (s.37) — Senza licensing (`src/licensing/` era vuota, M6 non esiste) una copia data
a un artista è una versione completa e illimitata per sempre. Il rischio reale non è che rubino
il codice — un `.vst3` è codice macchina — è la **redistribuzione**.

**Decisione** — `src/licensing/BetaGate.h`: aritmetica pura, l'ora arriva dal chiamante.
`HARMONIZER_BETA` in `CMakeLists.txt`, **OFF per default** (con ON le build di sviluppo
morirebbero dopo 30 giorni: trappola, e ucciderebbe lo standalone contro la regola 11).

**Il wet, non il plugin** — a scadenza il plugin continua a caricarsi e il dry continua a
passare: è il principio di **FR-68** applicato in anticipo. Un tester che riapre un progetto
dopo la scadenza ritrova il suo segnale asciutto, non una traccia muta.

**Dove si innesta** — `processBlock` forza `effectiveWetLevel = 0.0f` accanto al Bypass, che fa
già la stessa cosa (`PluginProcessor.cpp`). Il valore finisce in `wetGlide`, che lo interpola
campione per campione sulla rampa anti-click di 8 ms **già esistente**: nessuna dissolvenza
nuova da scrivere, nessun rischio di click. L'ora si legge in `prepareToPlay` e in
`timerCallback` (che gira già a 250 ms sul message thread); `processBlock` legge **solo** un
flag atomico — il pattern che la regola 1 consente esplicitamente.

**Due banchi, perché sono due difetti diversi** — `beta_expiry_test` copre *quando* scade;
`beta_gate_audio_test` copre *cosa fa all'audio*, istanziando il processore intero con le macro
forzate a "sempre scaduta". Senza il secondo, un'aritmetica giusta collegata male spedirebbe una
versione illimitata senza che nulla lo segnali. Misurato: wet a **4.3e-9** (il residuo è il dry
a `cos(π/2)` in virgola singola, non wet che sfugge) e dry a **0.0992 contro 0.0992** della
sorgente. 13/13 suite verdi in 2.2 s.

**Il limite, esplicito** — chiunque sposti indietro l'orologio di sistema riottiene il wet, e il
caso "orologio prima della data di build" è trattato **deliberatamente come non scaduto**:
bloccare un tester dall'orologio sbagliato costa più del pirata ingenuo, e per questi
destinatari spostare l'orologio rompe iLok e mezzo parco plugin installato. È un deterrente fra
persone che si conoscono, non un DRM. La protezione vera è M6/A-01 e non esiste.

**Stato** — Attiva e **temporanea**. Quando arriverà il licensing (A-01), `BetaGate.h`,
l'option e i due punti d'innesto **vanno cancellati**, non fatti evolvere: FR-64 chiede la
verifica offline di una licenza firmata, che è un altro problema con un'altra forma.

*(Nota: `PRD` §10.1 chiede per il trial futuro un fade ≥20 ms. Qui si riusano gli 8 ms del
declick esistente, che è la rampa provata del plugin. Se FR-62 vorrà i 20 ms, avrà la sua rampa
— divergenza segnalata, non silenziosa.)*

---
# Decisioni aperte

| ID | Decisione | Scadenza | Nota |
|---|---|---|---|
| A-01 | **Backend di licensing** | M5 (scaduta) | Criteri: IVA/MOSS UE, subscription native, qualità dell'SDK C++, costo per transazione. Costruirlo in proprio è sconsigliato. `LicenseManager` va comunque dietro interfaccia astratta. `src/licensing/` è **vuota** |
| A-02 | **Nome prodotto, marchio, dominio** | prima della **vendita** (non più della beta) | **Ridimensionata da D-24**: non blocca più la beta. `PLUGIN_MANUFACTURER_CODE` (`Hzso`) e `PLUGIN_CODE` (`Hmz1`) sono **congelati** e indipendenti dal nome; `PRODUCT_NAME`/`COMPANY_NAME`/`BUNDLE_ID` si cambiano senza rompere i progetti salvati. Resta da decidere per marchio, dominio e canale di vendita |
| A-03 | **Tipo di licenza JUCE** | prima della beta | Indie vs commerciale, in funzione del fatturato previsto |
| A-04 | **Certificati di firma e notarizzazione** | **rimandata per decisione → D-25** | Apple Developer ID + code signing Windows. Non più "mai avviata" per dimenticanza: rimandata sapendo cosa costa. Torna urgente al **primo** di questi tre — (1) un tester si blocca sulla quarantena, (2) serve un installer `.pkg`/`.exe` (FR-71, M8), (3) qualunque data di rilascio meno 8 settimane. Vincolo da sapere ora: Apple non accetta ditte individuali per gli account Organization; un account Individual si converte conservando Team ID e certificati |
| A-05 | **Sotto-blocchi in `processBlock`** | non pianificata | Diagnosi di s.13: la frequenza di controllo del motore è il reciproco del block size dell'host. Fix previsto (ciclo a sotto-blocchi 64–128 campioni) **mai scritto**. Intervento strutturale: da decidere **con l'utente** come trattare i messaggi MIDI, oggi consumati una volta per blocco. Misurare prima di implementare |
| ~~A-06~~ | ~~**CI: `ctest` invece dei tre `g++` a mano**~~ | **RISOLTA in s.36 → D-23** | Era: conseguenza di D-11, con 8 suite su 11 fuori dal gate e ogni target nuovo che restava fuori da solo. Ora la CI esegue `ctest` (11/11, Windows e macOS) e una guardia impedisce che un banco nuovo scivoli fuori in silenzio |
| A-07 | **Preset di fabbrica** | — | Solo "Min" è verificato contro il prototipo M4L. Gli altri 6 sono voicing jazz generici. Contenuto oltre i 7 tipi base ancora da decidere |
| A-08 | Prezzi dei tre tier · canale di vendita · consegna licenza nel bundle hardware | — | Non tecniche |
| A-09 | **Costo della CI su macOS** | prima del prossimo ciclo di build | **Misurato in s.37: job macOS ~3 h, Windows 15 min, stesso commit.** Cause: (1) `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` compila **tutto due volte**, banchi compresi; (2) `mode_switch_test` e `beta_gate_audio_test` ricompilano l'**intero** elenco dei sorgenti (quarto livello di D-16), ciascuno × 2 architetture; (3) nessuna cache. Repo privato → monte ore mensile, con i minuti macOS moltiplicati. **Due correzioni a copertura invariata**, pronte e non applicate: banchi compilati per **una sola architettura** (la fetta x86_64 dei test non viene **mai eseguita** — la CI gira su arm64) e `paths-ignore` per i push di sola documentazione, **escluse `BETA-*.md`** che entrano nel pacchetto. **La decisione vera che spetta all'utente**: far girare il job macOS **solo a richiesta** taglierebbe quasi tutto ma ridurrebbe la copertura appena conquistata con D-23, e A-06 nasceva proprio da buchi di copertura. **Scartata**: condividere una libreria statica fra i due banchi pesanti — hanno definizioni di compilazione diverse (`HARMONIZER_BETA_*`), quindi quei sorgenti non sono condivisibili. ⚠️ Ogni push fa partire un run completo: usare `[skip ci]` quando il commit non tocca il codice |
