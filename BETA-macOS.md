# Harmonizer — beta, installazione su macOS

Grazie per provarlo. Sono cinque minuti. C'è **un passaggio in più rispetto a un plugin
comprato**, il punto 3: non saltarlo, o il plugin non si carica. Sotto spiego perché.

---

## Installare

1. **Scompatta lo zip.** Dentro trovi due cartelle:
   - `Harmonizer.vst3` → per **Ableton**, Reaper, Cubase, Bitwig
   - `Harmonizer.component` → per **Logic** e GarageBand

   È giusto che siano cartelle e non file: i plugin su macOS sono fatti così.

2. **Copia ciascuna nella sua destinazione.** Nel Finder premi `⌘⇧G` e incolla il
   percorso:

   | Cosa | Dove |
   |---|---|
   | `Harmonizer.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
   | `Harmonizer.component` | `~/Library/Audio/Plug-Ins/Components/` |

   Puoi installarne anche solo una, quella che ti serve.

3. **Il passaggio in più.** Apri **Terminale** (`⌘Spazio`, scrivi "Terminale"), incolla
   questa riga tutta intera e premi Invio:

   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Harmonizer.vst3 ~/Library/Audio/Plug-Ins/Components/Harmonizer.component
   ```

   Non chiede password e non stampa niente: se non dice nulla, ha funzionato.

   **Perché serve:** macOS marca tutto ciò che scarichi e blocca i plugin che non sono
   passati dalla procedura di certificazione a pagamento di Apple. Non l'ho ancora
   avviata — sono a uno stadio troppo iniziale per pagarla, e la certificazione non
   cambierebbe di una virgola il codice che stai per sentire. Quella riga rimuove la
   marca **solo su questi due file.**

   *(Se hai installato solo uno dei due, cancella dalla riga il percorso dell'altro:
   il comando si lamenta di quello che non trova, ma fa il suo lavoro sull'altro.)*

4. **Riavvia il tuo DAW.**

## Se non lo vedi

**In Logic** — è il caso più probabile, e non è colpa tua:
`Logic Pro → Impostazioni → Plug-in Manager`, cerca `Harmonizer`, selezionalo e premi
**Reset & Rescan Selection**. Se resta segnato come non valido, dimmelo: sei la prima
persona che apre questo plugin in Logic, ed è una delle cose che ho più bisogno di sapere.

**In Ableton** — `Preferenze → Plug-in`, attiva le cartelle VST3 di sistema e premi
**Rescan**. Lo trovi sotto *Giacomo Cazzaro*.

**Se macOS dice che "lo sviluppatore non può essere verificato":** è esattamente la cosa
che il punto 3 risolve. Torna là e ricontrolla di aver incollato la riga intera.

---

## Tre cose da sapere prima di iniziare

**Non c'è la versione standalone,** ed è voluto: mi interessa sentirlo dentro il tuo
modo di lavorare.

**Scade dopo 30 giorni.** In alto a destra nel plugin vedi il conto alla rovescia.
Alla scadenza **il plugin continua ad aprirsi e il tuo segnale asciutto continua a
passare**: si zittiscono solo le voci armonizzate. I tuoi progetti non si rompono e non
perdi lavoro — ritrovi la traccia col suono originale. Se ti serve più tempo, chiedi e
ti manda una build nuova.

**Non redistribuirlo,** per favore. È una versione di lavoro e il tuo nome è dentro
questa copia.

---

## Cosa mi serve sentire

Non servono relazioni scritte bene. Una nota vocale mentre suoni vale più di un
paragrafo. I punti dove ho più dubbi:

1. **L'armonia entra in ritardo?** Quando cambi nota, le voci ti seguono subito o
   arrivano un attimo dopo? È la cosa che voglio sapere di più.

2. **L'attacco delle note è al posto giusto,** o sembra spostato rispetto a quando hai
   suonato? Se sì, dimmi anche a che dimensione di buffer stai lavorando.

3. **Il timbro delle voci regge?** Cercano di suonare come lo strumento vero o diventano
   vetrose, artificiali, "digitali"? Su quali intervalli peggiora?

4. **Il passaggio fra le due modalità** (Harmonizer e Play) fa quello che ti aspetti,
   anche a nota tenuta?

5. **Dove ti aspettavi un comando e non l'hai trovato.** L'interfaccia non ha ancora
   nessun manuale: se qualcosa non si capisce, quello è un difetto mio, non tuo.

6. **Se sei su un Mac Intel, dimmelo esplicitamente.** Questa è la prima volta in
   assoluto che il plugin gira su un processore Intel: se qualcosa scoppia, è
   un'informazione che vale oro.

Se una cosa suona male e non sai dire perché, **dimmelo comunque così**: "qui suona male"
è un'informazione utilissima. Ci penso io a capire il perché.

Se puoi, mandami anche il **file audio** dove lo senti — anche solo otto battute.
Vale dieci volte una descrizione.
