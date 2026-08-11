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

# Decisioni aperte

| ID | Decisione | Scadenza | Nota |
|---|---|---|---|
| A-01 | **Backend di licensing** | M5 (scaduta) | Criteri: IVA/MOSS UE, subscription native, qualità dell'SDK C++, costo per transazione. Costruirlo in proprio è sconsigliato. `LicenseManager` va comunque dietro interfaccia astratta. `src/licensing/` è **vuota** |
| A-02 | **Nome prodotto, marchio, dominio** | prima della beta | Bloccano `PLUGIN_MANUFACTURER_CODE` (`Hzso`), `PLUGIN_CODE` (`Hmz1`), `COMPANY_NAME` (`"TBD"`), `BUNDLE_ID`. Cambiarli dopo il rilascio rompe i progetti salvati, come D-01 |
| A-03 | **Tipo di licenza JUCE** | prima della beta | Indie vs commerciale, in funzione del fatturato previsto |
| A-04 | **Certificati di firma e notarizzazione** | era M0 | Apple Developer ID + code signing Windows. Il lead time più lungo del progetto, mai avviato |
| A-05 | **Sotto-blocchi in `processBlock`** | non pianificata | Diagnosi di s.13: la frequenza di controllo del motore è il reciproco del block size dell'host. Fix previsto (ciclo a sotto-blocchi 64–128 campioni) **mai scritto**. Intervento strutturale: da decidere **con l'utente** come trattare i messaggi MIDI, oggi consumati una volta per blocco. Misurare prima di implementare |
| A-06 | **CI: `ctest` invece dei tre `g++` a mano** | non pianificata | Conseguenza di D-11: **5 suite su 8** sono fuori CI e ogni target nuovo resta fuori da solo. Aggravata da D-16 (s.30): `phrase_scheduler_test` linka `juce_core` e **non è compilabile** con i tre `g++` a mano, quindi passare a `ctest` non è più solo più comodo — è l'unico modo di coprirla |
| A-07 | **Preset di fabbrica** | — | Solo "Min" è verificato contro il prototipo M4L. Gli altri 6 sono voicing jazz generici. Contenuto oltre i 7 tipi base ancora da decidere |
| A-08 | Prezzi dei tre tier · canale di vendita · consegna licenza nel bundle hardware | — | Non tecniche |
