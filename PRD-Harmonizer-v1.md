# PRD — Harmonizer Intelligente

> **Versione documento:** 1.0
> **Data:** luglio 2026
> **Destinatario:** sviluppo (umano + Claude Code)
> **Nome prodotto:** da definire — nel codice si usa il codename `HARMONIZER`
> **Origine:** industrializzazione di un prototipo Max for Live esistente e validato sul campo

---

## 0. Come leggere e usare questo documento

Convenzioni obbligatorie:

- `[MUST]` — bloccante per la v1.0
- `[SHOULD]` — desiderabile in v1.0, sacrificabile
- `[V1.1]` — architettura predisposta in v1.0, implementazione successiva
- `[LATER]` — fuori scope, tracciato per non essere reinventato
- `[DECISION]` — scelta aperta, con scadenza indicata

Ogni requisito ha un ID `FR-xx` / `NFR-xx` da citare nei commit e nelle issue.

**Regola per l'agente di coding:** questo documento è la fonte di verità. Se un'implementazione richiede di discostarsi da un requisito, va segnalato e discusso, non deciso unilateralmente.

---

## 1. Il prodotto in una frase

Un armonizzatore per strumenti monofonici che, invece di trasporre a intervalli fissi, **calcola la relazione tra la nota suonata e l'accordo corrente** e da quella relazione deriva un voicing coerente, definito dall'utente in tabelle editabili.

### 1.1 Cosa lo distingue

1. **L'armonia è dato, non codice.** L'utente scrive le proprie regole di armonizzazione in una tabella. Il plugin non impone una teoria: esegue la teoria dell'utente.
2. **Progettato per il palco.** Priorità assoluta alla reattività. Il compromesso latenza/qualità è esposto come controllo di primo livello, non nascosto nelle impostazioni.
3. **Integrazione hardware.** Chi acquista il controller dedicato ha un'esperienza plug and play senza mappature; chiunque altro può mappare tutto a piacere.

### 1.2 Utente target

Strumentisti a fiato e cantanti che suonano linee monofoniche — sax, tromba, voce — in contesto live e in studio, con conoscenza dell'armonia sufficiente a voler decidere i propri voicing.

### 1.3 Metriche di successo v1.0

| Metrica | Target |
|---|---|
| Latenza in modalità più reattiva | ≤ 15 ms @48 kHz |
| CPU, 8 voci attive @48 kHz / buffer 128 | ≤ 15% di un core moderno |
| `pluginval` strictness 10 | passa su tutti i formati |
| Crash riportati in beta | zero nelle ultime 2 settimane |

---

## 2. Modello dei dati — il cuore del prodotto

Questa sezione va implementata per prima e va capita completamente prima di scrivere qualunque altra cosa.

### 2.1 Preset armonico

Un **preset armonico** rappresenta **un tipo di accordo** (es. `Maj`, `Min`, `Dom`, `Sus`, `Half Dim`, `Dim`, `Aug7`) ed è costituito da una tabella:

- **12 colonne** — indice `d = (notaSuonataMIDI − fondamentaleMIDI) mod 12`, con `d = 0` quando la nota coincide con la fondamentale
- **8 righe** — le 8 voci di armonizzazione
- **ogni cella** — offset in semitoni rispetto alla nota suonata

Semantica delle celle:

| Contenuto | Significato |
|---|---|
| valore negativo | voce sotto la nota suonata |
| valore positivo | voce sopra la nota suonata |
| `0` | unisono (utile soprattutto nelle frasi ritmiche) |
| **vuoto** | voce muta su quel grado |

`0` e cella vuota sono **stati distinti** e vanno rappresentati distintamente anche nel formato di serializzazione: usare `null` per la cella vuota, non `0`.

**Esempio** (preset `Min`, dati dal prototipo, colonna `d=2`): la nota suonata è una D su un accordo di C-7 → offset `[−2, −4, −7, −11, null, null, null, null]` → suonano C, Bb, G, Eb sotto la D. Le voci 5–8 sono mute.

- **FR-01** `[MUST]` Il preset armonico contiene **esclusivamente** la tabella 12×8, il nome e un ID interno. Nessun parametro timbrico.
- **FR-02** `[MUST]` L'utente può creare, rinominare, duplicare ed eliminare preset armonici, senza limite pratico di numero (tetto tecnico 128, vedi §4.2).
- **FR-03** `[MUST]` Import ed export in **CSV**, con un formato che rispecchi la struttura 12×8 così da poter essere prodotto e modificato in un foglio di calcolo. La cella vuota nel CSV è la stringa vuota; `0` è lo zero.
- **FR-04** `[MUST]` I preset di fabbrica includono almeno i 7 tipi elencati sopra.

### 2.2 Ordinamento dei preset e mapping CC

Questo è un punto delicato, legato all'integrazione hardware.

- **FR-05** `[MUST]` I preset armonici vivono in una **lista ordinata**. La posizione nella lista (1-based) **è** il valore CC che li seleziona. Preset in posizione 3 → CC preset con valore 3.
- **FR-06** `[MUST]` L'ordine si modifica con **drag and drop**, con l'interazione tipica del riordino di icone su smartphone.
- **FR-07** `[MUST]` Il riordino **rinumera** i valori CC. È un comportamento voluto: il controller dedicato ha un navigation button a 5 direzioni che seleziona sempre i primi 5 preset della lista, quindi l'utente riorganizza la lista per decidere cosa avere sotto le dita.

**Conseguenza da gestire (FR-08):** poiché il valore CC dipende dall'ordine, riordinare i preset cambierebbe il significato delle automazioni già scritte in progetti esistenti.

- **FR-08** `[MUST]` La **libreria di preset armonici viene serializzata dentro lo stato del plugin**, quindi salvata nella sessione dell'host. Riordinare la libreria oggi non altera i progetti salvati ieri, perché ogni progetto porta con sé la propria copia della libreria e del suo ordine.
- **FR-09** `[MUST]` Esiste in parallelo una **libreria globale** su disco, usata per popolare le nuove istanze. Da UI l'utente può: caricare la libreria globale nell'istanza corrente, e salvare la libreria dell'istanza corrente come globale. Le due operazioni sono esplicite, mai automatiche.
- **FR-10** `[MUST]` Ogni preset ha comunque un **ID interno stabile** (UUID) che non cambia mai, indipendente dalla posizione. Serve per riferimenti interni e per future funzionalità; il CC resta posizionale.

### 2.3 Preset timbrici

Sistema di preset **separato e indipendente** da quello armonico.

- **FR-11** `[MUST]` Un preset timbrico contiene: numero di voci attive, gain e pan per voce, offset di formante per voce, modo Fix/Move per voce, spread globale delle formanti, livello dry, output gain.
- **FR-12** `[MUST]` Cambiare preset armonico **non** altera i parametri timbrici, e viceversa. Dal vivo, cambiare accordo non deve riconfigurare il mix.
- **FR-13** `[SHOULD]` I preset timbrici hanno la propria lista, il proprio CC di selezione e il proprio import/export.

---

## 3. Motore di armonizzazione

### 3.1 Catena logica

```
Audio in (mono)
   │
   ├──────────────────────────────► Dry path ─────────────────┐
   │                                                          │
   ▼                                                          │
PitchDetector ──► notaMIDI + confidenza + f0 continua         │
   │                                                          │
   ▼                                                          │
HarmonyEngine                                                 │
   d = (notaMIDI − fondamentale) mod 12                       │
   offsets[8] = presetArmonico.tabella[d]                     │
   │                                                          │
   ▼                                                          │
PhraseScheduler  (trigger su onset, pattern ritmico globale)  │
   │                                                          │
   ▼                                                          │
VoicePool  (tetto voci, furto della frase più vecchia)        │
   │                                                          │
   ▼                                                          │
Voice ×N:  PitchShifter → FormantProcessor → Gain → Pan       │
   │                                                          │
   ▼                                                          ▼
  Wet sum ────────────────────────► Mix ──► Output gain ──► Out
```

### 3.2 Requisiti

- **FR-14** `[MUST]` Rilevamento di pitch monofonico in tempo reale. Sorgenti da ottimizzare, in ordine: **voce, sax, tromba**.
- **FR-15** `[MUST]` La fondamentale dell'accordo si seleziona su 12 valori cromatici; il tipo di accordo si seleziona scegliendo un preset armonico.
- **FR-16** `[MUST]` Il calcolo del grado `d` è invariante rispetto all'ottava: gli offset si applicano sempre alla nota reale suonata, quindi le voci seguono il registro dell'esecutore.
- **FR-17** `[MUST]` **Cambio di accordo su nota tenuta:** se l'utente cambia fondamentale o preset mentre una nota è in suono, le voci attualmente in suono si ricalcolano **immediatamente** sul nuovo grado. Non è richiesto ribattere la nota. Il movimento avviene con un glide breve configurabile (default 30 ms) per evitare click.
- **FR-18** `[MUST]` Nessun limite di registro: gli offset si applicano anche in estremi. Il problema del suono impastato si risolve con le formanti (§5), non troncando le voci.
- **FR-19** `[MUST]` Il numero di voci attive è selezionabile da 1 a 8. Le voci oltre il numero selezionato sono mute anche se il preset contiene offset per loro.
- **FR-20** `[SHOULD]` Se la confidenza del detector scende sotto soglia (silenzio, rumore, transiente non intonato), le voci vengono silenziate con fade anziché produrre artefatti.

### 3.3 Fix / Move — comportamento su vibrato e portamento

Requisito che definisce l'identità sonora del prodotto.

- **FR-21** `[MUST]` Modalità **Move** (default): il pitch shifter lavora a **rapporto fisso** rispetto al segnale in ingresso. Vibrato, portamento e bending dell'esecutore si trasferiscono integralmente su tutte le voci. La sezione armonica respira con chi suona.
- **FR-22** `[MUST]` Modalità **Fix**: il target di ogni voce è `notaMIDI quantizzata + offset`, e il rapporto di shift viene ricalcolato a ogni blocco per inseguirlo. Le voci restano su intonazione fissa mentre l'ingresso oscilla.
- **FR-23** `[MUST]` Fix/Move è selezionabile **per singola voce**, non globalmente. Permette configurazioni ibride: voci gravi bloccate a fare da pad, voci acute che seguono l'espressività.

Nota implementativa: le due modalità differiscono solo nel calcolo del rapporto di shift, non nell'algoritmo. Move è il comportamento naturale del pitch shifter e non richiede codice aggiuntivo. Fix dipende criticamente dalla stabilità del detector: se il rilevamento oscilla tra due semitoni le voci sfarfallano, ed è lì che interviene il controllo Stability.

### 3.4 Modalità Play

- **FR-24** `[MUST]` In modalità **Play** la tabella del preset armonico è **completamente disattivata**. Le voci vengono pitchate sulle note MIDI ricevute in ingresso.
- **FR-25** `[MUST]` Fino a 8 note MIDI simultanee pilotano fino a 8 voci. Le voci in eccesso rispetto alle note premute restano **mute** (nessun raddoppio automatico all'ottava).
- **FR-26** `[MUST]` Il segnale dry resta sempre udibile: si sente sia la nota suonata sia le voci pitchate.
- **FR-27** `[MUST]` Se nessuna nota MIDI è premuta, passa **solo il dry**, senza voci.
- **FR-28** `[MUST]` Il passaggio tra modalità Harmonizer e Play avviene senza click e senza interruzioni del segnale dry.

Set up di riferimento in Ableton Live, da documentare per l'utente: traccia MIDI vuota che riceve dalla tastiera, output instradato verso il plugin che risiede sulla traccia audio. È lo stesso schema di Waves Harmony.

---

## 4. Controllo, MIDI e automazione

### 4.1 Tipo di plugin — decisione strutturale

- **FR-29** `[MUST]` Il plugin dichiara un **bus di ingresso MIDI**. In VST3 è un bus aggiuntivo; in Audio Unit il plugin deve essere di tipo **Music Effect (`aumf`)**, non Effect (`aufx`), altrimenti non può ricevere MIDI.

Questa decisione va presa in M0 e **non è reversibile**: cambiare il tipo AU dopo il rilascio rende irrecuperabili i progetti salvati dagli utenti. Conseguenza da documentare: in Logic il plugin comparirà in una categoria diversa dagli effetti audio ordinari.

### 4.2 Controllo via CC

- **FR-30** `[MUST]` Tre CC configurabili, con questa semantica:

| Funzione | Valori | Note |
|---|---|---|
| Fondamentale | **1–12** | 1 = C, 2 = C#/Db … 12 = B. Il valore 0 e i valori > 12 sono ignorati |
| Preset armonico | **1–N** | pari alla posizione in lista; valori oltre la lunghezza della lista sono ignorati |
| Bypass | **0–63 = off, 64–127 = on** | stessa soglia del pedale sustain |

- **FR-31** `[MUST]` Il numero di ciascun CC è configurabile dall'utente nella schermata impostazioni. I valori associati **non** sono configurabili: sono posizionali per progetto.
- **FR-32** `[MUST]` Il canale MIDI è configurabile, con **omni** come default.
- **FR-33** `[SHOULD]` Funzione MIDI Learn interna: l'utente preme "learn", muove un controllo sul proprio controller, il plugin cattura il numero di CC.

### 4.3 Automazione e precedenza

- **FR-34** `[MUST]` Tutti i parametri sono esposti all'host come parametri automatizzabili, così da essere mappabili con il MIDI Learn nativo di Ableton e degli altri host.
- **FR-35** `[MUST]` Fondamentale e selezione preset sono parametri **discreti a passi**, non continui. Un parametro continuo farebbe disegnare rampe alle automazioni, attraversando voicing intermedi indesiderati.
- **FR-36** `[MUST]` **Regola di precedenza:** un CC in ingresso mette l'automazione della DAW in stato di **override** per quel parametro. L'override permane fino allo **stop del transport**.
- **FR-37** `[MUST]` Nella versione standalone non esiste transport: il CC in ingresso comanda sempre, senza override da revocare.
- **FR-38** `[MUST]` In caso di conflitto tra più sorgenti di controllo esterne, il CC ricevuto per ultimo vince.

---

## 5. Formanti

- **FR-39** `[MUST]` Correzione automatica attiva di default, calcolata in funzione della **distanza di shift rispetto all'originale**, non della frequenza assoluta della voce risultante.
  - shift verso il basso → la voce viene **schiarita** (evita l'impasto nel mix)
  - shift verso l'alto → la voce viene **scurita** (evita l'effetto chipmunk)
  - a −24 semitoni la correzione è marcata; a −2 è appena percettibile
- **FR-40** `[MUST]` Knob globale **Spread**: regola quanto la correzione automatica influisce, da nulla a massima.
- **FR-41** `[MUST]` Offset di formante **manuale e indipendente per ogni voce**, sommato alla correzione automatica, per uso creativo (es. voci maschili sotto e femminili sopra).
- **FR-42** `[MUST]` La correzione delle formanti si applica anche in modalità Play.

---

## 6. Motore delle frasi e pattern ritmico

Architettura **obbligatoria in v1.0**, funzionalità utente `[V1.1]`. Il `PhraseScheduler` e il `VoicePool` vanno costruiti da subito anche se il pattern editor non viene esposto: aggiungerli dopo significherebbe riprogettare la gestione delle voci.

### 6.1 Modello a frase

- **FR-43** `[MUST]` Ogni **onset** rilevato è un trigger che genera una **frase**: le voci del preset disposte nel tempo secondo il pattern globale.
- **FR-44** `[MUST]` La frase suona **una sola volta**. Nessun loop, nessun feedback, nessuna ripetizione.
- **FR-45** `[MUST]` Se una nuova nota arriva mentre una frase è ancora in corso, la nuova frase **si somma**. Le frasi sono indipendenti e si esauriscono ciascuna per conto proprio.
- **FR-46** `[MUST]` Ogni frase **congela il proprio voicing al momento del trigger**. Le voci ancora in coda non ancora suonate mantengono il voicing calcolato al trigger anche se nel frattempo l'accordo è cambiato.

> `[DECISION]` — da verificare in prototipo entro M2: FR-46 e FR-17 descrivono comportamenti diversi per casi diversi (voce già in suono vs voce ancora in coda). Verificare all'ascolto che la combinazione sia musicalmente coerente e, se non lo è, allineare le due regole.

### 6.2 Pattern ritmico

- **FR-47** `[V1.1]` Il pattern ritmico è **globale**, non legato al preset armonico. Cambiando accordo lungo un giro armonico l'arpeggio resta lo stesso.
- **FR-48** `[V1.1]` Modalità **sincronizzata**: griglia stile piano roll, **8 righe** (una per voce) × **2 misure**, risoluzione minima **terzina di sedicesimi**. L'utente posiziona un blocco per decidere quando quella voce entra.
- **FR-49** `[V1.1]` Modalità **millisecondi**: per ogni voce uno slider su scala **0–2000 ms**, default 0, con rappresentazione grafica della dislocazione temporale delle voci.
- **FR-50** `[V1.1]` L'editor del pattern è accessibile da un pannello richiamabile, non occupa spazio permanente nella UI principale.

### 6.3 Pool di voci e furto

Con frasi sovrapposte il conto delle voci simultanee cresce rapidamente: un pattern di 2 misure a 120 bpm dura 4 secondi; una linea in ottavi genera 16 frasi attive contemporaneamente, cioè fino a 128 pitch shifter. Serve un tetto.

- **FR-51** `[MUST]` Tetto configurabile di voci simultanee. Default **32**, esposto nelle impostazioni.
- **FR-52** `[MUST]` Al superamento del tetto viene terminata **la frase più vecchia per intero**, con fade di almeno 20 ms, liberando tutte le sue voci in una volta. Non si terminano voci singole.
- **FR-53** `[MUST]` Il numero di voci attive è visibile nella UI, così che l'utente capisca quando sta saturando.

---

## 7. Latenza, qualità e controllo Stability

- **FR-54** `[MUST]` Il controllo **Stability** seleziona la dimensione della finestra di analisi, con un numero discreto di posizioni (proposta: 5, da `Fast` a `Accurate`). Finestra corta = più reattivo e meno accurato; finestra lunga = più accurato e più lento.
- **FR-55** `[MUST]` La finestra di analisi **determina la latenza del plugin**, che va dichiarata correttamente all'host tramite `setLatencySamples`.
- **FR-56** `[MUST]` Un cambio di Stability si applica **immediatamente a transport fermo**; se il transport sta suonando, il cambio resta in attesa e si applica allo stop. Molti host producono click o buchi se un plugin cambia latenza dichiarata durante la riproduzione.
- **FR-57** `[MUST]` Nella versione standalone il cambio si applica sempre immediatamente.
- **FR-58** `[MUST]` Il percorso dry è allineato al percorso wet, così che l'insieme resti coerente in fase di mixaggio.

---

## 8. Interfaccia utente

Tre schermate.

### 8.1 Schermata principale `[MUST]`

- Selezione **fondamentale**: griglia di 12 pulsanti cromatici
- Selezione **preset armonico**: lista o griglia, con i primi 5 evidenziati (corrispondono alle direzioni del navigation button hardware)
- Interruttore modalità **Harmonizer / Play**
- Display della **nota rilevata**
- Selettore **numero di voci** (1–8)
- Controllo **Stability**
- Slider **Dry** + slider **gain per voce**
- **Knob di pan per voce**
- Knob **Formant Spread** globale
- Indicatore stato licenza
- Indicatore voci attive
- Accesso alle altre due schermate

### 8.2 Editor dei preset `[MUST]`

- Tabella **12 colonne × 8 righe** editabile, con intestazioni di colonna che mostrano il grado in forma leggibile
- Lista dei preset con **drag and drop**, valore CC mostrato accanto a ciascuno e aggiornato in tempo reale durante il riordino
- Crea, duplica, rinomina, elimina
- **Import / export CSV**
- Carica libreria globale / salva come libreria globale

### 8.3 Impostazioni `[MUST]`

- Numeri dei tre CC + MIDI Learn
- Canale MIDI
- Tetto voci simultanee
- Comportamento latenza
- Offset formante per voce e modo Fix/Move per voce (se non trovano posto nella schermata principale)
- Gestione licenza e attivazione

### 8.4 Requisiti trasversali UI

- **FR-59** `[MUST]` Ridimensionamento 70%–200% con resa corretta su display HiDPI e Retina
- **FR-60** `[MUST]` Nessun controllo essenziale al live nascosto sotto più di un click dalla schermata principale
- **FR-61** `[SHOULD]` Tema chiaro e scuro

---

## 9. Architettura tecnica

### 9.1 Stack

| Componente | Scelta |
|---|---|
| Framework | JUCE 8.x (licenza commerciale/Indie da acquistare) |
| Linguaggio | C++20 |
| Build | CMake (JUCE CMake API, non Projucer) |
| Formati | **VST3, AU (Music Effect), Standalone** — tutti `[MUST]` |
| Piattaforme | macOS 11+ Universal (arm64 + x86_64), Windows 10+ x64 |
| Pitch detection | **Cycfi Q** (MIT) |
| Pitch shifting | **PSOLA proprietario**, dietro interfaccia astratta |
| Motore qualità alternativo | **Signalsmith Stretch** (MIT) |
| Test | Catch2 + pluginval |

### 9.2 Razionale delle scelte DSP

**Perché PSOLA e non una libreria spettrale.** Il caso d'uso è monofonico con pitch già noto — il caso facile. Le librerie spettrali sono progettate per materiale polifonico sconosciuto e pagano quella generalità in latenza e CPU. PSOLA su sorgente monofonica ha latenza dell'ordine di uno o due periodi d'onda (su una voce a 150 Hz, circa 7–13 ms), dà controllo delle formanti più preciso delle tecniche spettrali, e con 8 voci il vantaggio di CPU è decisivo perché tutte condividono la stessa analisi di pitch fatta una volta sola.

**Perché non Zynaptiq, per ora.** La versione gratuita ZTX LE non supporta il cambio dinamico dei parametri di pitch — che è precisamente ciò che un harmonizer fa a ogni nota. Neanche ZTX STUDIO lo supporta: servirebbe ZTX PRO, cioè trattativa commerciale e quotazione su misura. La licenza LE impone inoltre attribuzione nella documentazione, nell'info screen e sul sito. Resta un'opzione futura, non un punto di partenza.

**Perché non aubio.** Licenza GPL, incompatibile con un prodotto commerciale closed-source.

- **FR-62** `[MUST]` `PitchShifter` è un'**interfaccia astratta**. Nessun altro modulo può dipendere da una specifica implementazione. È questa astrazione che rende possibile passare a un motore commerciale in una settimana anziché in una riscrittura.

### 9.3 Struttura del repository

```
/
├── CMakeLists.txt
├── CLAUDE.md
├── src/
│   ├── PluginProcessor.{h,cpp}
│   ├── PluginEditor.{h,cpp}
│   ├── dsp/
│   │   ├── PitchDetector.{h,cpp}        # wrapper su Cycfi Q
│   │   ├── PitchShifter.h               # interfaccia astratta
│   │   ├── PsolaShifter.{h,cpp}
│   │   ├── SpectralShifter.{h,cpp}      # Signalsmith
│   │   ├── FormantProcessor.{h,cpp}
│   │   └── Glide.{h,cpp}
│   ├── harmony/
│   │   ├── HarmonyPreset.{h,cpp}        # tabella 12x8
│   │   ├── PresetLibrary.{h,cpp}        # lista ordinata, CC posizionale
│   │   ├── HarmonyEngine.{h,cpp}
│   │   └── CsvIo.{h,cpp}
│   ├── voices/
│   │   ├── Voice.{h,cpp}
│   │   ├── Phrase.{h,cpp}
│   │   ├── PhraseScheduler.{h,cpp}
│   │   └── VoicePool.{h,cpp}
│   ├── midi/
│   │   ├── CcRouter.{h,cpp}
│   │   ├── OverrideManager.{h,cpp}      # precedenza CC vs automazione
│   │   └── PlayModeInput.{h,cpp}
│   ├── state/
│   │   ├── ParameterLayout.{h,cpp}
│   │   └── StateSerializer.{h,cpp}
│   ├── licensing/
│   └── ui/
├── resources/
│   ├── factory_presets/
│   └── ...
├── tests/
└── tools/
```

### 9.4 Regole di threading — non negoziabili

Sull'**audio thread** (`processBlock`) è **vietato**: allocare o deallocare memoria, prendere lock contendibili, fare I/O di file o di rete, lanciare eccezioni, chiamare il `LicenseManager` al di fuori della lettura di un flag atomico.

- Tutte le allocazioni — pool di voci al completo, buffer, tabelle — avvengono in `prepareToPlay`, dimensionate sul caso peggiore (tetto massimo di voci, sample rate massimo, finestra di analisi massima).
- UI → audio: `AudioProcessorValueTreeState` con parametri atomici.
- audio → UI: FIFO lock-free (`juce::AbstractFifo`).
- Cambio di libreria preset da UI: la nuova libreria si costruisce sul message thread e si scambia con il puntatore atomico; la vecchia si distrugge sul message thread, **mai** sull'audio thread.

### 9.5 Requisiti non funzionali

| ID | Requisito |
|---|---|
| NFR-01 | Nessuna allocazione sull'audio thread, verificabile con strumenti di rilevamento real-time |
| NFR-02 | Nessuna eccezione può attraversare il confine dei callback dell'host |
| NFR-03 | Comportamento corretto con buffer da 32 a 4096 campioni, anche variabili, e con buffer di dimensione zero |
| NFR-04 | Sample rate 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz, incluso il cambio a runtime |
| NFR-05 | Configurazioni I/O mono→mono, mono→stereo, stereo→stereo |
| NFR-06 | Stato salvato con la v1.0 caricabile in tutte le versioni successive; lo schema include un numero di versione |
| NFR-07 | Un ID di parametro pubblicato non cambia mai |
| NFR-08 | Caricamento dell'istanza < 500 ms |

---

## 10. Licenze e monetizzazione

### 10.1 Modello

| Tier | Contenuto |
|---|---|
| **Trial** | Funzionalità complete, 14 giorni. Limitazione: silenziamento del wet per 3 secondi ogni 60, con fade di almeno 20 ms. Nessun export CSV |
| **Abbonamento** | Mensile o annuale a scelta del cliente, annuale scontato |
| **Licenza perpetua** | Aggiornamenti per la major version corrente. Indicativamente 2–2,5 anni di abbonamento |
| **Bundle hardware** | Chi acquista il controller dedicato riceve una licenza attivata: l'esperienza deve essere plug and play, senza passaggi di attivazione manuale al primo avvio |

- **FR-63** `[MUST]` Una sola build serve tutti i tier. Nessuna build demo separata.
- **FR-64** `[MUST]` **Verifica offline.** Dopo l'attivazione iniziale il plugin funziona senza connessione. Nessuna chiamata di rete durante l'uso. Non negoziabile per un prodotto da palco.
- **FR-65** `[MUST]` File di licenza firmato crittograficamente; nel binario solo la chiave pubblica.
- **FR-66** `[MUST]` Le chiamate di rete su thread dedicato con timeout, mai bloccanti per UI o audio.
- **FR-67** `[MUST]` Abbonamento: refresh silenzioso in finestra di grazia, più 14 giorni di tolleranza oltre la scadenza in assenza di rete.
- **FR-68** `[MUST]` Alla scadenza il plugin **carica comunque** e lascia passare il dry: non si rompono i progetti degli utenti.
- **FR-69** `[SHOULD]` Limite di attivazioni per licenza con deautorizzazione dal portale.
- **FR-70** `[MUST]` Nessuna telemetria non dichiarata.

### 10.2 Distribuzione

- **FR-71** `[MUST]` Installer nativi firmati: `.pkg` firmato e notarizzato per macOS, `.exe` firmato per Windows.
- **FR-72** `[MUST]` Le pratiche per i certificati vanno avviate **durante M0**: i tempi burocratici possono superare le settimane.

> `[DECISION]` Backend di licensing — da chiudere entro M5. Criteri: gestione IVA/MOSS UE, supporto nativo alle subscription, qualità dell'SDK C++, costo per transazione. Costruirlo in proprio è sconsigliato per la v1.0. `LicenseManager` va comunque dietro interfaccia astratta.

---

## 11. Compatibilità e test

Host da validare `[MUST]`: **Ableton Live** (primario), Logic Pro, Cubase, Reaper, FL Studio, Studio One.

Per ciascuno: caricamento, salvataggio e ripristino sessione, automazione, routing MIDI verso il plugin su traccia audio, cambio sample rate, bounce offline, freeze, 8+ istanze simultanee.

Casi di test specifici del prodotto:
- riordino preset → riapertura di una sessione salvata prima del riordino → verifica che il voicing sia invariato
- cambio accordo su nota tenuta → nessun click
- cambio Stability durante la riproduzione → nessun buco
- fraseggio veloce con pattern lungo → saturazione del pool → verifica del furto di frase
- CC in ingresso contro automazione attiva → verifica dell'override e del ripristino allo stop

`pluginval --strictness-level 10` su tutti i formati è un gate di CI obbligatorio.

---

## 12. Roadmap

| Milestone | Durata | Contenuto | Criterio di uscita |
|---|---|---|---|
| **M0 — Fondamenta** | 3 sett. | CMake, target VST3 + AU (`aumf`) + Standalone, CI su entrambe le piattaforme, `CLAUDE.md`, avvio pratiche certificati | Build automatica su ogni push, `pluginval` verde su plugin vuoto |
| **M1 — Detection e shifting** | 8 sett. | Integrazione Cycfi Q, `PsolaShifter`, Glide, Fix/Move, controllo Stability con latenza dichiarata | Una voce trasposta in tempo reale, qualità giudicata accettabile all'ascolto, latenza misurata |
| **M2 — Motore armonico** | 5 sett. | `HarmonyPreset`, `PresetLibrary`, `HarmonyEngine`, CSV I/O, import dei dati del prototipo | Parità funzionale col prototipo M4L verificata su casi di test armonici |
| **M3 — Voci e frasi** | 5 sett. | `Voice`, `VoicePool`, `PhraseScheduler` (senza editor), formanti, gain/pan per voce | 8 voci simultanee entro il budget di CPU; furto di frase verificato |
| **M4 — MIDI, modi e stato** | 5 sett. | CC router, override, modalità Play, APVTS completo, serializzazione libreria in sessione | Ciclo completo hardware → plugin senza mappature manuali |
| **M5 — UI** | 8 sett. | Le tre schermate, drag and drop, editor tabella, scaling | Un tester esterno usa il plugin senza documentazione |
| **M6 — Licensing** | 4 sett. | `LicenseManager`, trial, attivazione, bundle hardware | Ciclo acquisto → attivazione → uso offline |
| **M7 — Hardening** | 5 sett. | Matrice di compatibilità, ottimizzazione, fix | Zero bug bloccanti, target di performance raggiunti |
| **M8 — Beta** | 4 sett. | 30–50 tester, installer firmati, documentazione | Nessun crash per 2 settimane consecutive |
| **M9 — Lancio** | 3 sett. | Materiali di vendita, pagina prodotto, demo audio | Prodotto in vendita |

Totale ≈ 50 settimane. M1 e M5 hanno la varianza maggiore: prevedere buffer.

**Post-v1.0 (`[V1.1]`):** editor del pattern ritmico, preset timbrici come sistema completo, tema chiaro.

---

## 13. Rischi

| Rischio | Impatto | Mitigazione |
|---|---|---|
| La qualità del PSOLA proprietario non regge il confronto | Alto — è il prodotto | Valutazione all'ascolto a fine M1; interfaccia astratta che permette di passare a ZTX PRO senza riscrivere |
| Sviluppatore singolo alle prime armi con C++ su ~50 settimane | Alto | Milestone brevi con deliverable eseguibili; CI dal giorno uno; target standalone attivo da M0 per iterare sul DSP senza aprire la DAW |
| Costo di ZTX PRO se si rende necessario | Medio | Avviare il contatto con Zynaptiq già durante M1, prima che diventi urgente |
| Il modello CC posizionale confonde gli utenti non hardware | Medio | UI che mostra sempre il valore CC accanto al preset; documentazione dedicata |
| Tempi di certificati e notarizzazione | Medio | Pratiche avviate in M0 |
| Fiscalità delle vendite internazionali | Medio | Preferire un merchant of record che gestisca IVA e MOSS; valutare con un commercialista |
| Scope creep sulla UI | Medio | Feature list della UI congelata a fine M4 |

---

## 14. Fuori scope

- Formato AAX e Pro Tools
- Linux
- Riconoscimento automatico dell'accordo dall'audio polifonico
- Sorgenti polifoniche in ingresso
- Loop e feedback nel motore delle frasi
- Versione per DSP di schede audio (piattaforma UAD chiusa a sviluppatori indipendenti)
- Voice leading automatico tra cambi di accordo
- iOS / AUv3
- **Pedale hardware standalone che processa l'audio in autonomia** — visione di prodotto dichiarata, fuori dal perimetro software v1.0

---

## 15. Appendice — `CLAUDE.md`

Da riportare alla radice del repository:

1. **Non violare mai le regole di threading di §9.4.** Prima di ogni riga aggiunta in `processBlock`: non alloca, non prende lock, non fa I/O.
2. **`PitchShifter` è un'interfaccia astratta.** Nessun modulo fuori da `dsp/` può conoscere l'implementazione concreta.
3. **`0` e cella vuota sono cose diverse** nella tabella dei preset. Serializzare `null` per la cella vuota. Confonderli rompe la semantica delle voci mute.
4. **Il valore CC di un preset è la sua posizione in lista.** Non introdurre mai mappature alternative.
5. **La libreria di preset si serializza dentro lo stato del plugin.** Non salvare solo un riferimento a file esterni.
6. **Un ID di parametro pubblicato non cambia mai.** Aggiungere, mai rinominare.
7. **Il tipo AU è Music Effect (`aumf`).** Non cambiarlo per nessun motivo dopo M0.
8. **Prima di dichiarare completa una feature**, eseguire `pluginval --strictness-level 10` e riportare l'esito.
9. **Nessuna dipendenza esterna senza approvazione esplicita.** Ogni licenza ha implicazioni sul prodotto commerciale. GPL e LGPL statico sono esclusi a priori.
10. **Commit atomici che citano l'ID del requisito:** `feat(harmony): lookup tabella preset — FR-16`.
11. **Il target standalone deve restare sempre funzionante.** È lo strumento principale di iterazione sul DSP.

---

## 16. Questioni aperte

- [ ] Nome del prodotto, marchio, dominio
- [ ] Prezzi dei tre tier
- [ ] Tipo di licenza JUCE in funzione del fatturato previsto
- [ ] `[DECISION]` Backend di licensing (entro M5)
- [ ] `[DECISION]` Coerenza tra FR-17 e FR-46 (entro M2, all'ascolto)
- [ ] Numero esatto di posizioni del controllo Stability e finestre corrispondenti (entro M1)
- [ ] Contenuto dei preset di fabbrica oltre ai 7 tipi base
- [ ] Canale di vendita: negozio proprio, rivenditori specializzati, o entrambi
- [ ] Modalità di consegna della licenza nel bundle hardware
