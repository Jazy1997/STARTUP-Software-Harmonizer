# Sessione 34 — Il click all'attacco in modalità Play (B-15)

Data: 2026-08-11. Continua da s.33 (FR-59, scala UI).

---

## 1. Il punto di partenza

L'utente, provando la modalità Play:

> *"Dobbiamo sistemare la modalità play. Quando clicco una nota sulla tastiera midi il plug in
> la armonizza correttamente, ma sento un click all'inizio. Controlliamo che ci siano tutte le
> precauzioni necessarie affinché non ci siano click iniziali."*

Su domanda, ha precisato: **ogni volta che premo il tasto** — non solo alla prima nota dopo un
silenzio. Quella precisazione ha cambiato la diagnosi, perché escludeva subito l'ipotesi più
comoda (lo slot mai alimentato dall'avvio, B-06).

Il perimetro è stato deciso prima di scrivere qualunque riga, ed è stato scelto dall'utente:
**solo `PlayModeInput`**, e se la misura avesse mostrato che serviva toccare `Voice` (chiusa da
D-19), fermarsi e riportare i numeri. Compromesso accettato in anticipo: l'attacco percepito
può arrivare ~8 ms più tardi, come già accettato per B-12 in s.30.

---

## 2. Cosa c'era già, e perché non bastava

La rampa anti-click esiste ed è sana: `Voice::ampGlide`, 8 ms (`kDeclickMs`), applicata
campione per campione (`Voice.cpp`, fix B-03/s.13 — non è più il no-op a buffer lunghi).
Esistono `justReactivated` (B-01/s.16), la dissolvenza al note-off, la rampa dry/wet.

Il problema non era l'assenza della rampa: era **quando cade**. E in Play mancavano tutte e
tre le precauzioni che la catena Harmonizer aveva guadagnato fra s.27 e s.30:

- nessun riscaldamento del motore al note-on (`Phrase::warmupSamples`/B-12 non era mai stato
  portato qui);
- `goCold()` **mai chiamato** in Play (grep su tutti i call site di `playModeInput.`: zero);
- allocazione degli slot che riprendeva sempre il primo indice libero, cioè sistematicamente
  quello appena rilasciato e ancora in dissolvenza.

---

## 3. Fase 0 — il banco di misura, prima di ogni fix

`tests/play_mode_input_test.cpp`, nuovo target in `ctest`. Pilota il **vero** `PlayModeInput`
con veri `juce::MidiBuffer`: è l'unico livello a cui esistono note-on/note-off e allocazione
degli slot, cioè il perimetro del difetto. Linka `juce_audio_basics`, quindi sta nel secondo
livello di D-16 come `phrase_scheduler_test`.

Sorgente sintetica stazionaria a 220 Hz (`makeVowel` da `TestSignals.h`), `inputIsStable`
sempre vero: l'unica variabile è il tasto premuto. Controllo negativo obbligatorio prima di
tutto (modalità spenta e modalità accesa senza tasti devono dare RMS esattamente 0).

### Due correzioni alla misura stessa, prima di fidarsene

**PM-3, la finestra.** Ancorata al primo suono udibile e larga 20 ms, mancava la giunzione fra
contenuto vecchio e segnale vero ai livelli di Stability lenti — dove il difetto è più grosso.
Riancorata al note-on e allargata a 60 ms (la latenza dichiarata arriva a ~30 ms ad Accurate).

**PM-2 è stata scartata come cancello, ed è la lezione metodologica della sessione.** Era la
metrica ovvia: con il tempo di salita 10→90% fu chiuso B-12. Ma qui misurava male —
l'inviluppo RMS dell'uscita PSOLA è grumoso alla cadenza dei grani, e la stessa "prima nota"
dava 4.9–10.1 ms a MIDI 64 e 2.2 ms a MIDI 52. Cambiava con la profondità dello shift, non
con la presenza del difetto (CLAUDE.md regola 13). Il cancello è passato su **PM-3**, il salto
di ampiezza discontinuo — che è la definizione di click usata in questo progetto da s.12.

**PM-4 ha cambiato metrica in corsa** per lo stesso motivo: `measureF0` (autocorrelazione) su
una finestra a cavallo di una transizione di intonazione sbaglia ottava e restituisce numeri
che non corrispondono a nessuna delle due note in gioco (192–194 Hz, dove le note erano 164.8
e 392.0). La domanda vera non era "quale f0" ma "quale delle due note note sta suonando", e a
quella si risponde confrontando direttamente l'energia armonica alle due frequenze, senza
stimare nulla.

### Il quadro pre-fix

| | Fast | Fast+ | Balanced | Accurate− | Accurate |
|---|---|---|---|---|---|
| PM-3 prima nota | 2.12 | 2.09 | 2.07 | 2.08 | 2.08 |
| PM-3 2ª nota, altra altezza | 3.30 | 4.96 | 4.18 | 5.08 | 5.04 |
| PM-1 2ª nota (ms) | 1.09 | 1.36 | 0.70 | 1.47 | 1.41 |
| PM-4 2ª nota (Hz letti) | 402.9 | 395.8 | 399.6 | 191.0 | **164.7** |

Il **PM-1 di ~1 ms** sulla seconda nota è il dato che spiega tutto: il motore produceva suono
*subito*, prima ancora di aver ricevuto un campione della nota nuova. Quel suono era la coda
della precedente. E ad Accurate PM-4 lo conferma: 164.7 Hz contro i 164.81 della nota
rilasciata, dove ne erano richiesti 392.0.

---

## 4. Fase 1a — riscaldamento e raffreddamento (e la sorpresa)

In `PlayModeInput`: `warmupSamples` + `engineIsCold` per slot. Slot fermo e silenzioso →
`goCold()` una volta sola. Slot con nota premuta e motore freddo → `processWarmOnly` per la
latenza dichiarata, a voce muta, prima di far partire la dissolvenza. Il conto è **derivato**
dallo stato del motore, non registrato al note-on, così vale anche quando la nota resta
premuta ma l'ingresso perde stabilità (FR-20). I tre rami "non deve farsi sentire" (modalità
spenta / nessuna nota / ingresso instabile), prima copiati identici, sono diventati uno.

Risultato: **prima nota risolta** (PM-3 2.07–2.12 → 1.00–1.05). **Seconda nota no**
(3.30–5.08 → 2.32–5.11, praticamente invariata).

Era il caso che l'utente sente.

---

## 5. PM-7 — l'esperimento che ha separato le due cause

Ipotesi: durante il riscaldamento il motore gira ancora con il rapporto di trasposizione della
nota **precedente**, perché `processWarmOnly` alimenta il motore ma non gli passa il nuovo
shift (arriva solo al primo `processAdd`); e `PsolaShifter::synthesise()` riempie `outBuf`
**in anticipo** fino a `absWrite - maxPeriod`, quindi al momento della dissolvenza c'erano già
~10 ms sintetizzati all'intonazione sbagliata, con la giunzione a guadagno pieno.

Prova costruita per poterla **smentire**, non per confermarla: ripetere lo scenario con la
seconda nota **alla stessa altezza** della prima. Stesso rapporto ⇒ niente intonazione
sbagliata da sintetizzare ⇒ se l'ipotesi è giusta, PM-3 deve essere pulito.

| PM-3, 2ª nota stessa altezza | Fast | Fast+ | Balanced | Accurate− | Accurate |
|---|---|---|---|---|---|
| prima di qualunque fix | 0.98 | 1.03 | 1.00 | 1.00 | 1.00 |
| dopo | 0.98 | 0.98 | 0.96 | 0.99 | 1.00 |

Pulito, e **pulito già prima**. Non è un caso corretto dal fix: è il controllo che isola il
cambio di rapporto come causa unica. Il click della seconda nota non veniva dal ring affamato,
veniva dal rapporto.

Qui il lavoro si è **fermato** e i numeri sono stati riportati all'utente, come previsto: la
correzione richiedeva `Voice`. L'utente ha autorizzato la modifica minima.

---

## 6. Fase 1b — la modifica minima a `Voice`, e la sua forma

Tre vincoli, poi scritti come **D-21**:

1. Il percorso esistente non cambia: `processWarmOnly` a 4 argomenti per `PlayModeInput`, la
   versione a 3 argomenti resta identica per `PhraseScheduler`.
2. Il codice condiviso per **estrazione pura**: `runShifter` estratto dal corpo di
   `processAdd`, stessa matematica, stesse righe.
3. L'identità si **dimostra**: uscite di `voice`, `psola`, `phrase_scheduler` salvate prima di
   cominciare, confrontate dopo. Bit-identiche.

Il punto 3 ha coperto un dettaglio che il solo ragionamento avrebbe lasciato in dubbio:
l'estrazione ha spostato `ampGlide.processRamp` **prima** dell'aggancio di `justReactivated`,
che nell'originale veniva dopo. I due stati sono disgiunti e l'ordine non cambia un campione —
ma è stato verificato, non argomentato.

Manca un pezzo, e non è ovvio: **`setMuted(false)` va chiamato all'inizio del riscaldamento**,
non alla fine. È lui ad armare `justReactivated`, ed è `justReactivated` ad agganciare
l'intonazione al bersaglio invece di farcela scivolare sopra in `glideTimeMs`. Chiamandolo
alla fine, il motore passerebbe tutto il riscaldamento a sintetizzare in anticipo
un'intonazione che scivola — cioè lo stesso difetto con un'altra faccia. Non fa entrare la
voce in anticipo perché `ampGlide` avanza solo dentro `processAdd`.

---

## 7. Fase 2 — l'allocazione degli slot

Al note-off lo slot torna libero subito ma la voce impiega 8 ms a spegnersi, e
`std::find(..., -1)` restituiva **lo stesso** slot ancora in dissolvenza. Lì `justReactivated`
non si arma (si arma solo su una voce `isSilent()`) e l'intonazione scivolava dalla nota
vecchia alla nuova in `glideTimeMs`. Ora fra gli slot liberi si preferisce uno già `isSilent()`,
con fallback sul primo libero se non ce ne sono.

Misurato (PM-6, nota ribattuta un blocco dopo il rilascio), scostamento dal bersaglio:

| dal ri-attacco | prima | dopo |
|---|---|---|
| +10 ms | — | **+0.2 cent** |
| +30 ms | −1219.8 cent | **+2.3 cent** |
| +60 ms | +0.9 cent | +0.7 cent |

Non era il sintomo riportato — l'utente non ha segnalato il ribattuto — ma è un difetto
d'attacco reale, nella stessa funzione e nello stesso perimetro.

---

## 8. Il quadro finale

| PM-3 | Fast | Fast+ | Balanced | Accurate− | Accurate |
|---|---|---|---|---|---|
| prima nota | 1.02 | 1.01 | 0.94 | 0.99 | 0.99 |
| 2ª nota, stessa altezza | 0.98 | 0.98 | 0.96 | 0.99 | 1.00 |
| 2ª nota, altra altezza | 1.00 | 1.00 | 1.00 | 1.03 | 0.99 |

PM-2 sulla seconda nota: 2.15–5.24 ms → **7.71–8.59 ms**, cioè `kDeclickMs`. La dissolvenza
cade ora su segnale vero. PM-4: da −1502 cent (l'intonazione della nota precedente) a
+10.1…+10.6 dB a favore della nota richiesta.

`ctest` **10/10** (le 9 esistenti più `play_mode_input`), `voice`/`psola`/`phrase_scheduler`
bit-identiche alla baseline, `pluginval --strictness-level 10` **SUCCESS** su VST3/Windows,
Standalone costruito (regola 11). AU non verificabile su questa macchina — limite già noto.

**Niente di tutto questo chiude B-15.** Serve l'ascolto dell'utente (CLAUDE.md regola 12), e
finché non arriva l'entry resta `APERTO`.

---

## 9. Cosa è rimasto fuori, deliberatamente

- **Lo stesso miglioramento sui rami warm di `PhraseScheduler`** (celle vuote B-04,
  late-binding B-12): usano ancora la `processWarmOnly` a 3 argomenti e quindi scaldano il
  motore col rapporto vecchio. Va misurato sul percorso Harmonizer per conto proprio prima di
  toccarlo — la catena Harmonizer è oggi giudicata soddisfacente all'ascolto (D-19).
- **Split sample-accurate del `MidiBuffer`**: `metadata.samplePosition` non viene letto, il
  note-on vale dal campione 0 del blocco (fino a ~85 ms di anticipo a 4096). È A-05, e non
  produce click.
- **Nessun `ScopedNoDenormals`** in tutto `src/`: annotato durante l'esplorazione, non
  corretto qui. Non è il sintomo riportato.
- **8 note rilasciate e ripremute entro 8 ms**: nessuno slot è `isSilent()`, si ricade sulla
  scivolata di intonazione. Limite noto.
