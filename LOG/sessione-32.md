# Sessione 32 — 2026-08-11

> B-13 confermato all'ascolto e chiuso. La stessa conferma apre B-14: l'offset giusto arriva
> ~85 ms dopo l'attacco. Causa dominante trovata fuori dal nostro codice (la finestra
> d'analisi di Cycfi Q), ridotta a 44 ms rendendola una scelta dell'utente.
> **Manca la conferma all'ascolto di B-14.**

---

## B-13 chiuso

*"Test#3 e Test#2 ora suonano uguali. Il problema delle celle vuote e delle voci intermedie è
stato risolto."* La predizione scritta in s.31 **prima** dell'ascolto ha retto. Committato e
pushato in tre commit (`f11312a`, `edea576`, `82e2f4c`).

---

## Il sintomo nuovo, e dove finiscono gli 85 ms

*"Per una frazione di secondo, quando comincia a suonare la nota, suona l'offset della nota
precedente, poi salta a quello corretto."*

Misurato su `exp#1_Test#1_V1_01` contro la sua REF, transizione C4→D4:

| t | REF | plugin |
|---|---|---|
| 2.000 | sale a 283 Hz | 175 Hz |
| 2.010–2.070 | 277 Hz | **192 → 198 Hz** = nota nuova con l'offset vecchio |
| 2.090 | 276 Hz | 279 Hz |

**~85 ms.** Non un difetto nuovo: è il residuo che B-13 dichiarava di non risolvere.

`cycfi::q` ricava la finestra d'analisi dalla frequenza minima
(`_zc(hysteresis, lowest_freq.period() * 2 * sps)`) e produce una stima ogni mezza finestra;
sopra c'è un `median3` che su un cambio di nota chiede due frame concordi. Con i **60 Hz**
cablati fino a s.31: finestra **33.4 ms**, stima ogni **16.7 ms**, pitch nuovo **87 ms** dopo
l'attacco. Il resto era la nostra attesa fissa da 25 ms.

60 Hz è **B1** — sotto il Trombone (Ab1 = 51.9 Hz), che quindi nelle note gravi non veniva
rilevato affatto. Il parametro non serve solo a guadagnare velocità: chiude un buco.

`libs/q` è un submodule e non si patcha: ogni leva è configurazione o codice nostro.

---

## Tre interventi, nessuno sul motore

1. **`analysisLowestNote`**, parametro utente (D-18), presentato come scelta dello strumento:
   10 voci dal più acuto al più grave in Impostazioni. Serializza la **nota**, non la posizione
   in lista — l'opposto di D-03, e deliberato: la lista è ordinata per altezza, quindi uno
   strumento nuovo va inserito in mezzo.
2. **L'attesa di `PitchLatch` in frame d'analisi** invece che in millisecondi fissi
   (`settleSamplesForFrame`, 1.5 frame). A 60 Hz vale esattamente i 25 ms di s.31.
3. **L'aggancio si aggiorna a ogni stima nuova del rilevatore**, non una volta per blocco.

Il cambio di strumento riusa lo schema di Stability (costruzione sul message thread → scambio
di puntatore in `processBlock` → distruzione in `collectGarbage`) ma **senza** aspettare lo
stop del transport: la latenza dichiarata non cambia, FR-56 non c'entra.

---

## Il difetto che ho trovato nel mio fix di s.31

Misurando la matrice completa è saltata fuori una **regressione di B-13** al caso limite: a
nota minima C4 e blocco 1024, un offset di passaggio tornava.

Causa: l'attesa contava i campioni del **blocco**. Con un blocco più lungo dell'attesa, una
stima vista **una sola volta** si vedeva accreditare 23.2 ms e l'attesa diventava un no-op.
Nella traccia a 128 campioni si vede che il glitch è **una singola lettura** (61.771 dove il
vero è 63.984), correttamente rifiutata; a 1024 quella lettura capitava a fine blocco e veniva
adottata.

È il motivo dell'intervento 3. Ora il tempo passato all'attesa è quello vero fra due stime, e
il rifiuto non dipende più dal buffer. È anche il modo corretto di leggere Cycfi Q:
`getMidiNote()` vale nel momento in cui l'analisi si è appena conclusa, non a un istante
qualsiasi del blocco.

---

## La scoperta che ha cambiato il default

La prima matrice l'avevo fatta **solo su Test#1** e dava 0 ovunque. Estesa a tutte e quattro le
tabelle, a blocco 1024:

| nota minima | finestra | Test#1 | Test#2 | Test#3 | Maj |
|---|---|---|---|---|---|
| C4 | 8.7 ms | 0 | 0 | 2 | 2 |
| Ab3 | 10.2 ms | 0 | 0 | 2 | 3 |
| E3 | 13.1 ms | 0 | 0 | 0 | 2 |
| Db3 | 14.5 ms | 0 | 0 | 1 | 2 |
| Ab2 | 20.3 ms | 0 | 0 | 1 | 1 |
| **E2** | 24.7 ms | **0** | **0** | **0** | **0** |
| B1 (era) | 33.4 ms | 0 | 0 | 0 | 0 |

**Il compromesso ha due facce**: una finestra corta è più pronta ma anche più rumorosa, e su un
preset con tutti i gradi compilati ogni sbandata del rilevatore diventa un offset sbagliato.
Test#1 e Test#2 non lo vedono perché le loro celle intermedie sono vuote e `EmptyCellHold` le
assorbe — la stessa asimmetria che aveva mascherato B-13 in s.31.

**Nessuna taratura dell'attesa salva le finestre corte**: provato fino a 25 ms assoluti
(5.7 frame a C4), C4/Ab3/E3/Db3 restano con 1 corsa spuria. Non è l'attesa a essere breve, è
il rilevatore a sbagliare.

Sweep sull'attesa a nota Ab2 / E2, blocco 1024:

| attesa | Ab2: spurie / ritardo | E2: spurie / ritardo |
|---|---|---|
| 1.5 frame | 1 / 32.7 ms | **0 / 44.3 ms** |
| 2.5 frame | 0 / 55.9 ms | 0 / 55.9 ms |

E2 a 1.5 frame domina Ab2 a 2.5 su entrambi i fronti. L'utente aveva scelto Ab2 come default
prima di avere questo dato; messo davanti alla misura ha scelto **E2**, e di **tenere in lista
le voci acute con l'avvertenza**.

---

## Risultato

| buffer | prima (B1) | dopo (E2, default) |
|---|---|---|
| 128 | 93.6 ms | **55.9 ms** |
| 512 | 90.7 ms | **50.1 ms** |
| 1024 | 79.1 ms | **44.3 ms** |

Matrice di non-regressione B-13 al default: 4 tabelle × 6 block size (128→4096) = **24
configurazioni, 0 corse di passaggio**.

---

## Verifica

- `ctest -C Release`: **8/8**, `psola` e `voice` identiche (motore non toccato).
- `pluginval --strictness-level 10` su VST3: **SUCCESS**.
- **UI verificata a schermo** sullo standalone: le 10 voci compaiono nell'ordine giusto, il
  default è quello previsto, e selezionare Trombone scrive il parametro e sopravvive al sync
  del Timer (giro completo UI → parametro → UI).
- `pitch_latch_test` esteso con **TEST 12**: l'attesa a 60 Hz vale esattamente i 25 ms di
  s.31; un'attesa più corta non riapre B-13 (nessuna nota intermedia a qualunque attesa,
  zero inclusa).

---

## Due trappole pagate, da non ripagare

1. **La build completa non compila lo Standalone.** Si ferma al fallimento della copia in
   `Program Files` (atteso, D-12) e i target successivi non vengono mai toccati: ho
   screenshottato una UI vecchia di un'ora convinto che il selettore non ci fosse. Il target
   `Harmonizer_Standalone` va compilato **esplicitamente**.
2. **Solo ASCII nelle stringhe visibili.** Un trattino lungo nel testo di una `Label` è
   comparso a schermo come `â□□`: senza `/utf-8` MSVC rilegge i byte UTF-8 col codepage di
   sistema. I commenti non contano, le stringhe sì.

---

## Cosa resta

1. **La conferma all'ascolto di B-14** (regola 12). Riesportare `exp#1_Test#1_V1_02` e
   `exp#1_Maj_V1_02` col default Voice Male. La flem deve accorciarsi, e Test#2/Test#3 devono
   restare uguali fra loro.
2. **Le voci acute non sono verificate nel loro registro**: la tabella sopra viene da un file
   che suona C4-D4-E4. Serve un export di flauto o tromba nel proprio registro.
3. Il ritardo non va a zero: restano convergenza del rilevatore e confine di blocco. Azzerarlo
   richiede il **lookahead**, rimandato per scelta dell'utente.
