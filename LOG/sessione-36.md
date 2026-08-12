# Sessione 36 — 2026-08-12

Sessione corta e di sola infrastruttura. **A-06 chiuso**: la CI ora esegue `ctest` e copre
**11 suite su 11** su Windows e macOS, contro le 3 di prima. Nessuna riga di `src/` toccata.

Prima di arrivarci, però, una domanda dell'utente ha rimesso in discussione il prossimo passo,
e la risposta è la parte più utile di questa sessione (§1).

---

## 1. «Meglio fare prima il Delay o questo?»

L'utente ha osservato che **tutta la parte di Delay non è implementata, nemmeno a livello UI**,
e ha chiesto se non convenisse farla prima di A-06.

La risposta sta nel PRD, ed è netta: **il Delay non è in ritardo, è fuori da v1.0 per
specifica.**

- Sono **FR-47..FR-50**, tutti marcati `[V1.1]`, che il PRD §2 definisce *"architettura
  predisposta in v1.0, implementazione successiva"*.
- La §12 lo ripete fra i post-v1.0: *"editor del pattern ritmico"*.
- Il deliverable di **M3** è scritto letteralmente **"`PhraseScheduler` (senza editor)"**.
- L'unico obbligo v1.0 della §6 — *"il `PhraseScheduler` e il `VoicePool` vanno costruiti da
  subito"* — è **già assolto**.

Quindi a v1.0 non manca nulla; A-06 invece era debito v1.0 vero, già registrato fra le
decisioni aperte.

### Perché anticiparlo sarebbe costato caro

`Phrase.h` lo documenta da sé: senza i ritardi *"tutte le voci di una frase entrano insieme al
trigger — non c'è mai una coda di voci ancora in attesa"*. Introdurre il Delay **crea quella
coda per la prima volta**, e sveglia in blocco quattro cose che oggi dormono:

1. **una voce che entra in ritardo ha il motore freddo** — è la famiglia B-12/B-15/B-16/B-17 su
   una quarta strada, cioè esattamente ciò che è costato le sessioni 34 e 35;
2. **B-11** (tetto voci) oggi *"non morde (32 su 32)"* solo perché le frasi non si accavallano:
   col pattern, la §6.3 conta fino a 128 shifter e il furto FR-52 diventa caldo;
3. **Keep Tails** acquisterebbe per la prima volta la semantica vera descritta in `Phrase.h`, e
   B-10 su quella configurazione è già in attesa di conferma all'ascolto;
4. **A-05**: uno slider 0–2000 ms su un'infrastruttura che decide una volta per blocco sarebbe
   quantizzato fino a ~85 ms a 4096 — su un controllo dedicato alla dislocazione temporale è
   visibile, non trascurabile. E A-05 va deciso **con l'utente** (i MIDI, oggi consumati una
   volta per blocco) e misurato prima di scrivere.

Il Delay non è un task: è mezza milestone, e poggia su un prerequisito non pianificato.

**Deciso dall'utente**: si procede con A-06, il Delay resta `[V1.1]`. Nessuno scostamento dal
PRD, quindi nessun `D-NN` di scope — se un domani lo si volesse in v1.0, *quella* sarebbe la
decisione da mettere a verbale.

---

## 2. Il divario misurato

| | Prima | Dopo |
|---|---|---|
| Suite nel gate | 3 (`psola`, `override_manager`, `pitch_latch`) | **11** |
| Piattaforme sui test | solo ubuntu, `g++` nudo | **Windows + macOS** |
| Invocazione di `ctest` | mai | ad ogni push |

Fuori dal gate stavano `glide`, `cell_input_parser`, `voice`, `empty_cell_hold`,
`phrase_scheduler`, `play_mode_input`, `ui_scale`, `mode_switch` — fra cui **i due banchi che in
s.34/35 hanno chiuso B-15, B-16 e B-17**, e `voice`/`psola`, il cuore del motore.

Il divario si allargava da solo: per D-16 ogni banco nuovo tende a linkare JUCE, e
`mode_switch_test` linka il **plugin intero**, quindi con un `g++` a mano non è proprio
compilabile. `CMakeLists.txt:302` lo annotava già: *"gira in ctest, non nel gate a g++ nudo
della CI (A-06)"*.

---

## 3. Verifica locale, prima di toccare il workflow

Ordine deliberato: sapere **se le 8 suite mai girate in CI passano davvero**, prima di chiedere
alla CI di eseguirle.

```
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

La build esce con codice 1 per D-12 — `MSB3073` sulla copia in `C:\Program Files\Common
Files\VST3`, nessun `error C####` né `error LNK`: la compilazione è andata, fallisce solo la
copia. I 17 eseguibili in `build/Release/` erano tutti presenti e aggiornati.

**Esito: 11/11 passate, 3.19 s in tutto.** Nessuna sorpresa fra le 8 mai coperte — inclusi
`ui_scale` e `mode_switch`, i due che toccano JUCE lato GUI.

Questo ha liquidato due dei tre rischi previsti nel piano: i tempi (3.2 s contro minuti di
build) e l'headless (girano). Resta il terzo, verificabile solo sul primo push: se
`COPY_PLUGIN_AFTER_BUILD` facesse fallire il passo *Build* sul runner Windows, `ctest` non
verrebbe mai raggiunto. La CI oggi è verde con lo stesso comando, quindi il runner è
presumibilmente elevato — ma è un'assunzione, non una misura.

---

## 4. Cosa è cambiato in `build.yml`

**Un passo `ctest`** nel job `build`, fra *Build* e *Download pluginval*. Costo di build
**zero**: il passo *Build* gira già senza `--target`, quindi quegli eseguibili venivano
compilati e buttati via senza essere eseguiti.

**Il job `dsp-tests` resta**, non convertito. Il suo valore non erano i test, era il **tempo**:
senza JUCE fallisce in secondi invece che dopo la build completa su due piattaforme.
Convertirlo a CMake vorrebbe dire installare X11/freetype/ALSA e uccidere proprio quel
vantaggio. È stato però **ridichiarato per quello che è** — una corsia veloce su un
sottoinsieme — con il divieto esplicito, nel commento, di estenderlo aggiungendo altri `g++`:
è così che 8 suite su 11 sono finite fuori.

**Una guardia**, nel job veloce. La causa di A-06 non era una CI sbagliata: era che un target
nuovo restava fuori **da solo, in silenzio**. Il passo confronta l'insieme dei target
`add_executable(<nome>_test` con quello registrato da `add_test(` e fallisce nominando il
colpevole.

Provata **in entrambe le direzioni** prima di committare — una guardia mai vista fallire non è
una guardia:

- sugli 11 attuali: `OK: 11 target *_test, tutti registrati in ctest.`
- su una copia di `CMakeLists.txt` privata della riga `add_test(NAME mode_switch`: scatta e
  stampa `< mode_switch_test`.

I probe (`*_probe`, `sample_click_finder`, `voice_bench`) restano esclusi per costruzione dal
suffisso `_test`: vogliono file WAV non versionati.

Messo a verbale come **D-23**; A-06 chiuso nella tabella delle decisioni aperte.

---

## 5. Nota di metodo

Vale la pena isolarla, perché non riguarda la CI.

Il difetto strutturale di A-06 non era il contenuto della CI ma la sua **forma**: un elenco
scritto a mano, che per stare aggiornato dipendeva dal fatto che qualcuno si ricordasse di
aggiornarlo. Ha retto tre suite e ha ceduto silenziosamente sull'ottava. Sostituirlo con `ctest`
non aggiunge soltanto otto suite: toglie di mezzo l'elenco, e con esso l'unico modo che aveva di
sbagliarsi.

La guardia è la stessa idea applicata al livello sopra — perché anche `add_test()` è un elenco
scritto a mano.

---

## 6. Stato a fine sessione

- **11 suite su 11** nel gate di CI, su due piattaforme. `ctest` verde in locale, 3.19 s.
- Nessun sintomo aperto in `BUGS.md`; nessun ascolto in coda.
- Nessuna modifica a `src/`: `pluginval` non è richiesto da questo lavoro e resta nel workflow
  a valle di `ctest`.
- Da confermare sul primo push: che il passo *Build* non inciampi nella copia su Windows (§3).
