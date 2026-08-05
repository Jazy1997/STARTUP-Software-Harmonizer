# Handoff — HARMONIZER

> Ultimo aggiornamento: 2026-08-05 (sessione 22)

---

## 1. Goal

Costruire **HARMONIZER**: un plugin armonizzatore (VST3 / AU / Standalone) per strumenti monofonici — sax, tromba, voce — pensato per il palco.

Invece di trasporre a intervalli fissi, calcola la relazione tra la nota suonata e l'accordo corrente:

```
d = (notaMIDI − fondamentale) mod 12
```

e usa `d` per leggere un offset (in semitoni, per ognuna delle 8 voci) da una tabella 12×8 editabile dall'utente (**preset armonico**). L'armonia è dato dell'utente, non teoria imposta dal codice.

Punti chiave del prodotto (dettagli completi in `PRD-Harmonizer-v1.md`):
- Priorità assoluta alla reattività: latenza ≤ 15 ms nella modalità più rapida.
- Fix/Move per voce: le voci possono seguire (Move) o ignorare (Fix) vibrato/bending dell'esecutore.
- Modalità Play: armonizzazione pilotata da note MIDI in ingresso invece che dalla tabella.
- Motore di frasi/pattern ritmico (architettura obbligatoria in v1.0, UI in v1.1).
- Correzione formanti automatica in funzione dello shift.
- Controllo hardware via 3 CC configurabili, con CC del preset = posizione in lista (posizionale, non ID).
- Stack: JUCE 8.x, C++20, CMake (JUCE CMake API), Cycfi Q (pitch detection), PSOLA proprietario dietro interfaccia astratta, Signalsmith Stretch come motore alternativo.

Fonte di verità: `PRD-Harmonizer-v1.md` (v1.0, luglio 2026). In caso di conflitto tra questo file e il PRD, vince il PRD.

---

## 2. Stato attuale

**Fase: M0 completo dal punto di vista tecnico (restano solo licenza JUCE, certificati, nome prodotto — decisioni non tecniche, vedi §6). Vertical slice DSP M1/M2/M3 in corso su richiesta esplicita dell'utente: PresetLibrary (M2), Fix/Move+Glide+Stability (M1) e motore a frasi (M3, FR-43..53) sono completi e funzionali. Sessione 9: il PSOLA proprietario scoperto in sessione 8 e' stato PORTATO E INTEGRATO come motore di default dietro `PitchShifter`. Sessione 10: PRIMO TEST REALE in Ableton di tutto il lavoro di sessione 9 (PSOLA, Formanti, CC, Play) — trovato e corretto un bug reale nel ciclo di vita delle frasi, due bug di UI, aggiunta diagnostica. Sessione 11: canto legato non aggiornava l'armonizzazione — due bug distinti, non uno: isteresi di intonazione mancante (identificato dall'utente, corretto) E `freeAllPhrases()` innescato dalla confidenza del pitch invece che dalla presenza del segnale (la mia ipotesi originale, rivelatasi comunque necessaria dopo il primo fix). Sessione 12: causa delle "note saltate senza una logica precisa" (segnalata a fine sessione 11) confermata a lettura di codice — corsa fra `OnsetDetector` e `PitchDetector`, con una seconda causa concorrente (`pitchDetector` mai resettato al silenzio) — vedi sotto. **CONFERMATO ALL'ASCOLTO dall'utente**: "Active" non resta piu' a zero, armonizza sempre tutte le note, nessuna persa per strada. Sessione 12 (continuazione) — feedback utente sul timbro ("non fedele al segnale sorgente, robotico e granuloso"): trovato e corretto un bug reale in `PsolaShifter::emitGrain` (la correzione formantica automatica di `Voice.cpp`, attiva di default, accorciava i grani di sintesi sotto il minimo necessario alla sovrapposizione) — confermato con un nuovo test numerico (Test 8) che falliva PRIMA del fix e passa dopo, mentre tutti i test preesistenti restano bit-per-bit invariati. Sessione 12 (continuazione) — utente riporta "scricchiolii, click, glitch" e armonizzazione "non stabile al 100%": trovato un bug architetturale — QUALUNQUE voce smetteva di essere processata (fine frase, silenzio totale, cella tornata vuota su una frase viva/FR-17, uscita da Play mode) veniva tagliata di ampiezza piena a zero in un solo blocco, senza dissolvenza. Aggiunta una breve dissolvenza di ampiezza (8ms) per ogni voce, con un rilascio "morbido" invece che istantaneo per le frasi; stesso trattamento per il gain dry/wet/bypass (anch'esso applicato prima come salto istantaneo). **PARZIALMENTE CONFERMATO ALL'ASCOLTO**: l'utente riporta un miglioramento ("va meglio") ma con RESIDUI non ancora indagati — qualche click occasionale ancora presente, e un "wobbeling" nelle voci — deliberatamente NON approfonditi in questa sessione su richiesta esplicita dell'utente ("fermiamoci qua"), rimandati alla prossima. Sessione 13: uno screenshot delle impostazioni audio di Ableton usate nel test (buffer d'uscita 4096 campioni, driver MME/DirectX) ha permesso di diagnosticare ENTRAMBI i residui per calcolo diretto — click residui: la dissolvenza di sessione 12 (8ms = 353 campioni) era un no-op completo con un blocco da 4096 campioni, il salto restava pieno-scala in un solo campione; wobbling: ogni parametro del motore (pitch, formanti, f0) si aggiorna una volta per blocco, cioe' a ~10.8 Hz con questo block size. **Corretto e verificato (build/test/pluginval) solo il fix dei click** (`Glide::processRamp`, guadagno campione-per-campione), NON ancora confermato all'ascolto. Il wobbling resta diagnosticato ma non corretto: il fix (ciclo a sotto-blocchi dentro `processBlock`) e' un intervento strutturale, da discutere con l'utente prima di iniziare — vedi §6.**

**Novita' sessione 22 — evidenziazione dei primi 5 preset (secondo slice di M5, §8.2), su**
**esplicita scelta dell'utente fra le opzioni proposte a fine sessione 21:**

Testo PRD di riferimento (§8.2/FR-07): "il controller dedicato ha un navigation button a 5
direzioni che seleziona sempre i primi 5 preset della lista" — la UI deve rendere visibile
quella corrispondenza senza che l'utente debba consultare il manuale.

- **`src/ui/PresetListEditor.cpp`, `paint()`**: aggiunto un badge numerato (1-5) — cerchio
  arrotondato color ambra (`0xffe0a72e`) con il numero al centro — disegnato SOLO per le
  prime 5 righe (`i < numNavSlots`), in una colonna dedicata di 18px riservata per TUTTE le
  righe (anche oltre la quinta, lasciata vuota) cosi' che le colonne CC/nome restino allineate
  a prescindere dal numero di preset. Livello di disegno indipendente dallo sfondo di
  selezione/trascinamento gia' esistente (nessuna interferenza: il badge resta leggibile sia
  sulla riga selezionata sia durante un drag). **Nessuna modifica al modello ne' a
  `PresetListEditor.h`**: e' pura resa grafica, stessa lettura dal vivo di `PresetLibrary`
  gia' in uso per nome/CC — il badge segue automaticamente il preset quando si riordina la
  lista (nessun caching, nessun rischio di badge "stantio").
- **Verificato per calcolo**: build VST3 e Standalone riuscite (solo il consueto fallimento di
  copia post-build per permessi), tutte e 6 le suite `ctest` verdi (nessuna toccata da questo
  lavoro — modifica isolata a `paint()`), `pluginval --strictness-level 10` **SUCCESS**.
- **Verificato visivamente** (stessa tecnica di sessione 21: cattura diretta dello schermo via
  PowerShell/`System.Drawing`, non `PrintWindow`): badge ambra "1".."5" visibili sulle righe
  CC1-CC5 (Maj/Min/Dom/Sus/Half Dim), assenti su CC6/CC7 (Dim/Aug7) — coerente con la spec.
  **CONFERMATO visivamente dall'utente su entrambi Standalone e VST3 in Ableton**, dopo un
  intoppo iniziale: la prima verifica dell'utente su Ableton non mostrava i badge — causa
  trovata (non un bug del badge stesso): la build **Release** non era stata ricompilata in
  questa sessione (solo Debug), e nessuna copia di `Harmonizer.vst3` risulta installata in
  `Program Files\Common Files\VST3` ne' altrove nei percorsi standard (il fallimento di copia
  post-build per permessi, presente da sessioni, sembra quindi permanente) — Ableton molto
  probabilmente scansiona direttamente `build/Harmonizer_artefacts/Release/VST3`, non un
  percorso di sistema. Ricompilata anche la Release (VST3 + Standalone), `pluginval
  --strictness-level 10` **SUCCESS** anche su questa build — badge confermato visibile
  dall'utente su entrambi dopo il rebuild. **Nota per le prossime sessioni**: se un lavoro UI
  viene verificato solo sulla build Debug, verificare/ricompilare anche la Release prima di
  chiedere conferma dell'utente su Ableton — altrimenti si rischia un falso negativo come
  in questa sessione.

**Novita' sessione 21 — primo slice del punto "prossimi slice di M5 (UI)": lista preset con
drag&drop vero (FR-06/07, §8.2), sostituisce la ComboBox + bottoni Su/Giu':**

Su richiesta esplicita dell'utente ("procederei con il primo step del secondo punto", dalla
lista di prossimi passi proposta a fine sessione 20), primo slice concreto della UI di M5 dopo
l'editor tabella 12x8 di sessione 15.

- **`src/ui/PresetListEditor.{h,cpp}`** (nuovo, secondo componente custom del progetto dopo
  `PresetTableEditor`): disegna le righe dei preset direttamente in `paint()` leggendo il
  modello dal vivo (nome + `PresetLibrary::getCcValue`) — nessuna Label figlia per riga, a
  differenza della griglia 12x8 a dimensione fissa: qui il numero di righe varia da 1 a
  `PresetLibrary::maxPresets` (128) a ogni add/duplicate/delete, quindi disegnare a mano evita
  di gestire componenti figli che cambiano numero e rende impossibile un testo "stantio".
  Interazione "icone su smartphone" (FR-06): nessuno stato di anteprima separato dal modello —
  ogni volta che il trascinamento supera il confine della riga successiva/precedente si chiama
  subito `PresetLibrary::movePreset` (un passo, esattamente come facevano prima i bottoni
  Su/Giu' un click alla volta), si aggiorna l'indice trascinato e si notifica il genitore
  (`onReordered`) per tenere la selezione (parametro `presetIndex`, tabella 12x8) agganciata al
  preset che si sta spostando. Il CC accanto al nome si aggiorna da solo perche' e' disegnato
  dal vivo — soddisfa "aggiornato in tempo reale durante il riordino" (§8.2) senza codice
  dedicato. **Nessuna modifica al modello**: `PresetLibrary::movePreset` esisteva gia' (usato
  da sessione 15 dai bottoni Su/Giu', stessa identica semantica erase+insert), la mutazione
  passa sempre da `HarmonizerAudioProcessor::editPresetLibrary` (copy-on-write, mai sull'audio
  thread) — nessun nuovo meccanismo di threading.
- **`src/PluginEditor.{h,cpp}`**: `presetBox` (ComboBox) e i bottoni `moveUpButton`/
  `moveDownButton` **rimossi**, non lasciati in parallelo — erano esplicitamente il
  placeholder segnalato nel commento di testa del file come "in attesa" di questo lavoro.
  Nuovi membri `presetListViewport` (`juce::Viewport`, solo scroll verticale) +
  `presetListEditor`. Rimossi anche `lastKnownPresetCount`/`ignoreComboCallback`: non servono
  piu' — niente diff sui nomi da ComboBox (il componente nuovo legge sempre dal vivo),
  `refreshPresetBoxFromLibrary()` eliminato e sostituito da chiamate dirette a
  `presetListEditor.refresh()` in ogni punto che prima la richiamava (add/duplicate/delete/
  import/load/rename/timer). Aggiunto un piccolo scroll-into-view in
  `syncPresetSelectionFromParameter()` cosi' Add/Duplicate/CC/automazione non selezionino mai
  un preset fuori dall'area visibile senza segnale — gap non coperto dalla vecchia ComboBox
  (che non aveva bisogno di scroll) e non esplicitamente nel piano iniziale, aggiunto durante
  l'implementazione perche' altrimenti necessario per la correttezza della nuova UI.
  Dimensioni finestra aggiornate (`setResizeLimits`/`setSize`, stesso tipo di aggiustamento
  gia' fatto in sessione 15 per la tabella 12x8): la riga singola della ComboBox (26+6px)
  diventa un'area di 7 righe (7×22=154px, 7 perche' e' il numero di preset di fabbrica — di
  default nulla scrolla) — netto +122px di altezza minima.
- **`CMakeLists.txt`**: nuovo file sorgente `src/ui/PresetListEditor.cpp` nel target
  `Harmonizer`.
- **Nessun nuovo target di test automatico** (scelta esplicita, documentata nel piano prima di
  scrivere codice): `movePreset` non e' cambiato (gia' esercitato per mesi via Su/Giu'), la
  logica nuova e' interazione mouse (mouseDown/Drag/Up) poco adatta a un test headless — stesso
  ragionamento e stessa scelta di verifica (build + pluginval + controllo visivo/interattivo)
  gia' fatta per `PresetTableEditor` in sessione 15.
- **Verificato per calcolo**: build VST3 e Standalone riuscite (solo il consueto fallimento di
  copia post-build per permessi), `pluginval --strictness-level 10` **SUCCESS** (nessuna
  occorrenza di fail/error/crash), tutte e 6 le suite `ctest` verdi (nessuna toccata da questo
  lavoro).
- **Verifica visiva riuscita, a differenza del tentativo inconcludente di sessione 15**:
  lanciato lo Standalone appena ricompilato e catturato uno screenshot reale della finestra
  (via cattura diretta dello schermo su una finestra riposizionata in un'area visibile, non
  `PrintWindow` sul rendering accelerato JUCE — probabilmente e' quello il dettaglio che ha
  fatto la differenza rispetto al limite osservato in sessione 15). Confermato visivamente: i 7
  preset di fabbrica visibili senza scroll, ciascuno con "CC N" accanto al nome (CC 1 Maj, CC 2
  Min, ... CC 7 Aug7), la riga del preset selezionato evidenziata, coerente con il campo Name e
  la tabella 12x8 sotto (Maj selezionato mostra la sua tabella). **NON verificata visivamente
  l'interazione di trascinamento vera e propria** (simulare un drag del mouse in modo
  affidabile da qui avrebbe richiesto muovere il cursore reale dell'utente — scelta
  deliberata di non farlo, lasciata alla verifica dell'utente).
- **CONFERMATO dall'utente sul VST3 reale**: "riesco a spostare i preset e i CC rimangono
  'stabili' nel senso che se sposto il preset su o giu' cambia il suo numero di CC a seconda
  della posizione" — l'interazione di trascinamento funziona e il CC live durante il
  riordino (§8.2) e' verificato. **Non ancora testato con un controller MIDI hardware reale**
  (solo graficamente): resta un punto aperto per quando l'utente avra' accesso all'hardware,
  non un blocco per questo lavoro (la logica CC=posizione e' la stessa gia' in uso da mesi via
  i vecchi bottoni Su/Giu', non e' cambiata qui). Lavoro committato.

**Novita' sessione 20 — causa del wobbling trovata (non nella selezione degli epoch, come
sessione 19 aveva escluso, ma nella SINTESI) e corretta; verificato per calcolo su file reale
e con l'intera suite di test, NON ancora confermato all'ascolto:**

Ripresa diretta di sessione 19 (checkpoint di sicurezza `b67a741`, gia' su `origin/main`,
nessuna modifica pendente su `PsolaShifter` all'inizio di questa sessione). L'utente ha fornito
un quinto file, `SAMPLE TEST/Export V2.wav` gia' presente da sessione 19 (Voices=1, Dry=0,
Wet=1, Glide=0, preset Maj root C su "Test 1 - Basic Silk Horns.wav") come riferimento.

- **Fase 0 (validazione dell'esperimento di isolamento)**: `runFixedF0` di
  `sample_click_finder.cpp` applica UNA SOLA f0 fissa all'intero file di 8s, che contiene 4
  note diverse (C4/D4/E4/C4) — fuori dalla nota corrispondente, l'uscita e' degradata per
  costruzione, non per un difetto. Verificato con `real_export_probe` che gli eventi di
  instabilita' di sessione 19 (t≈0.75-0.78s, 0.90-0.93s) cadono DENTRO il plateau C4
  (0.09-2.01s, f0 reale 261.3Hz) dove `runFixedF0` a 261.35Hz e' effettivamente valido — non un
  artefatto della f0 sbagliata altrove nel file. L'isolamento di sessione 19 resta valido.
- **Fase 1 (Test 10/11 in `tests/psola_test.cpp`, permanenti in ctest)**: due tentativi di
  riprodurre il wobbling su segnale sintetico, ENTRAMBI negativi per misura (CLAUDE.md regola
  13: riportato onestamente come esito negativo, non forzato a "passare" cambiando soglia).
  Test 10 (tono perfettamente stazionario, periodo intero vs frazionario a SR=48000): nessuna
  degradazione misurabile nel tempo. Test 11 (stesso tono con un vibrato stretto e lento, ±15
  cent/5Hz, currentF0Hz aggiornato per blocco da un oracolo): nemmeno questo la riproduce. La
  ragione, capita solo DOPO la Fase 2: un generatore a singola risonanza produce un picco per
  periodo troppo netto perche' l'individuazione degli epoch lo posizioni mai male — a
  differenza di un timbro reale (corno), armonicamente piu' ricco. Entrambi i test restano in
  ctest come verifiche di trasparenza permanenti (nessuna deriva ammessa nel tempo), non come
  prova del meccanismo.
- **Fase 2 (strumentazione diretta di `PsolaShifter` sul file reale, decisiva)**: aggiunta
  diagnostica temporanea (`PSOLA_DEBUG_SYNTH`, poi rimossa) che traccia per ogni grano di
  sintesi: posizione `sp`, epoch di analisi scelto, scarto `sp-epoch`, spaziatura reale fra
  epoch consecutivi, periodo quantizzato `P`. Eseguita su `sample_click_finder --fixedF0` a
  261.35Hz sul dry di "Test 1", con la traccia salvata su file e confrontata campione per
  campione con le finestre di instabilita' misurate da `real_export_probe`. **Trovato un
  meccanismo preciso**: nella finestra t≈0.77-0.81s (coincide, a livello di singolo campione,
  con il glitch riportato da `real_export_probe`), un singolo epoch viene rilevato con una
  spaziatura anomala (137 campioni invece dei ~168-169 attesi — un posizionamento sbagliato su
  un campione non impulsivo, prevedibile su materiale reale ricco di armoniche). Poiche'
  `synthPos` avanzava di un passo FISSO (il periodo quantizzato `P/alpha`, non la spaziatura
  reale), questo singolo evento non restava locale: lo scarto fra `synthPos` e l'epoch piu'
  vicino si propagava per ~15 grani consecutivi (~30ms, coerente con la larghezza della
  finestra di glitch riportata) prima che `nearestEpoch()` saltasse a un epoch adiacente,
  riassorbendolo — l'aritmetica torna esattamente (+32 campioni di scarto = 169-137). Questo
  spiega perche' le due ipotesi ritirate in sessione 19 (peso sulla scelta del picco,
  interpolazione sub-campione) non avessero effetto: il problema non era DOVE `detectEpochs`
  posiziona un epoch, ma quanto a lungo un singolo posizionamento imperfetto (inevitabile su
  materiale reale) continua a propagarsi a valle nella sintesi.
- **Fase 3 (fix in `src/dsp/PsolaShifter.cpp`, `synthesise()`)**: il passo di avanzamento di
  `synthPos` ora usa il periodo di analisi LOCALE reale (`epochAfter(epoch) - epoch`, letto
  direttamente dal ring degli epoch per QUESTO grano) invece del periodo quantizzato globale —
  un singolo epoch mal posizionato sposta cosi' un solo grano invece di fare accumulare uno
  scarto su molti grani successivi. **Primo tentativo scartato dopo misura** (CLAUDE.md regola
  13): derivare il passo dalla differenza fra l'epoch di QUESTA sintesi e quello della sintesi
  PRECEDENTE sembrava equivalente ma non lo e' — a `alpha != 1` crea un ciclo di retroazione
  che diverge geometricamente (misurato: Test 1 di `psola_test` rotto, -12 semitoni tornava
  vicino all'originale invece di un'ottava sotto). Sostituito con `epochAfter()`, che legge la
  spaziatura fra due epoch ADIACENTI nel ring, indipendente da come `synthPos` si e' mosso in
  precedenza — nessuna retroazione. A `alpha != 1` questo elimina anche l'errore di
  quantizzazione di `currentPeriod()` (D1, pianificato dalle sessioni 17/18/19 come intervento
  separato: ora assorbito, non serve piu' farlo a parte).
- **Verificato DOPO il fix**: tutti gli 11 test di `psola_test` verdi (Test 1-9 preesistenti
  bit-per-bit compatibili nell'esito, invariati nella soglia; Test 10/11 nuovi). Tutte e 6 le
  suite verdi via `ctest`. Sul file reale, `sample_click_finder --fixedF0` + `real_export_probe`
  sullo stesso confronto di Fase 0: le finestre instabili crollano da **36/792 (4.5%) a 2/792
  (0.3%)**, l'unico residuo non banale e' un singolo evento a fine file (t=7.93-7.95s,
  probabile artefatto di bordo, non della stessa famiglia). Controllo di robustezza su un
  secondo timbro (`Test 2 - E-Piano.wav`, mai usato per orientare il fix): 10/786 (1.3%)
  finestre instabili, concentrate su transizioni/attacchi di nota, non su note sostenute —
  nessuna regressione, nessun overfitting al primo file. Build VST3 e Standalone riuscite
  (solo il consueto fallimento di copia post-build per permessi). `pluginval
  --strictness-level 10` **SUCCESS** (nessuna occorrenza di fail/error/crash nel log completo).
  Diagnostica temporanea (`PSOLA_DEBUG_SYNTH` in `PsolaShifter.cpp`, define in
  `CMakeLists.txt`) rimossa prima di chiudere la sessione — `git diff` su `src/` mostra solo il
  fix vero e proprio.
- **CONFERMATO ALL'ASCOLTO**: l'utente ha fornito `Export V3.wav` (stesse impostazioni di
  `Export V2.wav`) e riportato "ottimo lavoro, ora va molto meglio". Confronto numerico diretto
  con `real_export_probe` sul nuovo export reale (non solo sull'isolamento offline): finestre
  instabili 8/792 (1.0%), contro 35/792 (4.4%) di `Export V2.wav` prima del fix — coerente con
  il calo gia' misurato sull'isolamento fixedF0 (36->2 su 792). Gli eventi residui sono ora
  concentrati vicino alle transizioni di nota (t=0.00-0.05s, primo attacco; t=4.07-4.09s e
  t=5.98-6.04s, cambi nota a 4.07s/6.07s), non piu' dentro le note sostenute — lo stesso
  spostamento di pattern gia' osservato nel controllo di robustezza su "E-Piano".
- **Non toccato**: `detectEpochs()` (nessuna modifica: la Fase 2 ha mostrato che il problema
  non e' li'), `emitGrain()`, `PitchDetector`, `OnsetDetector`, `HarmonyEngine`,
  `PresetLibrary`, `PhraseScheduler`, tutta la UI. Il "secondo meccanismo" annotato in sessione
  17 (jitter fine anche in unisono, cali 0.89-0.93 senza coinvolgere il gate) potrebbe essere
  proprio questo, dato che il fix agisce esattamente sulla sintesi in unisono — da verificare
  se persiste ancora dopo la conferma all'ascolto.

**Novita' sessione 19 — click confermati spariti all'ascolto; committato/pushato il lavoro di
sessioni 16-18; indagine sul "wobbling"/instabilita' timbrica, due ipotesi su PsolaShifter
tentate e RITIRATE (nessun fix applicato in questa sessione):**

L'utente ha confermato all'ascolto che il lavoro di sessione 16-18 (click a inizio nota e a
meta' nota) ha funzionato: "ora non ci sono più click". Committato e pushato tutto il lavoro
di quelle sessioni in un solo commit (`b67a741`, gia' su `origin/main` — vedi sotto per
perche' un solo commit e non due). **Checkpoint di sicurezza per questa sessione**: `b67a741`
e' il punto a cui tornare se qualcosa si rompesse nel lavoro successivo (richiesto
esplicitamente dall'utente prima di toccare `PsolaShifter`).

Nuovo problema, distinto dal click: **wobbling/artefatti in sottofondo, timbro che cambia "a
scatti" nel tempo**. L'utente ha fornito un quarto file reale, `SAMPLE TEST/Export V2.wav`
(Voices=1, Dry=0, Wet=1, **Glide=0** — scelto deliberatamente per escludere FR-17 dal
quadro). Confermato leggendo `Voice.cpp`: con Glide=0 e Move mode, `semitonesToApply` resta
costante entro una nota tenuta, quindi qualunque instabilita' osservata dentro una nota non
puo' venire dal glide.

- **`real_export_probe.cpp` esteso**: nuova sezione "4b. CADENZA DEI DISTURBI" (istanti di
  calo di periodicita' e intervalli fra un evento e il successivo, per cercare una cadenza
  regolare riconducibile a un block size) e una modalita' `--trace` gia' presente riusata per
  ispezione fine campione-per-campione.
- **Analisi su Export V2**: pattern di instabilita' quasi identico a V1 (stessi istanti, stessa
  entita', 0.86-0.93 di periodicita' minima) nonostante Glide=0 — conferma che il glide FR-17
  non c'entra. Nessuna cadenza regolare riconducibile a un block size fisso (gli intervalli fra
  eventi spaziano continuamente da 40ms a 1430ms, senza clustering).
- **`sample_click_finder.cpp` esteso**: nuova `dumpPitchTrace` (traccia grezza di
  `PitchDetector::getMidiNote()`/confidenza a piu' block size) e nuova `runFixedF0` (Voice con
  f0 COSTANTE scelta a mano, bypassa completamente PitchDetector/PitchLatch/Glide).
- **Scoperta chiave 1**: la traiettoria grezza di `PitchDetector` sul dry, nella stessa
  finestra dov'e' presente il glitch nel wet (t≈0.68-0.85s in "Silk Horns"), e' perfettamente
  pulita a OGNI block size testato (64/256/1024/4096) — confidenza sempre ≥0.989, nessun
  salto. Esclude sia il rumore del rilevatore di pitch sia la quantizzazione a livello di
  blocco (sessione 13, mai confermata) come causa.
- **Scoperta chiave 2**: con `runFixedF0` (f0 fissa e corretta, unisono, bypass totale di
  PitchDetector/PitchLatch/Glide) sullo stesso file reale, il glitch **persiste quasi
  identico** (periodicita' 0.885-0.925, stessa posizione temporale, stessa entita'). Prova
  diretta e definitiva: il difetto e' **interno a `PsolaShifter`**, non a qualunque cosa gli
  stia a monte.
- **Due ipotesi tentate su `PsolaShifter::detectEpochs`, ENTRAMBE RITIRATE dopo misura**:
  1. Peso a coseno rialzato sulla ricerca del picco (preferire la continuita' con la
     predizione invece del massimo assoluto nella finestra ±P/4) — **zero effetto misurabile**
     sia sul segnale sintetico di prova sia (soprattutto) sul file reale isolato con
     `runFixedF0`.
  2. Interpolazione parabolica sub-campione della posizione dell'epoch (`lastEpoch`/
     `epochRing` da `long long` a `double`, stessa tecnica di `measureF0`) — motivata da una
     misura di debug temporanea (`PSOLA_DEBUG_EPOCHS`, poi rimossa) che mostrava scarti
     sempre di esattamente +1 campione rispetto alla predizione nella finestra del glitch.
     Anche questa: **zero effetto misurabile** sul file reale (34 vs 36 eventi d'instabilita',
     stessa entita', stesse posizioni).
  3. **Scoperta collaterale importante (CLAUDE.md regola 13)**: il segnale sintetico costruito
     per riprodurre l'ipotesi 1 (`makeCompetingPulsesVowel`, due impulsi per periodo che si
     scambiano ampiezza nel tempo) si e' rivelato un test VIZIATO — misurato che la
     periodicita' dell'INGRESSO stesso crollava (0.90-0.98) vicino al punto di scambio, quindi
     il test misurava in parte la non-perfetta periodicita' del segnale costruito, non un
     difetto puro dell'algoritmo. Scritto, usato per orientarsi, poi **rimosso** insieme al
     generatore in `TestSignals.h` — non lasciato nel codice come test permanente essendo
     concettualmente inaffidabile.
- **Esito**: dato che nessuna delle due modifiche a `detectEpochs` ha spostato la misura sul
  file reale, entrambe sono state **riportate al codice del checkpoint** (`git checkout
  b67a741 -- src/dsp/PsolaShifter.h src/dsp/PsolaShifter.cpp`) invece di essere tenute "perche'
  comunque plausibili" — esattamente la disciplina di CLAUDE.md regola 13. Verificato dopo il
  ripristino: `git status` su questi due file pulito (nessuna differenza dal checkpoint), tutte
  e 6 le suite verdi via `ctest`.
- **Cosa resta**: il meccanismo del wobbling e' confermato interno a `PsolaShifter` (non a
  monte), ma la selezione dell'epoch in `detectEpochs` non ne e' (o non ne e' l'unica) causa.
  Prossimi candidati, non ancora esplorati: la sintesi/overlap-add in `synthesise()`/
  `emitGrain()` (avanzamento di `synthPos`, accumulo di `synthPeriod` nel tempo, o l'ampiezza/
  forma della finestra di Hann del grano) — vedi §6.
- **Strumenti diagnostici che restano validi e riusabili** (nessuno di questi e' stato
  ritirato, solo il fix su `detectEpochs`): `tests/real_export_probe.cpp` (sezione 4b +
  `--trace`), `tests/sample_click_finder.cpp` (`dumpPitchTrace`, `runFixedF0`,
  `writeWavMono`/dump delle passate su file per rianalisi).

**Novita' sessione 17-18 — "una prima voce perfetta": dal file reale al gate dell'onset, causa reale trovata e corretta (era diversa da tutte le ipotesi precedenti):**

L'utente ha riascoltato il fix di sessione 16 (click a inizio nota): migliorato, non
risolto — restano click a inizio nota E "in mezzo alla nota", piu' un'instabilita' timbrica
generale. Ha poi isolato empiricamente in Ableton che il difetto c'e' gia' con Voices=1, e ha
chiesto di **fermare l'indagine multi-voce** e concentrarsi solo su V1. Ha fornito tre file
WAV reali in una nuova cartella non versionata `SAMPLE TEST/` (fuori da git/ctest per scelta
esplicita): due sorgenti dry (`Test 1 - Basic Silk Horns.wav`, `Test 2 - E-Piano.wav`,
44.1kHz stereo 16-bit, 8.00s) e un export VERO del plugin (`Export V1.wav`, Voices=1/Dry=0/
Wet=1, stesso formato/durata — confrontabile campione per campione col dry).

- **Sessione 17 (prima di avere l'export)**: tre ipotesi testate offline su segnale reale con
  un nuovo strumento diagnostico (`tests/sample_click_finder.cpp`, non in ctest — dipende da
  file esterni) — voce mai riattivata con offset fisso, riattivazioni guidate da onset reali,
  offset che segue `PitchLatch` dal vivo con una tabella armonica INVENTATA — tutte pulite su
  entrambi i file. Con l'arrivo dell'export reale, si e' scoperto che questo risultato "pulito"
  era in parte un artefatto della metrica usata (vedi sotto), non prova di assenza del bug.
- **Fatto derivato a mano da `PresetLibrary.cpp::generateDropVoicingTable`**: la colonna V1
  vera del preset "Maj" (root C) e' `[0,-1,-2,-3,0,-1,-2,0,-1,-2,-3,0]` per i 12 gradi — V1 non
  e' MAI muta in un accordo a 4+ toni (la regola "voci oltre numTones restano mute" colpisce
  solo v>=4). Confermata dalla sequenza di offset misurata sull'export reale (0/-2/0/0 su
  C4->D4->E4->C4) — `HarmonyEngine`/`PresetLibrary`/`PitchLatch` sono fuori causa.
- **Nuovi strumenti diagnostici** (entrambi non in ctest, dipendono da `SAMPLE TEST/`, non
  versionata): `tests/SampleAnalysis.h` (header-only, nessuna dipendenza JUCE, include
  `TestSignals.h` invece di duplicarne rms/maxJump/centsError) con `readWav`/`downmix`/
  `writeWavMono`, `measureFrame` (f0 + periodicita' per finestra, autocorrelazione con
  correzione d'ottava — stessa idea di `measureF0` ma espone anche il picco di correlazione),
  `envelopeRms`/`bestLagByEnvelope` (allineamento per inviluppo, non per forma d'onda — su un
  segnale periodico la correlazione di forma d'onda e' ambigua a meno di multipli del periodo),
  `fitWindow` (residuo di trasparenza in dB per finestra), `findClicks` (la metrica di
  sessione 17, con un commento di testa che ne documenta il limite — vedi sotto), e la tabella
  V1/Maj condivisa. `tests/real_export_probe.cpp` (nuovo target CMake): confronta DRY e WET
  REALI, con una modalita' di traccia fine per ispezionare un intervallo di tempo campione per
  campione prima di ipotizzare un meccanismo.
- **F-1, scoperta cruciale**: `findClicks` (slew vs baseline, la metrica di sessione 17) da'
  **0 anomalie sull'export reale che l'utente sente difettoso**. Le tre passate "pulite" di
  sessione 17 non erano prova di assenza del bug — erano prova che la metrica non lo vede. Il
  difetto reale non e' un salto di ampiezza in un campione, e' descritto sotto.
- **Ipotesi del periodo intero in PsolaShifter (D1), verificata e NON confermata in modo
  netto**: `PsolaShifter::currentPeriod()` arrotonda il periodo a un intero
  (`P=lround(sr/f0)`), quindi l'intonazione reale e' `alpha*sr/P` non l'ideale `alpha*f0` — un
  meccanismo vero, verificato leggendo il codice. Ma misurato per tratto sull'export reale (5
  plateau): il modello a periodo intero spiega meglio il dato in 3 casi su 5, l'ideale negli
  altri 2, scarti tutti piccoli (1-4 cent, sotto probabile soglia di percezione). Non
  abbandonata (resta un piccolo intervento pianificato, vedi §6), ma NON e' la causa
  principale del sintomo riportato — un'ipotesi "calcolabile dal codice" rivelatasi solo
  parzialmente vera una volta misurata, la stessa lezione di sessione 16 su H1.
- **Causa reale trovata e confermata due volte indipendenti**: a t≈5.95s nell'export reale, il
  WET crolla a periodicita' 0.000 (silenzio totale) mentre il DRY sta ancora suonando (nota E4
  in decadimento naturale, rms 0.014->0.011, periodicita' 1.000 costante — NON una pausa fra
  due note). Causa: `OnsetDetector` (`src/dsp/OnsetDetector.cpp`) usa `cycfi::q::onset_gate`
  con soglie fisse `(-24dB onset, -30dB slope, -36dB release)` — il decadimento naturale
  della nota attraversa -36dB di picco proprio li', il gate si chiude, la voce si ammutolisce
  (dissolvenza 8ms) e tace per il resto della coda della nota, anche se la nota e' ancora
  chiaramente udibile e perfettamente periodica nel dry. **Confermato indipendentemente**
  riproducendo l'intera catena offline (`sample_click_finder.cpp`, nuova Passata 4: modello a
  due slot fisici che riproduce `PhraseScheduler` con `keepTails=false` a numVoices=1, usando
  `PitchDetector`/`OnsetDetector` reali — nessuna dipendenza JUCE) e ri-analizzando l'uscita
  con `real_export_probe`: stesso identico buco, stessa posizione, stessa periodicita' a 0.000
  — riproducibile SENZA Ableton.
- **Trovato anche un secondo meccanismo, distinto**: cali di periodicita' piu' piccoli (0.89-
  0.93) dentro note sostenute ad alto livello (es. t≈0.77s, rms 0.13-0.15, ben sopra qualunque
  soglia del gate) — la voce non si ammutolisce mai qui, e' jitter fine del motore PSOLA su
  materiale reale non impulsivo anche in unisono. NON corretto in questa sessione (fuori
  scope, vedi §6) — probabile causa della sensazione generale di "non stabile", distinta dal
  buco grande.
- **Fix**: `src/dsp/OnsetDetector.cpp`, `release_threshold` del gate abbassato da -36dB a
  -45dB (9dB piu' permissivo). `onset_threshold` (-24dB) e `slope_threshold` (-30dB) — quelli
  che decidono se un NUOVO attacco apre il gate — non toccati: il fix riguarda solo quando il
  gate si richiude su una nota che si spegne, non la sensibilita' agli attacchi. Compromesso
  noto e accettato consapevolmente: un rilascio piu' permissivo tiene il gate aperto piu' a
  lungo, quindi in un legato molto stretto la nota successiva rischia (in teoria, non
  verificato quantitativamente qui) di non essere piu' riconosciuta come un nuovo attacco
  (stessa classe di bug delle sessioni 10-12) — -45dB e' una scelta moderata, non il minimo
  tecnico, scelta apposta per non spingersi troppo in quella direzione.
- **Verificato DOPO il fix**: rigenerata la Passata 4 offline e ri-analizzata con
  `real_export_probe` — il buco e' sparito completamente ("Buchi: nessuno", nessuna
  periodicita' a 0.000 in quella zona; le finestre instabili totali scendono da 40 a 36 su
  792, coerente con l'aver tolto UN meccanismo su due). Tutte e 6 le suite verdi via `ctest`
  (nessuna toccava `OnsetDetector` prima, ma nessuna regressione). Build VST3/Standalone
  riuscite (solo il consueto fallimento di copia post-build per permessi). `pluginval
  --strictness-level 10` **SUCCESS**.
- **Non toccato**: `PsolaShifter` (l'ipotesi D1 resta un intervento pianificato, non ancora
  fatto — vedi §6), `PitchDetector`, `HarmonyEngine`, `PresetLibrary`, `PhraseScheduler` reale
  (la Passata 4 lo *riproduce* con un modello a 2 slot per restare headless/senza `juce_core`,
  non lo modifica), tutta la UI di sessione 15, `CsvIo`, il meccanismo di sessione 14 (resta
  nel codice, invariato, era comunque un bug reale per il suo meccanismo specifico).
- **NON ancora confermato all'ascolto** (CLAUDE.md regola 12): l'utente deve fare un nuovo
  export identico (stesse impostazioni di `Export V1.wav`) e confermare se il buco/calo a
  meta' delle note lunghe e' sparito. Il secondo meccanismo (jitter fine anche in unisono) e
  l'ipotesi D1 (periodo intero) restano da affrontare — vedi §6.

**Novita' sessione 16 — click a inizio nota: ripartenza da zero, causa reale trovata e corretta (era diversa da quella di sessione 14):**

A inizio sessione l'utente ha chiesto il prossimo passo; riletti CLAUDE.md/handsoff/PRD, la
priorita' indicata da §6 era il click a inizio nota (sessione 14 SMENTITA all'ascolto: il fix
del reset dello shifter restava vero e misurato ma non era la causa, o non l'unica). Concordato
esplicitamente con l'utente di ripartire DA ZERO sulle ipotesi (non dare per assodato nulla di
sessione 14), invece di limitarsi ad "approfondire" — la lezione esplicita di CLAUDE.md regola
13. Due dati nuovi raccolti PRIMA di scrivere qualunque codice: il click si sente **solo sul
wet, mai sul dry** (esclude dry/wet glide, bypass, downmix, sorgente — la causa e' per forza
dentro `Voice`/`PitchShifter`), e **persiste identico con un buffer ASIO piccolo (1024
campioni)**, non solo con MME/4096 di sessione 13 (indebolisce fortemente l'ipotesi che sia un
artefatto di block size).

- **Ipotesi principale scritta PRIMA di misurare (H1)**: `PsolaShifter::reset()` finge che i
  primi `latency` campioni di silenzio siano gia' entrati (`absWrite=latency, absRead=0`); la
  dissolvenza anti-click di `Voice` (`kDeclickMs`=8ms=353 campioni) e' molto piu' corta della
  latenza dichiarata del motore (13.6-30ms secondo Stability) — sembrava plausibile che la
  rampa si esaurisse SUL SILENZIO e che l'uscita saltasse a piena scala quando il segnale vero
  emergeva dalla pipeline. Calcolabile dal codice, ma — coerentemente con l'errore gia' fatto
  in sessione 14 — **non ancora misurata**.
- **Nuovo `tests/voice_test.cpp`** (e nuovo target CMake `voice_test`, headless come le altre
  suite: nessuna dipendenza JUCE, collega `Voice.cpp` + il motore PSOLA reale, non un doppio
  finto): misura l'inviluppo e lo slew di ampiezza di una `Voice` alla riattivazione da
  silenzio, su tutti e 5 i livelli di Stability e due block size molto diversi (64/1024),
  con un controllo negativo obbligatorio (che la misura di slew non segnali nulla su un tratto
  di uscita gia' a regime) prima di fidarsi di qualunque altro numero — stesso principio di
  CLAUDE.md regola 13. Estratto `tests/TestSignals.h` da `tests/psola_test.cpp` (generatore di
  segnale, `measureF0`, `formantPeak`, ecc. — stessa matematica, solo il sample rate diventa un
  parametro esplicito) per condividerlo fra le due suite invece di duplicarlo; verificato che
  l'estrazione fosse comportamentale-identica rieseguendo `psola_test` subito dopo (stessi 9
  gruppi di verifiche, stessi valori numerici).
- **Risultato della misura — H1 REFUTATA**: `slewAtt/Regime = 1.00` su tutti i 10 combinazioni
  Stability×blockSize, nessuna discontinuita' rilevata. L'attacco e' in realta' una salita
  LISCIA (~232 campioni, ~5ms), solo RITARDATA (18-35ms secondo Stability, coerente con la
  latenza dichiarata) rispetto a dove la rampa di `ampGlide` aveva gia' finito. La mia ipotesi
  "calcolabile dal codice" era sbagliata una volta davvero misurata — esattamente il tipo di
  errore che lo strumento di misura serviva a intercettare prima di scrivere un fix inutile.
- **H4 (dipendenza dal block size host) ESCLUSA**: il punto di salto (`t1`) e' identico
  (differenza 0 campioni) fra blocco 64 e 1024 su tutti i livelli di Stability — coerente con
  la prova dell'utente (click identico a 1024 ASIO e 4096 MME).
- **H3 CONFERMATA, e' la causa reale**: `offsetGlide` (il `Glide` che porta l'offset armonico
  in semitoni verso il valore della cella corrente, FR-17) non veniva MAI "agganciato" al nuovo
  target quando uno slot fisico silenzioso viene riassegnato a una nuova nota — restava fermo
  al valore della nota PRECEDENTE su quello stesso slot e ci scivolava sopra in `glideTimeMs`
  (30ms di default), invece di scattare subito. Misurato PRIMA del fix con `voice_test.cpp`:
  riproducendo la sequenza esatta di `PhraseScheduler.cpp` (converge su +7 semitoni, si
  ammutolisce, si riattiva con target -5), l'intonazione del nuovo attacco risultava a ~195
  cent dal nuovo target (doveva essere vicina a 0). Spiega bene anche "specialmente legato":
  in legato lo slot fisico viene riassegnato quasi ad ogni nota (la dissolvenza dura solo 8ms,
  molto meno della durata tipica di una nota), e il preset armonico da' quasi sempre un offset
  diverso da nota a nota (vedi la tabella 12x8) — un rapido "chirp" di intonazione a ogni
  attacco, mascherato in staccato dal vero transiente della nota ma non in legato.
- **Fix**: nuovo `Voice::justReactivated` (`src/voices/Voice.h`), impostato nella stessa
  transizione silenzio->attiva gia' usata per il reset dello shifter (sessione 14) — non un
  secondo meccanismo scollegato, un'estensione dello stesso punto di aggancio gia' verificato
  corretto. Consumato pero' in `Voice::processAdd` (`src/voices/Voice.cpp`), non dentro
  `setTargetOffsetSemitones`: i due chiamanti impostano target e mute in ORDINE OPPOSTO
  (`PhraseScheduler.cpp`: `setMuted(false)` poi `setTargetOffsetSemitones`; `PlayModeInput.cpp`:
  `setTargetOffsetSemitones` al note-on, `setMuted(false)` solo quando il segnale torna
  stabile, anche blocchi dopo) — `processAdd` e' l'unico punto che arriva sempre PER ULTIMO in
  entrambi i casi, quando il target giusto e' gia' stato impostato per certo. Alla prima
  chiamata utile dopo la riattivazione, `offsetGlide.reset(offsetGlide.getTarget())` (nuovo
  `Glide::getTarget()` in `src/dsp/Glide.h`) aggancia `current` al target corrente, qualunque
  esso sia — zero rampa per il NUOVO attacco. Il percorso FR-17 (cambio accordo su una nota
  GIA' attiva, non appena riattivata) non e' toccato: `justReactivated` e' vero solo sulla
  transizione dal silenzio, mai per una voce gia' in corso — quel caso deve continuare a
  glidare, ed e' esattamente cosi'.
- **Verificato DOPO il fix**: `voice_test` — lo stesso scenario che misurava ~195 cent ora
  misura ~4 cent (controllo positivo, voce mai riattivata: ~4 cent — stesso ordine di
  grandezza, conferma che la misura di F0 e' fedele). L'asserzione H3 in `voice_test.cpp` e'
  stata invertita (prima "OK (bug confermato)" con soglia >100 cent, ora richiede <15 cent) —
  stesso schema di Test 8/9 in `psola_test.cpp`. Tutte e 6 le suite verdi via `ctest`
  (`psola`, `override_manager`, `pitch_latch`, `glide`, `cell_input_parser`, `voice` — nuova).
  Build VST3 e Standalone riuscite in Release (solo il consueto fallimento di copia post-build
  per permessi). `pluginval --strictness-level 10` **SUCCESS** su VST3 (nessuna occorrenza di
  fail/error/crash nel log completo).
- **Non toccato**: `PsolaShifter` (nessun cambiamento necessario: H1, l'unica ipotesi che lo
  riguardava, e' stata refutata), `PitchDetector`, `OnsetDetector`, `HarmonyEngine`,
  `PresetLibrary`, tutta la UI di sessione 15, `CsvIo`. Il meccanismo di sessione 14 (reset
  dello shifter alla riattivazione) resta nel codice, invariato: era comunque un bug reale
  (Test 9 di `psola_test.cpp` lo dimostra ancora), solo non la causa di QUESTO sintomo.
- **NON ancora confermato all'ascolto** (CLAUDE.md regola 12): l'utente deve riascoltare nella
  stessa config di riferimento di questa sessione (ASIO 1024, Stability Balanced, preset Maj,
  Dry 0/Wet alto, 4 voci, clip audio legata) e confermare se il click a inizio nota e' sparito.
  Se persiste, la misura ha comunque ristretto il campo: H1/H2 (discontinuita' di ampiezza) e
  H4 (block size) sono escluse per calcolo, quindi la causa residua andrebbe cercata altrove
  (posizionamento degli epoch su segnali non impulsivi, o le 8 istanze PSOLA indipendenti che
  correlano fra loro — i due candidati gia' annotati in sessione 12 per il timbro "robotico",
  mai esclusi come concorrenti anche per un click).

**Novita' sessione 15 — inizio M5 (UI): editor tabella preset armonici 12x8:**

Chiesto esplicitamente all'utente quale fosse il prossimo passo rispetto al PRD. Riletta
integralmente la roadmap (§12) e la sezione UI (§8): M0-M4 sono tecnicamente completi o in
sola verifica (M1: click in stand-by; M4: verifica hardware demandata all'utente, non
blocca lavoro strutturale), **M5 (UI) non era mai stato iniziato in modo strutturato** —
l'editor era un pannello piatto a riga singola, non le tre schermate del PRD. Discusso con
l'utente (che ha chiesto giustamente se non fosse meglio aspettare, dato che restano test
aperti): chiarito che nessuno dei problemi aperti (click, verifica CC/Play) tocca la
struttura della UI — sono comportamento interno, non aggiungono/tolgono controlli — mentre
il gap piu' concreto trovato e' proprio l'**editor dei preset (§8.2)**: prima di questa
sessione l'unico modo di popolare la tabella 12x8 di un preset era importare un CSV
esterno; il bottone "Add" creava un preset vuoto (96 celle mute) senza alcun modo di
editarlo dall'interfaccia. L'utente ha scelto questo come primo slice di M5, da inserire
nel pannello esistente (non aspettando lo scaffold delle 3 schermate, passo successivo di
M5, non toccato qui).

- **`src/harmony/PresetLibrary.{h,cpp}`**: nuovo `setCell(presetIndex, degree, voice, Cell)`,
  unico mutatore diretto di una cella — prima non esisteva alcun modo di modificare la
  tabella di un preset esistente (solo `addPreset`, che ne crea uno nuovo). Stesso schema
  di `renamePreset` (bound-check, nessuna eccezione).
- **`src/ui/CellInputParser.h`** (nuovo, header-only, nessuna dipendenza JUCE/harmony — stesso
  principio di `Glide.h`/`PitchLatch.h`): parser di input validato per le celle. Esiste per
  evitare una trappola gia' presente in `CsvIo::parseCsv`: `juce::String::getIntValue()`
  ritorna silenziosamente `0` per qualunque testo non numerico ("abc", "12x", "--3"
  diventerebbero tutti 0 = unisono) — solo la stringa vuota diventa cella muta. Confondere
  un refuso di battitura con un unisono vero e' esattamente la confusione vietata da
  CLAUDE.md regola 3. Il parser invece **rifiuta** esplicitamente l'input non valido (l'editor
  ripristina il valore precedente, non scrive nulla), entro un range sano di ±48 semitoni
  (4 ottave, protezione contro refusi come "1200" invece di "12").
- **`tests/cell_input_parser_test.cpp`** (nuovo, stesso schema di `pitch_latch_test`): 19
  verifiche — vuoto/spazi→muto, **"0"→valore esplicito zero, distinto dalla cella vuota**
  (verifica diretta di CLAUDE.md regola 3), interi positivi/negativi entro range, fuori
  range rifiutato, testo spazzatura rifiutato esplicitamente (mai coerto a 0, il contrario
  esatto della trappola in `CsvIo`). Tutte verdi.
- **`src/ui/DegreeNames.h`** (nuovo): tabella statica dei nomi di grado leggibili (R, b2, 2,
  b3, 3, 4, b5, 5, b6, 6, b7, 7 — convenzione standard di teoria), richiesta esplicitamente
  dal PRD (§8.2, "intestazioni di colonna che mostrano il grado in forma leggibile"). Non
  esisteva prima. Solo presentazione: `HarmonyEngine` continua a ragionare in interi 0-11.
- **`src/ui/PresetTableEditor.{h,cpp}`** (nuovo, primo componente custom del progetto — fin
  qui tutto era `juce::Rectangle::removeFromTop/Left` a mano dentro `PluginEditor`, coerenza
  mantenuta: niente `TableListBox`/`Grid`, solo una griglia di `juce::Label` editabili con lo
  stesso stile): 12 colonne (gradi) x 8 righe (voci), stessa indicizzazione di
  `harmony::Table` (`table[grado][voce]`, nessuna traduzione di indici). `showPreset(idx)`
  ricarica le 96 celle dal modello; ogni cella valida l'input via `CellInputParser` al
  commit e scrive con `PresetLibrary::setCell` tramite `editPresetLibrary` (stesso pattern
  gia' usato da tutti gli altri bottoni preset).
- **`src/PluginEditor.{h,cpp}`**: nuovo membro `presetTableEditor`, aggiunto sotto il campo
  nome preset. **Un solo punto di aggancio** per il refresh: dentro
  `syncPresetSelectionFromParameter()` (chiamata da tutti i percorsi che cambiano
  selezione/contenuto — add/duplicate/delete/move/import CSV/load global chiamano gia'
  `selectPresetIndex()` a valle, che forza il passaggio li'). Non serve agganciarsi anche a
  `refreshPresetBoxFromLibrary()` come inizialmente pianificato: verificato leggendo ogni
  singolo `onClick` che tutti i percorsi rilevanti gia' passano da `selectPresetIndex()`.
  Finestra ingrandita per fare spazio alla griglia (~214px): `setResizeLimits` da
  `(460,950)-(1200,1230)` a `(500,1170)-(1200,1450)`, `setSize` da `500x990` a `520x1200`.
- **`CMakeLists.txt`**: `src/ui/PresetTableEditor.cpp` aggiunto ai sorgenti del target
  principale (non c'e' glob nel progetto); nuovo target/test `cell_input_parser_test`.
- **Non toccato**: `HarmonyEngine`, `CsvIo` (la trappola descritta sopra resta nel suo
  dominio originale, l'I/O di file esterni — cambiarne il comportamento avrebbe impatto
  sulla retrocompatibilita' delle sessioni salvate, fuori scope), `PsolaShifter`,
  `PitchDetector`, `PhraseScheduler`, `PlayModeInput` — nessuno di questi era in causa.
- **Verificato**: le 5 suite di test verdi (`cell_input_parser_test` nuovo, le altre 4
  invariate), build VST3 e Standalone riuscite (solo il consueto fallimento di copia
  post-build per permessi), `pluginval --strictness-level 10` **SUCCESS** su VST3 (incluso
  "Editor Automation", che apre/ridimensiona l'editor senza errori — nessuna occorrenza di
  fail/error/crash nel log completo).
- **Verifica visiva tentata ma NON conclusiva**: lanciata la Standalone appena compilata e
  catturato uno screenshot via `PrintWindow` (API Win32). **Risultato positivo**: i valori
  mostrati nella griglia per il preset di default ("Maj") corrispondono ESATTAMENTE al
  calcolo a mano dell'algoritmo (`generateDropVoicingTable` con toni {0,4,7,11}) su tutte le
  colonne visibili — prova diretta che indicizzazione e lettura dal modello sono corrette,
  non solo che "compila". **Risultato inconcludente**: lo screenshot mostra solo 10 delle 12
  colonne (e, nelle righe preesistenti Fix/Move e Fmt/Voice mai toccate in questa sessione,
  solo 7-8 degli 8 controlli) — il taglio resta IDENTICO anche ridimensionando la finestra
  fisica da 520 a 900 a 1200px, il che esclude un problema di spazio reale (a 1200px di
  larghezza ci sarebbe abbondanza di margine per 12 colonne da ~40px l'una). Precedente
  diretto in `handsoff.md` sessione 10: lo stesso identico sintomo sugli 8 knob Fmt/Voice fu
  **confermato dall'utente come bounds validi, solo piccoli** — cioe' funzionavano davvero,
  il problema era percettivo (sessione 10), non di rendering. Ipotesi piu' probabile:
  `PrintWindow` non cattura in modo affidabile il contenuto renderizzato via accelerazione
  hardware di JUCE (limite noto di quell'API Win32 con Direct2D/OpenGL), non un bug del
  layout. **Non e' pero' una prova**, solo un'ipotesi con precedente a favore — CLAUDE.md
  regola 12 vieta di dichiarare "verificato" qualcosa che non e' stato davvero verificato:
  serve la conferma dell'utente in Ableton/Standalone con i propri occhi, vedi §6.
- **NON ancora verificato dall'utente** (CLAUDE.md regola 12 — per la UI, non il suono):
  aprire il plugin e controllare che tutte e 12 le colonne/8 righe siano visibili e
  leggibili senza sovrapposizioni; editare una cella a mano e verificare che l'armonizzazione
  dal vivo la rispetti; verificare che "0" e cella vuota si comportino in modo diverso;
  Export CSV di un preset editato a mano e reimportarlo (round-trip).

**Novita' sessione 14 — "un click solo a inizio nota, specialmente legato": stato PSOLA mai resettato fra una nota e l'altra sullo stesso slot fisico:**

L'utente ha riascoltato il fix di sessione 13 (dissolvenza campione-per-campione): "Migliorato ma non al 100%. Ora alle volte capita che ci sia un solo click solo ad inizio nota (specialmente quando viene suonata legata)." Sintomo distinto da quelli gia' corretti (non piu' un salto di ampiezza al rilascio/mix — quelli erano gia' stati sistemati) — la posizione ("a inizio nota") e la ricorrenza ("specialmente legato") hanno indirizzato l'indagine altrove: cosa succede quando uno slot fisico gia' usato viene RIASSEGNATO a una nuova nota.

- **Diagnosi**: `Voice::processAdd` esce subito (`if (shifter == nullptr || isSilent()) return;`) quando la voce e' completamente silenziosa — cioe' smette del tutto di chiamare `shifter->process()`. Il `PsolaShifter` di quello slot fisico resta quindi CONGELATO con qualunque contenuto avesse nella sua pipeline interna (`inBuf`/`outBuf`/`envBuf`, epoch) nel momento esatto in cui si e' azzittito. Verificato per `grep` che `shifter->reset()` non viene MAI chiamato durante la vita normale del plugin — solo a `prepareToPlay` (dentro `Voice::prepare`, che chiama gia' `reset()` internamente a fine `PsolaShifter::prepare()`) e nel path di reset completo del plugin, mai fra una frase e la successiva quando lo stesso slot fisico viene riassegnato a una nuova nota (`PhraseScheduler::triggerNewPhrase` → `allocateFreeSlot`).
- **Perche' e' un problema reale, non solo teorico**: la dissolvenza anti-click dura `kDeclickMs = 8ms` (353 campioni a 44.1kHz), ma la latenza dichiarata del motore PSOLA (`2*maxPeriod + maxBlock`) e' SEMPRE piu' lunga — calcolato per ogni livello di Stability: Fast 13.6ms, Balanced 21.5ms, Accurate 30ms. In OGNI configurazione, la dissolvenza si considera "finita" (`isSilent()==true`, si smette di chiamare `process()`) MOLTO prima che la pipeline interna del motore abbia davvero finito di smaltire il contenuto della nota precedente (fino a ~22ms di "resto" a Balanced). Quando quello slot viene poi riassegnato a una nuova nota, i primi campioni della nuova nota sono in parte ancora sintesi residua di quella vecchia.
- **Perche' "specialmente legato"**: la dissolvenza (8ms) e' molto piu' breve della durata tipica di una nota, quindi uno slot fisico si libera e viene riassegnato quasi ad ogni nuova nota gia' dalla seconda/terza nota di una frase — non e' un caso raro. In legato l'orecchio si aspetta continuita' assoluta (nessun transiente d'attacco reale a mascherare un piccolo difetto), mentre in staccato il vero attacco della nota maschera parzialmente un glitch della stessa entita'.
- **Verificato PRIMA di scrivere il fix (CLAUDE.md regola 12/13)**: nuovo Test 9 in `tests/psola_test.cpp`. Confronta tre esecuzioni dello stesso `PsolaShifter`: (a) "fresco" (mai usato, processa solo il segnale B), (b) "sporco" (ha gia' processato un segnale A con timbro diverso, poi smette di essere chiamato per un po' — il "mute" — poi riprende su B SENZA `reset()`, esattamente il comportamento attuale), (c) "reset-then-resume" (come (b) ma con `reset()` prima di riprendere — il fix proposto). Risultato **misurato PRIMA di toccare `Voice.h`**: (b) diverge da (a) di uno scostamento massimo di **0.214** (sostanziale, non rumore numerico) — il bug e' reale e misurabile, non solo un'ipotesi plausibile; (c) e' **bit-per-bit identico** ad (a) (scostamento 0.00000000) — conferma che `reset()` riporta davvero lo shifter allo stesso stato di uno slot mai usato, non un rimedio parziale.
- **Fix**: `Voice::setMuted(bool)` (`src/voices/Voice.h`) ora chiama `shifter->reset()` alla transizione silenzio→attiva (`!shouldBeMuted && isSilent()`), non ad ogni chiamata (la maggior parte non cambia nulla, resta economica). Un solo punto, copre uniformemente tutti i percorsi di riattivazione: nuova frase su uno slot libero, cella FR-17 ripopolata su una frase ancora viva, `PlayModeInput` che riusa uno slot per una nuova nota MIDI — nessuno di questi richiede modifiche separate, tutti passano da `setMuted(false)`.
- **`src/voices/Voice.h`**: `setMuted` modificato (vedi sopra). Nessuna nuova dipendenza, `shifter->reset()` gia' esposto dall'interfaccia `PitchShifter`.
- **`tests/psola_test.cpp`**: nuovo Test 9 (vedi sopra).
- **Non toccato**: `PsolaShifter` (il motore stesso, `reset()` gia' corretto — il bug era SOLO nel non chiamarlo mai al momento giusto), `PhraseScheduler`, `PlayModeInput`, `Glide`, `PluginProcessor` — nessuno di questi era in causa.
- **Verificato**: `psola_test` verde incluso il nuovo Test 9 (9/9 gruppi), le altre 3 suite invariate, build VST3 e Standalone riuscite (solo il consueto fallimento di copia post-build per permessi), `pluginval --strictness-level 10` SUCCESS su VST3 (nessuna occorrenza di fail/error/crash nel log completo).
- **SMENTITO ALL'ASCOLTO**: l'utente ha riascoltato e riportato "non è cambiato niente. Rimane comunque qualche click all'inizio." Il meccanismo diagnosticato (stato PSOLA congelato, mai resettato fra note sullo stesso slot fisico) resta VERO e MISURATO numericamente (Test 9), e il fix resta corretto per QUEL meccanismo specifico — ma evidentemente **non e', o non e' l'unica, causa del click che l'utente sente**. Non toccato oltre in questa sessione su richiesta esplicita dell'utente ("lasciamo in stand by questo punto") — vedi §6 per lo stato e le ipotesi ancora da esplorare quando si riprende.

**Novita' sessione 13 — click residui + wobbling: la causa reale era il block size dell'host, non una nuova dissolvenza mancante:**

A inizio sessione l'utente ha condiviso uno screenshot delle impostazioni audio di Ableton
usate per il test di sessione 12: **buffer d'uscita 4096 campioni, driver MME/DirectX (non
ASIO), 44.1kHz, 92.9ms di latenza dichiarata**. Test con Fix/Move tutte su Move (default),
sorgente una clip MIDI su un synth, non ingresso live. Questo singolo dato ha permesso di
diagnosticare ENTRAMBI i residui riportati a fine sessione 12 per calcolo diretto, senza
bisogno di nuove ipotesi: `processBlock` riceve blocchi fino a 4096 campioni, e OGNI
controllo del progetto (Glide, parametri del PitchShifter) si aggiorna una volta per
blocco.

- **Causa dei click residui**: `Glide::process(numSamples)` (`src/dsp/Glide.h`) ritorna un
  SOLO valore per l'intero blocco — `Voice::processAdd` lo applicava come guadagno
  costante su tutto il buffer. `kDeclickMs = 8ms` a 44.1kHz sono 353 campioni: con
  `numSamples = 4096`, `remainingSamples (353) - numSamples (4096) <= 0` fa scattare
  `current = target` all'INTERNO della stessa chiamata — la dissolvenza di sessione 12 era
  quindi un **no-op completo** in questa configurazione, il salto restava pieno-scala in
  un solo campione, esattamente il click che doveva eliminare. Stesso identico difetto su
  `dryGlide`/`wetGlide` (`PluginProcessor.cpp`) e sul glide musicale FR-17 (30ms = 1323
  campioni, comunque molto meno di 4096).
- **Causa del wobbling**: `Voice::processAdd` chiama `setPitchShiftSemitones`/
  `setInputF0Hz`/`setFormantRatio` una volta per blocco, e `PsolaShifter` li applica con
  assegnazione diretta, senza interpolazione. A 4096 campioni, `continuousInputMidiNote`
  viene letta una volta ogni 92.9ms (l'ULTIMA delle ~17 stime che Cycfi Q produce nel
  frattempo, le altre scartate); `currentPeriod()` e' un intero arrotondato che si muove a
  gradini; `synthPos` e' un accumulatore di fase persistente che cambia passo bruscamente
  a ogni aggiornamento di `P`/`alpha`. Risultato: il pitch d'uscita e' una scalinata
  aggiornata a ~10.8 Hz — un ondeggiamento a quella frequenza e' precisamente cio' che si
  percepisce come "wobbeling". Analisi completa fatta leggendo il codice (nessuna modifica
  in questa sessione, vedi §6 per il fix previsto).
- **Nota per l'utente**: MME/DirectX a 4096 campioni e' il caso peggiore possibile su
  Windows, e amplifica ogni difetto di ~32x rispetto a un ASIO a 128 campioni. Il codice
  andava comunque corretto (un plugin deve suonare bene a qualunque buffer size), ma resta
  utile provare con ASIO4ALL/FlexASIO a 128-256 campioni per separare "bug del plugin" da
  "artefatto di configurazione" nei test futuri.
- **Fix (solo per i click, non ancora per il wobbling — vedi sotto)**: nuova
  `Glide::processRamp(numSamples)` (`src/dsp/Glide.h`), che espone la STESSA retta che
  `process()` gia' calcola internamente (start/target/durata fissati da `setTarget`, mai
  ricalcolati per blocco) come struct `{ startValue, increment, rampSamples }`, cosi' il
  chiamante puo' interpolare campione per campione invece di applicare un unico scalare.
  `increment` e' algebricamente identico a `(target-start)/totalGlideSamples` in qualunque
  punto della rampa (dimostrato: `current` e' sempre esattamente sulla retta originale),
  quindi nessuna deriva quando la stessa rampa e' spezzata su piu' blocchi di dimensione
  diversa — verificato numericamente, non solo argomentato (vedi test sotto).
  `Glide::process()` resta INVARIATA e continua a essere usata per `offsetGlide` (FR-17):
  il pitch shift deve restare un valore per blocco, perche' `PitchShifter::setPitchShiftSemitones`
  accetta un solo rapporto per chiamata a `process()` — la sua granularita' e' il problema
  del wobbling, non di questo fix.
- **`src/voices/Voice.cpp`**: `ampGlide.process(numSamples)` sostituito da
  `ampGlide.processRamp(numSamples)`; il ciclo finale di mixing applica il guadagno
  campione per campione (rampa fino a `rampSamples`, poi fermo al target per il resto del
  blocco). Un solo punto, copre tutte e quattro le sedi di dissolvenza gia' esistenti
  (fine frase, cella svuotata su nota tenuta, uscita Play mode — tutte passano da
  `processAdd`).
- **`src/PluginProcessor.cpp`**: stesso schema per `dryGlide`/`wetGlide` nel ciclo sui
  canali di uscita (copre anche il bypass, che li sostituisce di netto).
- **`tests/glide_test.cpp`** (nuovo, header-only, stesso schema di `pitch_latch_test.cpp`):
  6 gruppi di verifiche. **Verificato PRIMA di scrivere il fix (CLAUDE.md regola 12/13)**:
  il file, scritto per chiamare la non ancora esistente `processRamp`, non compilava
  (`error C2039: 'processRamp': non e' un membro di 'Glide'`) — confermato che il test
  fallisce "prima" nel senso piu' forte possibile. Dopo l'aggiunta di `processRamp`: 15/15
  verifiche verdi, incluso un TEST 1 che riproduce e misura numericamente il bug originale
  (`process()` applicato come costante su un blocco da 4096 campioni: salto dell'88%+ di
  fondoscala in un solo campione) per confronto diretto con il comportamento corretto.
- **Diagnostica**: nuovo `getLastBlockSize()` (atomico, scritto ogni blocco in
  `PluginProcessor::processBlock`), mostrato nella label "Detected" dell'editor (`blk N`)
  — permette di confermare all'ascolto/a vista che l'host sta davvero usando il buffer che
  si sospetta, invece di assumerlo dallo screenshot di una singola sessione.
- **Non toccato**: `PsolaShifter`, `PitchDetector`, `OnsetDetector`, `PhraseScheduler`,
  `PlayModeInput`, `HarmonyEngine`, `VoicePool` — nessuno di questi era in causa per il
  fix dei click (il wobbling, che li coinvolge, resta un problema aperto, vedi §6).
- **Verificato**: `glide_test` verde (falliva a non compilare prima), le altre 3 suite
  invariate (`psola_test` bit-per-bit identico), build VST3 e Standalone riuscite (solo il
  consueto fallimento di copia post-build per permessi), `pluginval --strictness-level 10`
  **SUCCESS** su VST3 (nessuna occorrenza di fail/error/crash nel log completo) — non
  eseguito su Standalone, che pluginval non sa scansionare come bundle di plugin (coerente
  con `.github/workflows/build.yml`, che lo esclude anche in CI).
- **NON ancora verificato all'ascolto** (CLAUDE.md regola 12): l'utente deve riascoltare in
  Ableton nella STESSA configurazione (4096 campioni, MME/DirectX) e confermare se i click
  residui sono spariti. Il wobbling NON e' stato toccato in questa sessione (fix piu'
  invasivo, da discutere prima — vedi §6): resta atteso finche' non si interviene sulla
  Fase 4.

**Trovati durante l'indagine ma FUORI SCOPE di questa sessione, non corretti — da
discutere prima di toccarli:**
1. **Swap di Stability senza dissolvenza** (`VoicePool::applyPendingStabilityChangeIfSafe`
   → `Voice::swapShifterNoAlloc`): lo shifter attivo viene sostituito con uno nuovo a
   buffer azzerati e latenza diversa, a piena ampiezza, senza rampa — un salto vero, ma
   solo quando l'utente cambia il controllo Stability mentre le voci suonano.
2. **`setLatencySamples()` chiamata dall'audio thread** (`PluginProcessor.cpp`, dopo lo
   swap di Stability): non e' documentata come RT-safe in JUCE — potenziale violazione di
   CLAUDE.md regola 1 / PRD §9.4, mai notato prima perche' nessuno aveva ancora tracciato
   questo percorso in dettaglio.
3. **`quantizedPlayedNote` azzerato a 0 ogni blocco** (`PluginProcessor.cpp`) e
   riassegnato solo se `inputIsStable`: in modalita' **Fix** (non provata dall'utente in
   questo giro, ha testato solo Move), un blocco con confidenza bassa produce
   `semitonesToApply ≈ −(nota corrente)`, un salto di decine di semitoni clampato a −36 da
   `PsolaShifter::setPitchShiftSemitones`. Latente oggi, potenzialmente reale in Fix.
4. **`applyPendingStabilityChangeIfSafe` ritorna `true` spurio** se il pool ha zero slot
   (caso non raggiungibile nell'uso normale, solo teorico), causando un
   `setLatencySamples(0)` non voluto.

**Novita' sessione 12 — note saltate: corsa fra onset e rilevamento di pitch (FR-43/45/46):**

A fine sessione 11 l'utente aveva segnalato, senza altro dettaglio, che il plugin "continua a saltare alcune note ma senza una logica troppo precisa" (oltre a un problema di timbro separato, vedi sotto). Diagnosticato leggendo il codice, non ipotizzato — la stessa corsa onset/pitch gia' sospettata (mai confermata) per i punti 2/5 del test di sessione 10.

- **Causa 1 (principale)**: `PluginProcessor.cpp` calcola `offsets` (la tabella di offset per voce) solo dentro `if (! playModeEnabled && inputIsStable)`; se il pitch non e' ancora confidente in quel blocco, l'array resta quello di default — 8 celle vuote. `PhraseScheduler::triggerNewPhrase` (chiamato da `PhraseScheduler::process` sul ramo onset) salta l'allocazione dello slot per ogni cella vuota: con offsets tutti vuoti la frase nasce **attiva ma con zero slot fisici**, muta per sempre — il ramo di live-update (FR-17) aggiornava solo `frozenOffsets`, mai gli slot mancanti. Chi vince la corsa: `OnsetDetector` apre il gate in ~10ms su soglia di livello, mentre `cycfi::q::pitch_detector` (BACF, minimo 60Hz) ha bisogno di piu' periodi per dare confidenza — al primo attacco dopo un silenzio l'onset arriva quasi sempre prima.
- **Causa 2 (concorrente)**: `pitchDetector.reset()` non veniva mai chiamato dopo `prepareToPlay`. Nota e confidenza dell'ultima stima sopravvivevano al silenzio finche' il rilevatore non ne calcolava una nuova da solo: un onset poteva quindi trovare `hasStableSignal()` gia' vero ma su una nota STANTIA (quella precedente) — la frase nasceva armonizzata sull'accordo sbagliato invece che in attesa del pitch vero. Spiega il "senza una logica precisa": a seconda di chi vince la corsa, la stessa gestualita' produceva una nota muta, una nota sull'accordo sbagliato, o una nota corretta.
- **`src/voices/PhraseScheduler.{h,cpp}`**: nel ramo di live-update (`inputIsStable`), oltre ad aggiornare `frozenOffsets` come prima, ora si completa l'allocazione degli slot rimasti vuoti al trigger (`slotIndices[v] < 0` ma la cella ha un valore), riusando `allocateFreeSlot()` cosi' com'e'. Nessun timeout, nessuno stato nuovo: se il pitch non arriva mai la frase resta a zero slot e si libera normalmente alla chiusura del gate. Il caso gia' funzionante (onset con pitch gia' confidente) non cambia: il ramo `onsetDetectedThisBlock` ha ancora precedenza nell'`else if`. Nuovo contatore cumulativo `numLateBindingsTotal` (atomico, `getNumLateBindings()`) per rendere l'intervento osservabile.
- **`src/PluginProcessor.{h,cpp}`**: nuovo `signalPresentLastBlock` (solo audio thread); sul fronte di DISCESA di `signalPresent` si chiama anche `pitchDetector.reset()`, accanto al gia' presente `pitchLatch.reset()`. Verificato RT-safe leggendo `PitchDetector::reset()` (due assegnazioni float + `cycfi::q::pitch_detector::reset()`, che e' solo `_frequency = 0.0f` — nessuna allocazione/lock). Ordine importante: questo fix da solo, senza il punto precedente, avrebbe reso `inputIsStable` falso ad ogni attacco (peggiorativo) — i due fix vanno insieme.
- **Diagnostica**: nuovo atomico `lastGateOpen` (stato del gate, distinto da `lastInputStable` che riflette la confidenza del pitch) esposto via `getLastGateOpen()`. La label "Detected" in `PluginEditor.cpp` ora mostra anche `gate open/closed` e `late-bindings N` — permette di confermare all'ascolto che il fix interviene davvero (contatore che sale) invece di limitarsi a sperare che compili (CLAUDE.md regola 12).
- **Non toccato**: `PitchDetector`, `OnsetDetector`, `Voice.cpp`, `PsolaShifter`, `HarmonyEngine`, `PitchLatch` — nessuno di questi era in causa.
- **Verificato**: build VST3 e Standalone riuscite (solo il consueto fallimento di copia post-build per permessi, atteso e documentato dalla sessione 4), `pluginval --strictness-level 10` **SUCCESS** (exit code 0, nessuna occorrenza di fail/error/crash nel log completo), tutte e 3 le suite verdi via `ctest` (`psola_test`, `override_manager_test`, `pitch_latch_test` — nessuna tocca `PhraseScheduler`, riverificate per scrupolo dato che questo e' proprio il file modificato).
- **NON ancora verificato all'ascolto** (CLAUDE.md regola 12): l'utente deve suonare note staccate ripetute in Ableton e controllare che (a) nessuna venga saltata, (b) "Active" non resti a 0 sul primo attacco, (c) `late-bindings` salga davvero, (d) nessuna nota venga armonizzata sull'accordo della nota precedente. Solo dopo questa conferma il fix puo' considerarsi completo, non solo compilato.

**Novita' sessione 12 (continuazione) — timbro "robotico e granuloso": bug reale trovato e corretto in `PsolaShifter::emitGrain`:**

Dopo la conferma all'ascolto del fix precedente, l'utente ha chiesto di passare al feedback di qualita' lasciato in sospeso: "non fedele al segnale sorgente, robotico e granuloso". Dei tre candidati annotati a fine sessione precedente (vedi sotto la versione originale di questa nota), il primo si e' rivelato un bug reale, verificato per calcolo PRIMA di toccare il codice e poi confermato empiricamente con un test scritto apposta.

- **Diagnosi**: la garanzia di sovrapposizione dei grani in `emitGrain` (`2W >= synthPeriod`, commento originale nel codice) e' derivata assumendo implicitamente `beta = 1` — `Lg = 2W` nella derivazione. Il codice pero' calcola `Lg = round(2W/beta)`, per la correzione formantica (FR-39/41), senza mai ricompensare la sovrapposizione quando `beta != 1`. `Voice.cpp` applica la correzione formantica automatica **di default** (`formantSpread = 1.0`, non un caso limite): qualunque voce shiftata verso il basso ha `beta > 1` (schiarimento) in condizioni normali. Calcolo a mano: a -12 semitoni (il confine esatto dove il margine originale e' gia' teso al limite) con `beta` reale prodotto da `Voice.cpp` (k=0.3) il grano risulta ~19% piu' corto del minimo gia' provato appena sufficiente — a scostamenti maggiori (-14.5/-17 semitoni, dove sia `alpha` cala sia `beta` cresce) il deficit peggiora.
- **Verifica PRIMA del fix (coerente con l'abitudine di sessione 11 — scrivere il test che fallisce prima di correggere)**: aggiunto **Test 8** a `tests/psola_test.cpp`, stessa misura del Test 6 (RMS a breve termine minimo/medio) ma con la `beta` REALE che `Voice.cpp` produrrebbe (stessa formula, k=0.3, spread=1.0) invece di `beta=1` fisso. Eseguito sul codice non ancora corretto: **FALLITO su -14.5 e -17 semitoni** (ratio 0.222 e 0.146, sotto la soglia 0.25) — il bug non era solo teorico, era misurabile.
- **Fix**: aggiunto un floor a `Lg` basato su `synthPeriod` (indipendente da `beta`), invece di allargare `W` (la semiampiezza in campioni SORGENTE). Scelta deliberata: allargare `W` invece del floor su `Lg` avrebbe fatto piu' contenuto sorgente entrare in ogni grano, rischiando di reintrodurre esattamente l'artefatto di ottava del Test 1 (sessione 9) che l'attuale margine minimo su `W` evita di proposito. Il floor su `Lg` agisce solo nel dominio di uscita, dove serve davvero (far toccare grani consecutivi), senza toccare quanto segnale sorgente ciascun grano contiene.
- **Verifica DOPO il fix**: Test 8 ora passa (ratio 0.540/0.439/0.379). **Test 1 e Test 6 (il caso `beta=1` gia' validato) restano numericamente identici, cifra per cifra**, a prima del fix — conferma per calcolo (non solo per assenza di regressioni nei test) che il floor e' un no-op quando `beta=1`, esattamente come previsto analiticamente prima di scrivere la modifica.
- **`src/dsp/PsolaShifter.cpp`**: `emitGrain`, floor su `Lg` (vedi sopra).
- **`tests/psola_test.cpp`**: nuovo Test 8. Nota nel codice: la formula di `beta` e' una copia intenzionale di quella in `Voice.cpp` — se `k` cambia la', va aggiornato anche qui.
- **Non toccato**: `Voice.cpp`, `PitchDetector`, `OnsetDetector`, `PhraseScheduler`, `HarmonyEngine` — nessuno di questi era in causa per questo bug specifico.
- **Verificato**: build VST3 e Standalone riuscite, tutte e 3 le suite verdi via `ctest`, `pluginval --strictness-level 10` SUCCESS (exit code 0, log completo senza fail/error/crash).
- **NON ancora verificato all'ascolto** (regola 12): questo e' un fix di un bug numericamente reale e misurabile, ma non e' garanzia che risolva TUTTO il feedback "robotico e granuloso" — restano gli altri due candidati sotto, non ancora esplorati.

**Rimangono FUORI SCOPE — altri due candidati per il timbro, non ancora esplorati:**
1. Individuazione degli epoch come massimo di `|x|` in una finestra ±P/4 (`detectEpochs`): su segnali non impulsivi (synth, fiati) puo' posizionare male gli epoch, incoerenza di fase fra grani = "robotico".
2. Otto istanze PSOLA indipendenti sullo stesso ingresso: artefatti correlati che si sommano invece di mediarsi.

**Novita' sessione 12 (continuazione) — "scricchiolii, click, glitch" e armonizzazione "non stabile al 100%": bug architetturale trovato e corretto, dissolvenza di ampiezza per le voci:**

L'utente ha riportato (dopo aver confermato all'ascolto il fix dell'inviluppo) due sintomi distinti: click/scricchiolii ricorrenti, e un'armonizzazione che "suona sempre" ma non sembra stabile al 100% (senza sapere se dovuto a formanti, ottave riarmonizzate o altro). Diagnosticato leggendo `PhraseScheduler.cpp`, `PlayModeInput.cpp` e `PluginProcessor.cpp` insieme (non isolatamente): un'unica causa architetturale spiega entrambi i sintomi.

- **Causa**: ogni volta che una voce smetteva di essere processata, la sua ampiezza passava da piena a zero in UN SOLO BLOCCO, senza alcuna dissolvenza — un salto di ampiezza discontinuo, la definizione stessa di un click. Questo succedeva in QUATTRO punti distinti, tutti routine, non casi limite:
  1. **`PhraseScheduler::process`, silenzio totale** (`freeAllPhrases()`): TUTTE le frasi attive, con tutte le loro voci potenzialmente a piena ampiezza, smettevano di essere processate nello stesso blocco — un taglio multiplo simultaneo a ogni fine-frase/nota.
  2. **`PhraseScheduler::process`, nuovo onset con MENO voci della frase precedente** (voicing con un numero diverso di celle non vuote): le voci della vecchia frase non reclamate dalla nuova smettevano di essere processate, mentre quelle reclamate continuavano con un semplice glide di intonazione (questo caso, quando il conteggio voci NON cambia, era gia' corretto — nessun click li').
  3. **`PhraseScheduler::process`, cella diventata vuota su una frase ANCORA viva** (FR-17, cambio accordo su nota tenuta): `voice.setMuted(true); continue;` — nessuna chiamata a `processAdd`, taglio istantaneo nel bel mezzo di una nota sostenuta. Probabile causa diretta della sensazione di "non stabile al 100%" segnalata dall'utente, perche' e' esattamente lo scenario "modifiche in corso d'opera" che sospettava.
  4. **`PlayModeInput::process`**: stesso pattern, sia su note-off (`slotNote` torna -1) sia sull'uscita dalla modalita' Play (`!modeActive`, ritorno immediato senza toccare nessuna voce).
- **Fix — dissolvenza di ampiezza per voce (`Voice.h/.cpp`)**: nuovo `Glide ampGlide`, rampa fissa a 8ms (`kDeclickMs`, costante DELIBERATAMENTE indipendente dal `glideTimeMs` musicale di FR-17 — quello e' scelto dall'utente per il movimento armonico, questo e' un tempo anti-click fisso e breve). `setMuted(bool)` non azzera piu' di netto: avvia/ferma la rampa. `processAdd` non esce piu' subito se `muted`, esce solo se `isSilent()` (muted E rampa assestata a zero) — nel mezzo, continua a processare e a moltiplicare l'uscita per l'ampiezza corrente della rampa. Una voce completamente inattiva da tempo non consuma comunque CPU inutile (esce al primo controllo).
- **Fix — rilascio morbido delle frasi (`Phrase.h`, `PhraseScheduler.{h,cpp}`)**: nuovo flag `Phrase::releasing`. La vecchia `freePhrase()` (azzeramento istantaneo) e' rinominata `hardFreePhrase()` e usata SOLO per il furto d'emergenza (FR-52, pool esaurito — li' serve lo slot fisico SUBITO, la continuita' la da' gia' il Glide dell'offset sul nuovo target, nessun click). Il caso normale (fine frase, silenzio, superata da un nuovo onset) usa la nuova `beginRelease()`: la frase resta `active=true` (protetta da riuso) ma tutte le sue voci sfumano; una volta tutte silenziose (`Voice::isSilent()`), la frase si libera per davvero. `allocateFreeSlot()` preferisce rubare una frase gia' in rilascio (meno disruptivo) prima di rubare una frase ancora piena.
- **Fix — FR-17, cella tornata vuota su frase viva**: stesso principio applicato per singola voce dentro il ciclo di mixing (non serve `releasing` sulla frase intera, la frase resta viva, solo quella colonna sfuma).
- **Fix — `PlayModeInput.cpp`**: stesso pattern per note-off e per l'uscita da Play mode (prima: ritorno immediato senza toccare le voci; ora: un ciclo che le muta/sfuma prima di uscire).
- **Fix — dry/wet/bypass (`PluginProcessor.{h,cpp}`)**: `dryLevel`/`wetLevel`/il fronte di `bypass` venivano letti come valore grezzo e applicati di netto per l'intero blocco — un secondo, indipendente meccanismo di click (automazione, CC bypass, il bottone Bypass stesso). Nuovi `dryGlide`/`wetGlide` (stessa rampa fissa 8ms), calcolati una sola volta per blocco (`Glide::process` muta stato interno) prima del ciclo sui canali.
- **`src/dsp/Glide.h`**: nuovo `isSettled() const` (verifica se la rampa ha raggiunto il target senza confronti in virgola mobile fragili dall'esterno).
- **Non toccato**: `PsolaShifter`, `PitchDetector`, `OnsetDetector`, `HarmonyEngine`, `PitchLatch`, `VoicePool` (solo consumato, non modificato) — nessuno di questi centrava con questo bug.
- **Verificato**: build VST3 e Standalone riuscite, tutte e 3 le suite verdi via `ctest` (nessuna tocca questo codice, ma rieseguite per scrupolo), `pluginval --strictness-level 10` SUCCESS (exit code 0, log completo senza fail/error/crash).
- **NON ancora verificato all'ascolto** (regola 12): questo e' un fix architetturale di un meccanismo confermato per lettura di codice (un salto di ampiezza discontinuo E' un click, per definizione — non serve l'ascolto per stabilirlo), ma la durata scelta (8ms) e' un punto di partenza ragionevole, non tarato; e non e' garanzia di risolvere l'INTERO feedback "non stabile al 100%" (restano da escludere formanti/PSOLA come contributo residuo, vedi sopra).
- **Limite noto accettato consapevolmente**: quando una frase `releasing` viene rubata (`allocateFreeSlot`) prima che la sua dissolvenza finisca, la rampa si interrompe bruscamente a meta' — comunque un salto MOLTO piu' piccolo (dalla poca ampiezza residua invece che da piena scala), non un problema nuovo introdotto, un compromesso deliberato per non complicare ulteriormente FR-52 in questa sessione.

**Novita' sessione 11 — isteresi di intonazione per note legate (FR-16/17):**

Dopo le correzioni di sessione 10, l'utente ha ripreso il test: con un sample Rhodes tutto funziona, ma cantando note legate (C→D→E, senza stacco netto) solo la prima nota si armonizzava. Diagnosi iniziale mia (instabilita' del rilevatore -> `freeAllPhrases()` durante lo scivolamento fra due note) — **corretta dall'utente**: ha riconosciuto che la causa reale e' l'assenza di isteresi sulla nota usata per il lookup nella tabella armonica (`quantizedPlayedNote`, un semplice `juce::roundToInt` ricalcolato da zero ogni blocco), e ha specificato lui stesso il comportamento voluto con tolleranza numerica precisa (±25 cent) e verificato a mano che la matematica di Fix/Move in `Voice.cpp` fosse gia' corretta (lo era).

- **`src/harmony/PitchLatch.h`** (nuovo, header-only come `Glide.h`, nessuna dipendenza JUCE come `OverrideManager`): isteresi con aggancio/sgancio a gradino. Entro ±25 cent dalla nota agganciata non cambia nulla; oltre la soglia si sposta di un semitono per volta verso la nota piu' vicina alla stima corrente — **bloccato (min/max) al risultato di un arrotondamento standard**, non un passo incondizionato.
- **Bug trovato e corretto nel MIO stesso primo tentativo, prima di consegnarlo**: un passo incondizionato (`heldNote += 1` ogni volta che si supera la soglia) rimbalza avanti e indietro ad ogni blocco durante uno scivolamento lento — il salto di un intero semitono supera quasi sempre la stima attuale del pitch, quindi la nuova nota agganciata risulta "oltre soglia" nella direzione opposta e la chiamata successiva la disfa immediatamente. Scoperto scrivendo il test 4 (verifica esplicita "nessun rimbalzo") **prima** di integrare nel plugin, non dopo — coerente con CLAUDE.md regola 8 (verificare prima di dichiarare fatto) e con l'abitudine di questa sessione di validare ogni pezzo isolatamente. Risolto bloccando lo scatto al risultato di un arrotondamento standard di `continuousMidiNote` (`std::min`/`std::max`), che non puo' mai superare né disfare la nota di destinazione.
- **`tests/pitch_latch_test.cpp`** (nuovo): 8 gruppi di verifiche — primo aggancio, tolleranza ±25 cent, scatto a gradino nelle due direzioni, assenza di rimbalzo (il test che ha scoperto il bug sopra), `onAttack` forza l'aggancio immediato, `reset()`, un salto ampio che si risolve un semitono a chiamata, e la replica esatta dell'esempio dell'utente (vibrato ±50 cent su C alterna B/C/C#). Un solo intoppo, non nel design ma nel test: `std::lround` a un pareggio esatto di mezzo semitono arrotonda sempre "lontano da zero" (quindi verso l'alto anche scendendo) — corretto usando −51 cent invece di −50 esatti nel test, un pareggio che con dati di pitch reali non si presenta mai.
- **`PluginProcessor.cpp`**: `quantizedPlayedNote = juce::roundToInt(continuousInputMidiNote)` sostituita con `pitchLatch.update(continuousInputMidiNote, onsetDetectedThisBlock)` — `onsetDetectedThisBlock` (un vero attacco) forza l'aggancio immediato senza isteresi, gia' disponibile a quel punto del metodo. `pitchLatch.reset()` nel ramo "segnale non stabile".

**Novita' sessione 11 (continuazione) — secondo bug: `freeAllPhrases()` legato alla confidenza del pitch invece che alla presenza del segnale:**

Dopo il fix dell'isteresi, l'utente ha riprovato: la label "Detected" ora segue correttamente C→D→E (confermando che l'isteresi funziona), ma l'armonizzazione continuava ad aggiornarsi solo sulla prima nota. Questo ha riportato in gioco la MIA ipotesi originale di sessione 11 (poi accantonata quando l'utente aveva indicato l'isteresi come causa) — **si e' rivelata comunque necessaria**, come causa distinta e concorrente, non alternativa.

- **Causa**: `PhraseScheduler::process()` liberava tutte le frasi (`freeAllPhrases()`) quando `!inputIsStable` — cioe' quando la CONFIDENZA del rilevatore di pitch scende sotto soglia. Durante uno scivolamento di intonazione fra due note cantate legato la confidenza puo' calare per pochi blocchi anche se il performer sta chiaramente ancora suonando (la periodicita' e' meno stabile mentre il pitch si muove) — abbastanza per svuotare tutto lo stato armonico. Senza un onset a ribattere (canto legato, per definizione), nulla lo ricostruisce: l'armonizzazione resta muta da li' in poi.
- **Fix, questa volta SENZA una finestra temporale** (esplicitamente scartata dall'utente in questa stessa sessione per l'ipotesi precedente): distinti due segnali che prima erano confusi in uno solo.
  - `signalPresent` (nuovo, da `onsetDetector.isGateOpen()` — il gate a livello di segnale, gia' esistente, non pitch-detection): "il performer sta ancora suonando qualcosa" — governa SOLO la liberazione delle frasi.
  - `inputIsStable` (invariato, da `PitchDetector::hasStableSignal()`): "il pitch di QUESTO blocco e' abbastanza confidente da fidarsene" — governa SOLO l'aggiornamento dal vivo degli offset (FR-17).
  - Durante un calo di confidenza transitorio con segnale ancora presente: non si libera nulla (il gate resta aperto) e non si aggiornano gli offset con un valore inaffidabile — si mantiene l'ultimo voicing valido, semplicemente non facendo nulla quel blocco. Nessuna soglia temporale, nessun ritardo di reazione aggiunto.
- **`PhraseScheduler::process()`** guadagna un parametro (`signalPresent`, distinto da `inputIsStable`); il ramo altrimenti "libera tutto" ora testa `!signalPresent`, il ramo di live-update (FR-17) resta condizionato a `inputIsStable`, con un terzo caso esplicito (segnale presente, nessun onset, pitch non confidente) che non tocca nulla.
- **`PluginProcessor.cpp`**: nuova variabile `signalPresent`; `pitchLatch.reset()` ora legato a `!signalPresent` invece che a `!inputIsStable` (un calo di confidenza transitorio non deve piu' azzerare l'aggancio dell'isteresi); in modalita' Play, sia `signalPresent` sia `inputIsStable` vengono forzati a `false` per la catena Harmonizer (stesso principio di sessione 9, esteso al nuovo segnale).
- **Non toccato**: `PitchDetector`, `OnsetDetector` (il gate esisteva gia', riusato cosi' com'era), `Voice.cpp`, `PsolaShifter`.
- **Verificato**: build VST3/Standalone riuscite, `pluginval --strictness-level 10` verde, tutte e 3 le suite di test ancora verdi (nessuna delle tre tocca `PhraseScheduler`, riverificate per scrupolo).
- **Non ancora verificato all'ascolto**: questo secondo fix, appena scritto — richiede un altro giro di test in Ableton sullo stesso C→D→E legato.
- **Scartata esplicitamente una finestra temporale di tolleranza (150ms)** proposta inizialmente da me per un'ipotesi diversa (instabilita' del rilevatore): l'utente ha correttamente osservato che, anche non toccando la latenza dichiarata del motore, avrebbe comunque introdotto un ritardo di reazione percepibile a fine frase. Non implementata; l'ipotesi "instabilita' -> `freeAllPhrases()`" resta non verificata e non toccata in questa sessione.
- **Verificato**: build VST3/Standalone riuscite, `pluginval --strictness-level 10` verde, tutte e 3 le suite di test verdi via CTest (`psola_test`, `override_manager_test`, `pitch_latch_test` nuovo).
- **Non toccato**: `Voice.cpp` (Fix/Move gia' corretti, verificato per calcolo diretto contro gli esempi in Hz dell'utente), `PhraseScheduler.cpp` (il meccanismo di sessione 10 resta invariato), `HarmonyEngine`, `PitchDetector`, `OnsetDetector`.

**Novita' sessione 10 — primo test reale in Ableton, bug nel ciclo di vita delle frasi (FR-45/46), fix UI, diagnostica pitch:**

Prima sessione in cui l'utente ha effettivamente ascoltato/usato in Ableton il lavoro delle sessioni 8-9 (PSOLA, Formanti, CC MIDI, Play — tutto verificato fino a qui solo con test numerici, build e `pluginval`). Ha riportato 8 osservazioni con uno screenshot dell'editor e un file Excel che illustra il comportamento atteso del futuro Pattern Ritmico. Confermato funzionante senza modifiche: Dry/Wet, Voices, Root/Chord/Name, Up/Down, e il cambio di accordo su nota tenuta che si aggiorna dal vivo (FR-17).

- **Bug principale — le frasi superate da un nuovo onset non si liberavano mai** (`PhraseScheduler::process()`, ramo onset): venivano solo "smontate" (`isLive=false`) ma restavano `active` per sempre, finche' non rubate (pool esaurito) o il segnale taceva. Conseguenza osservata dall'utente: ogni frase "smontata" continuava a chiamare `voice.processAdd()` sul segnale live CORRENTE coi propri offset congelati vecchi — cambiare preset tra una nota e l'altra faceva sentire insieme tutti i preset selezionati fino a quel momento, e le voci attive salivano di `numVoices` ad ogni nuovo attacco (osservato: 0→4→8→12). Cambiare Stability "sembrava" resettare tutto, ma non tocca `phrases[]` — quasi certamente solo l'effetto del silenzio naturale mentre si maneggia la UI.
- **Non risolto con un comportamento fisso**: l'utente ha chiarito (con un file Excel che illustra tre note in corsa scaglionata nel tempo, C→E→D, dove la seconda nota della corsa di E taglia la terza nota — ancora "in coda" — della corsa di C) che il caso reale a cui pensa e' il futuro **Pattern Ritmico** (FR-47..49, `[V1.1]`, voci scaglionate nel tempo dentro una frase, non ancora costruito). Li' "tronca la coda non ancora suonata" vs "lasciala finire, la nuova armonizzazione parte dal prossimo suono" e' una scelta creativa legittima, non un bug. Oggi, senza pattern (tutte le voci di una frase partono insieme, nessuna mai "in coda"), le due scelte collassano nello stesso caso limite. **Risolto con un bottone invece di un'interpretazione fissa**: nuovo parametro APVTS `keepPhraseTails` ("Keep Tails" in UI), **default OFF (tronca)** — deciso con l'utente. `PhraseScheduler::setKeepTails(bool)`: quando `false`, le frasi smontate si liberano subito (`freePhrase`) invece di restare vive; quando `true`, comportamento precedente invariato. Sara' lo stesso bottone a guadagnare il pieno significato descritto nell'Excel quando il Pattern Ritmico esistera' — nessun ritocco di plumbing previsto in quel momento. Aggiornato il commento in `Phrase.h` per riflettere questa risoluzione (era ancora "[DECISION] da validare all'ascolto" dalla sessione 7).
- **Due bug di UI, entrambi da lettura diretta del codice, non ipotizzati**: `fixMoveLabel` (dalla sessione 6) e `voiceFormantLabel` (da questa sessione, stesso pattern replicato) venivano aggiunte con `addAndMakeVisible()` ma **mai posizionate** in `resized()` — nessun `attachToComponent()` ne' `setBounds()` esplicito, quindi bounds vuoti/invisibili. Corretto catturando il rettangolo di `row.removeFromLeft(labelWidth)` e assegnandolo alla label invece di scartarlo. Le 8 slider Fmt/Voice (i "puntini" nello screenshot dell'utente) avevano bounds validi — solo troppo piccole (26px, rotary senza text box): ingrandite a 36px di riga già che si toccava quel layout.
- **Titolo dell'editor obsoleto**: `paint()` diceva ancora "motore Signalsmith interinale", mai aggiornato dal cambio a PSOLA in sessione 9 — attivamente fuorviante durante un test. Sostituito con un testo che non richiede sync manuale ad ogni milestone.
- **Diagnostica "nota rilevata" aggiunta** (soddisfa anche un requisito PRD mai implementato, §8.1 "Display della nota rilevata"): nuovi atomici in `PluginProcessor` (`lastDetectedMidiNote`/`lastDetectedConfidence`/`lastInputStable`), popolati ogni blocco da `PitchDetector` (gia' pubblico, nessuna modifica li'), letti dall'editor nel timer 15Hz esistente. Serve a diagnosticare i punti 2 e 5 del test (mancato riconoscimento con certi synth, primo attacco a "0 voci attive"): l'ipotesi piu' probabile e' una corsa fra `OnsetDetector` (apre il gate in pochi ms) e `PitchDetector` (BACF, serve una finestra piu' lunga per agganciare con confidenza) — **non verificabile senza dati reali, quindi NESSUN fix speculativo sulle costanti del rilevatore** (CLAUDE.md regole 12/13, adottate proprio in sessione 9). Deciso esplicitamente con l'utente: solo diagnostica in questa sessione, il fix vero arrivera' dal prossimo giro di test con la nuova label visibile.
- **Verificato**: build VST3 (bloccata una volta da `LNK1104` — Ableton aveva ancora il plugin caricato, stesso caso gia' visto in sessione 6, risolto chiedendo all'utente di chiuderlo) e Standalone riuscite, `pluginval --strictness-level 10` verde su tutte le sezioni, `psola_test` e `override_manager_test` ancora verdi (nessuno dei due tocca questo codice, riverificati per scrupolo).
- **Non toccato**: `PitchDetector`, `OnsetDetector`, `Voice`, `VoicePool`, `PsolaShifter`, `CcRouter`, `OverrideManager`, `PlayModeInput` — nessuno di questi era in causa nella diagnosi.

**Novita' sessione 9 (continuazione) — Modalita' Play (FR-24..28):**

Dopo il controllo MIDI CC, chiesto di nuovo esplicitamente all'utente quale area (Play / Pattern ritmico / Preset timbrici / altro): scelta la modalita' Play.

- **Decisione architetturale centrale**: `PlayModeInput` (`src/midi/PlayModeInput.{h,cpp}`, nuovo) e' deliberatamente SEPARATO da `PhraseScheduler`, non costruito sopra. Il modello e' fondamentalmente diverso: `PhraseScheduler` esiste per "un onset genera una frase con voicing congelato che vive di vita propria" (FR-43/46); in Play mode non c'e' alcun concetto di frase, solo "nota MIDI premuta -> una voce insegue quella nota assoluta finche' non arriva il note-off". Questo E' esattamente la logica gia' esistente per la modalita' Fix di `Voice` (FR-22: bersaglio = `quantizedPlayedNote + offset`): con `quantizedPlayedNote=0` e `offset=notaMidi`, il bersaglio assoluto e' semplicemente la nota MIDI — **riusato cosi' com'era, zero codice nuovo dentro `Voice`**.
- **`VoicePool` dedicato a 8 slot** (non i 32 della catena Harmonizer): le due modalita' sono mutuamente esclusive in ogni istante (FR-24, "la tabella e' completamente disattivata"), quindi non serve condividere/contendere lo stesso pool. Mapping nota->slot: 1:1 in ordine di arrivo; oltre 8 note simultanee, le eccedenti restano mute (FR-25, coerente con "le voci in eccesso restano mute" applicato anche al caso limite non esplicitamente coperto dal PRD).
- **FR-20 si applica anche qui**: senza un ingresso audio stabile (`pitchDetector.hasStableSignal()`) le voci Play tacciono, indipendentemente da quali note MIDI sono premute — la nota resta "premuta" internamente (`slotNote` invariato), solo l'audio si interrompe finche' l'ingresso non torna stabile. **Confermato dalla lettura del PRD, non solo assunto**: il setup di riferimento (§3.4, "stesso schema di Waves Harmony") descrive una traccia MIDI separata che pilota il plugin residente sulla traccia AUDIO — Play non e' un sintetizzatore, richiede comunque una sorgente audio viva da pitchare.
- **`PlayModeInput::process()` viene chiamato SEMPRE**, anche a modalita' spenta (`modeActive=false`): il tracking note-on/off resta aggiornato (riattivare Play non richiede un nuovo note-on) e lo swap di Stability continua ad applicarsi in modo uniforme indipendentemente dalla modalita' corrente — stesso principio gia' usato per `phraseScheduler`, che a sua volta viene sempre chiamato con `inputIsStable` forzato a `false` mentre Play e' attivo (FR-24: nessuna frase nuova o viva, ma lo swap di Stability del suo pool continua a funzionare).
- **Canale MIDI condiviso col controllo CC** (`ccRouter.getMidiChannel()`): un'unica impostazione di canale per tutto il MIDI in ingresso al plugin (note E CC), non una seconda impostazione ridondante — interpretazione di FR-32, che nel PRD vive nella stessa sezione Impostazioni della configurazione CC.
- **Nuovo parametro APVTS `playModeEnabled`** (bool, automatizzabile FR-34, ma NON uno dei 3 CC di FR-30 — quindi non passa per `OverrideManager`). Le due modalita' non si sommano mai (`voicesMix` sceglie l'uno o l'altro buffer in base al flag, mai entrambi).
- **UI**: `ToggleButton` "Play Mode" (stesso pattern del toggle Bypass). Finestra ri-allargata (910px default).
- **Non fatto**: nessuna gestione velocity (non richiesta dal PRD). Nessuna regola di furto per oltre 8 note (il PRD non la definisce per Play, a differenza di FR-51/52 per Harmonizer — le eccedenti restano semplicemente mute). Nessun test numerico dedicato (a differenza di CC/Formanti/PSOLA): la logica nota->slot e' semplice e deterministica ma coinvolge `juce::MidiBuffer`/`juce::MidiMessage`, stesso limite gia' incontrato per `CcRouter`.
- **Verificato**: build VST3/Standalone riuscite, `pluginval --strictness-level 10` verde, entrambe le suite di test (psola, override_manager) ancora verdi — invariate, nessuna delle due tocca questo codice.
- **Non verificabile in questa sessione**: nessun controller MIDI fisico ne' tastiera collegata — il ciclo reale "traccia MIDI + traccia audio" del setup di riferimento non e' mai stato provato, ne' l'assenza di click nel passaggio Harmonizer<->Play (FR-28) e' stata verificata all'ascolto (il meccanismo scelto, `freeAllPhrases()` forzato ogni blocco mentre Play e' attivo, e' lo stesso gia' usato — e mai verificato all'ascolto nemmeno li' — per il caso "segnale non stabile", quindi non introduce un rischio nuovo, solo lo stesso rischio pre-esistente in un caso d'uso in piu').

**Novita' sessione 9 (continuazione) — Controllo MIDI CC (M4, FR-29..38):**

Dopo il push del lavoro precedente, scelto esplicitamente dall'utente di procedere col controllo MIDI CC, pur non potendo ancora testare in Ableton — l'utente ha notato che questa e' un'area verificabile con test numerici (override) e con pluginval, non richiede ascolto.

- **`src/midi/OverrideManager.{h,cpp}`** (nuovo): logica PURA (nessuna dipendenza JUCE) della regola di precedenza CC/automazione (FR-36/37/38). Tre stati indipendenti (root/preset/bypass), ciascuno "override attivo + valore" o "segui l'host". `resolve()` applica i nuovi eventi CC del blocco e ritorna i valori effettivi; `clearOverrides()` va chiamata sul fronte di stop del transport. FR-38 (l'ultimo CC vince) soddisfatto per costruzione, nessuna logica dedicata necessaria.
- **`tests/override_manager_test.cpp`** (nuovo): 6 test — pass-through senza override, indipendenza tra i tre parametri, persistenza dell'override sui blocchi successivi anche con nuova automazione host in arrivo, `clearOverrides()` che restituisce il controllo, l'ultimo CC che vince, soglia booleana del bypass. Tutti verdi al primo tentativo. Target CMake separato (`override_manager_test`), stesso principio di `psola_test`: **testabile senza un controller MIDI fisico**, perche' la logica di precedenza non dipende dall'hardware — e' l'equivalente, per il MIDI, di "non puoi ascoltare" per il DSP.
- **`src/midi/CcRouter.{h,cpp}`** (nuovo): interpreta i CC grezzi secondo FR-30 (root 1-12, preset 1-N con 0 ignorato, bypass soglia 64 come il sustain) e gestisce il MIDI Learn (FR-33) — durante l'apprendimento il filtro canale viene ignorato di proposito (si vuole imparare da QUALUNQUE controller fisico, prima ancora di aver deciso il canale). Configurazione (numeri CC, canale, target di apprendimento) in `std::atomic`: scritta dal message thread (UI) o dall'audio thread stesso (quando l'apprendimento cattura un numero), letta dall'audio thread ogni blocco — nessun lock, nessuna allocazione.
- **`PluginProcessor`**: nuovo parametro APVTS `bypass` (automatizzabile come tutti gli altri, FR-34). `processBlock` ora nomina il `MidiBuffer` (prima ignorato) e lo passa a `ccRouter.process()`; rileva il fronte di stop del transport (nuovo helper `isTransportPlaying()`, distinto da `canApplyStabilityChangeNow()` perche' la semantica standalone e' diversa: qui lo standalone va escluso del tutto dal rilevamento, mai incluso come "sempre fermo") e chiama `overrideManager.clearOverrides()` solo li'; **mai in standalone** (FR-37). I valori effettivi (`OverrideManager::Effective`) sostituiscono le letture dirette di `rootNote`/`presetIndex`/`bypass` a valle. Bypass implementato come `dryLevel=1, wetLevel=0` per quel blocco — il percorso dry e' gia' il segnale non processato, non serve un secondo percorso audio.
- **Persistenza (FR-31)**: numeri CC e canale salvati in un nodo `MidiCcSettings` dentro lo stato del plugin (sibling di APVTS e PresetLibrary) — sono configurazione di routing, non valori automatizzabili, quindi deliberatamente FUORI dall'APVTS (non avrebbe senso automatizzare "quale CC controlla cosa").
- **UI**: ComboBox canale MIDI (Omni + 1-16), 3 slider CC (0-127) con bottone "Learn" ciascuno — non sono `SliderAttachment` (i numeri CC non sono parametri APVTS), si sincronizzano dal timer 15Hz gia' esistente, con guardia `isMouseButtonDown()` per non "strappare" lo slider da sotto il mouse durante il polling. `ToggleButton` bypass attaccato al parametro APVTS per test senza hardware. Finestra riallargata (880px di default).
- **Non fatto in questo passaggio** (scope FR-30..38, non tutto M4): **Modalita' Play (FR-24..28)** resta esplicitamente fuori — l'utente aveva scelto "punto 1" (CC), non Play, che era un'opzione separata proposta. Nessun indicatore UI di "override attivo" (non richiesto dal criterio di uscita M4, "ciclo hardware -> plugin senza mappature manuali" e' comunque soddisfatto). Il selettore root/preset in UI continua a mostrare il valore del parametro APVTS, non il valore effettivo quando un override CC e' attivo — gap di UX noto, non un errore funzionale (l'audio segue correttamente il CC).
- **Verificato**: `override_manager_test` verde (6/6, anche via CTest), `psola_test` ancora verde (invariato), build VST3 e Standalone riuscite, `pluginval --strictness-level 10` verde su tutte le sezioni.
- **Non verificabile in questa sessione**: il parsing dei CC veri (`CcRouter::process`) non ha un test dedicato — richiede `juce::MidiBuffer`/`juce::MidiMessage`, che avrebbe richiesto linkare `juce_audio_basics` in un target di test separato; rimandato, coperto per ora da lettura attenta del codice + `pluginval` (che include un fuzzing dei parametri, non della porta MIDI). **Il ciclo reale hardware -> plugin non e' mai stato provato**: nessun controller MIDI fisico disponibile in questa sessione, esattamente come per l'ascolto del motore PSOLA.

**Novita' sessione 9 (continuazione) — Formanti (FR-39..42):**

Dopo il commit del motore PSOLA, chiesta di nuovo esplicitamente all'utente la prossima area (Formanti / MIDI CC / Pattern ritmico / aspettare l'ascolto): scelte le Formanti, resa naturale dal fatto che `beta` era gia' implementato e testato nel motore ma non collegato a nulla.

- **Dove si calcola**: in `Voice::processAdd`, subito dopo aver calcolato `semitonesToApply` (lo shift REALMENTE applicato a quella voce in quel blocco, identico in Fix e Move, e identico anche in una futura modalita' Play — FR-42 soddisfatto strutturalmente senza codice dedicato, perche' tutte le modalita' producono lo stesso `semitonesToApply`).
- **Unita' di misura scelta**: "semitoni-equivalenti" invece del rapporto `beta` direttamente. Motivo: FR-41 dice che l'offset manuale per voce va "sommato alla correzione automatica" — lavorando in semitoni quella somma e' letterale (`totalFormantSemitones = autoFormantSemitones + formantOffsetSemitones`), invece di dover decidere se moltiplicare due rapporti o convertire avanti e indietro. Coerente con come il resto del progetto ragiona gia' in semitoni (`setPitchShiftSemitones`, gli offset della tabella armonica). Alla fine si converte una sola volta: `beta = 2^(totalFormantSemitones/12)`.
- **Formula automatica** (FR-39, presa da `psola-spec.md` §3 di `TIPS`, gia' verificata concettualmente nella sessione precedente): `autoFormantSemitones = -k * spread * semitonesToApply`, `k = 0.3`. Per costruzione: shift in giu' (`semitonesToApply < 0`) da' `autoFormantSemitones > 0` (schiarisce, FR-39), shift in su scurisce. Il clamp di `beta` gia' presente dentro `PsolaShifter::setFormantRatio` (`[0.25, 4.0]`, cioe' circa ±24 semitoni-equivalenti) copre gia' i casi estremi, non serve un clamp duplicato in `Voice`.
- **Propagazione**: stesso schema gia' rodato per `ShiftMode` (sessione 6) — `setFormantSpread(float)` globale su tutti gli slot fisici del `VoicePool` (una nuova frase la trova gia' impostata), `setVoiceFormantOffset(int, float)` per colonna armonica (0-7), applicato a qualunque slot fisico la stia interpretando ora, identico a `setVoiceMode`.
- **Parametri APVTS nuovi**: `formantSpread` (float 0..1, default **1.0**, non 0 — FR-39 dice "attiva di default"); `voiceFormantOffset1..8` (float −24..24 semitoni-equivalenti, default 0, range scelto per coincidere esattamente col clamp di `beta` nel motore).
- **UI**: slider "Fmt Spread" accanto agli altri controlli globali; riga di 8 knob rotativi "Fmt/Voice" sotto la riga Fix/Move — stesso pattern (`layoutRowOfButtons`, gia' generico su `Component*`, riusato senza modifiche). Finestra allargata di conseguenza (720px di default, limite minimo 680px).
- **Non toccato**: `PitchDetector`, `HarmonyEngine`, `PresetLibrary`, `VoicePool`, `PhraseScheduler` (solo due metodi aggiunti, nessuna riga esistente modificata), `PsolaShifter`/`SpectralShifter` (l'interfaccia `setFormantRatio` esisteva gia' dalla sessione precedente).
- **Verificato**: build VST3 e Standalone riuscite (solo il consueto fallimento di copia post-build per permessi), `pluginval --strictness-level 10` verde su tutte le sezioni, suite `psola_test` riverificata verde (non toccata da queste modifiche, ma rieseguita per scrupolo).
- **Non verificato**: nessun ascolto, ne' della correzione automatica ne' dell'offset manuale — il motore PSOLA stesso non e' ancora stato provato all'ascolto (vedi sotto), quindi le Formanti lo sono ancora meno. La costante `k=0.3` e' quella di partenza della spec sorgente, mai tarata.

**Novita' sessione 9 — PSOLA proprietario integrato come motore di default (M1):**

Su scelta esplicita dell'utente tra le direzioni proposte (PSOLA / MIDI CC / Formanti / solo processo), si e' portato il motore TD-PSOLA scoperto in sessione 8 dentro il progetto, sostituendolo a Signalsmith Stretch come motore attivo di default dietro l'interfaccia astratta `PitchShifter` (FR-62, CLAUDE.md regola 2). Perimetro concordato con l'utente: solo il motore (Formanti rimandate a una sessione dedicata), selezione a compile-time (nessun parametro APVTS/UI), suite di test numerici portata nel repo e in CI.

- **`src/dsp/PsolaShifter.{h,cpp}`** (nuovo): algoritmo TD-PSOLA (dominio pubblico) split `.h`/`.cpp` in stile progetto, namespace globale (non `harm`), derivato dalla nostra `PitchShifter`. Tre problemi reali trovati leggendo i sorgenti (non noti alla sessione 8) e risolti PRIMA che il motore entrasse in `processBlock`, ciascuno verificato con la suite numerica prima di procedere al successivo:
  1. **`std::deque<long long> epochs` allocava sull'audio thread** (`push_back`/`pop_front` dentro `detectEpochs()`, violazione CLAUDE.md regola 1/PRD §9.4 — su MSVC il blocco del deque e' da soli 16 byte, allocazione quasi ad ogni chiamata). Sostituito con un ring buffer a capacita' fissa (`epochRing`/`epochHead`/`epochCount`), pre-allocato in `prepare()`, dimensionato su `bufSize/2/minPeriod + 4`. **Verificato bit-per-bit identico** al deque originale (stessa suite di test, stessi numeri esatti prima e dopo la sostituzione).
  2. **La latenza sarebbe esplosa** (`latency = 2*maxPeriod + maxBlockSize`, e `PluginProcessor.cpp` passa agli shifter `maxBlockSize = 8192`, limite prudenziale per gli scratch buffer, non il blocco reale dell'host — sarebbero stati ~170ms). Risolto con **chunking interno**: `process()` ora suddivide qualunque blocco ricevuto in fette di `kInternalChunk = 64` campioni e la formula di latenza usa questa costante invece del `maxBlockSize` esterno. **Verificato con un controllo ad-hoc** (fuori dal progetto, scratchpad) che l'uscita e' identica bit-per-bit indipendentemente da come il chiamante spezza le chiamate a `process()` (testato con blocchi da 256, 8192 e 777 campioni sullo stesso segnale).
  3. **Perdita di sovrapposizione sotto circa un'ottava sotto** (bug latente nell'originale, non nella nostra checklist di sessione 8): il commento di `emitGrain()` diceva che la semiampiezza del grano va legata al maggiore fra periodo di analisi e di sintesi "cosi' la sovrapposizione resta sempre garantita", ma il codice usava solo il periodo di analisi. Sotto `alpha <= 0.5` (~-12 semitoni) la spaziatura fra grani di sintesi supera la loro lunghezza: l'inviluppo crolla a vuoti periodici. **Primo tentativo di correzione (margine largo, `W = P/alpha`) ha rotto il test 1** (a -12 semitoni la f0 misurata torna quella originale, errore di un'ottava): un grano troppo lungo, a beta=1, e' una copia diretta e non trasposta del segnale sorgente, e reintroduce direttamente la periodicita' originale al suo interno. **Corretto con il margine minimo analiticamente necessario** (`W = 1.2 * max(P, P/(2*alpha))`, il fattore 1.2 e non 1.0 perche' la finestra di Hann si azzera ai bordi e il contatto esatto lasciava comunque vuoti stretti) — l'intera indagine (formula sbagliata -> test 1 rotto -> formula corretta -> tutti i test verdi) e' stata condotta empiricamente con il compilatore, non "a orecchio" (CLAUDE.md regola 12).
  - Mappatura **Stability -> minF0Hz** (non piu' una finestra STFT come in `SpectralShifter`): tabella `{165, 130, 100, 85, 70}` Hz per i 5 livelli Fast..Accurate, valori di partenza **da tarare all'ascolto**. E' uno scostamento deliberato e documentato dalla lettera di FR-54 ("seleziona la dimensione della finestra di analisi") — il PRD non e' stato modificato, solo annotato qui e nel codice.
  - `setPitchShiftSemitones(float)` converte in `alpha` internamente (`alpha = 2^(semitoni/12)`); il resto del progetto continua a ragionare in semitoni.
- **`src/dsp/PitchShifter.h`** (modificato): due metodi virtuali nuovi con default no-op, cosi' `SpectralShifter` non ha dovuto essere toccato: `setInputF0Hz(double)` (PSOLA ne ha bisogno per gli epoch) e `setFormantRatio(double)` (FR-39..42, implementato nel motore ma non ancora collegato a nulla — sessione futura).
- **`src/voices/Voice.cpp`** (modificato): una riga in `processAdd`, `shifter->setInputF0Hz(440.0 * exp2((continuousInputMidiNote-69.0)/12.0))` — nessuna firma cambiata in `Voice`/`PhraseScheduler`/`PluginProcessor`, perche' la nota MIDI continua era gia' un parametro esistente e la conversione a Hz e' un calcolo esatto (round-trip dello stesso valore che `PitchDetector` ricava da Hz).
- **`src/dsp/PitchShifterFactory.cpp`** (nuovo): `createDefaultPitchShifter()` spostata qui da `SpectralShifter.cpp`. Default: PSOLA. `SpectralShifter` resta compilato e disponibile come fallback dietro `#define HARMONIZER_USE_SPECTRAL_SHIFTER`, nessun parametro APVTS/UI (scelta esplicita dell'utente per questa sessione).
- **`tests/psola_test.cpp`** (nuovo): suite portata da `TIPS` e adattata alla nostra interfaccia (niente JUCE, si compila ed esegue in meno di un secondo). Test 1-5 della suite originale (accuratezza di trasposizione, ortogonalita' pitch/formanti nei due sensi, assenza di discontinuita', monotonia della latenza — quest'ultimo riformulato su `stabilityLevel` invece che su `minF0Hz` diretto) piu' **due test nuovi**: Test 6 (inviluppo minimo di sovrapposizione, RMS a breve termine su finestra scorrevole — e' quello che ha scoperto il problema 3 sopra) e Test 7 (tenuta con f0 variabile nel tempo, mai esercitata dalla suite originale che passa sempre una f0 costante — verifica il ricambio continuo del ring di epoch). **Tutti e 7 verdi**, esito completo riportato durante la sessione, non solo "passa".
- **`CMakeLists.txt`** (modificato): nuovi sorgenti (`PsolaShifter.cpp`, `PitchShifterFactory.cpp`) nel target del plugin; nuovo target `psola_test` (eseguibile separato, senza dipendenze JUCE, `enable_testing()` + `add_test`).
- **`.github/workflows/build.yml`** (modificato): nuovo job `dsp-tests` (ubuntu, compila ed esegue `psola_test` con g++) che gira per primo; i job di build del plugin (Windows/macOS) ora dipendono da esso (`needs: dsp-tests`) — se il DSP e' rotto non si aspettano 20 minuti di build per scoprirlo.
- **`CLAUDE.md`** (modificato): aggiunte le regole 12 ("non puoi ascoltare") e 13 ("un test che fallisce puo' essere il test sbagliato — ma vale anche il contrario"), adottate da `TIPS/CLAUDE.md` dopo che si sono dimostrate utili proprio in questa sessione (problema 3 sopra). Nota di stato milestone aggiornata da M0 a M1.
- **Correzione di un errore della sessione 8**: la nota su `_USE_MATH_DEFINES`/`M_PI` (riga 337 della versione precedente di questo file) attribuiva il problema al motore PSOLA in generale; verificato leggendo `PsolaShifter.h` che **non usa affatto `M_PI`** (costante letterale scritta a mano). Il problema riguardava solo `psola_test.cpp` originale, e nel nostro porting e' stato evitato definendo una `constexpr double kPi` locale invece di includere `<cmath>` con la define — non serve alcun flag di compilazione speciale.

**Verificato in questa sessione (build reale, non solo lettura):**
- Compilazione isolata (MSVC via `vcvarsall.bat x64`, scratchpad, nessun file di progetto toccato) di ogni fase intermedia del porting, con la suite di test rieseguita ad ogni modifica (deque -> ring, poi chunking, poi correzione `emitGrain`) per confermare invarianza o correggere regressioni prima di andare avanti.
- **Build reale del progetto**: `cmake --build` Release, VST3 compilato e linkato con successo (il solo passo di copia post-build in `C:\Program Files\Common Files\VST3` fallisce per permessi, comportamento gia' noto e documentato dalla sessione 4 — non un problema di codice). **Standalone compilato con successo** (target buildato separatamente per bypassare l'interruzione del grafo MSBuild causata dal fallimento del passo di copia del VST3).
- **`pluginval --strictness-level 10` verde sul VST3**, nessun fallimento su nessuna sezione (audio processing, state, automation, parametri, thread safety, bus, fuzz — log completo controllato riga per riga, non solo il codice di uscita).
- **Target CMake `psola_test` verificato anche tramite `ctest`** (non solo la compilazione manuale in scratchpad): `1/1 Test #1: psola ... Passed`.
- **Latenza misurata, target PRD raggiunto**: Stability Fast = 646 campioni = **13.5 ms** @48kHz — sotto la soglia <=15ms di NFR §1.3 per la prima volta nel progetto. Accurate = 1436 campioni = 29.9ms (comunque migliore del minimo di Signalsmith, che partiva da 30ms).

**Novita' sessione 8 — scoperta cartella `TIPS` (PSOLA esterno, NON ANCORA INTEGRATO):**

L'utente ha segnalato una cartella `TIPS` (trovata in `C:\Users\cazza\Downloads\TIPS`, **fuori dal progetto**) prodotta da una sessione Claude web separata, concentrata specificamente sul motore PSOLA proprietario — il rischio più alto del prodotto secondo il PRD (§13). Contenuto:

```
TIPS/
├── CLAUDE.md                          (variante indipendente delle nostre regole, con alcune aggiunte utili — vedi sotto)
├── PsolaShifter.h, psola-spec.md      (copie duplicate di quelle sotto)
└── harmonizer-scaffold/
    ├── CLAUDE.md
    ├── CMakeLists.txt                  (JUCE via FetchContent tag 8.0.4, non submodule; target psola_test)
    ├── docs/prd.md                     (VERIFICATO byte-per-byte identico al nostro PRD-Harmonizer-v1.md)
    ├── docs/psola-spec.md              (specifica tecnica del TD-PSOLA, ben scritta)
    ├── src/dsp/PitchShifter.h          (interfaccia astratta, namespace harm, SIMILE ma non identica alla nostra)
    ├── src/dsp/PsolaShifter.h          (implementazione, ~300 righe, ZERO dipendenze esterne)
    └── tests/psola_test.cpp            (5 test numerici)
```

**Non esistono PluginProcessor/PluginEditor in quella cartella**: è DSP puro, pensato per essere validato da riga di comando prima ancora di toccare JUCE. Il loro `CLAUDE.md` documenta un ordine di lavoro diverso dal nostro: PSOLA prima (validato numericamente) → M0 infrastruttura → CI → verifica AU → **solo dopo** PitchDetector — mentre noi abbiamo fatto M0 prima e costruito in ampiezza con un motore interinale (Signalsmith).

**Verifica fatta io stesso (non solo lettura):** ho compilato `psola_test.cpp` con MSVC in una cartella temporanea fuori dal progetto (nessun file di progetto toccato) ed eseguito i test:
- **TUTTI I TEST SUPERATI (0 fallimenti)**: errore di trasposizione < 0.01 cent su ±12 semitoni; indipendenza pitch/formanti confermata numericamente; nessuna discontinuità; latenza monotona al variare di `minF0`.
- Un solo intoppo, cosmetico e non-mio-problema: mancava `_USE_MATH_DEFINES` per `M_PI` su MSVC (la libreria è stata scritta/testata altrove, probabilmente con g++/clang dove `M_PI` è disponibile di default) — risolto con un flag di compilazione, non è un bug dell'algoritmo.
- I numeri di latenza misurati coincidono ESATTAMENTE con quelli dichiarati nella spec (minF0=70Hz → 33.9ms / 1628 campioni; minF0=120Hz → 22.0ms / 1056 campioni).

**Perché è rilevante per noi:**
- Algoritmo TD-PSOLA di dominio pubblico (anni '90), **zero dipendenze e zero problemi di licenza** (coerente con CLAUDE.md regola 9).
- **Latenza reale misurata (22-34ms secondo `minF0`) già inferiore al nostro motore interinale Signalsmith** (30-180ms secondo Stability) — e ulteriormente riducibile con un blocco più piccolo (con blocco 32-64 campioni si stima si scenda sotto i 15ms target del PRD a `minF0` intorno a 150Hz, cosa che Signalsmith non potrà mai fare per costruzione).
- **`alpha`/`beta` ortogonali per costruzione**: `alpha` controlla SOLO il pitch, `beta` SOLO le formanti — la formula proposta per FR-39 (`beta = alpha^(-k*spread)`) è già scritta nella spec e ricalca quasi alla lettera il requisito. Noi non abbiamo ancora toccato le Formanti (FR-39..42): questo ci darebbe un punto di partenza concreto.
- Loro `CLAUDE.md` ha due regole in più che vale la pena adottare anche nel nostro: **"Non puoi ascoltare"** (mai dichiarare completo un lavoro sul suono perché "compila" — o tradurlo in una misura numerica con un test, o segnalare che serve un ascolto umano) e un promemoria che un test che fallisce potrebbe essere il test sbagliato, non l'algoritmo (successo due volte nella loro sessione, entrambe le volte l'algoritmo era giusto — vedi dettagli su autocorrelazione/sub-armonica e centroide spettrale in `psola-spec.md` §8.1).

**Perché NON è plug-and-play — differenze architetturali reali da colmare prima di integrarlo:**
1. La loro interfaccia `PitchShifter` (namespace `harm`) richiede `setF0(double f0Hz)` esplicito — PSOLA ne ha bisogno per posizionare gli epoch sul periodo reale. La nostra interfaccia attuale (`src/dsp/PitchShifter.h`) espone solo `setPitchShiftSemitones(float)`: va estesa.
2. Loro lavorano in `alpha` (rapporto moltiplicativo, es. 2.0 = ottava sopra) e `beta` (rapporto formanti); noi lavoriamo in semitoni. Conversione banale (`alpha = 2^(semitoni/12)`), ma va scritta.
3. Il nostro `PitchDetector` oggi espone solo `getMidiNote()` (nota MIDI continua); serve anche l'Hz grezzo (`getFrequencyHz()` o simile) per alimentare `setF0`.
4. Loro `prepare()` prende `minF0Hz` direttamente; il nostro prende `stabilityLevel` (0-4, mappato internamente da `SpectralShifter`). Andrebbe deciso come le 5 posizioni di Stability mappano a `minF0Hz` per questo motore (la loro tabella in `psola-spec.md` §6 da' gia' dei valori di riferimento).
5. Nessun collegamento a Formanti/UI/parametri APVTS per `beta` — tutto da costruire (FR-39..42 sono ancora del tutto assenti dal nostro progetto).
6. Va portato nella nostra struttura (`src/dsp/PsolaShifter.h` + eventuale `.cpp`), verificato con la NOSTRA build CMake/pluginval, non solo con la loro suite standalone.

**Nessun file del progetto è stato modificato in questa sessione**, su richiesta esplicita dell'utente: solo lettura, ricerca e una compilazione di verifica fuori dal progetto.

**Novita' sessione 7 — motore a frasi (M3):**
- **Rilevamento onset** (FR-43, nuovo `src/dsp/OnsetDetector.{h,cpp}`): inviluppo di picco (`cycfi::q::peak_envelope_follower`) + `cycfi::q::onset_gate` (soglia di livello O pendenza rapida, per catturare anche attacchi morbidi). Un EVENTO di onset e' il fronte di salita del gate; pattern d'uso copiato esattamente dalla documentazione di Cycfi Q in `noise_gate.hpp`.
- **`Phrase`** (nuovo `src/voices/Phrase.h`): dati di una frase — offset congelati (FR-46) + quali slot fisici occupa + un contatore d'eta' (per FR-52) + un flag `isLive`.
- **`VoicePool` generalizzato**: da 8 slot fissi a un pool generico di N slot fisici (default/tetto tecnico 32), con lo stesso schema di swap Stability di prima ma ora parametrizzato sul numero di slot.
- **`PhraseScheduler`** (nuovo, orchestratore): ad ogni onset crea una nuova frase, congelandone gli offset correnti; alloca fino a 8 slot fisici (uno per voce armonica non muta) dal pool, **rubando per intero la frase piu' vecchia** (FR-52) se il pool e' esaurito. **Risoluzione della tensione FR-17/FR-46** (segnalata dal PRD come `[DECISION]` da validare all'ascolto, mai fatto qui): solo la frase piu' recente resta "viva" e segue in tempo reale i cambi di preset/fondamentale finche' la nota che l'ha generata continua a suonare (FR-17); nel momento in cui arriva un nuovo onset, quella frase smette di essere "viva" e resta congelata per sempre a quello che era il suo ultimo voicing (FR-46). Tutte le frasi si liberano insieme quando il segnale in ingresso torna silenzioso.
- **Furto senza dissolvenza dedicata**: quando uno slot rubato viene riassegnato a una nuova frase, il `Glide` gia' presente in `Voice` fornisce naturalmente la transizione morbida richiesta da FR-52 (>= 20ms, di default 30ms) — nessuna dissolvenza/crossfade separata da costruire.
- Nuovo parametro `maxSimultaneousVoices` (FR-51, 1..32, default 32): tetto REGOLABILE A COSTO ZERO senza riallocare, perche' i 32 slot fisici sono sempre pre-allocati e il parametro si limita a restringere quanti sono utilizzabili.
- Editor: slider "Voice Cap" + label "Active" (FR-53, aggiornata dal timer esistente).

**Novita' sessione 6 — qualita' pitch shifting (M1):**
- **Fix/Move per voce** (FR-21/22/23): `enum class ShiftMode { move, fix }` in `Voice`, settabile indipendentemente per ciascuna delle 8 voci (8 parametri APVTS `voiceFix1..8`, bool). Move (default) = rapporto fisso rispetto all'ingresso, comportamento naturale dello shifter. Fix = la voce insegue una nota ASSOLUTA (nota quantizzata + offset), ricalcolando il rapporto di shift **ogni blocco** sulla base del pitch continuo rilevato — verificato che questo e' sicuro perche' `SignalsmithStretch::setTransposeSemitones` non alloca mai (letto il sorgente: assegna due float e azzera un puntatore a funzione gia' nullo).
- **Glide** (FR-17): nuova classe `src/dsp/Glide.h`, rampa lineare a **durata fissa** (default 30ms, parametro APVTS `glideTimeMs`), applicata all'offset armonico grezzo prima che Fix/Move lo interpretino. Un salto (es. cambio accordo su nota tenuta) impiega sempre lo stesso tempo a completarsi, qualunque sia l'ampiezza.
- **Controllo Stability** (FR-54..58): 5 posizioni discrete (Fast..Accurate, parametro APVTS `stabilityLevel`) che mappano a finestre STFT di Signalsmith Stretch da 30ms a 180ms — **anche la piu' reattiva resta ben oltre il target <=15ms del PRD**, limite noto del motore interinale (vedi sotto). Cambiare Stability richiede riallocare i buffer interni (`configure()` di Signalsmith alloca), quindi non puo' avvenire sull'audio thread: implementato uno schema realtime-safe a due fasi — il message thread (un `juce::Timer` in `HarmonizerAudioProcessor`, 250ms) nota il cambio di parametro e **costruisce** 8 nuovi shifter (`VoicePool::requestStabilityChange`); l'audio thread, dentro `processBlock`, **applica** lo scambio solo quando e' sicuro (`canApplyStabilityChangeNow()`: transport fermo, o sempre in standalone — FR-56/57) tramite `std::unique_ptr::swap` (nessuna allocazione/distruzione), mettendo i vecchi shifter in una lista che lo stesso Timer distrugge poi sul message thread (`VoicePool::collectGarbage`). `setLatencySamples` viene richiamato solo nel blocco in cui lo scambio e' stato effettivamente applicato (FR-55/56).
- Editor: ComboBox Stability, slider Glide, 8 ToggleButton Fix/Move per voce (una riga sopra i bottoni di gestione preset).

Fatto e verificato:
- Repository git con remote **origin** = https://github.com/Jazy1997/STARTUP-Software-Harmonizer.git, branch `main`, storia pushata. Repo pubblico. CI del primo push verde su Windows e macOS incluso AU (run https://github.com/Jazy1997/STARTUP-Software-Harmonizer/actions/runs/30536034297).
- JUCE 8.0.15 + **Cycfi Q** (pitch detection) + **Signalsmith Stretch** (pitch shifting interinale, tag `1.1.0`) come submodule, tutte licenze permissive.
- Catena audio end-to-end: `PluginProcessor` → downmix mono → `PitchDetector` (Cycfi Q) → snapshot di `harmony::PresetLibrary` → `harmony::HarmonyEngine::getOffsets` (puro calcolo, stateless) → `VoicePool`/`Voice` (fino a 8 voci continue, ciascuna con un `PitchShifter` dietro interfaccia astratta) → somma pesata dry/wet.
- **PresetLibrary reale (nuovo, sessione 5)** — `src/harmony/PresetLibrary.{h,cpp}`:
  - Lista ordinata di preset (7 di fabbrica all'avvio), CRUD completo: add/duplicate/rename/remove, `movePreset` per il riordino (FR-06/07: la posizione, 1-based via `getCcValue`, e' gia' concettualmente il futuro valore CC).
  - ID stabile `juce::Uuid` per preset, indipendente dalla posizione (FR-10). Tetto tecnico 128 preset (FR-02).
  - **CSV import/export** (`src/harmony/CsvIo.{h,cpp}`, FR-03): intestazione con i 12 gradi + 8 righe (voci) x 12 colonne, cella vuota = stringa vuota, `0` = zero. Verificato andata/ritorno.
  - **Serializzazione nello stato del plugin** (FR-08): `PresetLibrary::toValueTree()`/`loadFromValueTree()`, incorporata in `getStateInformation`/`setStateInformation` insieme ai parametri APVTS — un progetto salvato porta con se' la propria copia della libreria.
  - **Libreria globale su disco** (FR-09): `PresetLibrary::saveAsGlobal()`/`loadGlobal()`, file XML in `%APPDATA%\Harmonizer\GlobalPresetLibrary.xml` (o equivalente utente su macOS), operazioni esplicite via bottoni UI, mai automatiche.
  - **Swap thread-safe verso l'audio thread** (PRD §9.4): la libreria vive dietro `std::shared_ptr<const PresetLibrary>` scambiato sotto `juce::SpinLock` (`HarmonizerAudioProcessor::getPresetLibrary()`/`editPresetLibrary()`), con un singolo "slot retired" per tenere in vita la versione precedente finche' non arriva la modifica successiva. E' un compromesso pragmatico (non hazard-pointer/epoch-based reclamation rigorosa) — vedi §5 per i limiti noti.
  - `presetIndex` e' ora `AudioParameterInt` 1..128 (non piu' `AudioParameterChoice`): le choices fisse di APVTS non reggono una lista che cambia dimensione a runtime; il valore 1-based coincide gia' col futuro CC posizionale, e valori oltre la libreria attuale vengono ignorati (stesso comportamento di FR-30).
  - Editor: ComboBox preset sincronizzata via polling (`juce::Timer`, 15 Hz, non un `ComboBoxAttachment`), text editor per rinominare, bottoni Add/Duplicate/Delete/Up/Down/Import CSV/Export CSV/Load Global/Save As Global.
- Build locale Windows verificata: **VST3 compila** (incluso tutto il codice sopra) **ed e' verde su `pluginval --strictness-level 10`**. `COPY_PLUGIN_AFTER_BUILD` e' di nuovo `TRUE` su richiesta dell'utente — su questa macchina, senza shell elevata, il solo passo di copia post-build fallisce con "Permission denied" (atteso, documentato in `CMakeLists.txt`); l'artefatto compilato resta comunque valido in `build/Harmonizer_artefacts/Release/VST3`.

Non ancora fatto / semplificazioni consapevoli di questo vertical slice (da NON scambiare per requisiti soddisfatti):
- **Pattern ritmico** (FR-47..50, `[V1.1]`): non implementato, correttamente fuori scope per v1.0. Conseguenza pratica: tutte le voci di una frase entrano "in sync" al trigger (nessun offset temporale tra voci) — questo rende FR-46 osservabile solo nel caso "chord change mentre una frase e' gia' congelata da un onset successivo", MAI nel caso "voce ancora in coda non ancora suonata" (che richiederebbe il pattern con ritardi reali). Quando il pattern editor arrivera', la logica di congelamento in `PhraseScheduler` va rivista per questo caso aggiuntivo.
- **Risoluzione FR-17/FR-46 non validata all'ascolto**: e' un'interpretazione mia (unica frase "viva" = quella piu' recente, tutte le altre congelate), consistente con la lettera del PRD ma esplicitamente segnalata come `[DECISION]` da verificare — l'utente dovrebbe ascoltarla e confermare che il comportamento sia musicalmente sensato.
- **Formanti**: nessuna correzione (FR-39..42).
- **Latenza minima ben oltre il target del PRD**: anche Stability "Fast" (30ms) e' molto piu' della soglia <=15ms richiesta — limite intrinseco del motore STFT interinale (Signalsmith), non raggiungibile prima del PSOLA proprietario.
- **Swap Stability e furto di frase**: sicuri nel caso normale (stesso compromesso pragmatico della PresetLibrary: nessuna garanzia assoluta in ogni intreccio di timing estremo, niente hazard-pointer/epoch-based reclamation rigorosa).
- **MIDI CC, modalita' Play, licensing**: tutti placeholder/non iniziati (M4/M6). Il riordino preset in UI usa bottoni Su/Giu', non drag&drop vero (quello e' UI di M5).
- **Preset armonici**: 7 di fabbrica generati algoritmicamente — solo **Min** e' verificato contro il prototipo (vedi §5). Gli altri 6 sono standard jazz generici, da sostituire via import CSV quando disponibili i dati reali.
- **AU non compilabile ne' testabile su questa macchina** (Windows) — verificato in CI su macOS.
- Nessuna licenza JUCE acquistata, nessun certificato di firma avviato.
- **Non ancora testato per davvero in Ableton** dalla sessione 6 in poi (solo verificato via pluginval) — l'utente non ha ancora potuto riprovare il caricamento del VST3 aggiornato.

Contenuto attuale della cartella di progetto (esclusi `build/`, `libs/*` interni e `tools/` — vedi `.gitignore`):
```
SVILUPPO SOFTWARE/
├── .github/workflows/build.yml
├── .gitignore / .gitmodules
├── CLAUDE.md
├── CMakeLists.txt
├── PRD-Harmonizer-v1.md
├── handsoff.md
├── libs/{JUCE, q, signalsmith-stretch}/   (submodule)
├── src/
│   ├── PluginProcessor.{h,cpp}, PluginEditor.{h,cpp}
│   ├── dsp/PitchDetector.{h,cpp}, PitchShifter.h, PsolaShifter.{h,cpp}, SpectralShifter.{h,cpp},
│   │       PitchShifterFactory.cpp, Glide.h, OnsetDetector.{h,cpp}
│   ├── harmony/HarmonyPreset.h, HarmonyEngine.{h,cpp}, PresetLibrary.{h,cpp}, CsvIo.{h,cpp}
│   └── voices/Voice.{h,cpp}, VoicePool.{h,cpp}, Phrase.h, PhraseScheduler.{h,cpp}
└── tests/psola_test.cpp   (target CMake `psola_test`, niente JUCE, gate in CI)
```

Questioni aperte dal PRD (§16) che restano da chiudere (nessuna blocca la prosecuzione tecnica):
- Nome del prodotto, marchio, dominio — non deciso. `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt` restano placeholder da confermare prima della beta.
- Tipo di licenza JUCE in funzione del fatturato previsto — non deciso.
- Backend di licensing — `[DECISION]` entro M5, non ancora aperta.
- Numero esatto di posizioni del controllo Stability — `[DECISION]` entro M1.
- Coerenza FR-17 vs FR-46 (nota tenuta vs frase in coda) — da verificare all'ascolto entro M2.

---

## 3. File su cui sto lavorando

**Sessione 1:**

| File | Stato | Scopo |
|---|---|---|
| `PRD-Harmonizer-v1.md` | letto, invariato | Specifica di prodotto, fonte di verità |
| `handsoff.md` | creato | Questo documento di handoff |

**Sessione 2 (M0):**

| File | Stato | Scopo |
|---|---|---|
| `.gitignore` | creato | Esclude `build/`, `tools/` (pluginval scaricato), file IDE/OS |
| `.gitmodules` + `libs/JUCE` | creato | Submodule JUCE pinnato al tag `8.0.15` |
| `CMakeLists.txt` | creato | Target `juce_add_plugin`, formati VST3/AU/Standalone |
| `src/PluginProcessor.h` / `.cpp` | creato | Processor stub, dry passthrough, nessuna allocazione in `processBlock` |
| `src/PluginEditor.h` / `.cpp` | creato | Editor placeholder ridimensionabile |
| `CLAUDE.md` | creato | Le 11 regole non negoziabili del PRD §15 + note di stato milestone |
| `.github/workflows/build.yml` | creato | CI Windows+macOS, gate pluginval strictness 10 — non ancora eseguita (nessun remote) |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 3 (push su GitHub):**

| File | Stato | Scopo |
|---|---|---|
| `handsoff.md` | aggiornato | Registrato remote, force-push, run CI avviata inavvertitamente |

**Sessione 4 (vertical slice DSP):**

| File | Stato | Scopo |
|---|---|---|
| `.gitmodules` + `libs/q` | creato | Submodule Cycfi Q (+ submodule annidato `infra`) |
| `.gitmodules` + `libs/signalsmith-stretch` | creato | Submodule Signalsmith Stretch, tag `1.1.0` |
| `src/harmony/HarmonyPreset.h` | creato | Tipi `Cell`/`Table`/`Preset` (12×8, `null` vs `0`) |
| `src/harmony/HarmonyEngine.{h,cpp}` | creato | 7 preset di fabbrica generati algoritmicamente, `degreeOf`/`getOffsets` |
| `src/dsp/PitchDetector.{h,cpp}` | creato | Wrapper Cycfi Q, pimpl con `unique_ptr` |
| `src/dsp/PitchShifter.h` | creato | Interfaccia astratta (FR-62) + factory `createDefaultPitchShifter()` |
| `src/dsp/SpectralShifter.{h,cpp}` | creato | Implementazione interinale su Signalsmith Stretch |
| `src/voices/Voice.{h,cpp}` | creato | Una voce = un `PitchShifter` + scratch buffer |
| `src/voices/VoicePool.{h,cpp}` | creato | Somma fino a 8 voci continue (non a frase) |
| `src/PluginProcessor.{h,cpp}` | modificato | Da dry passthrough a catena completa + APVTS (5 parametri) + save/restore stato |
| `src/PluginEditor.{h,cpp}` | modificato | ComboBox root/preset, slider voci/dry/wet con attachment APVTS |
| `CMakeLists.txt` | modificato | Nuovi sorgenti, include dir per q/infra/signalsmith-stretch, `COPY_PLUGIN_AFTER_BUILD FALSE` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 5 (PresetLibrary):**

| File | Stato | Scopo |
|---|---|---|
| `src/harmony/HarmonyPreset.h` | modificato | `Preset` ora ha `juce::Uuid id` (FR-10); `name` diventato `juce::String` |
| `src/harmony/HarmonyEngine.{h,cpp}` | riscritto | Diventa puro calcolo stateless (namespace di funzioni), non possiede piu' la lista preset |
| `src/harmony/PresetLibrary.{h,cpp}` | creato | Lista ordinata, CRUD, CC posizionale, ValueTree (FR-08), libreria globale su disco (FR-09) |
| `src/harmony/CsvIo.{h,cpp}` | creato | Import/export CSV della tabella 12x8 (FR-03) |
| `src/PluginProcessor.{h,cpp}` | modificato | Swap thread-safe `shared_ptr<const PresetLibrary>`, `presetIndex` da Choice a Int 1..128, stato serializzato include la libreria |
| `src/PluginEditor.{h,cpp}` | modificato | ComboBox preset dinamica (Timer, no ComboBoxAttachment), text editor rinomina, bottoni gestione libreria |
| `CMakeLists.txt` | modificato | Nuovi sorgenti `PresetLibrary.cpp`/`CsvIo.cpp`; `COPY_PLUGIN_AFTER_BUILD` rimesso `TRUE` su richiesta utente |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 6 (Fix/Move, Glide, Stability):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/Glide.h` | creato | Rampa lineare a durata fissa (default 30ms, FR-17) |
| `src/dsp/PitchShifter.h` | modificato | `prepare()` prende uno `stabilityLevel`; namespace `Stability` (5 nomi/livelli, FR-54) |
| `src/dsp/SpectralShifter.{h,cpp}` | modificato | Mappa stabilityLevel -> finestra STFT (30-180ms); rimossa guardia inutile su `setPitchShiftSemitones` |
| `src/voices/Voice.{h,cpp}` | modificato | `ShiftMode` (Fix/Move, FR-21/22/23), `Glide` per voce, `swapShifterNoAlloc` |
| `src/voices/VoicePool.{h,cpp}` | modificato | `requestStabilityChange`/`collectGarbage`/swap differito thread-safe (FR-56/57) |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametri `stabilityLevel`, `glideTimeMs`, `voiceFix1..8`; `juce::Timer` per notare i cambi Stability + garbage collect; `canApplyStabilityChangeNow()` (transport/standalone) |
| `src/PluginEditor.{h,cpp}` | modificato | ComboBox Stability, slider Glide, 8 ToggleButton Fix/Move |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 7 (motore a frasi, M3):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/OnsetDetector.{h,cpp}` | creato | Inviluppo di picco + onset_gate (Cycfi Q); evento = fronte di salita (FR-43) |
| `src/voices/Phrase.h` | creato | Dati di una frase: offset congelati, slot assegnati, eta', flag isLive |
| `src/voices/VoicePool.{h,cpp}` | riscritto | Da 8 slot fissi a pool generico di N slot fisici (default 32) |
| `src/voices/PhraseScheduler.{h,cpp}` | creato | Trigger onset, congelamento (FR-46), live-update solo frase piu' recente (FR-17), furto (FR-51/52) |
| `src/PluginProcessor.{h,cpp}` | modificato | Usa `PhraseScheduler` invece di `VoicePool` diretto; nuovo parametro `maxSimultaneousVoices`; wiring onset detection |
| `src/PluginEditor.{h,cpp}` | modificato | Slider "Voice Cap", label "Active" (FR-53) |
| `CMakeLists.txt` | modificato | Nuovi sorgenti `OnsetDetector.cpp`, `PhraseScheduler.cpp` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 8 (scoperta PSOLA esterno — NESSUN file di progetto modificato):**

| File | Stato | Scopo |
|---|---|---|
| `handsoff.md` | aggiornato | Questo aggiornamento — unico file toccato in questa sessione |

File letti (fuori dal progetto, in `C:\Users\cazza\Downloads\TIPS`, mai modificati): `CLAUDE.md`, `harmonizer-scaffold/CLAUDE.md`, `harmonizer-scaffold/CMakeLists.txt`, `harmonizer-scaffold/docs/prd.md`, `harmonizer-scaffold/docs/psola-spec.md`, `harmonizer-scaffold/src/dsp/PitchShifter.h`, `harmonizer-scaffold/src/dsp/PsolaShifter.h`, `harmonizer-scaffold/tests/psola_test.cpp`.

Nessuna modifica a `PRD-Harmonizer-v1.md`. Questa tabella va estesa (non sovrascritta) a ogni sessione futura.

**Sessione 9 (integrazione PSOLA, M1):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/PsolaShifter.{h,cpp}` | creato | Motore TD-PSOLA portato da `TIPS`, ring di epoch RT-safe, chunking interno, mappa Stability->minF0Hz |
| `src/dsp/PitchShifterFactory.cpp` | creato | `createDefaultPitchShifter()` spostata qui da `SpectralShifter.cpp`; default PSOLA, `#define HARMONIZER_USE_SPECTRAL_SHIFTER` per fallback |
| `tests/psola_test.cpp` | creato | Suite numerica portata (5 test originali + 2 nuovi), niente JUCE |
| `src/dsp/PitchShifter.h` | modificato | `setInputF0Hz`/`setFormantRatio` virtuali con default no-op |
| `src/dsp/SpectralShifter.cpp` | modificato | Rimossa la factory (spostata) |
| `src/voices/Voice.cpp` | modificato | Una riga: `setInputF0Hz` da `continuousInputMidiNote` |
| `CMakeLists.txt` | modificato | Nuovi sorgenti + target `psola_test` + `enable_testing()` |
| `.github/workflows/build.yml` | modificato | Job `dsp-tests` come gate prima della build plugin |
| `CLAUDE.md` | modificato | Regole 12/13 (processo) + nota di stato milestone M0->M1 |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 9 (continuazione — Formanti, FR-39..42):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/Voice.{h,cpp}` | modificato | `setFormantSpread`/`setFormantOffsetSemitones`; calcolo di `beta` in `processAdd` da `semitonesToApply` |
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | `setFormantSpread` (globale) e `setVoiceFormantOffset` (per colonna, come `setVoiceMode`) |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametri `formantSpread`, `voiceFormantOffset1..8`; lettura e propagazione in `processBlock` |
| `src/PluginEditor.{h,cpp}` | modificato | Slider "Fmt Spread", 8 knob rotativi "Fmt/Voice"; finestra allargata |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 9 (continuazione — Controllo MIDI CC, M4, FR-29..38):**

| File | Stato | Scopo |
|---|---|---|
| `src/midi/OverrideManager.{h,cpp}` | creato | Logica pura di precedenza CC/automazione (FR-36/37/38), nessuna dipendenza JUCE |
| `tests/override_manager_test.cpp` | creato | 6 test sulla logica di override, target CMake separato |
| `src/midi/CcRouter.{h,cpp}` | creato | Interpretazione CC (FR-30) + MIDI Learn (FR-33), config in `std::atomic` |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametro `bypass`; `MidiBuffer` nominato e passato a `CcRouter`; fronte di stop transport; valori effettivi a valle; persistenza `MidiCcSettings` |
| `src/PluginEditor.{h,cpp}` | modificato | ComboBox canale, 3 slider CC + bottoni Learn, toggle bypass; finestra allargata |
| `CMakeLists.txt` | modificato | Nuovi sorgenti `midi/*.cpp`; target `override_manager_test` + `add_test` |
| `.github/workflows/build.yml` | modificato | Job `dsp-tests` compila ed esegue anche `override_manager_test` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 9 (continuazione — Modalita' Play, FR-24..28):**

| File | Stato | Scopo |
|---|---|---|
| `src/midi/PlayModeInput.{h,cpp}` | creato | `VoicePool` dedicato 8 slot, mapping nota MIDI -> slot, riusa `ShiftMode::fix` di `Voice` con bersaglio assoluto |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametro `playModeEnabled`; secondo scratch buffer; `phraseScheduler` forzato a `inputIsStable=false` mentre Play e' attivo; scelta del buffer wet in base alla modalita' |
| `src/PluginEditor.{h,cpp}` | modificato | `ToggleButton` "Play Mode"; finestra allargata |
| `CMakeLists.txt` | modificato | Nuovo sorgente `midi/PlayModeInput.cpp` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 10 (primo test reale in Ableton — fix ciclo di vita frasi, UI, diagnostica):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/Phrase.h` | modificato | Commento aggiornato: risoluzione FR-17/FR-46 col bottone Keep Tails |
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | `setKeepTails(bool)`; le frasi smontate si liberano subito quando `keepTails=false` (default) |
| `src/PluginProcessor.{h,cpp}` | modificato | Parametro `keepPhraseTails`; atomici `lastDetectedMidiNote`/`lastDetectedConfidence`/`lastInputStable` + getter |
| `src/PluginEditor.{h,cpp}` | modificato | Fix bounds `fixMoveLabel`/`voiceFormantLabel` (erano invisibili); slider Fmt/Voice ingrandite; titolo aggiornato; toggle "Keep Tails"; label "Detected"; finestra allargata |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 11 (isteresi di intonazione per note legate, FR-16/17):**

| File | Stato | Scopo |
|---|---|---|
| `src/harmony/PitchLatch.h` | creato | Isteresi ±25 cent, aggancio/sgancio a gradino bloccato al nearest-round; pura, nessuna dipendenza JUCE |
| `tests/pitch_latch_test.cpp` | creato | 8 gruppi di verifiche, incluso il test che ha scoperto il bug di rimbalzo prima dell'integrazione |
| `src/PluginProcessor.{h,cpp}` | modificato | Nuovo membro `pitchLatch`; `quantizedPlayedNote` ora da `pitchLatch.update(...)` invece di `roundToInt` |
| `CMakeLists.txt` | modificato | Nuovo target `pitch_latch_test` + `add_test` |
| `.github/workflows/build.yml` | modificato | Job `dsp-tests` compila ed esegue anche `pitch_latch_test` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 11 (continuazione — `freeAllPhrases()` legato al segnale, non al pitch):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | Nuovo parametro `signalPresent` (governa `freeAllPhrases`), distinto da `inputIsStable` (governa il live-update FR-17) |
| `src/PluginProcessor.{h,cpp}` | modificato | `signalPresent` da `onsetDetector.isGateOpen()`; `pitchLatch.reset()` legato a `signalPresent`; Play mode forza entrambi i segnali |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 15 (inizio M5 — editor tabella preset 12x8):**

| File | Stato | Scopo |
|---|---|---|
| `src/harmony/PresetLibrary.{h,cpp}` | modificato | Nuovo `setCell(presetIndex, degree, voice, Cell)` |
| `src/ui/CellInputParser.h` | nuovo | Parser di input validato per le celle (0 vs vuoto vs rifiutato) |
| `src/ui/DegreeNames.h` | nuovo | Nomi di grado leggibili per le intestazioni di colonna |
| `src/ui/PresetTableEditor.{h,cpp}` | nuovo | Griglia 12x8 editabile, primo componente custom del progetto |
| `src/PluginEditor.{h,cpp}` | modificato | Nuovo membro `presetTableEditor`; aggancio in `syncPresetSelectionFromParameter()`; finestra ingrandita |
| `tests/cell_input_parser_test.cpp` | nuovo | 19 verifiche, tutte verdi |
| `CMakeLists.txt` | modificato | `src/ui/PresetTableEditor.cpp` nei sorgenti principali; nuovo target `cell_input_parser_test` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

Nessuna modifica a `HarmonyEngine`, `CsvIo`, `PsolaShifter`, `PitchDetector`, `PhraseScheduler`, `PlayModeInput`.

**Sessione 14 (click "a inizio nota" — PSOLA mai resettato fra una nota e la successiva):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/Voice.h` | modificato | `setMuted(bool)`: chiama `shifter->reset()` alla transizione silenzio→attiva |
| `tests/psola_test.cpp` | modificato | Nuovo Test 9: riattivazione di uno slot dopo inattivita', con/senza `reset()`, confrontato con un riferimento "mai usato" |
| `handsoff.md` | aggiornato | Questo aggiornamento |

Nessuna modifica a `PsolaShifter`, `PhraseScheduler`, `PlayModeInput`, `Glide`, `PluginProcessor`.

**Sessione 13 (click residui + diagnosi wobbling — causa: block size dell'host):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/Glide.h` | modificato | Nuovo `processRamp(numSamples)`: espone la rampa gia' calcolata da `process()` come struct `{startValue, increment, rampSamples}`, per interpolazione campione-per-campione; `process()` invariata |
| `src/voices/Voice.cpp` | modificato | `ampGlide.process()` → `ampGlide.processRamp()`; guadagno applicato campione per campione nel ciclo di mixing finale |
| `src/PluginProcessor.{h,cpp}` | modificato | `dryGlide`/`wetGlide` idem; nuovo atomico `lastBlockSize`/`getLastBlockSize()` (diagnostica) |
| `src/PluginEditor.cpp` | modificato | Label "Detected" mostra anche `blk N` (block size dell'host in questo blocco) |
| `tests/glide_test.cpp` | nuovo | 6 gruppi di verifiche (15 controlli): rampa in un blocco grande, rampa spezzata su piu' blocchi piccoli (nessuna deriva), re-target a meta' rampa, `process()` invariata per `offsetGlide` |
| `CMakeLists.txt` | modificato | Nuovo target/test `glide_test` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

Nessuna modifica a `PsolaShifter`, `PitchDetector`, `OnsetDetector`, `PhraseScheduler`, `PlayModeInput` — il wobbling, diagnosticato ma non corretto in questa sessione, li coinvolgerebbe (vedi §6, Fase 4).

**Sessione 12 (note saltate — corsa onset/pitch, FR-43/45/46):**

| File | Stato | Scopo |
|---|---|---|
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | Ramo live-update completa l'allocazione degli slot rimasti vuoti al trigger; nuovo contatore `numLateBindingsTotal`/`getNumLateBindings()` |
| `src/PluginProcessor.{h,cpp}` | modificato | `signalPresentLastBlock`; `pitchDetector.reset()` sul fronte di discesa di `signalPresent`; nuovo atomico `lastGateOpen`/`getLastGateOpen()` |
| `src/PluginEditor.cpp` | modificato | Label "Detected" mostra anche `gate open/closed` e `late-bindings N` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 12 (continuazione — timbro robotico/granuloso, fix `emitGrain`):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/PsolaShifter.cpp` | modificato | `emitGrain`: floor su `Lg` (lunghezza del grano in uscita) basato su `synthPeriod`, indipendente da `beta` |
| `tests/psola_test.cpp` | modificato | Nuovo Test 8: sovrapposizione dei grani con la `beta` reale prodotta dalla correzione formantica di default di `Voice.cpp` |
| `handsoff.md` | aggiornato | Questo aggiornamento |

**Sessione 12 (continuazione — fix click/scricchiolii, dissolvenza di ampiezza per voce):**

| File | Stato | Scopo |
|---|---|---|
| `src/dsp/Glide.h` | modificato | Nuovo `isSettled()` |
| `src/voices/Voice.{h,cpp}` | modificato | `ampGlide` (rampa fissa 8ms), `setMuted`/`isSilent()`, `processAdd` non esce piu' subito su `muted` |
| `src/voices/Phrase.h` | modificato | Nuovo flag `releasing` |
| `src/voices/PhraseScheduler.{h,cpp}` | modificato | `freePhrase` diviso in `hardFreePhrase` (solo furto d'emergenza) e `beginRelease` (rilascio morbido); ciclo di mixing gestisce frasi in rilascio e celle tornate vuote su frasi vive (FR-17) |
| `src/midi/PlayModeInput.cpp` | modificato | Stesso pattern di dissolvenza su note-off e uscita da Play mode |
| `src/PluginProcessor.{h,cpp}` | modificato | `dryGlide`/`wetGlide`: dry/wet/bypass non piu' un salto istantaneo per blocco |
| `handsoff.md` | aggiornato | Questo aggiornamento |

---

## 4. Cambiamenti in questa sessione

**Sessione 1:**
- Letto integralmente `PRD-Harmonizer-v1.md` per ricostruire il contesto del prodotto.
- Ricognizione della cartella di progetto (era vuota a parte il PRD).
- Creato `handsoff.md` con la struttura di handoff richiesta dall'utente.

**Sessione 2 — M0 avviato:**
- Verificata la toolchain locale: git 2.54, CMake 4.4.0-rc2, Visual Studio 18 Community con MSVC 19.51 (C++ workload presente). Connettività GitHub e spazio disco confermati.
- `git init` nella cartella di progetto; creata la struttura di directory `src/{dsp,harmony,voices,midi,state,licensing,ui}`, `resources/factory_presets`, `tests`, `tools`, `.github/workflows` (PRD §9.3).
- JUCE aggiunto come submodule e pinnato al tag `8.0.15` (ultimo rilascio 8.x disponibile).
- Scritto `CMakeLists.txt`: `juce_add_plugin` con `NEEDS_MIDI_INPUT TRUE`, `IS_MIDI_EFFECT FALSE`, `AU_MAIN_TYPE kAudioUnitType_MusicEffect`, `VST3_CATEGORIES Fx "Pitch Shift"`, `FORMATS VST3 AU Standalone`.
- Scritto uno stub minimo di `PluginProcessor`/`PluginEditor`: dry passthrough, nessuna allocazione/lock/I/O in `processBlock` (rispetta la regola 1 di `CLAUDE.md` fin dal primo commit).
- Scritto `CLAUDE.md` alla radice con le 11 regole del PRD §15.
- Configurato il progetto con CMake (generatore Visual Studio 18 2026, x64) — configurazione riuscita.
- Compilati con successo in locale (Release x64): target **VST3** e target **Standalone**.
- Scaricato `pluginval` v1.0.4 per Windows in `tools/` (ignorato da git) ed eseguito `--strictness-level 10 --validate` sul VST3 → **SUCCESS**, nessun test fallito.
- Scritta la CI GitHub Actions (`build.yml`) per Windows + macOS con gate pluginval strictness 10 su VST3 (entrambe) e AU (solo macOS). Non ancora eseguita: manca un remote.
- Corretto un errore proprio: avevo impostato `user.email` nella git config locale del repo per errore (violazione della regola "mai modificare la git config"); rimosso subito con `git config --unset`. Per i commit di questa sessione uso `-c user.name=/-c user.email=` inline invece di persistere l'identità in config.

Nessuna modifica al PRD.

**Sessione 3 — push su GitHub:**
- Controllato il contenuto della repo remota (4 file caricati manualmente dall'utente via web UI) prima di sovrascrivere.
- Aggiunto remote `origin`, rinominato branch locale `master` → `main`, force-push del commit M0 completo.
- Push del workflow CI ha avviato automaticamente una run di GitHub Actions (effetto collaterale non richiesto, discusso con l'utente e lasciato proseguire).

**Sessione 4 — vertical slice DSP end-to-end:**
- Verificata l'esito della run CI della sessione precedente (Windows + macOS entrambe verdi, incluso AU).
- Aggiunti come submodule: **Cycfi Q** (`libs/q`, con submodule annidato `infra` inizializzato via `git submodule update --init --recursive`) e **Signalsmith Stretch** (`libs/signalsmith-stretch`, tag `1.1.0`). Verificate le licenze (Boost License / MIT) prima di integrarle.
- Studiata l'API pubblica di entrambe le librerie leggendo header e doc/esempi nel repository stesso (non assunta a memoria), in particolare: `cycfi::q::pitch_detector` (costruttore con range di frequenza + soglia in dB, `operator()` per-sample, `get_frequency()`/`periodicity()`), `q::pitch` per la conversione Hz→MIDI, e `SignalsmithStretch::process()` (STFT, richiede buffer in/out distinti, latenza riportata via `inputLatency()`/`outputLatency()`).
- Progettato e implementato un algoritmo di generazione dei preset di fabbrica ("drop voicing" ciclico sui toni dell'accordo) **verificato per calcolo diretto** contro l'unico dato di prototipo presente nel PRD (preset Min, d=2 → [-2,-4,-7,-11,null×4]): l'algoritmo riproduce esattamente quei valori. Gli altri 6 preset (Maj, Dom, Sus, Half Dim, Dim, Aug7) usano tonalita' standard di jazz con lo stesso algoritmo, **non verificate sul prototipo reale** — da sostituire con import CSV (FR-03) quando l'utente avra' i dati originali.
- Implementata la catena completa (vedi §2) e i parametri APVTS.
- Aggiornato `CMakeLists.txt` con i nuovi sorgenti e gli include path delle librerie header-only.
- Compilazione, debug e validazione locale (vedi §5 per gli errori incontrati e come sono stati risolti).

**Sessione 5 — PresetLibrary reale (scelta dall'utente tra 4 direzioni proposte):**
- Chiesto esplicitamente all'utente quale area sviluppare dopo il vertical slice (PresetLibrary / qualita' pitch shifting / motore a frasi / MIDI CC): scelta la PresetLibrary.
- Riattivato `COPY_PLUGIN_AFTER_BUILD TRUE` su richiesta diretta dell'utente, documentando nel commento CMake che serve una shell elevata su Windows perche' funzioni.
- Analizzato il requisito FR-05/FR-35 (CC posizionale + parametro discreto automatizzabile) e riconosciuto che confligge con l'uso di `AudioParameterChoice` se la libreria puo' cambiare dimensione a runtime (le "choices" di un parametro APVTS sono fisse). Risolto passando a `AudioParameterInt` 1..128, che modella gia' correttamente la semantica "posizione = valore" richiesta da FR-05.
- Riconosciuto e affrontato esplicitamente il problema di **thread-safety** descritto (ma non risolto in dettaglio) dal PRD §9.4: mutare la libreria da UI mentre l'audio thread la legge e' una race condition reale. Implementato uno schema a snapshot immutabili (`shared_ptr<const PresetLibrary>`) scambiati sotto `SpinLock`, con un singolo slot "retired" per evitare la distruzione sull'audio thread nel caso normale — vedi §2 per il limite noto di questo compromesso.
- Riprogettata la separazione dei moduli per rispecchiare l'albero di file del PRD §9.3: `HarmonyEngine` (calcolo puro) + `PresetLibrary` (stato/CRUD/CC) + `CsvIo` (I/O) invece di un'unica classe.
- Costruito l'editor con sincronizzazione a polling (Timer 15Hz) invece di un `ComboBoxAttachment`, dato che quest'ultimo assume un parametro con scelte statiche.
- Compilazione, debug (vedi sotto) e validazione pluginval.

**Sessione 6 — qualita' pitch shifting, scelta dall'utente tra 4 direzioni proposte:**
- Chiesto di nuovo esplicitamente all'utente quale area sviluppare (stesse 4 opzioni + Formanti aggiunta): scelta "Qualita' pitch shifting (M1)".
- Prima di implementare Fix, **verificato leggendo il sorgente** di `signalsmith-stretch.h` che `setTransposeSemitones`/`setTransposeFactor` non allocano mai (assegnano due float e azzerano un `std::function` gia' nullo, dato che non usiamo mai `setFreqMap`) — questo smentiva una mia precedente cautela eccessiva (la guardia "chiama solo se cambia" nella sessione 4) e ha sbloccato la modalita' Fix, che richiede il ricalcolo ad ogni blocco.
- Progettato lo schema realtime-safe per il cambio di Stability (che invece ALLOCA davvero, via `configure()`): costruzione sul message thread (Timer) + applicazione sull'audio thread solo quando sicuro (transport fermo o standalone) + smaltimento dei vecchi shifter sul message thread — stesso principio della PresetLibrary (sessione 5) ma con un meccanismo diverso (`unique_ptr::swap` + lista di "retired" invece di `shared_ptr`+`SpinLock`), perche' un `PitchShifter` non e' un value-type economico da copiare come un preset.
- L'utente ha chiuso Ableton Live per errore di sequenza (l'avevamo lasciato aperto con il vecchio VST3 caricato) causando un fallimento di link (`LNK1104`, file bloccato) al primo tentativo di ricompilare: risolto chiudendo Ableton e ricompilando.
- Dopo il fix del link, build e `pluginval --strictness-level 10` verdi con Fix/Move, Glide e Stability inclusi.

**Sessione 7 — motore a frasi, scelta dall'utente tra le direzioni proposte (dopo una domanda di status su M0):**
- Chiesto lo stato di M0 prima di procedere: confermato tecnicamente completo (CMake, 3 target, CI, pluginval), con solo decisioni non tecniche in sospeso (licenza JUCE, certificati, nome prodotto).
- Chiesto di nuovo quale area sviluppare: scelto "Motore a frasi (M3)".
- Studiata l'API di `q::onset_gate`/`q::noise_gate` (Cycfi Q) leggendo la documentazione inline in `noise_gate.hpp`, che mostra esplicitamente il pattern d'uso raccomandato (inviluppo -> gate): seguito alla lettera invece di indovinare i parametri.
- **Decisione di design centrale**: interpretato il modello a frase come "una ricetta di offset congelati applicata in continuo al segnale live in ingresso" (non un frammento audio a durata fissa) — dedotto dall'esempio del PRD sulle 16 frasi simultanee generate da una linea di ottavi su un pattern di 2 misure, che ha senso solo se le frasi restano vive finche' non vengono rubate o il segnale tace, non per una durata fissa breve.
- Risolta esplicitamente (con una scelta motivata, non ancora validata all'ascolto) la tensione FR-17/FR-46 segnalata come `[DECISION]` nel PRD: solo la frase piu' recente resta "viva". Vedi §2 e Phrase.h per il dettaglio.
- Riutilizzato il `Glide` gia' esistente (da sessione 6) per risolvere la "dissolvenza di almeno 20ms" richiesta da FR-52 sul furto di frase, evitando di costruire un sistema di crossfade/slot di riserva separato.
- Generalizzato `VoicePool` da 8 slot fissi a un pool di N slot riutilizzabile sia dalle 8 "colonne armoniche" di ogni frase sia dal meccanismo di furto.
- Build e `pluginval --strictness-level 10` verdi al primo tentativo (nessun errore di compilazione in questa sessione).

**Sessione 8 — scoperta e verifica del PSOLA esterno (richiesta esplicita: nessuna modifica al progetto):**
- Localizzata la cartella `TIPS` (non era nel progetto, ne' menzionata prima): trovata in `C:\Users\cazza\Downloads\TIPS` cercando piu' in profondita' dopo che una ricerca nel progetto non ha dato risultati.
- Letta per intero: `CLAUDE.md` (loro), `docs/prd.md` (confrontato con `diff` contro il nostro — **identico**), `docs/psola-spec.md`, `src/dsp/PitchShifter.h`, `src/dsp/PsolaShifter.h`, `tests/psola_test.cpp`, `CMakeLists.txt`.
- **Compilato ed eseguito io stesso** `psola_test.cpp` con MSVC (`cl.exe` via `vcvarsall.bat x64`), in una cartella temporanea creata sotto lo scratchpad di sessione — non sotto il progetto, non sotto `Downloads/TIPS`. Un solo intoppo (mancava `_USE_MATH_DEFINES` per `M_PI`, aggiunto come flag `/D_USE_MATH_DEFINES` senza toccare i sorgenti), poi tutti i 5 test superati con i numeri riportati nella spec confermati esattamente.
- Confrontata l'interfaccia `harm::PitchShifter` (loro) con la nostra `PitchShifter` per capire il gap di integrazione reale (vedi §2 per il dettaglio: `setF0` esplicito mancante da noi, unita' di misura diverse — alpha/beta vs semitoni, `minF0Hz` vs `stabilityLevel`).
- Nessun file di progetto toccato in questa sessione, ne' prima ne' dopo questo aggiornamento a `handsoff.md`, come esplicitamente richiesto dall'utente.

**Sessione 9 — integrazione PSOLA (scelta dall'utente tra le direzioni proposte):**
- Chiesta esplicitamente all'utente la direzione tra le opzioni segnalate come aperte in sessione 8 (PSOLA / MIDI CC / Formanti / solo processo): scelto "Integrare il PSOLA di TIPS", poi due decisioni di perimetro chieste esplicitamente (selezione motore a compile-time vs UI; solo motore vs anche Formanti; test in CI o solo locali) — vedi §6 vecchia versione per le opzioni presentate.
- **Esplorazione parallela** (due agenti) del nostro DSP attuale e dei sorgenti `TIPS`, poi lettura diretta dei file chiave (`PsolaShifter.h`, `PitchShifter.h` nostro e loro, `Voice.cpp`, `VoicePool.h`) prima di scrivere qualunque riga di piano, per non fidarsi solo dei riassunti degli agenti sui dettagli che contavano di piu' (le firme esatte, dove alloca il codice).
- Lavoro condotto a piccoli passi verificati singolarmente col compilatore (deque->ring, poi chunking, poi correzione `emitGrain`), non come un'unica riscrittura: ha permesso di isolare la regressione del problema 3 (sotto) al passo esatto che l'ha introdotta, invece di dover fare debug su un cambiamento cumulativo.
- **Il primo tentativo di correggere il bug di sovrapposizione (`emitGrain`) ha rotto un test che prima passava** (test 1, accuratezza di trasposizione a -12 semitoni): invece di allentare la soglia del test o di scartare il fix, si e' analizzato perche' l'uscita fosse davvero cambiata (grano troppo lungo = periodicita' originale reintrodotta), trovata la causa esatta e corretta la formula stessa (margine minimo analitico invece del margine largo iniziale). Coerente con la nuova regola 13 di `CLAUDE.md`: un test che fallisce dopo una correzione non e' automaticamente "il test sbagliato" — a volte lo e' davvero il codice.
- Costruito un controllo ad-hoc separato (fuori dalla suite permanente, in scratchpad) per verificare l'invarianza dell'uscita rispetto a come l'host suddivide le chiamate a `process()` — proprieta' non coperta dai 7 test della suite ma cruciale per la correttezza del chunking interno introdotto in questa sessione.
- Build reale (non solo compilazione isolata) e `pluginval --strictness-level 10` eseguiti a fine sessione, con esito riportato per intero (regola 8 di `CLAUDE.md`).

**Sessione 10 — primo test reale in Ableton, diagnosi e correzioni:**
- L'utente ha condiviso uno screenshot dell'editor e 8 osservazioni testuali dal primo uso reale in Ableton. Ogni causa e' stata confermata **leggendo direttamente il codice** (non ipotizzata): `PluginEditor.cpp`, `PhraseScheduler.cpp`, `OnsetDetector.cpp`, `PitchDetector.cpp` riletti integralmente prima di proporre qualunque fix.
- L'utente ha poi condiviso un file Excel (letto estraendo l'XML interno, `.xlsx` e' uno zip) che illustra il comportamento desiderato del futuro Pattern Ritmico con un esempio concreto (tre note in corsa scaglionata, una che taglia la coda dell'altra) — questo ha permesso di **riformulare il fix del bug principale come un bottone** ("Keep Tails") invece di un comportamento fisso, risolvendo il bug oggi E preparando il terreno per la feature futura senza lavoro sprecato.
- Due domande di chiarimento poste esplicitamente prima di scrivere il piano (comportamento del bottone Keep Tails e relativo default; se tentare anche un fix speculativo per la corsa onset/pitch o solo aggiungere diagnostica) — l'utente ha risposto scegliendo le opzioni consigliate per entrambe.
- Il vecchio file di piano (relativo all'integrazione PSOLA, sessione 9, gia' commessa) e' stato **sovrascritto** con il nuovo piano di questa sessione, come da istruzioni del sistema di planning per un task diverso.
- Build VST3 bloccata una volta da `LNK1104` (Ableton aveva ancora il plugin caricato) durante il primo tentativo — stesso caso esatto di sessione 6, risolto chiedendo all'utente di chiudere Ableton e ricompilando con successo.

**Sessione 11 — isteresi di intonazione, diagnosi corretta dall'utente:**
- Riletti `PhraseScheduler.cpp`, `PluginProcessor.cpp`, `HarmonyEngine.cpp`, `PresetLibrary.cpp` per formulare una prima ipotesi (instabilita' del rilevatore -> `freeAllPhrases()`), presentata all'utente in un piano — **l'utente l'ha corretta**, riconoscendo che la causa reale era l'assenza di isteresi sulla nota per il lookup armonico, con una proposta numerica precisa (±25 cent) e la matematica di Fix/Move verificata a mano con esempi in Hz.
- **Scoperto un bug nel MIO stesso design prima di consegnarlo**, non dall'utente: il primo tentativo di implementazione (passo incondizionato di un semitono oltre la soglia) rimbalza avanti e indietro ad ogni blocco durante uno scivolamento lento — provato scrivendo apposta un test "nessun rimbalzo" (test 4) e vedendolo fallire prima ancora di integrare il codice nel plugin. Corretto bloccando lo scatto al risultato di un arrotondamento standard (`std::min`/`std::max`), che non puo' mai superare la nota di destinazione. Lezione per le prossime sessioni: quando un test esplicito di un caso limite fallisce PRIMA dell'integrazione, e' esattamente il momento in cui costa meno correggerlo.
- Il piano di sessione 10 (finestra temporale) e' stato sovrascritto con quello di sessione 11 (isteresi in cent) dopo la correzione dell'utente, come da istruzioni del sistema di planning per un task diverso ma correlato.
- **Dopo aver consegnato il fix dell'isteresi, l'utente ha rifatto il test e il sintomo persisteva** (solo la prima nota si armonizzava, nonostante la label "Detected" seguisse correttamente le note): segno che l'isteresi era necessaria ma non sufficiente. Rianalizzato `PhraseScheduler::process()` e trovata la mia ipotesi originale (accantonata, non scartata come sbagliata) — confermato che `freeAllPhrases()` era ancora legato alla confidenza del pitch (`inputIsStable`), non alla presenza del segnale. Risolto separando i due segnali (vedi §2) **senza** la finestra temporale scartata in precedenza, usando invece un segnale gia' disponibile (`OnsetDetector::isGateOpen()`) che risponde alla domanda giusta ("il performer sta ancora suonando?") invece che a quella sbagliata ("il pitch e' confidente in questo esatto blocco?"). Lezione: due bug distinti possono produrre lo stesso sintomo osservabile — risolverne uno non implica che il sintomo sia sparito per la ragione giusta, va sempre riverificato.

**Sessione 12 — note saltate, causa confermata a lettura di codice (nessuna nuova ipotesi discussa con l'utente, il "perche'" era gia' verificabile nel codice esistente):**
- Rilette `PluginProcessor.cpp`, `PhraseScheduler.{h,cpp}`, `OnsetDetector.cpp`, `PitchDetector.cpp` per formulare la diagnosi (vedi §2) prima di proporre qualunque piano — nessuna riga scritta prima di aver rintracciato la causa esatta nel codice.
- Chiesto esplicitamente all'utente su cosa lavorare tra 4 opzioni proposte (corsa onset/pitch consigliata / taratura PSOLA / preset timbrici / pattern ritmico) e, separatamente, di specificare il feedback di qualita' lasciato in sospeso a fine sessione 11: l'utente ha scelto la corsa onset/pitch e ha aggiunto due dettagli (note saltate "senza una logica troppo precisa"; timbro "non fedele al segnale sorgente, robotico e granuloso").
- Il secondo dettaglio (timbro) e' stato deliberatamente lasciato fuori scope per questa sessione (l'utente ha scelto l'altra area) ma tre candidati concreti in `PsolaShifter.cpp` sono stati annotati per non ripartire da zero — vedi §2.
- Build, ctest, `pluginval` eseguiti ed esito riportato per intero (regola 8); nessun ascolto possibile da questa sessione (regola 12) — il fix resta "da confermare", non "completo".

**Sessione 12 (continuazione) — timbro, bug reale trovato e corretto in `PsolaShifter`:**
- L'utente ha confermato all'ascolto il fix precedente (note saltate) e ha chiesto di passare al feedback sul timbro lasciato in sospeso.
- Letto `PsolaShifter.cpp`/`Voice.cpp` insieme, non isolatamente: il bug e' emerso proprio dall'incrocio fra i due file (la formula di `beta` in `Voice.cpp`, che di per se' e' corretta, interagisce male con un'assunzione implicita in `PsolaShifter::emitGrain` che nessuno dei due file da solo rendeva visibile).
- **Verificato per calcolo A MANO prima di scrivere qualunque codice** (il deficit di ~19% a -12 semitoni), poi **verificato empiricamente scrivendo un test che fallisce sul codice non ancora corretto** (Test 8), poi corretto, poi riverificato che lo stesso test passa E che nulla di preesistente e' cambiato (Test 1/6 identici cifra per cifra) — stessa disciplina di sessione 11 (regola 13: un test che fallisce dopo un fix va capito, non scartato; e viceversa, un test che ora passa va controllato che non sia passato "per caso" riducendo la soglia).
- Deliberatamente NON esplorati in questa sessione gli altri due candidati (posizionamento epoch, correlazione fra le 8 istanze): un bug alla volta, verificato, per poter attribuire correttamente un eventuale miglioramento (o assenza di miglioramento) all'ascolto.

**Sessione 12 (continuazione) — click/scricchiolii, fix architetturale sulla dissolvenza delle voci:**
- L'utente ha riportato due sintomi (click ricorrenti; armonizzazione "non stabile al 100%", sospettando formanti/riarmonizzazioni) senza sapere la causa esatta — non ho chiesto di specificare ulteriormente, perche' un salto di ampiezza discontinuo (ciò che ho trovato) e' per definizione un click, verificabile a lettura di codice senza bisogno di altri dettagli dall'utente.
- Letti insieme `PhraseScheduler.cpp`, `PlayModeInput.cpp`, `PluginProcessor.cpp`: il pattern (voce smette di essere processata → ampiezza da piena a zero in un blocco) si ripeteva identico in quattro punti indipendenti — non un singolo bug isolato ma un gap architetturale sistemico (nessun meccanismo di dissolvenza esisteva per NESSUNA transizione di stato di una voce).
- Progettato un fix con DUE regole di sicurezza esplicite per non introdurre regressioni sul furto di voce (FR-52): (1) il furto d'emergenza resta istantaneo come prima (nessun ritardo nell'ottenere uno slot quando il pool e' esaurito — la continuita' li' viene comunque dal Glide dell'offset gia' esistente); (2) `allocateFreeSlot` preferisce rubare una frase gia' in rilascio piuttosto che una piena, minimizzando il disturbo quando serve rubare comunque.
- Build, ctest, `pluginval` eseguiti ed esito riportato per intero; nessun ascolto possibile in questa sessione — il fix resta "da confermare", non "completo", nonostante la certezza logica che elimina la CLASSE di bug diagnosticata.

---

## 5. Cosa non ha funzionato e perché

**Sessione 2:**
- Primo tentativo di aggiungere JUCE come submodule con `git submodule add --branch 8.0.15 --depth 1 ...` è fallito: git non riesce a fare shallow-clone diretto di un **tag** trattandolo come branch (`'origin/8.0.15' is not a commit and a branch '8.0.15' cannot be created from it`). Risolto clonando il submodule per intero (senza `--depth`/`--branch`) e poi facendo `git checkout 8.0.15` dentro il submodule. Costo: clone completo di JUCE invece che shallow, ma nessun impatto pratico (spazio disco abbondante).
- Ho impostato per errore `git config user.email` in locale nel repo appena creato (per permettere ai commit di funzionare, dato che non c'era un'identità git globale). Questo viola la regola di non toccare mai la git config. Rimediato subito con `git config --unset user.email` prima di procedere; per committare userò i flag `-c user.name=... -c user.email=...` per-comando, senza persistere nulla.
- **AU non compilabile né validabile su questa macchina**: è previsto e non un fallimento — il formato Audio Unit richiede macOS/Xcode. **Aggiornamento sessione 3**: verificato con successo in CI su `macos-latest`, incluso AU.

**Sessione 4:**
- **Errore di compilazione (pimpl con `std::optional<Impl>`)**: `PitchDetector` nascondeva `cycfi::q::pitch_detector` dietro un tipo `Impl` forward-dichiarato, tenuto in `std::optional<Impl>`. MSVC ha fallito con una serie di errori `C2139`/`C2079` (`Impl` incompleto non valido per `__is_trivially_destructible` ecc.) in ogni TU diversa da `PitchDetector.cpp`. Causa: a differenza di `std::unique_ptr<T>`, `std::optional<T>` istanzia i type-trait di `T` ovunque il tipo contenitore venga usato, non solo dove il costruttore/distruttore sono definiti — quindi non supporta tipi incompleti come member, anche con distruttore dichiarato esplicitamente e definito nel `.cpp`. Corretto sostituendo con `std::unique_ptr<Impl>` (il pattern pimpl standard, che invece funziona correttamente con tipi incompleti).
- **Copia automatica del VST3 fallita** (`COPY_PLUGIN_AFTER_BUILD TRUE`): il post-build step di JUCE prova a copiare in `C:\Program Files\Common Files\VST3`, che richiede permessi di amministratore non disponibili in questo ambiente ("Permission denied"). Non e' un bug del codice: e' il comportamento normale su Windows senza privilegi elevati (documentato nella CMake API di JUCE stessa). Risolto disattivando `COPY_PLUGIN_AFTER_BUILD` e documentando in `CMakeLists.txt`/qui come caricare il plugin in un host puntando alla cartella di build (vedi §6).
- Dopo questi due fix, build VST3 e `pluginval --strictness-level 10` sono verdi con la catena DSP reale.

**Sessione 5:**
- **Falso "exit code 0"**: la prima build dopo aver aggiunto `PresetLibrary`/`CsvIo` sembrava riuscita (notifica di completamento con exit code 0), ma conteneva in realta' una ventina di errori di compilazione reali (`juce::ValueTree` non trovato). Causa: avevo incanalato l'output della build in `| tail -N`, e in una pipeline bash l'exit code riportato e' quello dell'**ultimo** comando (`tail`, sempre 0), non quello di `cmake --build` — quindi il fallimento vero passava inosservato. Corretto aggiungendo `set -o pipefail` prima dei comandi di build/validazione successivi, cosi' l'exit code della pipeline riflette il primo comando che fallisce. **Lezione da applicare sempre in questo progetto**: mai fidarsi dell'exit code di un comando incanalato in `tail`/`head`/`grep` senza `pipefail`, controllare comunque il contenuto del log per errori.
- **Errore reale**: `juce::ValueTree` non e' dichiarato includendo solo `<juce_core/juce_core.h>` — `ValueTree` vive nel modulo separato `juce_data_structures`, non in `juce_core`. `PluginProcessor.cpp` aveva gia' funzionato perche' include `juce_audio_processors.h`, che dipende transitivamente da `juce_data_structures`; `PresetLibrary.h`, includendo solo `HarmonyPreset.h` (-> `juce_core`), non ce l'aveva. Risolto aggiungendo `#include <juce_data_structures/juce_data_structures.h>` a `PresetLibrary.h`.
- Dopo questi due fix, build VST3 e `pluginval --strictness-level 10` sono di nuovo verdi, con la `PresetLibrary` completa inclusa.

**Sessione 6:**
- **LNK1104 al primo tentativo di ricompilare**: "impossibile aprire il file Harmonizer.vst3" durante il link. Causa: Ableton Live aveva ancora il VST3 della sessione precedente caricato/aperto, e Windows blocca la sovrascrittura di un file (dll) in uso da un altro processo. Non un bug — chiesto all'utente di chiudere Ableton, poi ricompilato con successo. **Lezione per le prossime sessioni**: se un link fallisce con LNK1104/1103 su un file .vst3/.dll, prima ipotesi da controllare e' "un host ha ancora il plugin caricato", non un errore di codice.

**Sessione 7:**
- Nessun errore incontrato: build e pluginval verdi al primo tentativo, nonostante la riscrittura sostanziale di VoicePool e i due nuovi file (OnsetDetector, PhraseScheduler). Probabilmente dovuto ad aver progettato con cura la sincronizzazione dei thread PRIMA di scrivere codice (stesso schema gia' rodato in sessione 5/6), invece di scoprirla per tentativi.

**Sessione 8:**
- **`M_PI` non dichiarato compilando `psola_test.cpp` con MSVC**: `error C2065: 'M_PI': identificatore non dichiarato`. Non e' un bug dell'algoritmo — MSVC espone `M_PI` solo se `_USE_MATH_DEFINES` e' definito PRIMA di includere `<cmath>`, a differenza di g++/clang dove e' disponibile di default. Risolto passando `/D_USE_MATH_DEFINES` come flag di compilazione (nessuna modifica ai sorgenti, che restano quelli scaricati). **Correzione sessione 9**: la frase seguente (rimossa) attribuiva il problema al motore PSOLA in generale — falso, verificato leggendo `PsolaShifter.h`: non usa affatto `M_PI` (costante letterale scritta a mano), riguardava solo il file di test. Nel nostro porting evitato del tutto con una `constexpr double` locale.

**Sessione 9:**
- **La prima correzione del bug di sovrapposizione in `emitGrain` ha rotto il test 1**: allargando la semiampiezza del grano al margine "largo" (`W = P * max(1, 1/alpha)`, cio' che il commento originale sembrava suggerire), a -12 semitoni la f0 misurata tornava quella originale (errore di un'ottava esatto). Causa: a `beta=1` il grano e' una copia diretta, non trasposta, del segnale sorgente — un grano piu' lungo della spaziatura fra grani reintroduce direttamente la periodicita' ORIGINALE (non shiftata) al suo interno, che l'autocorrelazione del test rileva come dominante sulla periodicita' "strutturale" data dalla spaziatura. Non era un problema del test (la sua stessa logica anti-ottava, controllata, era corretta): l'uscita conteneva davvero energia forte alla frequenza originale. Risolto usando il margine minimo analiticamente necessario a far toccare i grani (`W = 1.2 * max(P, P/(2*alpha))`, derivato dalla condizione `Lg=2W >= synthPeriod`) invece del margine largo — tutti e 7 i test verdi dopo la correzione, confermato che nessun altro test e' peggiorato.
- Nessun errore di compilazione incontrato nel resto della sessione (build VST3/Standalone e `pluginval` verdi al primo tentativo dopo il porting completo).

**Sessione 20:**
- **Primo design del fix del wobbling ha rotto la trasposizione di pitch**: far avanzare `synthPos` della differenza fra l'epoch scelto in QUESTA sintesi e quello scelto nella sintesi PRECEDENTE (`lastSynthEpoch`, persistente fra chiamate) sembrava equivalente a "usare la spaziatura reale", ma a `alpha != 1` non lo e': `sp` avanza a un passo diverso da `P`, quindi l'epoch piu' vicino a iterazioni successive non e' in generale il successivo nel ring — il passo cosi' calcolato dipende dal passo precedente, un ciclo di retroazione che diverge geometricamente. Misurato PRIMA di procedere oltre (`psola_test`): Test 1 rotto su tutta la linea, -12 semitoni dava ~245Hz invece di 100Hz (quasi tornava all'originale), Test 6/8 (inviluppo) crollati a RMS minimo/medio = 0.000. Corretto sostituendo con `epochAfter()`, che legge la spaziatura fra due epoch ADIACENTI nel ring (indipendente da come `synthPos` si e' mosso in precedenza) invece di derivarla dalla cronologia della sintesi — nessuna retroazione, tutti gli 11 test tornano verdi.
- **Due tentativi di riprodurre il wobbling su segnale sintetico, entrambi negativi per misura, NON forzati a "passare"** (CLAUDE.md regola 13): un tono stazionario a periodo frazionario (Test 10) e lo stesso tono con un vibrato realistico ±15cent/5Hz (Test 11) non mostrano alcuna degradazione misurabile, ne' prima ne' dopo il fix. La causa, capita solo dopo aver strumentato il file reale (Fase 2): un generatore a singola risonanza produce un picco per periodo troppo netto e inequivocabile perche' l'individuazione degli epoch lo posizioni mai male — il meccanismo reale richiede un timbro armonicamente piu' ricco (come un corno vero) per manifestarsi. I due test sono stati mantenuti in ctest come verifiche di trasparenza permanenti (soglia onesta: "non degrada", non "riproduce il bug"), non come prova del meccanismo — quella e' venuta dalla strumentazione diretta del file reale, non da un segnale sintetico.

Rischi e nodi noti da tenere d'occhio, già identificati nel PRD e non ancora affrontati:
- **Qualità del PSOLA proprietario**: rischio piu' alto secondo il PRD. **Aggiornamento sessione 9**: PSOLA e' ora INTEGRATO come motore di default, verificato numericamente (7 test verdi, incluso un test di sovrapposizione che ha scoperto e permesso di correggere un bug reale nell'algoritmo sorgente) e verificato in build reale (`pluginval` verde, latenza Fast misurata a 13.5ms, sotto il target PRD). Resta comunque solo su segnale sintetico (onda a impulsi + risonanza singola), non su registrazioni reali di sax/tromba/voce ne' provato all'ascolto dentro il nostro plugin. Il rischio "suona bene dal vivo" resta aperto finche' l'utente non lo prova in Ableton. Se anche cosi' non dovesse reggere, resta l'opzione ZTX PRO di Zynaptiq (costo/trattativa commerciale).
- **Tipo di plugin AU** deve essere Music Effect (`aumf`) fin da M0: è una decisione strutturale irreversibile dopo il rilascio (PRD §4.1).
- **FR-17 / FR-46**: implementate in sessione 7, **validate all'ascolto in sessione 10**. FR-17 (live-update su nota tenuta) confermata funzionante cosi' com'era. La risoluzione della tensione fra le due (cosa succede a una frase superata da un nuovo onset) e' risultata sbagliata cosi' com'era (bug di accumulo, vedi sopra) ed e' stata sostituita da un bottone utente ("Keep Tails") invece di un comportamento fisso — vedi `Phrase.h` e novita' sessione 10 in §2.
- **Sviluppatore singolo alle prime armi con C++** su un progetto di ~50 settimane — mitigato nel piano con milestone brevi e CI dal giorno uno.

---

## 6. Quale sarebbe il prossimo passo

**Sessione 22 — badge di evidenziazione dei primi 5 preset: CONFERMATO dall'utente su Standalone**
**e VST3 in Ableton (vedi §2), dopo aver ricompilato anche la build Release** (la Debug da sola
non bastava — vedi la nota su Ableton/percorso di scansione in §2). Lavoro committato.

Prossimi slice di M5 ancora da iniziare dopo questo (lista invariata da sessione 21): griglia
cromatica dei 12 pulsanti per la fondamentale (oggi ComboBox), gain/pan per voce (assenti
dall'UI), indicatore stato licenza, scaffold delle 3 schermate PRD, tema chiaro/scuro (FR-61,
`[SHOULD]`).

**Sessione 21 — CONFERMATO dall'utente sul VST3 reale (vedi §2): il drag&drop funziona,**
**CC aggiornato live durante il riordino.** Lavoro committato. Non ancora testato con un
controller MIDI hardware reale (solo graficamente) — punto aperto per quando l'utente avra'
accesso all'hardware, non un blocco.

Non ancora provato esplicitamente (non bloccante, da tenere d'occhio se emergono problemi):
con piu' di 7 preset (dopo qualche Add) la lista deve scorrere correttamente; ridimensionare
la finestra (ora minimo 1292px di altezza, prima 1170) non deve rompere il layout.

**Prossimi slice di M5 gia' annotati, non ancora iniziati**: evidenziazione dei primi 5 preset
(le direzioni del navigation button hardware), griglia cromatica dei 12 pulsanti per la
fondamentale (oggi ComboBox), gain/pan per voce (assenti dall'UI), indicatore stato licenza,
scaffold delle 3 schermate PRD, tema chiaro/scuro (FR-61, `[SHOULD]`). Nessuna `LookAndFeel`
custom esiste ancora nel progetto: forma dei knob/colori/font sono tutti lavoro non iniziato,
esplicitamente rimandato (l'utente ha chiesto conferma di questo a fine sessione 21).

**Sessione 20 — CONFERMATO all'ascolto e per calcolo (vedi §2): il fix del wobbling funziona.**
L'utente ha fornito `Export V3.wav` (stesse impostazioni di `Export V2.wav`) e confermato "ora
va molto meglio". Confronto numerico diretto sul nuovo export reale: 8/792 finestre instabili
(1.0%) contro 35/792 (4.4%) prima del fix, residuo ormai concentrato su transizioni/attacco di
nota, non piu' dentro le note sostenute. Lavoro committato (`4087d69`, pushato).

Il "secondo meccanismo" annotato in sessione 17 (jitter fine anche in unisono, cali 0.89-0.93
senza coinvolgere il gate dell'onset): l'utente ha riportato a inizio sessione 21 di non
sentire "cose troppo fastidiose per il momento" — un segnale informale, non una verifica
numerica dedicata (nessun nuovo confronto `real_export_probe` fatto apposta). Se in futuro
dovesse riemergere un residuo di instabilita' fine, ripartire da qui con una misura diretta
prima di aprire una nuova indagine da zero.

Se dovesse emergere un residuo non spiegato dal secondo meccanismo, i candidati non ancora
toccati restano:
- La forma/ampiezza della finestra di Hann del grano (`emitGrain`, il calcolo di `Lg`/`W`) —
  non toccata in sessione 20.
- La normalizzazione d'inviluppo in `processChunk` (`out[i] = outBuf[idx] / max(e, 1.0f)`,
  asimmetrica: si divide solo sopra 1.0) — ipotesi W-B del piano di sessione 20, non esplorata
  perche' W-A (la deriva di `synthPos`) si e' confermata sufficiente per calcolo. Se il residuo
  persiste, e' il prossimo candidato naturale.
- Costruire un segnale sintetico che riproduca il fenomeno originale (spaziatura epoch anomala
  su materiale non impulsivo) resta utile per un test permanente in ctest indipendente dal file
  reale — sessione 20 ha scoperto che serve un generatore PIU' RICCO armonicamente di una
  singola risonanza (Test 10/11 non ci sono riusciti, vedi §2): due o piu' risonanze comparabili
  in ampiezza, o un impulso non ideale, sono i candidati piu' promettenti — non ancora tentati.

**Strumenti pronti per la prossima sessione** (nessuno ritirato): `tests/real_export_probe.cpp`
(sezione 4b "cadenza dei disturbi", modalita' `--trace <inizio> <fine>` per ispezione fine),
`tests/sample_click_finder.cpp` (`dumpPitchTrace` per la traiettoria grezza di PitchDetector a
piu' block size, `runFixedF0`/`writeWavMono` per isolare Voice/PsolaShifter con f0 costante e
rianalizzare l'uscita con `real_export_probe`), `tests/psola_test.cpp` Test 10/11 (trasparenza
in unisono nel tempo, permanenti in ctest — non riproducono il meccanismo ma proteggono da
regressioni). La diagnostica `PSOLA_DEBUG_SYNTH` usata in sessione 20 per instrumentare
`synthesise()` grano per grano e' stata rimossa dopo l'uso (schema: aggiungerla dietro
`#ifdef`, attivarla con un `target_compile_definitions` temporaneo su `sample_click_finder` in
`CMakeLists.txt`, rimuovere entrambi prima di chiudere la sessione — vedi sessioni 19/20).

**Sessione 19 — SUPERATO da sessione 20 (vedi sopra): i due tentativi su `detectEpochs`**
**restano correttamente ritirati (non erano la causa). Lasciato per il contesto storico:**
il wobbling e' confermato (due misure indipendenti: file reale + isolamento con f0 fissa)
**interno a `PsolaShifter`**, non a Voice/PitchDetector/PitchLatch/Glide — ma due tentativi
mirati su `detectEpochs` (peso sulla ricerca del picco, poi interpolazione sub-campione) sono
stati misurati e RITIRATI perche' inefficaci sul file reale. La causa vera (sessione 20) era
nella sintesi, non nella selezione degli epoch — coerente con questi due tentativi risultati
inefficaci.

**Sessione 18 — CONFERMATO all'ascolto (vedi sopra), lasciato per il contesto storico:**
il fix del gate dell'onset (`OnsetDetector.cpp`, release_threshold -36dB->-45dB, vedi §2) e'
verificato per calcolo su due percorsi indipendenti (export reale + riproduzione offline
senza DAW): il buco a t≈5.95s in `Export V1.wav` (periodicita' crollata a 0.000 mentre la
nota E4 stava ancora decadendo naturalmente nel dry) e' sparito completamente dopo il fix in
entrambi i confronti. Build/test/pluginval tutti verdi. **L'utente deve fare un NUOVO export
identico** (stesse impostazioni di `Export V1.wav`: Voices=1, Dry=0, Wet=1, preset Maj, root
C, Stability Balanced, sullo stesso file "Test 1 - Basic Silk Horns.wav") e confermare
all'ascolto se il calo/buco a meta' delle note lunghe e' sparito — CLAUDE.md regola 12 vieta
di dichiararlo risolto solo per calcolo. Se l'utente fornisce il nuovo export, rieseguire
`real_export_probe` sul dry originale e sul nuovo export per un confronto numerico
diretto (non solo un ascolto) prima di chiudere il punto.

**Prossimi due interventi gia' concordati con l'utente per quando si riprende ("entrambi,
buco prima" — il buco e' stato affrontato in questa sessione, restano gli altri due):**
1. **D1 — periodo intero in `PsolaShifter::currentPeriod()`** (`P=lround(sr/f0)`): verificato
   che il meccanismo esiste per costruzione, ma misurato sull'export reale con supporto
   MISTO (3 tratti su 5 lo confermano, 2 no, scarti piccoli 1-4 cent — non la causa
   principale del sintomo, ma un piccolo contributo plausibile). Intervento pianificato: far
   diventare `currentPeriod()` un `double` invece di un `int` (clampato a `minPeriod`/
   `maxPeriod`), propagare la parte frazionaria in `synthesise()`/`detectEpochs()`. `Lg`/`W`
   restano interi (lunghezze di grano, non fase). Verificare con un test dedicato in
   `psola_test.cpp` a periodo non intero (es. SR=44100, f0=261.63 C4) PRIMA di scrivere il
   fix — deve fallire con la tolleranza stretta (3 cent) e passare dopo, stesso schema di
   Test 8/9. Ri-misurare con `real_export_probe` dopo per vedere se i cali di periodicita'
   piccoli (vedi punto 2) migliorano.
2. **Jitter fine anche in unisono** (periodicita' 0.89-0.93 dentro note sostenute ad alto
   livello, es. t≈0.77s in "Silk Horns", MAI il gate coinvolto — livello ben sopra qualunque
   soglia): non ancora diagnosticato oltre "probabilmente il motore PSOLA su segnale non
   impulsivo" — i due candidati gia' annotati dalla sessione 12 restano aperti (posizionamento
   degli epoch come massimo di `|x|`, corretto per segnali impulsivi ma non necessariamente
   per fiati/voce reali; correlazione fra le 8 istanze PSOLA — quest'ultimo pero' non si
   applica a V1 da sola, quindi probabilmente non e' la causa qui). Da riprendere DOPO D1: se
   D1 riduce questi cali, il meccanismo e' (anche) il periodo quantizzato; se restano
   identici, la causa e' altrove e va cercata nel posizionamento degli epoch.

**Sessione 16 — SUPERATO da sessione 17 all'ascolto (vedi sopra): il click a inizio nota era**
**"migliorato, non risolto" — la ripartenza da zero di sessione 17-18 ha trovato la causa**
**vera (gate dell'onset), non ancora questo testo. Lasciato per il contesto storico:**
il fix del click a inizio nota (`Voice::justReactivated`, vedi §2) e' verificato per calcolo —
`voice_test.cpp` misura ~4 cent di scostamento dal nuovo target dopo il fix, contro ~195 cent
prima — build/test/pluginval tutti verdi, ma CLAUDE.md regola 12 vieta di dichiararlo risolto
senza ascolto. **L'utente deve riascoltare nella stessa configurazione di riferimento di
questa sessione** (ASIO Focusrite, buffer 1024, Stability Balanced, preset Maj, Dry 0/Wet
alto, 4 voci, la stessa clip audio legata dello screenshot) e confermare se il click a inizio
nota e' sparito. Se persiste, la misura di questa sessione ha comunque escluso per calcolo due
classi di causa (discontinuita' di ampiezza/H1, dipendenza dal block size/H4) — i prossimi
candidati sono il posizionamento degli epoch su segnali non impulsivi e la correlazione fra le
8 istanze PSOLA indipendenti (gia' annotati in sessione 12 per il timbro "robotico", mai
esclusi come concorrenti anche per questo sintomo — vedi sotto). Se invece e' sparito, resta
comunque da riverificare che la Fase 4 (sotto-blocchi per il wobbling, mai iniziata, vedi sotto)
non lo faccia ricomparire per un motivo diverso, dato che tocchera' lo stesso file.

**Sessione 15 — DA VERIFICARE VISIVAMENTE (ancora aperta, seconda priorita'):**
l'editor tabella preset 12x8 (M5, §8.2) e' scritto, compila, passa `pluginval`, e i dati
mostrati corrispondono al calcolo atteso (verificato per calcolo su uno screenshot, vedi
§2) — ma un tentativo di catturare uno screenshot completo della Standalone per verificare
visivamente il layout e' risultato inconcludente (probabile limite dello strumento di
cattura, `PrintWindow` su rendering accelerato JUCE, non un bug — vedi §2 per il
ragionamento e il precedente diretto di sessione 10). **L'utente deve aprire il plugin
(Standalone o Ableton) e controllare a occhio**: tutte e 12 le colonne (R, b2, 2, b3, 3, 4,
b5, 5, b6, 6, b7, 7) e le 8 righe (V1-V8) visibili senza sovrapposizioni o tagli, editare
una cella a mano (provare sia un numero che una cella vuota) e sentire che l'armonizzazione
dal vivo la rispetti, verificare che "0" e cella vuota si comportino in modo diverso (0 =
voce all'unisono, vuota = voce muta), ed esportare/reimportare un preset editato a mano
(round-trip CSV). Se il layout risultasse davvero tagliato (non solo nello screenshot), il
problema piu' probabile e' la larghezza minima della finestra (attuale minimo 500px) non
sufficiente su schermi piccoli — soluzione naturale sarebbe un `Viewport` con scroll,
esplicitamente rimandato in sessione 15 (vedi piano) perche' non necessario a griglia fissa
96 celle, da rivalutare se la conferma visiva lo richiede.

**Prossimi slice di M5, non ancora iniziati (per quando questo sara' confermato):**
drag&drop del riordino preset (FR-06, oggi Up/Down), scaffold delle 3 schermate del PRD
(Main/Editor/Impostazioni — oggi tutto in un pannello piatto), griglia cromatica dei 12
pulsanti per la fondamentale (oggi ComboBox), gain/pan per voce (assenti dall'UI), CC
mostrato accanto al nome nella lista preset (§8.2, mitiga il rischio "CC posizionale
confonde"), indicatore stato licenza (placeholder, la logica vera e' M6), evidenziazione
primi 5 preset, scaling 70-200% (FR-59), tema chiaro/scuro (FR-61, `[SHOULD]`).

**Sessione 14 — SMENTITO ALL'ASCOLTO, RIPRESO in sessione 16 (vedi §2): causa reale trovata**
**(`Voice::justReactivated`, offsetGlide che non si agganciava al nuovo target), ora in attesa**
**di conferma all'ascolto — vedi la voce "Sessione 16" in cima a questa sezione.** Testo
originale della nota, per il contesto storico:
il fix di `Voice::setMuted` (reset dello shifter alla riattivazione) e' stato verificato
numericamente (Test 9: scostamento 0.214 senza reset, 0.0 con reset — la carry-over di
stato PSOLA fra una nota e la successiva sullo stesso slot fisico E' un bug reale, dimo-
strato per calcolo) ma **l'utente riporta che il click "a inizio nota" persiste
IDENTICO all'ascolto** dopo il fix. Conclusione, non ancora verificata: il meccanismo
diagnosticato in sessione 14 e' un bug reale ma **non e', o non e' l'unica, causa del
click udibile** — CLAUDE.md regola 13 (una correzione che non risolve il sintomo non
significa che la diagnosi fosse sbagliata in senso assoluto, ma che va trattata come
"causa insufficiente", non "causa confermata all'ascolto"). **Su richiesta esplicita
dell'utente ("lasciamo in stand by questo punto"), NON approfondito oltre in questa
sessione.** Il fix di sessione 14 RESTA nel codice (non e' dannoso, verificato pulito da
regressioni: `pluginval`/build/4 suite verdi) ma non va considerato la soluzione.

Quando si riprende il punto, ripartire da zero sulle ipotesi, non dare per assodato che il
meccanismo di sessione 14 sia irrilevante — potrebbe essere una causa CONCORRENTE che da
sola non basta a spiegare tutto il sintomo residuo. Candidati non ancora esplorati:
- Lo swap di Stability senza dissolvenza (`VoicePool::applyPendingStabilityChangeIfSafe` →
  `Voice::swapShifterNoAlloc`, trovato in sessione 13, mai corretto) — ma l'utente non ha
  riportato di aver toccato il controllo Stability durante l'ascolto, quindi e' un
  candidato debole per QUESTO specifico test, a meno che lo swap scatti per altri motivi
  non ovvi.
- Qualcosa non ancora identificato nel percorso di ONSET/trigger stesso (non nel motore
  PSOLA): es. il primo blocco processato da una voce appena smutata potrebbe avere un
  transiente indipendente dallo stato del PSOLA (l'ampiezza parte da 0 correttamente via
  `ampGlide`, ma forse non e' l'ampiezza il problema — potrebbe essere il contenuto stesso
  del primissimo grano sintetizzato, indipendentemente da quanto "pulito" sia lo stato
  interno).
- Non ancora chiesto esplicitamente all'utente: il click e' udibile SOLO sulle voci
  armonizzate o anche sul segnale dry? Se anche sul dry, la causa non puo' essere nel
  motore PSOLA per definizione (il dry non lo attraversa) — andrebbe cercata altrove
  (dry/wet glide, gate di rilevamento onset, o a monte nel segnale stesso).

**Sessione 13 — DA CONFERMARE ALL'ASCOLTO, ancora aperto:**
fix dei click residui (vedi §2 per la diagnosi completa: il block size di Ableton, 4096
campioni con MME/DirectX, rendeva no-op la dissolvenza di sessione 12). L'utente deve
riascoltare **nella stessa identica configurazione audio** (stesso screenshot: 4096
campioni, driver MME/DirectX) e confermare se "qualche click ogni tanto" e' sparito. Se
persiste, i quattro candidati "fuori scope" elencati in §2 (swap di Stability senza
dissolvenza, in particolare) sono il punto da cui ripartire — quello e' oggi il salto di
ampiezza piu' probabile rimasto non coperto.

**Il "wobbeling" NON e' stato toccato in questa sessione**, solo diagnosticato (§2): la
causa e' la frequenza di controllo dell'intero motore, pari al reciproco del block size
dell'host (~10.8 Hz a 4096 campioni/44.1kHz) — non un bug isolato in un singolo file, ma
una conseguenza architetturale di come `processBlock` chiama `Voice::processAdd` una
volta per blocco. Il fix previsto (Fase 4, non ancora iniziata): un ciclo a sotto-blocchi
di dimensione fissa (64-128 campioni) dentro `PluginProcessor::processBlock`, sullo stesso
principio gia' usato con successo dentro `PsolaShifter` (`kInternalChunk`, sessione 9).
**Da decidere esplicitamente con l'utente prima di iniziare**: e' un intervento
strutturale su `processBlock`, in particolare come trattare i messaggi MIDI (oggi
consumati una volta per blocco da `ccRouter`/`playModeInput`) dentro il ciclo a
sotto-blocchi. Misurare prima di implementare (nuovo test in `psola_test.cpp` con
parametri aggiornati a intervalli di 4096 campioni, per confermare che la deviazione di
pitch cresca con l'intervallo di aggiornamento, prima di scrivere il fix vero).

**Sessione 13 — suggerimento per l'utente, non un blocco al lavoro**: la configurazione
audio del test (buffer 4096, MME/DirectX) e' il caso peggiore possibile su Windows —
amplifica ogni difetto di controllo-per-blocco di circa 32x rispetto a un ASIO a 128
campioni. Vale la pena provare anche con ASIO4ALL o FlexASIO a 128-256 campioni nei
prossimi test, per separare "bug del plugin" (da correggere qui) da "artefatto di una
configurazione audio non ottimale per il palco" (comunque rilevante: il PRD punta a
latenza ≤15ms nella modalita' piu' rapida, irraggiungibile con un buffer da 92.9ms).

**CONFERMATO in sessione 11 all'ascolto:** il canto legato (C→D→E) armonizza correttamente ogni nota — isteresi PitchLatch + `signalPresent` separato da `inputIsStable`.

**Sessione 12 — CONFERMATO all'ascolto:** fix della corsa onset/pitch — l'utente ha verificato in Ableton che "Active" non resta piu' a zero sul primo attacco, armonizza sempre tutte le note, nessuna persa per strada. Feedback esplicito ma non dettagliato voce per voce: non e' stato confermato singolarmente ne' il contatore "late-bindings" (se sale davvero) ne' il caso specifico "nota armonizzata sull'accordo della nota precedente" (causa 2 della diagnosi, `pitchDetector` stantio) — nessun segnale che sia ancora un problema, solo non verificato in modo esplicito e separato. Se in futuro dovesse ricomparire un caso limite (es. attacchi molto ravvicinati, staccato molto rapido), ripartire da li'.

**Sessione 12 (continuazione) — DA CONFERMARE ALL'ASCOLTO (priorita' immediata della prossima sessione):** fix del deficit di sovrapposizione dei grani in `PsolaShifter::emitGrain` quando la correzione formantica di default e' attiva (vedi §2). Bug reale confermato per calcolo e con un test che falliva prima del fix — ma non e' garanzia che risolva l'intero feedback "robotico e granuloso": l'utente deve riascoltare in Ableton, in particolare su voci shiftate parecchio verso il basso (ottava o piu'), e confermare se il suono e' piu' pulito o se resta un problema.

Se dopo l'ascolto il problema persiste (in tutto o in parte), i due candidati seguenti restano da esplorare, in ordine di probabilita':
1. Individuazione degli epoch come massimo di `|x|` in una finestra ±P/4 (`detectEpochs` in `PsolaShifter.cpp`): su segnali non impulsivi (synth, fiati, voce) puo' posizionare male gli epoch, incoerenza di fase fra grani percepita come "robotico".
2. Otto istanze PSOLA indipendenti sullo stesso ingresso: artefatti correlati che si sommano invece di mediarsi tra le voci.

**Sessione 12 (continuazione) — RISULTATO DELL'ASCOLTO IN ABLETON, PRIORITA' IMMEDIATA della prossima sessione:** l'utente ha confermato un miglioramento reale ("va meglio") rispetto a prima del fix di dissolvenza, ma con due RESIDUI riportati testualmente, non ancora indagati (fermati qui su richiesta esplicita dell'utente — "per il momento fermiamoci qua... le prendiamo in considerazione in un secondo momento"):
1. **"Continuo a sentire qualche click ogni tanto"** — meno frequenti di prima (il fix ha chiaramente eliminato la classe di click piu' grande, quella dei tagli di ampiezza netti), ma non azzerati. Nessuna diagnosi fatta in questa sessione. Prime ipotesi DA VERIFICARE, non da dare per buone (regola 12 — servono numeri/lettura di codice prima di agire):
   - `kDeclickMs = 8.0f` potrebbe non bastare in tutti i casi, o il vero residuo potrebbe essere un meccanismo NON ancora coperto dal fix di sessione 12 (che copre: fine frase/silenzio, cella svuotata su nota tenuta, uscita Play mode, dry/wet/bypass) — es. lo swap di Stability (`VoicePool::swapShifterNoAlloc`, sessione 6): quando cambia, l'intero `PitchShifter` viene sostituito con uno NUOVO (buffer azzerati, zero storia) senza alcuna dissolvenza — mai toccato ne' in questa ne' in sessioni precedenti, e "sempre applicato in standalone" (nessun gate di transport fermo li'). Se l'utente cambia Stability mentre suona, o se lo swap scatta per un motivo non ovvio, questo resta un salto secco non coperto dal fix attuale.
   - Il furto d'emergenza (FR-52, `hardFreePhrase`) resta deliberatamente istantaneo per design (vedi sopra) — se il pool di voci si esaurisce spesso nell'uso reale dell'utente (es. `maxSimultaneousVoices` alto con molte note rapide), questo potrebbe essere una fonte residua non coperta.
2. **"Un po' di wobbeling nelle voci"** — sintomo NUOVO, non riportato prima di questa sessione, non indagato. Prime ipotesi DA VERIFICARE:
   - `Glide::process()` aggiorna l'offset UNA VOLTA PER BLOCCO, non per campione: fra un blocco e l'altro il target si muove a scalini (quanti scalini dipende dal block size dell'host) invece che in modo continuo — possibile causa di un "ondeggiamento" percepito, mai investigata finora.
   - In modalita' Fix (FR-22), `semitonesToApply` si ricalcola OGNI blocco da `continuousInputMidiNote` (la stima di pitch grezza, non filtrata) — se quella stima ha jitter blocco-per-blocco (rumore naturale del rilevatore), si traduce DIRETTAMENTE in micro-variazioni di `alpha` applicate al motore PSOLA, potenzialmente udibili come un tremolio di intonazione. Non verificato se le voci del test dell'utente fossero in Fix o Move.
   - La correzione formantica automatica (FR-39, `beta` in `Voice::processAdd`) non e' glideata direttamente (segue `semitonesToApply` che a sua volta e' derivato dall'offset glideato, ma in Fix mode `semitonesToApply` include anche `continuousInputMidiNote` grezzo, non glideato) — stesso possibile canale di micro-variazione.
   - Nessuna di queste e' verificata: sono ipotesi di partenza per non ripartire da zero, non diagnosi.
3. **Timbro robotico/granuloso** (fix di sessione 12 sull'inviluppo dei grani): l'utente non ha commentato specificamente se questo aspetto e' migliorato — probabilmente da riverificare esplicitamente, dato che il feedback di questo giro si e' concentrato su click/wobbling.

**Prossimi passi possibili — da ridiscutere con l'utente:**
- **Click residui + wobbling delle voci** (vedi sopra): priorita' segnalata dall'utente per la prossima sessione, ma nessuna decisione ancora presa su quale investigare per primo — chiedere.
- Ascolti ancora non fatti: controllo MIDI CC con hardware/automazioni reali (FR-36/37), modalita' Play (setup PRD §3.4, verificare FR-27/28).
- **Pattern ritmico (M3, `[V1.1]`)**: griglia piano-roll o modalita' millisecondi per il timing di entrata delle voci — fuori scope v1.0 per il PRD, ma l'architettura di `PhraseScheduler` e' gia' pronta ad accoglierlo.
- **Preset timbrici (FR-11..13)**: sistema di preset separato da quello armonico, non ancora iniziato — conterrebbe anche Formant Spread/offset per voce una volta esistente (oggi sono solo parametri APVTS piatti, come Stability/Dry/Glide).

**Limiti noti dopo questa sessione (da non scambiare per requisiti soddisfatti):**
- **`f0 <= 0` senza fade dedicato**: oggi `PhraseScheduler` smette semplicemente di processare le voci quando il segnale non e' stabile (`freeAllPhrases()`) — comportamento pre-esistente, non peggiorato ne' risolto da questa sessione.
- **Nessun crossfade esplicito sul cambio di Stability** dentro il motore stesso: la transizione si appoggia interamente al `Glide` gia' presente in `Voice` (sessione 6) — da verificare all'ascolto che basti.
- **Validato solo su segnale sintetico** (onda a impulsi + risonanza singola) — nessuna registrazione reale di sax/tromba/voce ancora provata, ne' in `tests/`, ne' all'ascolto.
- **Finestra di Hann ricalcolata per campione** in `emitGrain` (chiamata a `std::cos`): nota ottimizzazione non fatta, rilevante se la profilazione CPU con 8 voci (mai eseguita) rivelasse problemi rispetto al budget ≤15% del PRD §1.3.
- **`SpectralShifter` non piu' usato ma ancora compilato**: se in futuro si rimuove per pulizia, verificare prima che nessuno faccia piu' riferimento a `HARMONIZER_USE_SPECTRAL_SHIFTER`.
- **Formanti (FR-39..42) implementate ma non tarate**: costante `k=0.3` presa cosi' com'era dalla spec sorgente, mai all'ascolto. Nessun test numerico scritto per le Formanti in questa sessione (a differenza del motore PSOLA) — la correzione formantica e' per natura una preferenza timbrica soggettiva, non ha un criterio "giusto/sbagliato" oggettivo come l'accuratezza di trasposizione.
- **Controllo MIDI CC (FR-29..38) implementato ma mai provato con hardware/host reale**: la logica di precedenza e' testata numericamente (`override_manager_test`), ma il parsing dei messaggi MIDI veri (`CcRouter`) no — nessun target di test dedicato (avrebbe richiesto linkare `juce_audio_basics`), verificato solo a lettura e con `pluginval`. UI: il selettore root/preset non riflette visivamente un override CC attivo (l'audio segue comunque correttamente il CC).
- **Modalita' Play (FR-24..28) implementata ma mai provata con hardware/host reale**: stesso limite del controllo CC — nessun test dedicato (coinvolge `juce::MidiBuffer`/`juce::MidiMessage`), verificata solo a lettura e con `pluginval`. Nessuna regola di furto definita per oltre 8 note simultanee (il PRD non la specifica per Play, a differenza di FR-51/52 per Harmonizer): le eccedenti restano semplicemente mute. FR-28 (nessun click nel passaggio di modalita') non verificato all'ascolto.
- **"Keep Tails" (sessione 10) e' binario oggi**: tronca subito o lascia vivere per sempre. Il significato pieno descritto dall'utente (tronca solo la coda non ancora suonata di una frase, lascia finire il resto) richiede il Pattern Ritmico (FR-47..49, `[V1.1]`), non costruito qui — vedi `Phrase.h`.
- **Mancato riconoscimento pitch con alcuni synth / primo attacco a "0 voci" (sessione 10, punti 2/5 del test)**: causa confermata e corretta in sessione 12 (corsa `OnsetDetector`/`PitchDetector`, vedi §2) — **CONFERMATO risolto all'ascolto** dall'utente.
- **Isteresi di intonazione + `signalPresent` (sessione 11): CONFERMATI all'ascolto** — canto legato C→D→E armonizza correttamente ogni nota. Soglia ±25 cent e soglie del gate (-24/-30/-36 dB, ereditate dal rilevamento onset di sessione 7) restano comunque punti da tarare ulteriormente se emergono altri casi limite (es. un portamento su un intervallo molto ampio, o un legato molto piano/debole).
- **Note saltate senza logica precisa (sessione 12)**: causa confermata a lettura di codice (corsa onset/pitch + `pitchDetector` mai resettato), fix scritto, verificato con build/test/pluginval e **CONFERMATO all'ascolto** — nessuna nota persa, "Active" mai a zero al primo attacco. Non verificato singolarmente il caso "accordo della nota precedente" (causa 2) ne' il contatore "late-bindings": nessun segnale che sia un problema, solo non isolato esplicitamente.
- **Qualità del suono — timbro robotico/granuloso (sessione 12)**: un bug reale trovato e corretto in `PsolaShifter::emitGrain` (deficit di sovrapposizione dei grani quando la correzione formantica di default e' attiva), verificato per calcolo e con un test (Test 8) che falliva prima del fix — **NON ancora confermato all'ascolto**. Restano due candidati non esplorati se il problema persiste in tutto o in parte: posizionamento degli epoch su segnali non impulsivi, correlazione fra le 8 istanze PSOLA.
- **Click/scricchiolii + armonizzazione "non stabile al 100%" (sessione 12)**: bug architetturale confermato a lettura di codice — nessuna voce aveva mai una dissolvenza di ampiezza quando smetteva di essere processata (fine frase, silenzio, cella tornata vuota su nota tenuta/FR-17, uscita da Play mode); stesso problema su dry/wet/bypass. Fix scritto (dissolvenza fissa 8ms, rilascio morbido delle frasi con `Phrase::releasing`), verificato con build/test/pluginval. **PARZIALMENTE confermato all'ascolto**: l'utente riporta un miglioramento reale, ma con due residui non ancora indagati — click occasionali ancora presenti, e un "wobbeling" nelle voci (sintomo nuovo).
- **Click residui (sessione 13)**: causa confermata per calcolo (non solo lettura di codice) — la dissolvenza di sessione 12 era un no-op completo con il block size reale dell'utente (4096 campioni, MME/DirectX in Ableton), perche' `Glide::process()` ritorna un solo valore per l'intero blocco. Fix scritto (`Glide::processRamp`, guadagno campione-per-campione in `Voice::processAdd` e nel mix dry/wet), verificato con un test dedicato (`glide_test`, 15/15 verifiche verdi, falliva a non compilare prima del fix) e con build/pluginval. **NON ancora confermato all'ascolto** — vedi §6. Se persiste, il candidato piu' probabile e' lo swap di Stability senza dissolvenza (`VoicePool::applyPendingStabilityChangeIfSafe` → `Voice::swapShifterNoAlloc`), trovato durante l'indagine ma non ancora corretto (fuori scope di sessione 13, da discutere).
- **Wobbeling delle voci (sessione 12 → diagnosticato in sessione 13, non ancora corretto)**: causa confermata per calcolo — la frequenza di controllo dell'intero motore (pitch, formanti, f0) e' pari al reciproco del block size dell'host: ~10.8 Hz con 4096 campioni a 44.1kHz. Nessuna interpolazione fra un aggiornamento e il successivo in nessun punto della catena (`continuousInputMidiNote` letta una volta per blocco, `PsolaShifter::currentPeriod()` un intero arrotondato, `synthPos` un accumulatore di fase che cambia passo bruscamente). Fix previsto ma non scritto: ciclo a sotto-blocchi dentro `processBlock` (Fase 4, vedi §6) — intervento strutturale, da discutere con l'utente prima di iniziare, in particolare come trattare i messaggi MIDI nel ciclo a sotto-blocchi.

**A seguire, per chiudere M0 davvero (non urgente per continuare lo sviluppo):**
- Avviare le pratiche per i certificati di firma/notarizzazione (Apple Developer ID, code signing Windows).
- Decidere la licenza JUCE (Indie vs commerciale, in base al fatturato previsto).

**Questioni ancora aperte:**
- Nome prodotto/azienda non deciso: `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt` restano placeholder.
- Solo un preset (Min) e' verificato contro il prototipo reale — gli altri 6 sono standard jazz generici, da correggere quando l'utente fornira' i dati veri (ora importabili via CSV).
- Risoluzione FR-17/FR-46: validata all'ascolto e corretta in sessione 10 (bottone Keep Tails) — vedi sopra.
- Valori della tabella Stability->minF0Hz (sessione 9) e della costante `k=0.3` delle Formanti (sessione 9) sono ancora un punto di partenza, non tarati all'ascolto.
- Causa del mancato riconoscimento pitch con alcuni synth (punti 2/5 del test di sessione 10): determinata in sessione 12 (corsa onset/pitch), fix scritto — resta la conferma all'ascolto, vedi sopra.
- **Push**: verificato a inizio sessione 12 — `git status`/`git log origin/main..HEAD` puliti, tutti i commit fino a sessione 11 gia' sul remoto (la nota precedente su 5 commit "solo locali" era obsoleta). I commit di sessione 12 non sono ancora stati creati: richiedere conferma esplicita all'utente prima di committare/pushare, come da istruzioni di sistema (mai commit non richiesti).
