# Sessione 33 — FR-59: la scala 70–200%

> 2026-08-11. Prosegue direttamente dalla s.32, che chiudeva il fronte DSP (D-19) e indicava
> FR-59 come unico prossimo passo.

---

## 1. Il punto di partenza, verificato nel codice e non nei documenti

`HANDOFF.md` dava FR-59 come *"più piccolo di come sembra: la finestra è già ridimensionabile,
manca la scala percentuale"*. Verificato prima di scrivere, ed è esatto:

- `PluginEditor.cpp:430-438` — `setResizable(true,true)`, `setResizeLimits(520,620,1200,900)`,
  `setSize(900,660)`, dalla s.25.
- Zero occorrenze di `setScaleFactor` / `AffineTransform` in `src/`.
- Tutte le costanti di layout in pixel, sia nell'editor (`rowHeight = 26`, `labelWidth = 60`,
  `presetTableHeight = 22 + 8*24`, font a 11/13/14 pt) sia dentro i sotto-componenti
  (`PresetTableEditor.cpp:113-115`, `PresetListEditor.h:26`).
- Dimensione dell'editor mai serializzata (`PluginProcessor.cpp:742-790`).

Quello che c'era era **reflow**, e per giunta solo orizzontale: verticalmente non era una
funzione ma il difetto Keep Tails della s.30.

## 2. La decisione dell'utente, presa prima di scrivere

Due domande, entrambe risolte dall'utente:

1. **Scala uniforme su layout logico fisso 900×660** (contro "due gradi di libertà separati",
   cioè scala da menu più reflow libero dentro la dimensione logica risultante). Aspect ratio
   bloccato: trascinare l'angolo *è* cambiare la scala.
2. **Il valore vive nello stato del plugin, non nell'APVTS.**

Entrambe sono in **D-20**.

## 3. La scoperta che ha cambiato *come*, non *cosa*

L'anteprima mostrata all'utente diceva `setTransform (AffineTransform::scale (s))` sull'editor.
**Non si può fare**, ed è JUCE stessa a dirlo —
`libs/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessorEditor.cpp:193-199`:

```cpp
void AudioProcessorEditor::editorResized (bool wasResized)
{
    // The host needs to be able to rescale the plug-in editor and applying your own transform
    // will obliterate it! ... consider putting the component you want to transform in a child
    // of the editor and transform that instead.
    jassert (getTransform() == hostScaleTransform);
```

e `setScaleFactor()` (`:227-233`) **sovrascrive** il transform dell'editor con
`hostScaleTransform`. Un transform nostro sull'editor sarebbe quindi (a) un assert in Debug e
(b) — molto peggio — **cancellato silenziosamente** alla prima notifica di DPI dell'host, cioè
proprio nel caso HiDPI che FR-59 chiede di far funzionare.

Il rimedio è quello indicato dal commento stesso: un **unico componente contenitore**
(`ScaledContent`) figlio dell'editor porta il transform. Il modello scelto dall'utente non
cambia di una virgola; anzi HiDPI diventa corretto **per costruzione**, perché i due transform
si compongono invece di sovrascriversi.

Il costo di ristrutturazione si è rivelato minimo: solo **6** `addAndMakeVisible` erano su
`*this` (i 3 pulsanti nav e le 3 pagine); tutto il resto era già figlio di una pagina. Dopo la
modifica l'unico figlio diretto dell'editor è `content`.

## 4. Cosa è stato scritto

**`src/ui/UiScale.h`** (nuovo, header-only, JUCE-free) — l'unico posto dove vivono i numeri di
FR-59: `kLogicalWidth/Height` 900×660, `kMin/MaxScalePercent` 70/200, le sei tacche del menu,
e le conversioni percentuale ↔ pixel nei due sensi.

**`src/PluginEditor.{h,cpp}`** — `ScaledContent`, che porta il transform. `resized()` si divide
in due: l'editor calcola la scala da `getWidth() / 900` e la applica al contenitore; il
contenitore esegue il vecchio corpo di `resized()` **invariato riga per riga**, ora sempre su
900×660. Il titolo disegnato in `paint()` si sposta nel contenitore, altrimenti sarebbe l'unica
cosa a schermo a non scalare. Lo sfondo resta sull'editor: copre lo slack di arrotondamento fra
`getHeight()` e `660 × scala`. I limiti di ridimensionamento diventano 630×462 e 1800×1320, più
`setFixedAspectRatio (900.0/660.0)`.

**Menu della scala su Impostazioni** — sei tacche (70/85/100/125/150/200%), nessun Attachment,
legatura via il Timer a 15 Hz già esistente. Con la scala continua da trascinamento la casella
**deseleziona e mostra la percentuale vera** invece di arrotondare alla tacca più vicina:
mostrare "100%" a 103% sarebbe una piccola bugia proprio sul controllo che deve dire dove ci si
trova.

**`src/PluginProcessor.{h,cpp}`** — `uiScalePercent` atomico + nodo `UiSettings` nello stato,
accanto a `MidiCcSettings`. Le sessioni salvate prima di oggi non hanno il nodo e aprono al
100%.

**Nessuna riga di `processBlock`, nessun file in `src/dsp/`, `src/voices/`, `src/harmony/`.**
Il motore è chiuso (D-19) e questo lavoro non lo sfiora.

## 5. Il difetto Keep Tails si chiude per inciso

Con l'altezza logica fissa a 660, `layoutEdit()` dispone di 562 px e ne chiede 528: **34 px di
margine, a qualunque scala**. Prima, all'altezza minima di 620 px fisici, i disponibili erano
522 contro 528 richiesti.

Il calcolo è stato rifatto sommando le dieci righe di `layoutEdit()` e **riproduce esattamente
i 522/528 documentati in s.30** — è il controllo che dice che il metodo di misura è quello
giusto, non un numero inventato a posteriori.

Resta però **verificato per calcolo, non visto**: che a schermo il toggle ci sia davvero è la
prima cosa da guardare al 70%.

## 6. Verifica

- **`tests/ui_scale_test.cpp`**, nona suite (JUCE-free, livello D-11). 21 verifiche, tutte
  verdi. La più utile è il **round-trip su tutto il range**: per ogni percentuale in [70, 200],
  `percentuale → larghezza → percentuale` torna identica. È la proprietà che si rompe se
  qualcuno cambia la larghezza logica in un valore sotto i 100 px, dove due percentuali diverse
  darebbero la stessa larghezza e la finestra "scatterebbe" da sola. Verificate anche la
  coerenza fra i limiti e `setFixedAspectRatio` (1.363636 identico ai due estremi) e il clamp
  su stato di sessione corrotto.
- **`ctest -C Release`: 9/9 verdi.**
- **`pluginval --strictness-level 10` su VST3 (Win): SUCCESS.** I test *Editor*, *Open editor
  whilst processing* ed *Editor Automation* sono passati: aprono, chiudono e ridimensionano
  davvero l'editor, quindi esercitano il percorso nuovo.
- **Standalone ricostruito e funzionante** (regola 11).
- Il fallimento della copia in `Program Files` è quello atteso di D-12: **zero** `error C####`,
  **zero** `error LNK`. `moduleinfo.json` viene generato regolarmente; a fallire è solo l'ultimo
  passo, `copyDir`.

**Warning preesistente, non introdotto oggi e non corretto**: `PluginEditor.cpp:371` C4244
(`convertTo0to1 (note)` con `note` intero) è codice della s.32 che riaffiora solo perché il file
è stato ricompilato. Fuori scope.

## 7. Cosa questa sessione NON può dire

FR-59 chiede *"resa corretta su display HiDPI e Retina"*. La composizione dei due transform è
corretta per costruzione e verificabile leggendo il codice JUCE, ma **che a schermo non ci siano
testi sfocati o bordi a mezzo pixel è una verifica visiva dell'utente** — l'analogo per gli
occhi di ciò che la regola 12 dice per le orecchie. E **AU e Retina restano scoperti**:
richiedono macOS, che non è mai entrato nel ciclo di verifica di questo progetto.
