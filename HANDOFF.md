# HANDOFF — HARMONIZER

> Ultimo aggiornamento: **2026-08-11**, sessione 31.
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
- **s.31: gli attacchi sporchi avevano una causa trovata e corretta — B-13, ora CHIUSO.**
  `PitchLatch` si spostava di un semitono per chiamata (una per blocco), quindi su ogni salto
  di nota attraversava i gradi intermedi e ne **suonava davvero** gli offset. Ora 0 corse di
  passaggio su tutte le tabelle e a tutti i block size, e **confermato all'ascolto** in s.32
  (*"test#3 e test#2 ora suonano uguali"*).
- **La stessa conferma ha aperto B-14**: l'offset corretto arriva **~85 ms dopo** l'attacco,
  quindi la nota parte con l'offset di quella precedente. Misurato sull'export contro la REF.
  Causa dominante: la finestra d'analisi di Cycfi Q, derivata dalla frequenza minima (60 Hz
  oggi → finestra 33.4 ms, una stima ogni 16.7 ms).

---

## Prossimo passo

**Uno solo: B-14, accorciare il ritardo con cui l'offset corretto arriva.** La leva è la
frequenza minima passata a Cycfi Q, oggi fissa a 60 Hz (= B1), molto più in basso di quanto
serva a voce/sax/tromba — e **più in basso del Trombone (Ab1 = 51.9 Hz)**, che oggi quindi
non viene nemmeno rilevato nelle note gravi.

Deciso con l'utente (s.32): diventa un **parametro utente** con 10 strumenti in lista, dal
più acuto al più grave (l'ordine *è* l'informazione: più si scende, più cresce il ritardo),
default **Bb Tenor Sax = Ab2**. Il **lookahead** — ritardare l'audio e dichiarare la latenza,
l'unica strada che azzererebbe il ritardo — è **rimandato di proposito**: prima si sente
quanto si guadagna senza toccare la latenza.

Dopo B-14 il passo torna a essere **FR-59, la scala 70–200% + HiDPI**: ultima voce di M5 in
PRD §12, `[MUST]`, e più piccola di come sembrava (la finestra è già ridimensionabile, manca
la scala percentuale).

---

## In attesa di conferma all'ascolto

| Cosa | Dove | Stato |
|---|---|---|
| `kNoteSettleMs = 25.0f` | `src/harmony/PitchLatch.h` | Scelto più lungo dei **14.5 ms** di stima confidente ma sbagliata misurati a un cambio di nota reale, con margine. **Misurato, non tarato all'ascolto.** Contribuisce a B-14: va riespresso in **frame d'analisi** del rilevatore, così scala con lo strumento scelto. |
| **B-10 con Keep Tails ON** | `PhraseScheduler.cpp` | B-07, B-10 e B-12 confermati e chiusi (s.30), ma la conferma fu **generale**, non su questa configurazione, dove il ramo di B-10 si applica anche alle code (tensione con FR-46). |
| Isteresi cella vuota, `kEmptyCellHoldMs = 80.0f` | `src/voices/EmptyCellHold.h` | Committata in `be9a40f`. Misurata **irrilevante** sul materiale reale. La soglia non è mai stata tarata né ascoltata. |

---

## Limiti noti

Da non scambiare per requisiti soddisfatti.

- **La quantizzazione al blocco resta** (A-05). B-13 garantisce *quali* offset si applicano —
  mai più gradi intermedi, a qualunque buffer — non l'istante esatto in cui l'unico cambio
  atterra, che cade su un confine di blocco (fino a 23 ms a 1024, 3 ms a 128). I sotto-blocchi
  in `processBlock` sono deliberatamente fuori scope: sono il cuore del motore.
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
  le ha vuote (D-17). Dopo la conferma di B-13 va deciso se quella regola serva ancora.
- **Preset di fabbrica non verificati**: dei 7, **solo "Min"** è confrontato col prototipo
  Max4Live. Gli altri 6 sono voicing jazz generici scritti algoritmicamente.
- **CI copre 3 delle 8 suite**: `build.yml` ricompila a mano con `g++` e non invoca mai
  `ctest`. Fuori CI: `glide`, `cell_input_parser`, `voice`, `empty_cell_hold`,
  `phrase_scheduler`, e ogni target futuro. `phrase_scheduler` non è nemmeno *compilabile*
  con quei `g++` (linka `juce_core`, D-16). Vedi A-06.
- **Il tetto voci simultanee ha lo stesso difetto di B-10** (B-11): abbassare
  `maxSimultaneousVoices` non spegne gli slot già assegnati. Oggi non morde (default 32 su
  32). Serve una politica su quale frase perde slot.
- **La UI non riflette un override CC attivo** (FR-36/37): il CC non scrive nel parametro
  APVTS (`PluginEditor.cpp:790-795`).
- **Formanti mai tarate**: `k = 0.3` non è mai passato per l'ascolto. T-6/T-7 verificano che
  i knob *arrivino*, non che il valore *sia giusto*.
- **Knob formanti senza rampa**: `Voice::setFormantSpread`/`setFormantOffsetSemitones`
  scrivono il float grezzo, senza `Glide`. Se si sentisse un click, aprire un'entry propria.
- **CC e Play mode mai provati con hardware reale**: il parsing MIDI di `CcRouter` è scoperto.
- **Nessuna `LookAndFeel` custom** (D-10, rimandata di proposito).
- **CPU mai profilata** con 8 voci contro il budget ≤15% del PRD §1.3.
- **Catch2 mai adottato** (previsto da PRD §9.1): i test sono `int main()` scritti a mano.
- **Sporcizia nell'artefatto VST3**: dentro il bundle c'è
  `Contents/x86_64-win/Harmonizer (1).vst3` del 30/07, residuo di una copia vecchia.
  Probabilmente inerte. Trovato in s.31, non toccato.

---

## Questioni aperte

Nessuna di queste è tecnica; nessuna è chiusa.

- **La guida utente non esiste** — nessun README utente, nessuna `docs/`, nessun tooltip
  in-app. Deve ospitare il modello CC posizionale (D-03) e, **solo se dopo B-13 serve
  ancora**, la regola sulle celle da compilare. In tensione col criterio d'uscita di M5
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
`Program Files` è atteso, D-12: filtrare `error C####`/`error LNK`) · `ctest -C Release`
· `tools/pluginval.exe --strictness-level 10 --validate <path.vst3>`
· `build/Release/degree_trace_probe.exe "<dry.wav>" "-7,-2,-7,-2,-7,,,,,,," 0 1024`
