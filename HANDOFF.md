# HANDOFF — HARMONIZER

> Ultimo aggiornamento: **2026-08-11**, sessione 32.
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
  confermato stabile all'ascolto (s.26). **Il motore è off-limits** per scelta esplicita
  dell'utente (s.31): i difetti si correggono a monte.
- **M6 (licensing) non esiste**: `src/licensing/` è una cartella vuota.
- **8** suite `ctest` verdi. `pluginval --strictness-level 10` SUCCESS su VST3 (Win).
- **Il motore di pitch shifting è CHIUSO** (D-19, s.32): *"il timbro è corretto e gli attacchi
  pure"*. Si chiude la catena che ha occupato le sessioni 12→32 — B-02 (timbro), B-04 (buco),
  **B-13** (attacco sporcato dai gradi intermedi, s.31) e **B-14** (offset in ritardo, s.32).
  Non si tocca più senza un sintomo nuovo **riportato all'ascolto**.
- Il lavoro di B-14: la nota più grave d'analisi è un **parametro utente** (scelta dello
  strumento, 10 voci dal più acuto al più grave, default **Voice Male E2**), l'attesa del latch
  si misura in **frame d'analisi**, e l'aggancio si aggiorna **a ogni stima nuova** invece che
  una volta per blocco. Misurato a 1024 campioni: **79.1 → 44.3 ms**, con **0 corse di
  passaggio** su 24 configurazioni.
- **Trombone risolto per inciso**: Ab1 = 51.9 Hz stava *sotto* i 60 Hz cablati fino a s.31,
  quindi le sue note gravi non venivano rilevate affatto.

---

## Prossimo passo

**Uno solo: FR-59, la scala 70–200% + HiDPI.** Chiuso il fronte DSP (D-19), è l'ultima voce
nominata nel contenuto di M5 in PRD §12 (*"Le tre schermate, drag and drop, editor tabella,
**scaling**"*), è `[MUST]`, ed è più piccola di come sembra: la finestra è **già
ridimensionabile**, manca la scala percentuale.

Da decidere prima di scrivere: scala uniforme via `AffineTransform` su un layout logico fisso
900×660 (lettura letterale di FR-59, HiDPI gratis, si perde il reflow libero) oppure tenere
separati i due gradi di libertà. Porta con sé il difetto dell'altezza minima (Keep Tails,
sotto).

---

## In attesa di conferma all'ascolto

| Cosa | Dove | Stato |
|---|---|---|
| **La conferma di B-14 è stata dal vivo, non su export** | — | Data in Ableton sulla build delle 12:40; in `SAMPLE TEST/` non esiste alcun `_02`. I numeri (79.1 → 44.3 ms) restano quindi **verificati per calcolo**, e con loro la rete anti-regressione di B-13 (Test#2 ≡ Test#3) sulla build nuova. Se la flem tornasse, il primo passo è quell'export mancante. |
| `kSettleFrames = 1.5` | `src/harmony/PitchLatch.h` | L'attesa in frame d'analisi. A 60 Hz vale esattamente i 25 ms di s.31. **Misurato, non tarato all'ascolto**: se il cambio d'armonia sembrasse in ritardo, è la prima manopola. |
| Default **E2 (Voice Male)** | `PluginProcessor.h` | Scelto misurando: l'impostazione più pronta che resta **pulita su tutte e quattro** le tabelle. Ab2 sarebbe 12 ms più veloce ma riapre in piccolo B-13. |
| **B-10 con Keep Tails ON** | `PhraseScheduler.cpp` | B-07, B-10 e B-12 confermati e chiusi (s.30), ma la conferma fu **generale**, non su questa configurazione, dove il ramo di B-10 si applica anche alle code (tensione con FR-46). |
| Isteresi cella vuota, `kEmptyCellHoldMs = 80.0f` | `src/voices/EmptyCellHold.h` | Committata in `be9a40f`. Misurata **irrilevante** sul materiale reale. La soglia non è mai stata tarata né ascoltata. |

---

## Limiti noti

Da non scambiare per requisiti soddisfatti.

- **Le voci acute della lista strumenti non sono verificate nel loro registro.** Sopra Eb Alto
  Sax la misura mostra 1-3 offset di passaggio, e **nessuna taratura dell'attesa li elimina**:
  una finestra corta rende il rilevatore più rumoroso. Ma il file di prova suona C4-D4-E4,
  cioè il registro **grave** per un flauto o un soprano sax — il dato potrebbe non valere
  quando suonano davvero nel proprio. **Serve un export dedicato** (flauto o tromba nel loro
  registro) per deciderlo. Le voci restano in lista per scelta dell'utente.
- **Il ritardo non va a zero.** Restano la convergenza del rilevatore e il confine di blocco.
  Azzerarlo richiede il **lookahead** — ritardare l'audio e dichiarare la latenza all'host —
  rimandato per scelta dell'utente in s.32: contrasta con PRD §1.3 (≤ 15 ms nel modo più
  reattivo) e va aperto come decisione a sé.
- **La quantizzazione al blocco resta** (A-05): l'istante in cui l'unico cambio atterra cade su
  un confine di blocco (fino a 23 ms a 1024). I sotto-blocchi in `processBlock` sono
  deliberatamente fuori scope: sono il cuore del motore.
- **FR-59 — manca la SCALA, non il ridimensionamento.** La finestra è **già
  ridimensionabile**: `PluginEditor.cpp:392` `setResizable(true,true)` + `:399`
  `setResizeLimits(520,620,1200,900)`. Non esiste la scala percentuale 70–200%: zero
  occorrenze di `setScaleFactor`/`AffineTransform` in `src/`, costanti di layout hardcoded,
  dimensione dell'editor non serializzata. HiDPI/Retina mai verificato.
- **Keep Tails irraggiungibile a finestra minima**: a 620 px `layoutEdit()` chiede ~528 px su
  522 disponibili e `keepTailsToggle` (`PluginEditor.cpp:592`) collassa ad altezza ~0.
  Nessun viewport verticale. Trovato in s.30, mai corretto.
- **La regola utente di D-15 non va scritta com'è.** *"Compilare tutte le celle"* è
  **controproducente** sugli attacchi: Test#3 ha le celle piene ed era peggio di Test#2 che
  le ha vuote (D-17). Con B-13 chiuso va deciso se quella regola serva ancora.
- **Preset di fabbrica non verificati**: dei 7, **solo "Min"** è confrontato col prototipo
  Max4Live. Gli altri 6 sono voicing jazz generici scritti algoritmicamente.
- **CI copre 3 delle 8 suite** e non invoca mai `ctest`; `phrase_scheduler` non è nemmeno
  compilabile con quei `g++` (D-16). Vedi A-06.
- **Il tetto voci simultanee ha lo stesso difetto di B-10** (B-11): abbassare
  `maxSimultaneousVoices` non spegne gli slot già assegnati. Oggi non morde (default 32 su
  32). Serve una politica su quale frase perde slot.
- **La UI non riflette un override CC attivo** (FR-36/37): il CC non scrive nel parametro
  APVTS (`PluginEditor.cpp:790-795`).
- **Formanti mai tarate**: `k = 0.3` non è mai passato per l'ascolto (T-6/T-7 verificano che i
  knob *arrivino*), e i due setter scrivono il float grezzo senza `Glide` — se si sentisse un
  click girandoli, è un'entry propria.
- **CC e Play mode mai provati con hardware reale**: il parsing MIDI di `CcRouter` è scoperto.
- **Nessuna `LookAndFeel` custom** (D-10) · **CPU mai profilata** con 8 voci contro il budget
  ≤15% del PRD §1.3 · **Catch2 mai adottato**: i test sono `int main()` scritti a mano.
- **Sporcizia nell'artefatto VST3**: dentro il bundle c'è
  `Contents/x86_64-win/Harmonizer (1).vst3` del 30/07, residuo di una copia vecchia.
  Probabilmente inerte. Trovato in s.31, non toccato.

---

## Questioni aperte

Nessuna di queste è tecnica; nessuna è chiusa.

- **La guida utente non esiste** — nessun README utente, nessuna `docs/`, nessun tooltip
  in-app. Deve ospitare il modello CC posizionale (D-03) e, da s.32, **la scelta dello
  strumento**: cosa succede se si suona sotto la nota dichiarata (il rilevatore non aggancia)
  e perché scendere nella lista rallenta l'armonia. In tensione col criterio d'uscita di M5
  (PRD §12: *"un tester esterno usa il plugin senza documentazione"*).
- Nome prodotto, marchio, dominio → bloccano `PLUGIN_MANUFACTURER_CODE` (`Hzso`),
  `PLUGIN_CODE` (`Hmz1`), `COMPANY_NAME` (`"TBD"`), `BUNDLE_ID`. **Cambiarli dopo il
  rilascio rompe i progetti salvati**, come il tipo AU.
- **Certificati di firma e notarizzazione** (Apple Developer ID + code signing Windows):
  il lead time più lungo del progetto, previsto in M0, mai avviato.
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
| `LOG/archivio-s01-s28.md` · `LOG/sessione-NN.md` | Il racconto per esteso |

**Materiale di test** (`SAMPLE TEST/`, non versionato): `Test 1 - Basic Silk Horns.wav`
(C4-D4-E4-C4, ~2 s per nota) e `Test 2 - E-Piano.wav` sono i **dry sorgente**;
`DBG Timbro/` contiene gli export dell'utente e le reference Autoshift.
**Nome degli export** (s.31): `exp#<N>_<Preset>_<Voce>_<Versione>.wav`, con `exp#1` =
Dry/Wet 1 (solo wet), Balanced, Fmt Spread 0, Glide 0 ms, Voices 4, Gain/Voice a 0 tranne la
voce isolata; `REF_<nome>` è il riferimento fatto con Autoshift. Buffer 1024, Focusrite ASIO.

**Comandi** · `cmake --build build --config Release` (il fallimento della copia in
`Program Files` è atteso, D-12: filtrare `error C####`/`error LNK`) · **poi**
`--target Harmonizer_Standalone`, che quel fallimento salta · `ctest -C Release`
· `tools/pluginval.exe --strictness-level 10 --validate <path.vst3>`
· `build/Release/degree_trace_probe.exe "<dry.wav>" "<12 celle>" <root> <block> <notaMin>`
