# Consegna della beta — foglio operativo

Questo file è **per te, non per i tester**. Loro ricevono `BETA-Windows.md` o
`BETA-macOS.md`, che la CI mette già dentro lo zip come `LEGGIMI.md`.

---

## Prima del primo invio, una volta sola

- [ ] **Verifica che il repository sia privato.**
      `github.com/Jazy1997/STARTUP-Software-Harmonizer` → *Settings* → in fondo,
      *Danger Zone*: deve offrire **"Change visibility → Make public"**. Se invece
      offre *"Make private"*, il codice sorgente è pubblico adesso e va sistemato
      prima di qualunque altra cosa. I repository privati hanno comunque 2000 minuti
      di CI gratis al mese.
- [ ] Decidi se ti basta `COMPANY_NAME "Giacomo Cazzaro"` (è ciò che i tester leggono
      come produttore nel browser dell'host). Si cambia con una parola in
      `CMakeLists.txt` quando avrai un nome commerciale, **senza rompere i progetti
      dei tester** — l'identità del plugin non dipende dal nome (D-24).

## Per ogni tester

1. **GitHub → Actions → "Build & validate" → Run workflow.** Compila:
   - `beta` = **true**
   - `beta_days` = `30`
   - `tester` = nome e cognome della persona
2. A build finita, scarica dalla pagina del run gli zip che ti servono:
   - `Harmonizer-Windows-beta-Nome-Cognome`
   - `Harmonizer-macOS-beta-Nome-Cognome`
3. **Manda un link privato per persona** (Drive, Dropbox), non un link unico girato a
   tutti: se una copia esce, il link si revoca e il nome dentro il plugin dice da dove
   è passata.

**Una build per tester, col suo nome.** Costa un click e rende tracciabile ogni copia.

### Perché usare gli zip della CI e non la tua build locale
Gli artefatti della CI nascono da un albero pulito. La tua cartella `build/` contiene
ancora il residuo `Harmonizer (1).vst3` dentro il bundle (s.31) e i `.pdb` in `Debug/`,
che non vanno spediti: i `.pdb` contengono nomi di funzioni e i percorsi dei tuoi file.

---

## Testo dell'email

> Ciao [nome],
>
> ti mando in anteprima Harmonizer, il plugin di armonizzazione per strumenti
> monofonici a cui sto lavorando. Non è un prodotto finito: è una versione di lavoro,
> e ti scrivo proprio perché mi fido delle tue orecchie.
>
> Nel link trovi lo zip con il plugin e un file `LEGGIMI.md` con l'installazione —
> cinque minuti. Se sei su Mac c'è un passaggio in più, è spiegato lì.
>
> Due cose in chiaro:
>
> - **La copia scade fra 30 giorni.** Dopo, il plugin continua ad aprirsi e il tuo
>   segnale asciutto continua a passare: si zittiscono solo le voci armonizzate,
>   quindi non perdi lavoro e i progetti non si rompono. Se ti serve più tempo,
>   scrivimi e te ne mando una nuova.
> - **Ti chiedo di non passarla ad altri.** Questa copia è intestata a te — il tuo
>   nome è dentro il plugin. Non è diffidenza, è che il lavoro non è ancora protetto
>   e sto ancora decidendo come distribuirlo.
>
> Quello che mi serve non è una relazione: **una nota vocale mentre suoni vale più di
> una pagina scritta.** In fondo al `LEGGIMI` ci sono i cinque punti su cui ho più
> dubbi. E se qualcosa suona male ma non sai dire perché, dimmelo comunque così —
> "qui suona male" mi basta, al perché ci penso io.
>
> Grazie davvero,
> Giacomo

---

## Cosa NON è coperto, e va detto se qualcuno chiede

- **Non c'è nessuna licenza né attivazione** (`src/licensing/` è vuota, M6 non esiste).
  La scadenza è un semplice controllo della data: chi sposta indietro l'orologio di
  sistema riottiene il wet. È un deterrente fra persone che si conoscono, non una
  protezione — vedi D-26.
- **Non è notarizzato da Apple** (A-04 rimandato per decisione, D-25). Da qui il
  passaggio della quarantena nella guida macOS.
- **AU e macOS non sono mai stati provati in un host vero.** I due tester su Logic
  sono la prima verifica in assoluto, ed è una scelta consapevole.
- **La fetta x86_64 non è mai stata eseguita da nessuno.** La CI compila universal ma
  gira su runner arm64: un tester su Mac Intel è la prima esecuzione di quel codice.
