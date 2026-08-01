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

    void requestStabilityChange (int newStabilityLevel) { voicePool.requestStabilityChange (newStabilityLevel); }
    void collectGarbage() { voicePool.collectGarbage(); }

    // mixOutput deve avere numSamples campioni; viene azzerato internamente.
    // currentOffsetsForTrigger sono gli offset calcolati ORA (preset/root/nota
    // correnti): usati per congelare una nuova frase al trigger, o per
    // aggiornare dal vivo la frase piu' recente (FR-17). Ritorna true se e'
    // stato applicato un cambio di Stability in questa chiamata.
    bool process (const float* monoIn,
                  float* mixOutput,
                  int numSamples,
                  bool onsetDetectedThisBlock,
                  bool inputIsStable,
                  int quantizedPlayedNote,
                  float continuousInputMidiNote,
                  const std::array<harmony::Cell, harmony::numVoices>& currentOffsetsForTrigger,
                  int numRequestedVoices,
                  bool applyStabilityChangeNow);

private:
    void triggerNewPhrase (const std::array<harmony::Cell, harmony::numVoices>& offsets, int numRequestedVoices);
    void freeAllPhrases();
    static void freePhrase (Phrase& phrase);
    bool isSlotInUse (int slotIndex) const;
    int allocateFreeSlot();

    VoicePool voicePool;
    std::vector<Phrase> phrases; // capacita' = hardSlotCapacity (limite superiore sul numero di frasi)
    int currentVoiceCap = 0;     // <= voicePool.getNumSlots(); regolabile a runtime senza allocare
    bool keepTails = false;      // vedi setKeepTails()
    uint64_t ageCounter = 0;
    std::atomic<int> numActiveSlotsLastBlock { 0 };
};
