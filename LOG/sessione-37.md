# Sessione 37 — la beta agli artisti, e A-04 rimandato con criterio

> D-24, D-25, D-26. Nessun sintomo aperto in `BUGS.md`, nessuno chiuso.
> 13/13 suite verdi (erano 11): due banchi nuovi.

---

## 0. Come è cominciata, e perché è finita altrove

Domanda d'apertura: *"qual è il prossimo passo?"*. `HANDOFF.md:43` diceva **A-04**, firma e
notarizzazione: lead time più lungo del progetto, previsto per M0, mai avviato, cinque milestone
di ritardo. Ho proposto di iscriversi all'Apple Developer Program.

Due correzioni sono arrivate dall'utente, entrambe fondate:

1. **L'intento non era rilasciare, era far sentire il plugin a degli artisti** e riceverne
   feedback — dandolo in concessione, con l'idea di firmare come persona fisica e passare alla
   società in un secondo momento.
2. **€99/anno ricorrenti sono presto** per un prodotto senza nome né data di rilascio.

Entrambe hanno retto alla verifica, e il piano è cambiato di conseguenza. Vale la pena scriverlo
perché è il secondo caso (dopo il Delay in s.36) in cui la voce "prossimo passo" dell'HANDOFF
era formalmente corretta ma tarata su un obiettivo che non era quello dell'utente.

---

## 1. L'errore che ho fatto e che ha cambiato il piano

Ho detto all'utente che cambiare l'identità di firma dopo il rilascio è come cambiare
`PLUGIN_CODE`. **È falso**, e la differenza è esattamente ciò che rende praticabile una beta
adesso:

- il **certificato** non finisce nei progetti salvati dagli host;
- `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` + il tipo AU **sì**.

Verificato leggendo JUCE invece di andare a memoria:

```
libs/JUCE/modules/juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h:61
    return VST3Interface::jucePluginId (JucePlugin_ManufacturerCode,
                                        JucePlugin_PluginCode, interfaceType);
```

L'UID VST3 dipende **solo dai due codici a 4 lettere**. Il ramo che usa `JucePlugin_Name`
(`:58`) è dietro `JUCE_VST3_CAN_REPLACE_VST2`, che `CMakeLists.txt` tiene a `0`. Per l'AU,
`JUCEUtils.cmake:1612` → `JucePlugin_AUSubType = JucePlugin_PluginCode`.

**Conseguenza (D-24):** `Hzso`/`Hmz1`/`aumf` si possono congelare **oggi**, senza aver deciso il
nome commerciale, perché sono codici opachi che l'utente non vede mai. E `PRODUCT_NAME` /
`COMPANY_NAME` / `BUNDLE_ID` si possono rinominare **dopo** senza rompere un solo progetto dei
tester. Quindi **A-02 non blocca più la beta**: blocca solo il marchio.

Il commento in `CMakeLists.txt` che diceva *"placeholder … da confermare prima della beta
pubblica"* era la formulazione pericolosa: dal primo progetto salvato da un tester quei codici
sono pubblici di fatto. Riscritto in due gruppi con regole opposte.

Ed era anche l'ordine di priorità sbagliato: avevo mandato l'utente a comprare certificati
quando il rischio irreversibile stava altrove. **Firmare è un fastidio in meno per il tester;
non congelare i codici è un danno permanente ai suoi progetti.**

---

## 2. La verifica sul percorso "persona fisica adesso, società dopo"

La domanda dell'utente meritava una risposta verificata, non un'impressione:

- **Un account Apple Individual può emettere Developer ID e notarizzare.** Non serve una società.
- **La conversione Individual → Organization conserva Team ID, certificati e app**: cambia solo
  l'entità legale mostrata. È una migrazione, non un rifacimento — quindi firmare a nome proprio
  non è lavoro buttato.
- **Vincolo scoperto ora, che vale per il futuro:** Apple **non accetta ditte individuali né
  DBA** per gli account Organization. Quando si costituirà dovrà essere una società di capitali,
  o la conversione non sarà possibile.

Utile anche il rovescio: **non serve un Mac per notarizzare** — la CI gira già su runner macOS
(D-23) e `notarytool` potrebbe stare lì. Il limite macOS dell'HANDOFF riguarda l'ascolto e il
Retina, non la firma. Resta valido per quando A-04 si riaprirà.

---

## 3. Perché il gratis è reale (D-25)

- **Windows: nessun attrito.** Ableton carica un `.vst3` non firmato copiato in
  `Common Files\VST3` senza avvisi. SmartScreen gate gli `.exe` scaricati, non le cartelle di
  plugin.
- **macOS: firma ad-hoc** (`codesign -s -`, nessun account, €0). Su Apple Silicon serve *una*
  firma perché il codice giri, e ad-hoc basta. Resta la quarantena → una riga di Terminale a
  carico del tester.
- **Windows rimandato anche per una ragione di calendario:** dal 15 febbraio 2026 i certificati
  di code signing durano al massimo 1 anno. Comprarne uno ora ne brucerebbe la vita su una beta
  che non distribuisce un `.exe`.

**L'AU entra pur non essendo mai stato caricato in un host vero.** Due tester usano Logic;
l'utente ha risposto *"non mi interessa che non è stata testata, ci sono loro per questo"*.
Scelta consapevole, registrata, non discussa.

**Lo standalone non entra**, e la ragione è concreta: manca `MICROPHONE_PERMISSION_ENABLED`,
quindi da macOS 10.14 non riceve segnale **in silenzio**. Sembrerebbe rotto e genererebbe
segnalazioni false. Aggiunto in `CMakeLists.txt` (due righe, corrette per costruzione, **non
verificabili qui**), ma fuori dal pacchetto.

---

## 4. La scadenza (D-26) — il codice era già pronto

Cercavo dove innestare il silenziamento del wet e ho trovato che `processBlock` fa **già**
esattamente questa cosa per il Bypass:

```cpp
const float effectiveDryLevel = effective.bypassed ? 1.0f : dryLevel;
float effectiveWetLevel = effective.bypassed ? 0.0f : wetLevel;
...
wetGlide.setTarget (effectiveWetLevel);
const auto wetRamp = wetGlide.processRamp (numSamples);
```

`processRamp` interpola **campione per campione** sulla rampa anti-click di 8 ms
(`kMixDeclickMs`), la stessa provata da `glide_test`. Quindi l'intervento nel thread audio è
**una riga** — `effectiveWetLevel = 0.0f` — e non c'è nessuna dissolvenza da scrivere né rischio
di click da valutare. È il caso in cui il lavoro fatto in s.12/13 sui click paga da solo.

**La regola 1 non è stata sfiorata.** `std::time()` è una chiamata di sistema e in `processBlock`
è vietata; non serviva, perché `prepareToPlay` avvia già `startTimer(250)` e `timerCallback`
gira sul message thread per Stability. L'ora si legge lì, il risultato va in un
`std::atomic<bool>`, e `processBlock` fa solo una `load` — il pattern che `CLAUDE.md` regola 1
consente in modo esplicito per il `LicenseManager`.

**Il dry continua a passare.** Scade il wet, non il plugin: principio di FR-68 anticipato. Un
tester che riapre un progetto fra due mesi ritrova il suo segnale asciutto.

**OFF per default**, e non è un dettaglio: con ON le build di sviluppo dell'utente morirebbero
dopo 30 giorni — trappola, e violazione della regola 11 (lo standalone deve restare vivo).

### Due banchi, perché sono due difetti indipendenti

`beta_expiry_test` (20 controlli) copre **quando** scade: i confini a ±1 secondo, `days=0`, il
conto alla rovescia con arrotondamento per eccesso, e l'orologio spostato indietro. Le funzioni
sono `constexpr`, quindi le proprietà portanti sono anche `static_assert`: una regressione non
arriva nemmeno a produrre un eseguibile.

`beta_gate_audio_test` copre **cosa fa all'audio**, e senza di lui restava un buco vero:
un'aritmetica giusta collegata male spedirebbe agli artisti una versione illimitata senza che
nulla lo segnali — esattamente il rischio che la scadenza esiste per chiudere. Nessun altro
banco lo copre, perché tutti gli altri girano in configurazione non-beta. Quarto livello di
D-16 come `mode_switch_test`: istanzia il processore intero, con le macro forzate nel
`CMakeLists.txt` a "sempre scaduta" (epoch fisso del 2023 + `DAYS=0`), così la misura non
dipende né dalla configurazione né dal giorno in cui si esegue.

Misurato:

| | valore | |
|---|---|---|
| wet a regime, `dryWetMix=1.0` | **4.334e-09** | silenzio digitale |
| dry a regime, `dryWetMix=0.0` | **0.0992** | sorgente: **0.0992** |

Il residuo di 4.3e-9 **non è wet che sfugge**: con `dryWetMix=1.0` il crossfade a potenza
costante dà `dryLevel = cos(π/2)`, che in virgola singola è ~6e-8, non zero esatto; per l'RMS
della sorgente fa ~6e-9. Annotato nel banco, perché a un rilettore futuro sembrerebbe una
perdita. La soglia a 1e-6 sta due ordini sopra quel residuo e quattro sotto un wet vero.

### Il limite, scritto e non nascosto

Chiunque sposti indietro l'orologio riottiene il wet. E il caso "ora prima della data di build"
è trattato **deliberatamente come non scaduto**: bloccare un tester dall'orologio sbagliato
costa più del pirata ingenuo, e per un musicista spostare l'orologio rompe iLok e mezzo parco
plugin installato — non è una strada che percorre per comodità. Deterrente fra persone che si
conoscono, non DRM.

---

## 5. La CI adesso consegna qualcosa

Prima di questa sessione `build.yml` compilava su Windows **e macOS** e **buttava via tutto**:
nessun `upload-artifact`. È il motivo per cui l'utente non aveva modo di ottenere una build
macOS senza possedere un Mac, mentre il runner macOS produce già un universal binary.

Aggiunti, **senza toccare** `dsp-tests`, `ctest` né `pluginval`:

- staging dei bundle in `pkg/` (con `-type d` nella `find`: su Windows `Harmonizer.vst3` è sia
  la cartella del bundle sia la DLL che sta dentro, e impacchettare la DLL nuda darebbe un
  plugin che nessun host carica; `ditto` su macOS, non `cp`);
- **firma ad-hoc** dei due bundle su macOS, con `codesign -dv` a stampare l'esito nel log;
- la guida d'installazione **dentro lo zip** come `LEGGIMI.md`, quella giusta per piattaforma;
- `upload-artifact` con `retention-days: 30` — tanto quanto dura una beta;
- un `workflow_dispatch` con tre input (`beta`, `beta_days`, `tester`) che è il pulsante per
  confezionare il pacchetto di una persona. Gli input passano da `env`, non interpolati nello
  script: un nome con virgolette non deve poter diventare codice shell.

Su push normale gli input non esistono, `EXTRA` resta vuoto e il comando di configure è identico
a quello di prima.

Effetto collaterale utile: gli artefatti della CI nascono da un albero pulito, quindi **non
contengono il residuo `Harmonizer (1).vst3`** che vive dentro il bundle della build locale
(s.31), né i `.pdb` di `Debug/` — che non vanno spediti, perché contengono nomi di funzioni e i
percorsi dei file dell'utente. Conviene spedire quelli, non la cartella `build` di casa.

---

## 6. Le guide, e cosa chiedono

`BETA-Windows.md` e `BETA-macOS.md`, separate di proposito: si manda quella giusta e il tester
non deve capire quale metà lo riguarda. Scritte per musicisti — percorsi da incollare, nessun
gergo, e per ogni intoppo una riga "se non lo vedi, fai questo".

Il punto che decide se un tester dà feedback o si arrende è **la riga della quarantena su
macOS**, con la spiegazione del perché (non ho pagato Apple, e la certificazione non cambierebbe
il codice che stai per sentire). Per Logic è previsto in anticipo il *Reset & Rescan* nel Plug-in
Manager, perché con una firma ad-hoc è lo scenario probabile.

Le due guide chiudono chiedendo esattamente i punti che `HANDOFF.md` aveva in attesa di
conferma, tradotti in domande da musicista: *l'armonia entra in ritardo* (`kSettleFrames`),
*l'attacco è al posto giusto* (quantizzazione al blocco, A-05), *il timbro regge* (formanti mai
tarate), *il passaggio fra le due modalità*. Più una richiesta esplicita a chi è su Mac Intel di
dirlo, perché **la fetta x86_64 non è mai stata eseguita da nessuno**: la CI compila universal
ma gira su runner arm64.

`BETA-consegna.md` è il foglio operativo dell'utente (non dei tester): la checklist prima del
primo invio, la procedura per ogni tester, e la bozza dell'email.

---

## 7. Il sorgente, e la cosa da controllare subito

Timore dell'utente: che il sorgente diventi pubblico e gli rubino il plugin. Risposta: un
`.vst3` è codice macchina, il C++ non c'è. Disassemblarlo è possibile ma restituisce assembly,
non l'algoritmo — e nessuno nella produzione musicale lo fa a una beta ricevuta da un
conoscente. **La minaccia realistica non è il furto del codice, è la redistribuzione del file**,
che è ciò che D-26 affronta.

Ma nel verificarlo è emerso qualcosa che non riguarda la beta: il remote è
`github.com/Jazy1997/STARTUP-Software-Harmonizer` e **non ho potuto controllare se è privato**
(`gh` non è installato su questa macchina). Se fosse pubblico, il sorgente è già leggibile e
nessuna precauzione sul pacchetto conta. Messo come prima voce della checklist in
`BETA-consegna.md`. **Resta da confermare dall'utente.**

---

## 8. File toccati

| File | Cosa |
|---|---|
| `src/licensing/BetaGate.h` | **nuovo** — aritmetica pura della scadenza. La cartella non è più vuota, ma il commento in testa vieta di confonderlo col `LicenseManager` di M6 |
| `tests/beta_expiry_test.cpp` | **nuovo** — 20 controlli sull'aritmetica, con `static_assert` |
| `tests/beta_gate_audio_test.cpp` | **nuovo** — il cablaggio sul percorso audio, processore intero |
| `src/PluginProcessor.h/.cpp` | `betaExpired` atomico, `updateBetaGate()` in `prepareToPlay` e `timerCallback`, una riga in `processBlock` accanto al Bypass |
| `src/PluginEditor.cpp` | avviso e conto alla rovescia nella striscia del titolo — dentro `ScaledContent`, quindi scala da sé e **non tocca** `resized()` né l'aritmetica di FR-59 |
| `CMakeLists.txt` | `option(HARMONIZER_BETA OFF)`, identità in due gruppi (D-24), `COMPANY_NAME`/`BUNDLE_ID`, `MICROPHONE_PERMISSION_ENABLED`, i due banchi + `add_test` |
| `.github/workflows/build.yml` | `workflow_dispatch`, configure condizionale, staging, firma ad-hoc, `upload-artifact` |
| `BETA-Windows.md` · `BETA-macOS.md` | **nuovi** — per i tester |
| `BETA-consegna.md` | **nuovo** — per l'utente |

Guardia di D-23 verificata in locale prima di committare: 13 target `*_test`, tutti registrati.

**Non toccati di proposito:** `BUGS.md` (nessun sintomo osservato — quando i tester
risponderanno, è lì che nasceranno le entry), il motore PSOLA (D-19/D-21), `dsp-tests` e i passi
`ctest`/`pluginval` della CI.

---

## 9. Verifica

| Cosa | Esito |
|---|---|
| `ctest` in configurazione normale | **13/13 in 2.17 s** (erano 11) |
| La scadenza è OFF per default | build normale: nessuna macro definita, il codice non viene compilato affatto |
| La configurazione beta compila | `-DHARMONIZER_BETA=ON -DHARMONIZER_BETA_DAYS=0`, standalone linkato, exit 0 |
| Guardia D-23 | 13 target, tutti registrati |
| YAML del workflow | valido, `dsp-tests` e `build` intatti, tre trigger |
| Wet a scadenza | 4.334e-09 |
| Dry a scadenza | 0.0992 contro 0.0992 della sorgente |

**Falso allarme da annotare:** al primo `--parallel` avevo filtrato l'output anche su `error MSB`
e ne sono usciti venti errori. Non erano errori: `MSB3073` è l'eco dello script del passo
post-build, e la causa vera era la copia in `Program Files` — **D-12, attesa e documentata**.
`HANDOFF.md:145` dice di filtrare solo `error C####`/`error LNK`, e di quelli non ce n'era
nessuno. Il filtro giusto era già scritto: bastava usarlo.

---

## 10. Dopo il push: due scoperte, entrambe costose

### 10.1 Sei commit, non quattro — `ctest` in CI non era mai girato

Il push ha portato `99a50ae..b18d0ce`, cioè **sei** commit: i quattro di oggi più
`78c8e13 ci: ctest al posto dei tre g++ a mano — A-06` e `22d04ae docs: sessione 36`, che erano
rimasti **solo in locale** dalla sessione precedente.

Conseguenza da non sottovalutare: **il run #26 è il primo in assoluto in cui `ctest` è girato in
CI.** Tutti i run verdi precedenti (#22-#25) compilavano e lanciavano solo `pluginval`. D-23 era
scritto, committato e mai arrivato su GitHub. La frase "25 run verdi" dell'HANDOFF era vera del
*build*, non delle suite — e l'ho ripetuta prima di accorgermene.

**Lezione operativa:** `git log --oneline origin/main..HEAD` **prima** di affermare cosa la CI
verifica. Il sintomo era in piena vista sulla pagina Actions: l'ultimo run mostrava un commit più
vecchio di `HEAD`.

### 10.2 Il job macOS costa ~3 ore (→ A-09)

Misurato sul run #26: **macOS ~3 h, Windows 15 min**, stesso commit. Non è la CI in generale, è
specificamente macOS.

| Causa | Peso |
|---|---|
| `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` — **ogni sorgente compila due volte**, banchi compresi | grosso |
| `mode_switch_test` e `beta_gate_audio_test` ricompilano l'**intero** elenco dei sorgenti del plugin, ciascuno × 2 architetture | grosso |
| Nessuna cache di build; coda dei runner macOS su repo privato | variabile |

**Una parte del costo l'ho introdotta io** in questa sessione: `beta_gate_audio_test` è il secondo
target che ricompila tutto il plugin. In locale non si vedeva, perché è incrementale.

Il repo è privato, quindi i minuti escono da un monte ore mensile con i minuti macOS
moltiplicati. Il limite di spesa predefinito è 0, quindi **nessuna fattura a sorpresa**: Actions
si ferma e basta.

**Due correzioni a copertura invariata**, pronte e non applicate su richiesta dell'utente (il
pacchetto beta era in volo):

1. **Banchi compilati per una sola architettura.** La CI gira su runner arm64, quindi la fetta
   x86_64 dei *test* viene compilata e **non eseguita mai da nessuno**: toglierla non riduce la
   copertura di un millimetro. È il grosso del tempo.
2. **`paths-ignore`** per i push di sola documentazione, **escludendo `BETA-Windows.md` e
   `BETA-macOS.md`**, che la CI copia nel pacchetto e devono poterlo rigenerare.

La terza — **job macOS solo a richiesta** — taglierebbe quasi tutto ma **è una decisione
dell'utente**: ridurrebbe la copertura appena conquistata con D-23, e A-06 nasceva proprio da
buchi di copertura.

**Strada scartata, da non riproporre:** far condividere ai due banchi pesanti una libreria statica
dei sorgenti del plugin. Hanno definizioni di compilazione diverse (`beta_gate_audio_test` forza
le macro `HARMONIZER_BETA_*`), quindi quei sorgenti compilano in modo diverso e non sono
condivisibili.

**Trappola:** ogni push fa partire un run completo. `[skip ci]` nel messaggio di commit lo evita,
ed è quello che serve per i commit di sola documentazione — che in questo progetto sono la
maggioranza e finora hanno compilato tutto per niente.

### 10.3 Ancora da verificare sul pacchetto beta

I passi di pacchettizzazione di D-25 **non hanno mai completato con successo**: i percorsi degli
artefatti macOS sono stati scritti dalla struttura di JUCE, non osservati. Alla fine del run:

- devono comparire gli artefatti `Harmonizer-Windows-beta` e `Harmonizer-macOS-beta`;
- il passo `Firma ad-hoc dei bundle` deve stampare due blocchi `codesign -dv` contenenti `adhoc`.

---

## 11. Cosa resta aperto dopo questa sessione

- **Il repository è privato?** Da confermare (§7).
- **Nessuna verifica su macOS è possibile da qui.** Installazione, quarantena, Logic, permesso
  microfono, fetta x86_64: tutto in mano al primo tester.
- **A-04** rimandata con tre grilletti (D-25), non dimenticata.
- **A-01 / M6** invariata: `src/licensing/` contiene un cancello temporaneo, non un licensing.
- Il conto alla rovescia nell'editor è **corretto per costruzione ma mai visto a schermo**: qui
  non si apre una GUI.
