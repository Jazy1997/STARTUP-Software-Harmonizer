# HANDOFF — HARMONIZER

> Ultimo aggiornamento: **2026-08-10**, sessione 29.
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
  confermato stabile all'ascolto (s.26).
- UI: tre schermate Main / Edit / Impostazioni con barra di navigazione sempre visibile.
- **M6 (licensing) non esiste**: `src/licensing/` è una cartella vuota.
- 7 suite `ctest` verdi. `pluginval --strictness-level 10` verde su VST3 (Win) e AU (CI macOS).
- Working tree **sporco**: il lavoro di s.28 non è mai stato committato (vedi sotto).

---

## Prossimo passo

**Uno solo: chiudere B-05 (il "ribattuto").** Serve un input che solo l'utente può dare —
riesportare lo stesso materiale con un **bounce offline** o dopo freeze della traccia.

- Se il buco sparisce → era un underrun real-time del DAW, niente da correggere nel DSP.
- Se resta identico → si instrumenta `PsolaShifter::detectEpochs()`/`synthesise()`
  direttamente sull'attacco di nota.

Dettagli e misure in `BUGS.md` § B-05.

---

## In attesa di conferma all'ascolto

Regola 12 di `CLAUDE.md`: nulla che tocchi il suono si dichiara completo per calcolo.

| Cosa | Dove | Stato |
|---|---|---|
| Isteresi cella vuota, `kEmptyCellHoldMs = 80.0f` | `src/voices/EmptyCellHold.h` | **Scritta, testata (12 verifiche verdi), MAI committata.** Misurata **irrilevante** sul materiale reale: le attese vere durano 2136 ms e 279 ms, quindi soglia 0 e soglia 80 ms danno output identico. Da presentare come miglioramento indipendente, **non** come il fix di B-05. La soglia di 80 ms non è mai stata tarata. |

---

## Limiti noti

Da non scambiare per requisiti soddisfatti.

- **FR-42 non arriva in Play mode**: `PlayModeInput` non riceve `setFormantSpread` /
  `setVoiceFormantOffset`. Buco reale su un `[MUST]`, aperto da s.23.
- **FR-59 assente**: finestra a dimensione fissa `setSize(900, 660)`. Nessun
  ridimensionamento 70–200%, nessuna verifica HiDPI/Retina.
- **Preset di fabbrica non verificati**: dei 7, **solo "Min"** è confrontato col prototipo
  Max4Live. Gli altri 6 sono voicing jazz generici scritti algoritmicamente.
- **CI copre 3 delle 7 suite**: `build.yml` ricompila a mano con `g++` e non invoca mai
  `ctest`. Fuori CI: `glide`, `cell_input_parser`, `voice`, `empty_cell_hold`, e ogni
  target futuro.
- **La UI non riflette un override CC attivo** (FR-36/37): il CC non scrive nel parametro
  APVTS, quindi la griglia fondamentale non si aggiorna.
- **Formanti mai tarate**: `k = 0.3` non è mai passato per l'ascolto, nessun test numerico.
- **CC e Play mode mai provati con hardware reale**: `override_manager_test` copre la
  precedenza, il parsing MIDI di `CcRouter` no.
- **Nessuna `LookAndFeel` custom**: forma dei knob, colori, font — lavoro mai iniziato.
- **Colonne tagliate**: griglia fondamentale e tabella 12×8 non mostrano tutte e 12 le
  colonne alla larghezza attuale. `Viewport` orizzontale rimandato da s.21.
- **CPU mai profilata** con 8 voci contro il budget ≤15% del PRD §1.3.
- **Catch2 mai adottato** (previsto da PRD §9.1): i test sono `int main()` scritti a mano.

---

## Questioni aperte

Nessuna di queste è tecnica; nessuna è chiusa.

- Nome prodotto, marchio, dominio → bloccano `PLUGIN_MANUFACTURER_CODE` (`Hzso`),
  `PLUGIN_CODE` (`Hmz1`), `COMPANY_NAME` (`"TBD"`), `BUNDLE_ID`. **Cambiarli dopo il
  rilascio rompe i progetti salvati**, come il tipo AU.
- **Certificati di firma e notarizzazione** (Apple Developer ID + code signing Windows).
  Il PRD li vuole avviati in M0: nessuna traccia che sia stato fatto. È il lead time
  più lungo del progetto.
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
| `LOG/archivio-s01-s28.md` | Il racconto per esteso delle sessioni 1–28 |
| `LOG/sessione-NN.md` | Racconto per esteso, dalla sessione 29 in poi |

**Materiale di test** (`SAMPLE TEST/`, non versionato): `Test 1 - Basic Silk Horns.wav`
e `Test 2 - E-Piano.wav` sono i **dry sorgente** delle sonde; `DBG Timbro/` contiene i
riferimenti forniti dall'utente. Gli intermedi rigenerabili sono stati eliminati in s.29.

**Comandi**: `ctest -C Release` (7 suite) · `cmake --build build --config Release`
· `tools/pluginval.exe --strictness-level 10 --validate <path.vst3>`
