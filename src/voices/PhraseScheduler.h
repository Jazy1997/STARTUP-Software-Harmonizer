#pragma once

#include "VoicePool.h"
#include "Phrase.h"
#include "../harmony/HarmonyPreset.h"
#include <vector>
#include <atomic>

// Orchestratore delle frasi (FR-43..53): ogni onset genera una frase con
// voicing congelato (FR-46); solo la frase piu' recente segue dal vivo i
// cambi di preset/fondamentale finche' la stessa nota continua a suonare
// (FR-17 — vedi Phrase.h per come si risolve la tensione tra i due requisiti,
// validata all'ascolto in sessione 10 col bottone Keep Tails/setKeepTails).
// Al superamento del tetto di voci simultanee, la frase piu' vecchia viene
// liberata per intero (FR-51/52); la transizione morbida richiesta (>=20ms)
// e' data dal Glide gia' presente in Voice quando lo slot fisico viene
// riassegnato con un nuovo offset.
class PhraseScheduler
{
public:
    // hardSlotCapacity e' il numero di slot fisici pre-allocati (fisso per
    // tutta la vita del plugin — nessuna riallocazione possibile senza lo
    // stesso schema usato per Stability). setVoiceCap() regola invece,
    // a costo zero, QUANTI di quegli slot sono utilizzabili in un dato
    // momento (FR-51, "tetto configurabile"): un limite morbido <= hardSlotCapacity.
    void prepare (int hardSlotCapacity, double sampleRate, int maxBlockSize, int stabilityLevel);
    void reset();

    void setVoiceMode (int harmonicVoiceIndex, ShiftMode mode);
    void setGlideTimeMs (float ms);

    // FR-40: globale, si applica a tutti gli slot fisici (non solo a quelli
    // occupati ora: una frase futura la trovera' gia' impostata).
    void setFormantSpread (float spread);
    // FR-41: come setVoiceMode, proprieta' della colonna armonica (0-7).
    void setVoiceFormantOffset (int harmonicVoiceIndex, float semitones);
    // FR-11/§8.1: come setVoiceFormantOffset, proprieta' della colonna
    // armonica (0-7) — gia' lineare in ingresso (conversione da dB fatta a
    // monte, in PluginProcessor).
    void setVoiceGainLinear (int harmonicVoiceIndex, float gainLinear);
    void setVoicePan (int harmonicVoiceIndex, float pan);
    void setVoiceCap (int cap) noexcept { currentVoiceCap = cap; }
    // Vedi Phrase.h per la semantica completa. false (default) = tronca
    // subito una frase superata da un nuovo onset; true = comportamento
    // precedente (resta viva finche' non rubata o segnale silenzioso).
    void setKeepTails (bool keep) noexcept { keepTails = keep; }

    int getLatencySamples() const { return voicePool.getLatencySamples(); }
    // FR-53: letto anche dal message thread (UI); scritto dall'audio thread
    // in process() — un contatore informativo, non usato per decisioni di
    // correttezza, quindi un semplice atomic relaxed basta.
    int getNumActiveVoices() const noexcept { return numActiveSlotsLastBlock.load (std::memory_order_relaxed); }
    // Sessione 12 (FR-43/45/46): quante volte process() ha dovuto completare
    // l'allocazione di uno slot su una frase gia' nata (perche' il pitch non
    // era ancora confidente al momento dell'onset) — contatore cumulativo,
    // informativo, letto dall'UI per confermare all'ascolto che il fix delle
    // note saltate sta intervenendo davvero.
    int getNumLateBindings() const noexcept { return numLateBindingsTotal.load (std::memory_order_relaxed); }

    void requestStabilityChange (int newStabilityLevel) { voicePool.requestStabilityChange (newStabilityLevel); }
    void collectGarbage() { voicePool.collectGarbage(); }

    // mixL/mixR devono avere numSamples campioni ciascuno; vengono azzerati
    // internamente (FR-11/§8.1: percorso wet stereo, gain/pan per voce).
    // currentOffsetsForTrigger sono gli offset calcolati ORA (preset/root/nota
    // correnti): usati per congelare una nuova frase al trigger, o per
    // aggiornare dal vivo la frase piu' recente (FR-17). Ritorna true se e'
    // stato applicato un cambio di Stability in questa chiamata.
    //
    // signalPresent e inputIsStable sono due segnali DIVERSI, non
    // ridondanti (sessione 11 — vedi PluginProcessor.cpp per la scoperta):
    //   - signalPresent: c'e' ancora energia sul segnale in ingresso (tipicamente
    //     lo stato del gate di OnsetDetector) — "il performer sta ancora
    //     suonando qualcosa". Governa SOLO se liberare tutte le frasi.
    //   - inputIsStable: il pitch rilevato QUESTO blocco e' abbastanza
    //     confidente da fidarsene (tipicamente PitchDetector::hasStableSignal()).
    //     Governa se aggiornare dal vivo gli offset della frase corrente E
    //     (sessione 12, FR-43/45/46) se completare l'allocazione degli slot
    //     rimasti vuoti al trigger perche' il pitch non era ancora noto in
    //     quel blocco — vedi PhraseScheduler.cpp (process()) per il dettaglio
    //     della corsa onset/pitch.
    // Usare la confidenza del pitch anche per la prima decisione (come si
    // faceva prima di sessione 11) libera erroneamente tutte le frasi durante
    // un calo di confidenza transitorio — es. lo scivolamento di intonazione
    // fra due note cantate legato, dove il segnale c'e' ancora (il gate resta
    // aperto) ma la stima di frequenza per quell'istante e' meno affidabile.
    bool process (const float* monoIn,
                  float* mixL,
                  float* mixR,
                  int numSamples,
                  bool onsetDetectedThisBlock,
                  bool signalPresent,
                  bool inputIsStable,
                  int quantizedPlayedNote,
                  float continuousInputMidiNote,
                  const std::array<harmony::Cell, harmony::numVoices>& currentOffsetsForTrigger,
                  int numRequestedVoices,
                  bool applyStabilityChangeNow);

private:
    void triggerNewPhrase (const std::array<harmony::Cell, harmony::numVoices>& offsets, int numRequestedVoices);
    void freeAllPhrases();
    // Sessione 12: due modi di "liberare" una frase, non piu' uno solo.
    // beginRelease() e' il caso normale (fine frase/silenzio/superata da un
    // nuovo onset): avvia una dissolvenza morbida, vedi Phrase.h.
    // hardFreePhrase() e' SOLO per il furto d'emergenza (FR-52): serve lo
    // slot fisico immediatamente, nessun tempo per sfumare — la continuita'
    // la da' comunque il Glide dell'offset sul nuovo target.
    static void beginRelease (Phrase& phrase);
    static void hardFreePhrase (Phrase& phrase);
    bool isSlotInUse (int slotIndex) const;
    int allocateFreeSlot();

    VoicePool voicePool;
    std::vector<Phrase> phrases; // capacita' = hardSlotCapacity (limite superiore sul numero di frasi)
    int currentVoiceCap = 0;     // <= voicePool.getNumSlots(); regolabile a runtime senza allocare
    bool keepTails = false;      // vedi setKeepTails()
    uint64_t ageCounter = 0;
    std::atomic<int> numActiveSlotsLastBlock { 0 };
    std::atomic<int> numLateBindingsTotal { 0 };

    // FR-17 (sessione 28 — "ribattuto", vedi src/voices/EmptyCellHold.h):
    // quanti campioni consecutivi di cella vuota tollerare su una colonna
    // GIA' legata a uno slot fisico prima di mutarla per davvero. 80ms e'
    // un punto di partenza (stesso ordine di grandezza discusso per il
    // debounce del gate, mai scritto perche' Fase 0 lo ha escluso su questo
    // materiale) — da tarare all'ascolto (CLAUDE.md regola 12), non un
    // valore definitivo. Calcolato in prepare() perche' dipende dalla SR.
    static constexpr float kEmptyCellHoldMs = 80.0f;
    int emptyCellHoldSamples = 0;
};
