# PRD — UI di HARMONIZER

> **Versione documento:** 1.0
> **Data:** agosto 2026
> **Destinatario:** sviluppo (umano + Claude Code)
> **Origine:** elaborazione concreta del §8 di `PRD-Harmonizer-v1.md`, definita in una
> sessione di discussione dedicata fra l'utente e Claude Code — vedi `handsoff.md`
> sessione 24 per il contesto della decisione.

---

## 0. Come leggere e usare questo documento

Questo documento **non sostituisce** `PRD-Harmonizer-v1.md`: lo **elabora**. Il §8 del
documento principale resta la fonte di verità per l'elenco dei requisiti trasversali
(FR-59/60/61) e per l'esistenza stessa delle tre schermate; qui si fissa **come sono
fatte davvero**, a un livello di dettaglio che il documento principale lascia
volutamente aperto ("lista o griglia", "se non trovano posto nella schermata
principale").

Stesse convenzioni di `PRD-Harmonizer-v1.md` §0:

- `[MUST]` — bloccante per la v1.0
- `[SHOULD]` — desiderabile in v1.0, sacrificabile
- `[V1.1]` — architettura predisposta in v1.0, implementazione successiva
- `[LATER]` — fuori scope, tracciato per non essere reinventato
- `[DECISION]` — scelta aperta, con scadenza indicata

Gli ID `FR-xx` proseguono la numerazione di `PRD-Harmonizer-v1.md` (l'ultimo ID
usato lì è **FR-72**): questo documento introduce **FR-73** in avanti, stessa
numerazione condivisa, nessun conflitto fra i due file.

**Regola per l'agente di coding**: identica a quella del documento principale — se
un'implementazione richiede di discostarsi da quanto scritto qui, va segnalato e
discusso, non deciso unilateralmente.

---

## 1. Filosofia

Il PRD principale, letto alla lettera, chiederebbe una schermata "Principale" già
piuttosto densa (dodici voci in §8.1, incluse le 8 manopole di pan e le 8 di gain per
voce introdotte in sessione 23). Discutendo la struttura reale con l'utente, la
direzione scelta è diversa: **Main non è una versione compressa di tutto**, è un
**sottoinsieme curato** di controlli — quelli che si toccano davvero mentre si suona,
non in fase di preparazione del suono.

Il riferimento esplicito è **Portal, di Output**: una schermata principale
volutamente minimale, con un salto netto — non un semplice scroll — verso una vista
di dettaglio quando serve editare davvero. Applicato a Harmonizer, questo significa
che alcuni controlli che il PRD principale assegna a Main (es. pan/gain per voce)
finiscono invece in **Edit**, e altri che il PRD lascia ambigui (es. il layout della
griglia fondamentale, il knob Dry/Wet, le righe CC) vengono qui specificati in modo
concreto e in parte diverso dalla lettera originale del PRD.

- **FR-73** `[MUST]` Tre schermate — **Main**, **Edit**, **Impostazioni** — con una
  barra di navigazione a 3 pulsanti **sempre visibile su tutte e tre**, non solo
  raggiungibile da Main. Qualunque schermata è quindi a un click da qualunque altra,
  non solo da Main verso le altre due (supera in questo la lettera di §8.1 del
  documento principale, "Accesso alle altre due schermate", rendendola simmetrica) —
  coerente con FR-60 (nessun controllo essenziale a più di un click da Main).

---

## 2. Mappa delle schermate

```
┌─────────────────────────────────────────────┐
│              HARMONIZER — motore PSOLA        │  <- titolo, invariato
│   [ Main ]     [ Edit ]     [ Impostazioni ]   │  <- barra di navigazione, FR-73
├─────────────────────────────────────────────┤
│                                                │
│         (contenuto della schermata attiva)     │
│                                                │
└─────────────────────────────────────────────┘
```

Cambiare schermata **non ridimensiona la finestra**: le tre condividono lo stesso
`setResizeLimits`/dimensione, calcolato sulla più alta delle tre (verosimilmente
Main). Nessuno scatto visivo al cambio vista.

---

## 3. Schermata Main — performance

**FR-74** `[MUST]` Elenco chiuso. Nessun controllo oltre questi senza una decisione
esplicita discussa (coerente con la filosofia del §1 — Main resta piccola per scelta,
non fatica ad esserlo).

```
┌───────────────────┬───────────────────────────┐
│   FONDAMENTALE     │        PRESET              │
│   ┌───┬───┐        │  ①  Maj        CC 1        │
│   │ C │C# │        │  ②  Min        CC 2        │
│   ├───┼───┤        │  ③  Dom        CC 3        │
│   │ D │D# │        │  ④  Sus        CC 4        │
│   ├───┼───┤        │  ⑤  HalfDim    CC 5        │
│   │ E │ F │        │        ⋮ (scroll)          │
│   ├───┼───┤        └───────────────────────────┘
│   │F# │ G │
│   ├───┼───┤
│   │G# │ A │
│   ├───┼───┤
│   │A# │ B │
│   └───┴───┘
├────────────────────────────────────────────────┤
│  (Dry/Wet)   (Stability)   (Fmt Spread)   (Glide)  (Voices) │
├────────────────────────────────────────────────┤
│   [ Bypass ]        [ Play Mode ]                  │
├────────────────────────────────────────────────┤
│   Nota: A4                    Voci attive: 3       │
└────────────────────────────────────────────────┘
```

- **FR-75** `[MUST]` **Griglia fondamentale**: 12 pulsanti cromatici disposti su **2
  colonne × 6 righe** (non più 1×12 come nell'implementazione di sessione 22) —
  indice = `riga×2 + colonna` (riga 0 = C/C#, riga 1 = D/D#, ... riga 5 = A#/B). In
  alto a sinistra.
- **FR-76** `[MUST]` **Selezione preset**: lista con drag&drop per il riordino, i
  primi 5 slot evidenziati (badge numerato, corrispondenza col navigation button
  hardware — lavoro di sessione 22, invariato), valore CC mostrato accanto a ogni
  riga. Mostra **5 righe visibili** alla volta con scorrimento verticale se i preset
  sono di più — non 7 come nell'implementazione attuale, per restare più compatta.
  Vive **SOLO qui**, non è duplicata sulla schermata Edit (scelta esplicita
  dell'utente: un'unica lista, un'unica fonte di verità visiva per il riordino). In
  alto a destra, affiancata alla griglia fondamentale.
- **FR-77** `[MUST]` **Knob Dry/Wet unico** — non due slider indipendenti come oggi.
  Un nuovo parametro `dryWetMix` (0 = tutto dry, 1 = tutto wet, crossfade a potenza
  costante — vedi §6). Sostituisce sulla UI (non nel codice, vedi §6) gli slider Dry
  e Wet separati.
- **FR-78** `[MUST]` **Knob Stability, Num Voices, Fmt Spread, Glide** — tutti
  manopole rotative (non ComboBox, non slider lineari), per coerenza visiva fra loro
  e con lo stile "performance" della schermata.
- Bottoni **Bypass** e **Play Mode** (Harmonizer/Play, FR-24).
- **Nota rilevata**: versione sintetica — solo il nome della nota (es. "A4") o "--"
  se non c'è segnale. La versione diagnostica completa (confidenza, stabile/instabile,
  stato del gate, late-bindings, dimensione del blocco) **non sta qui** — vedi FR-83.
- **Indicatore voci attive** (FR-53, invariato).

---

## 4. Schermata Edit — costruzione del suono

**FR-81** `[MUST]` Elenco chiuso.

```
┌──────────────────────────────────────────────┐
│  Nome preset: [ Maj                        ]   │
│                                                │
│         R   b2   2   b3   3   4  b5   5  ...   │
│   V1    0   -1  -2   -3   0  -1  -2   0  ...   │
│   V2   -2   -3  -4   -5  -4  -5  -6  -3  ...   │
│    ⋮                                            │
│                                                │
│  [Add] [Duplicate] [Delete]                    │
│  [Import CSV] [Export CSV] [Load Global] [Save As Global] │
├──────────────────────────────────────────────┤
│        V1   V2   V3   V4   V5   V6   V7   V8   │
│  Fix   [ ]  [ ]  [ ]  [ ]  [ ]  [ ]  [ ]  [ ]   │
│  Fmt   (o)  (o)  (o)  (o)  (o)  (o)  (o)  (o)   │
│  Pan   (o)  (o)  (o)  (o)  (o)  (o)  (o)  (o)   │
│  Gain  (o)  (o)  (o)  (o)  (o)  (o)  (o)  (o)   │
├──────────────────────────────────────────────┤
│  [ Keep Tails ]                                │
└──────────────────────────────────────────────┘
```

- **Tabella 12×8** editabile (FR §8.2, invariata dall'implementazione di sessione
  15): intestazioni di colonna col grado leggibile, 8 righe voce.
- **Rinomina preset** (campo testo, sopra la tabella).
- **Gestione libreria**: Add, Duplicate, Delete, Import CSV, Export CSV, Load
  Global, Save As Global — tutti insieme, stessa famiglia di azioni.
- **Blocco voci unificato**: le 4 righe × 8 colonne costruite in sessione 23
  (Fix/Move, Fmt/Voice, Pan, Gain) restano **intere e nello stesso ordine verticale**
  — nessuno split fra schermate diverse, tutto il controllo per voce vive qui.
- **Keep Tails** (comportamento di rilascio delle frasi, sessione 10).

---

## 5. Schermata Impostazioni — configurazione e diagnostica

**FR-82** `[MUST]` Elenco chiuso.

```
┌──────────────────────────────────────────────┐
│  CC Root:    [ 20 ]  [ Learn ]                 │
│  CC Chord:   [ 21 ]  [ Learn ]                 │
│  CC Bypass:  [ 22 ]  [ Learn ]                 │
│                                                │
│  MIDI Channel: [ Omni ▾ ]                      │
│  Voice Cap:    [ 32     ]                      │
│                                                │
│  Detected: A4 (440.02 Hz, conf 0.97) stable    │
│            gate open  late-bindings 0  blk 512  │
└──────────────────────────────────────────────┘
```

- **FR-79** `[MUST]` **Righe CC**: nome del CC + **casella di testo numerica**
  (0-127, sola immissione da tastiera — non uno slider trascinabile come
  nell'implementazione attuale) + pulsante **Learn** (MIDI Learn, invariato — resta
  perché resta più comodo con un controller reale rispetto a scrivere il numero a
  memoria).
- **MIDI Channel** (Omni/1-16, ComboBox, invariato).
- **Tetto voci simultanee** (`maxSimultaneousVoices`, FR-51, invariato).
- **FR-83** `[MUST]` **Nota rilevata — versione diagnostica completa**: la stringa
  con confidenza, stabile/instabile, stato del gate, contatore late-bindings,
  dimensione del blocco host — esattamente il testo già presente oggi, spostato qui.
  Su Main resta solo il nome nota (FR-76 — vedi §3).

---

## 6. Parametri e comportamento — dettaglio tecnico

### 6.1 `dryWetMix` (FR-77)

Nuovo parametro APVTS, `AudioParameterFloat` 0..1. Legge di crossfade a **potenza
costante** (stessa tecnica già in uso per il pan per voce, `Voice.cpp`, sessione 23):

```
dry = cos(mix * pi/2)
wet = sin(mix * pi/2)
```

I parametri esistenti `dryLevel`/`wetLevel` **restano dichiarati nel codice per
sempre** — CLAUDE.md regola 6, un ID di parametro pubblicato non si rimuove mai — ma
**smettono di essere letti** in `processBlock`: `dryWetMix` li sostituisce
interamente nel calcolo del guadagno dry/wet effettivo. La logica di bypass esistente
(bypass = dry pieno, wet zero) resta identica, applicata dopo il calcolo del
crossfade.

**Questo è un cambiamento reale del comportamento sonoro**, non solo della UI: oggi
dry e wet sono due manopole indipendenti che possono stare entrambe al massimo
contemporaneamente (somma libera, non crossfade); con `dryWetMix` questo non è più
possibile per costruzione. Il default proposto è **0.7** (un punto di partenza
ragionevole per restare vicino al bilanciamento "wet in evidenza" di oggi, entro i
limiti di un vero crossfade) — **non è un valore calcolato, va confermato
all'ascolto** (CLAUDE.md regola 12) prima di considerarlo definitivo. Potrebbe
servire un aggiustamento del default o della curva stessa (es. non equal-power ma
un'altra legge) dopo il primo ascolto reale.

### 6.2 Knob su parametri esistenti (FR-78)

Stability, Num Voices, Fmt Spread, Glide diventano manopole rotative senza alcun
cambiamento ai parametri APVTS sottostanti: sono lo stesso meccanismo di attach
(`SliderAttachment`) già in uso oggi per Num Voices e Voice Cap, esteso a Stability
(oggi una `ComboBox` con `ComboBoxAttachment` — `AudioParameterChoice` espone un
range normalizzabile compatibile con `SliderAttachment` esattamente come un
`AudioParameterInt`, quindi il cambio di widget non richiede toccare il modello dei
parametri). Il testo del knob Stability mostra il nome del livello (Fast/Balanced/...),
non un numero grezzo.

### 6.3 Griglia fondamentale 2×6 (FR-75)

Nessun cambiamento al parametro `rootNote` (resta un `AudioParameterChoice` a 12
valori) — cambia solo come `ui::RootNoteGrid` calcola quale cella sta sotto il
mouse e come disegna le 12 celle (da una riga di 12 a una griglia 2×6).

---

## 7. Fuori scope di questa revisione

- **FR-80** `[SHOULD]` Auto-scroll della lista preset durante il trascinamento,
  quando il mouse si avvicina al bordo superiore/inferiore del viewport (comportamento
  "stile smartphone"). Rimandato esplicitamente: la lista a 5 righe visibili (FR-76)
  userà per ora lo stesso scroll manuale e lo stesso drag&drop a un passo alla volta
  già funzionanti oggi, solo su un'area più piccola.
- **Indicatore e gestione licenza reale**: `src/licensing/` è vuoto, è lavoro di M6.
  Nessun placeholder finto viene aggiunto alla UI per non fabbricare funzionalità che
  non esiste — la voce resta assente, non simulata.
- **"Comportamento latenza"** (§8.3 del documento principale): nessun controllo
  esiste oggi per questo (il comportamento è oggi automatico, legato a Stability e al
  gating su stop del transport, FR-56/57) — non se ne costruisce uno finto qui.
- **FR-59** (ridimensionamento 70%-200%, HiDPI/Retina) e **FR-61** (tema chiaro/scuro)
  del documento principale: voci separate della roadmap, invariate da questo documento.

---

## 8. Note per l'implementazione

Da tenere a mente quando questo documento verrà tradotto in codice (non in questa
sessione):

- **Riusabili così come sono**: `ui::PresetListEditor` (nessun cambiamento
  funzionale, solo l'altezza del viewport che lo contiene, 5 righe invece di 7),
  `ui::PresetTableEditor` (invariato), tutto il meccanismo di sincronizzazione
  parametro→UI via `Timer` già esistente in `PluginEditor`.
- **Da modificare**: `ui::RootNoteGrid` (reshape da 1×12 a 2×6 — cambia `cellAt()` e
  `paint()`, non l'API pubblica); `PluginProcessor.cpp` (nuovo parametro `dryWetMix`,
  lettura di `dryLevel`/`wetLevel` rimossa da `processBlock` ma parametri mai
  rimossi dalla `ParameterLayout`); `PluginEditor.h/.cpp` (riorganizzazione estesa:
  tre container per le tre schermate, barra di navigazione, i controlli esistenti
  cambiano genitore ma non la loro logica di wiring/attachment).
- **Beneficio collaterale atteso**: l'attuale pannello piatto è alto 1428px al
  minimo. Dividendo i controlli su 3 schermate invece che in un'unica colonna, ogni
  schermata dovrebbe risultare molto più bassa — stima approssimativa 550-650px per
  la più alta delle tre (verosimilmente Main), da verificare scrivendo il codice.
- Nessuna suite di test in `tests/` è interessata da questi cambiamenti (sono tutti
  headless, non toccano `PluginEditor`/`PluginProcessor` a livello di UI) — la sola
  eccezione è `dryWetMix`, che tocca `processBlock`: se in futuro si vorrà misurare
  per calcolo la legge di crossfade (non solo verificarla all'ascolto), andrà scritto
  un test dedicato prima di dichiararla completa, stesso schema già in uso per il pan
  per voce (`tests/voice_test.cpp`, T-1/T-2/T-3).
