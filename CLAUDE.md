# CLAUDE.md — HARMONIZER

Regole non negoziabili per chi (umano o agente) scrive codice in questo repository.
Fonte di verità completa: `PRD-Harmonizer-v1.md`. In caso di dubbio o di necessità di
discostarsi da un requisito, **va segnalato e discusso, non deciso unilateralmente**.

1. **Non violare mai le regole di threading di §9.4.** Prima di ogni riga aggiunta in
   `processBlock`: non alloca, non prende lock, non fa I/O di file o rete, non lancia
   eccezioni, non chiama il `LicenseManager` al di fuori della lettura di un flag atomico.
2. **`PitchShifter` è un'interfaccia astratta.** Nessun modulo fuori da `src/dsp/` può
   conoscere l'implementazione concreta (PSOLA o Signalsmith).
3. **`0` e cella vuota sono cose diverse** nella tabella dei preset armonici. Serializzare
   `null` per la cella vuota, mai `0`. Confonderli rompe la semantica delle voci mute.
4. **Il valore CC di un preset è la sua posizione in lista.** Non introdurre mai mappature
   alternative o ID stabili al posto della posizione per la selezione via CC.
5. **La libreria di preset si serializza dentro lo stato del plugin** (sessione host).
   Non salvare solo un riferimento a file esterni.
6. **Un ID di parametro pubblicato non cambia mai.** Aggiungere, mai rinominare o rimuovere.
7. **Il tipo AU è Music Effect (`aumf`).** Non cambiarlo per nessun motivo dopo M0: è una
   decisione irreversibile per i progetti già salvati dagli utenti.
8. **Prima di dichiarare completa una feature**, eseguire
   `pluginval --strictness-level 10` su tutti i formati e riportare l'esito.
9. **Nessuna dipendenza esterna senza approvazione esplicita.** Ogni licenza ha implicazioni
   sul prodotto commerciale. GPL e LGPL statico sono esclusi a priori.
10. **Commit atomici che citano l'ID del requisito:**
    `feat(harmony): lookup tabella preset — FR-16`.
11. **Il target standalone deve restare sempre funzionante.** È lo strumento principale di
    iterazione sul DSP, va tenuto vivo ad ogni milestone.
12. **Non puoi ascoltare.** Non hai modo di sapere se qualcosa "suona bene". Se un requisito è
    formulato in termini percettivi, traducilo in una misura numerica e aggiungi un test
    (vedi `tests/psola_test.cpp`), oppure segnala esplicitamente che serve una verifica
    all'ascolto da parte dell'utente. Non dichiarare mai completo un lavoro sul suono
    basandoti sul fatto che il codice compila.
13. **Quando un test fallisce, considera anche l'ipotesi che sia sbagliato il test**, non
    l'algoritmo — prima di riscrivere il codice, verifica che la misura stia davvero misurando
    ciò che pensi. Vale anche il contrario: se una correzione fa fallire un test che prima
    passava, non è detto che sia il test ad essere sbagliato — verifica cosa è cambiato
    davvero nell'uscita prima di allentare una soglia.

---

## Note di stato (aggiornare ad ogni milestone)

- Milestone corrente: **M1 — Detection e shifting** (M0 tecnicamente completo; PSOLA proprietario
  integrato come motore di default dietro `PitchShifter`, verificato da `tests/psola_test.cpp`).
- Formati target: VST3, AU (Music Effect), Standalone — tutti `[MUST]`.
- `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` in `CMakeLists.txt` sono **placeholder**
  (nome prodotto/azienda non ancora deciso, vedi PRD §16). Vanno confermati prima della
  beta pubblica: cambiarli dopo il rilascio rompe la compatibilità degli host con i
  progetti già salvati, esattamente come per il tipo AU.
- Vedi `handsoff.md` per lo stato dettagliato sessione per sessione.
