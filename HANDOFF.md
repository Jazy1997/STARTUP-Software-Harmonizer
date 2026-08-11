# HANDOFF — HARMONIZER

> Ultimo aggiornamento: **2026-08-10**, sessione 30.
> Questo file descrive **solo lo stato di oggi**. La storia sta in `LOG/`, i sintomi
> aperti in `BUGS.md`, le decisioni durature in `DECISIONS.md`, la mappa dei moduli
> in `MAPPA.md`. Fonte di verità del prodotto: `PRD-Harmonizer-v1.md` + `PRD-UI.md`.
> **Tetto: ~150 righe.** Se cresce, va potato — non è un archivio.

---

## Stato

Plugin armonizzatore per strumenti monofonici (VST3 / AU / Standalone). Calcola
`d = (notaMIDI − fondamentale) mod 12` e legge gli offset delle 8 voci da una tabella
12×8 editabile dall'utente.

- **Milestone reale: M5 (UI), circa 80%.** M0→M4 costruiti e funzionanti.
- Il plugin **gira in VST3 su Ableton e l'utente lo giudica soddisfacente** (s.29).
- Motore: PSOLA proprietario dietro `PitchShifter` astratto. Timbro a nota tenuta
  confermato stabile all'ascolto (s.26).
- UI: tre schermate Main / Edit / Impostazioni con barra di navigazione sempre visibile.
- **M6 (licensing) non esiste**: `src/licensing/` è una cartella vuota.
- **8** suite `ctest` verdi (s.30: nuova `phrase_scheduler`, prima copertura di quel modulo —
  D-16). `pluginval --strictness-level 10` SUCCESS su VST3 (Win) e verde su AU (CI macOS).
- **I sintomi dei buchi sono sospesi per decisione dell'utente** (s.30): B-05 e B-06 passano
  a `SOSPESO`, la mitigazione è documentale — *"vanno compilate tutte le celle delle voci
  che si vogliono attivare"*. La discrepanza fra questa regola e le misure di B-05 è
  annotata dentro l'entry: leggerla prima di riaprire.
- Working tree **pulito**, `main` allineato a `origin/main`. Il lavoro di s.30 è in
  `ab79ae7` (FR-42), `5c985e3` (FR-19) e nel commit di documentazione che li segue.

---

## Prossimo passo

**Uno solo: FR-59, la scala 70–200% + HiDPI.** È l'ultima voce nominata nel contenuto di M5
in PRD §12 (*"Le tre schermate, drag and drop, editor tabella, **scaling**"*), è `[MUST]`, ed
è più piccola di come questo file la descriveva fino a s.29 — vedi limiti noti: la finestra è
**già ridimensionabile**, manca la scala percentuale.

Da decidere prima di scrivere: scala uniforme via `AffineTransform` su un layout logico fisso
900×660 (lettura letterale di FR-59, HiDPI gratis, si perde il reflow libero) oppure tenere
separati i due gradi di libertà. Porta con sé il difetto dell'altezza minima qui sotto.

---

## In attesa di conferma all'ascolto

Regola 12 di `CLAUDE.md`: nulla che tocchi il suono si dichiara completo per calcolo.

| Cosa | Dove | Stato |
|---|---|---|
| **B-10 con Keep Tails ON** | `PhraseScheduler.cpp`, loop di rendering | B-07, B-10 e B-12 sono confermati all'ascolto e **chiusi** (s.30). Ma la conferma è stata **generale**, non su questa configurazione, dove il ramo di B-10 si applica anche alle code: è la scelta di progetto più discutibile del fix (tensione con FR-46). |
| Isteresi cella vuota, `kEmptyCellHoldMs = 80.0f` | `src/voices/EmptyCellHold.h` | **Committata** in `be9a40f`. Misurata **irrilevante** sul materiale reale (le attese vere durano 2136 ms e 279 ms, quindi soglia 0 e soglia 80 ms danno output identico). La soglia di 80 ms **non è mai stata tarata** e non è mai passata per l'ascolto. |

---

## Limiti noti

Da non scambiare per requisiti soddisfatti.

- **FR-59 — manca la SCALA, non il ridimensionamento.** La finestra è **già
  ridimensionabile**: `PluginEditor.cpp:392` `setResizable(true,true)` + `:399`
  `setResizeLimits(520,620,1200,900)`. Quello che non esiste è la scala percentuale
  70–200%: zero occorrenze di `setScaleFactor`/`AffineTransform` in `src/`, tutte le
  costanti di layout sono pixel hardcoded (quindi oggi è *reflow*, non *scaling*), e la
  dimensione dell'editor non è serializzata. HiDPI/Retina mai verificato.
  *(Corregge la voce "finestra a dimensione fissa, nessun ridimensionamento" che questo
  file riportava fino a s.29: era falsa.)*
- **Keep Tails irraggiungibile a finestra minima**: a 620 px di altezza `layoutEdit()`
  chiede ~528 px di contenuto su 522 disponibili, e `keepTailsToggle`
  (`PluginEditor.cpp:592`) collassa ad altezza ~0. Nessun viewport verticale sulle pagine,
  quindi l'utente non lo recupera se non allargando. Trovato in s.30, mai corretto.
- **Le colonne NON sono tagliate** — voce rimossa perché falsa su entrambi i componenti.
  `RootNoteGrid` è 2×6 **per requisito** (FR-75, reshape in `39c3282`);
  `PresetTableEditor.cpp:120,129` divide la larghezza disponibile per 12, quindi le 12
  colonne ci sono sempre tutte e si **comprimono** (69 px a 900, 38 px a finestra minima).
  Il limite reale è di leggibilità alle larghezze piccole, non di raggiungibilità.
- **Preset di fabbrica non verificati**: dei 7, **solo "Min"** è confrontato col prototipo
  Max4Live. Gli altri 6 sono voicing jazz generici scritti algoritmicamente.
- **CI copre 3 delle 8 suite**: `build.yml` ricompila a mano con `g++` e non invoca mai
  `ctest`. Fuori CI: `glide`, `cell_input_parser`, `voice`, `empty_cell_hold`,
  `phrase_scheduler`, e ogni target futuro — quindi **tutto il lavoro di s.30**. La nuova
  `phrase_scheduler` non è nemmeno *compilabile* con quei `g++` (linka `juce_core`, D-16):
  passare a `ctest` non è più solo più comodo, è l'unico modo di coprirla. Vedi A-06.
- **Il tetto voci simultanee ha lo stesso difetto di B-10** (B-11): abbassare
  `maxSimultaneousVoices` non spegne gli slot già assegnati. Oggi non morde (default 32 su
  32). Non riparato con B-10 perché è un tetto globale fra frasi e serve una politica.
- **La UI non riflette un override CC attivo** (FR-36/37): il CC non scrive nel parametro
  APVTS, quindi la griglia fondamentale non si aggiorna (`PluginEditor.cpp:790-795`).
- **Formanti mai tarate**: `k = 0.3` non è mai passato per l'ascolto. T-6/T-7 verificano
  che i knob *arrivino*, non che il valore *sia giusto*.
- **Knob formanti senza rampa**: `Voice::setFormantSpread`/`setFormantOffsetSemitones`
  scrivono il float grezzo, senza `Glide` (a differenza di gain e pan). Girare il knob è un
  gradino nel rapporto formantico. Vale già in Harmonizer; se si sentisse un click, aprire
  un'entry propria in `BUGS.md`.
- **CC e Play mode mai provati con hardware reale**: `override_manager_test` copre la
  precedenza, il parsing MIDI di `CcRouter` no.
- **Nessuna `LookAndFeel` custom** (D-10, rimandata di proposito): forma dei knob, colori,
  font — lavoro mai iniziato. `paint()` disegna ancora un titolo segnaposto.
- **CPU mai profilata** con 8 voci contro il budget ≤15% del PRD §1.3.
- **Catch2 mai adottato** (previsto da PRD §9.1): i test sono `int main()` scritti a mano.

---

## Questioni aperte

Nessuna di queste è tecnica; nessuna è chiusa.

- **La guida utente non esiste** — nessun README utente, nessuna `docs/`, nessun tooltip
  in-app: i 7 `.md` alla radice sono tutti documenti di processo. È ora il posto designato
  per la regola *"compila tutte le celle delle voci che vuoi attivare"* (s.30) e per il
  modello CC posizionale (D-03, che il PRD §13 segnala come rischio di confusione).
  **In tensione col criterio d'uscita di M5** (PRD §12: *"un tester esterno usa il plugin
  senza documentazione"*): se la regola serve, andrebbe resa evidente anche dalla UI.
- Nome prodotto, marchio, dominio → bloccano `PLUGIN_MANUFACTURER_CODE` (`Hzso`),
  `PLUGIN_CODE` (`Hmz1`), `COMPANY_NAME` (`"TBD"`), `BUNDLE_ID`. **Cambiarli dopo il
  rilascio rompe i progetti salvati**, come il tipo AU.
- **Certificati di firma e notarizzazione** (Apple Developer ID + code signing Windows).
  Il PRD li vuole avviati in M0: nessuna traccia che sia stato fatto. È il lead time
  più lungo del progetto.
- Tipo di licenza JUCE (Indie vs commerciale) in funzione del fatturato previsto.
- `[DECISION]` Backend di licensing, scadenza M5 — vedi `DECISIONS.md` § aperte.
- Prezzi dei tre tier; canale di vendita; consegna licenza nel bundle hardware.

---

## Puntatori

| File | Cosa |
|---|---|
| `PRD-Harmonizer-v1.md` | Fonte di verità, FR-01..FR-72 |
| `PRD-UI.md` | Elabora §8, FR-73..FR-83 |
| `CLAUDE.md` | Regole non negoziabili + ciclo di vita dei documenti |
| `BUGS.md` | Un'entry per sintomo, con ID stabile e storia |
| `DECISIONS.md` | Decisioni durature e perché |
| `MAPPA.md` | Mappa dei moduli com'è adesso |
| `LOG/archivio-s01-s28.md` | Il racconto per esteso delle sessioni 1–28 |
| `LOG/sessione-NN.md` | Racconto per esteso, dalla sessione 29 in poi |

**Materiale di test** (`SAMPLE TEST/`, non versionato): `Test 1 - Basic Silk Horns.wav`
e `Test 2 - E-Piano.wav` sono i **dry sorgente** delle sonde; `DBG Timbro/` contiene i
riferimenti forniti dall'utente. Gli intermedi rigenerabili sono stati eliminati in s.29.

**Comandi**: `ctest -C Release` (7 suite) · `cmake --build build --config Release`
· `tools/pluginval.exe --strictness-level 10 --validate <path.vst3>`
