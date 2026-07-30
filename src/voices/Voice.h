#pragma once

#include "../dsp/PitchShifter.h"
#include "../dsp/Glide.h"
#include <memory>
#include <vector>

// FR-21/22/23: Move (default) segue il rapporto fisso rispetto all'ingresso
// (comportamento naturale dello shifter, nessun calcolo aggiuntivo). Fix
// insegue una nota assoluta (nota quantizzata + offset), ricalcolando il
// rapporto ogni blocco sulla base del pitch continuo rilevato.
enum class ShiftMode { move, fix };

class Voice
{
public:
    void prepare (double sampleRate, int maxBlockSize, int stabilityLevel);
    void reset();

    void setMuted (bool shouldBeMuted) noexcept { muted = shouldBeMuted; }
    bool isMuted() const noexcept { return muted; }

    void setMode (ShiftMode newMode) noexcept { mode = newMode; }
    void setGlideTimeMs (float ms) noexcept { offsetGlide.setGlideTimeMs (ms); }

    // Offset armonico grezzo (semitoni) prima del glide (FR-17): la rampa
    // verso il nuovo valore avviene dentro processAdd, non qui.
    void setTargetOffsetSemitones (float semitones) noexcept { offsetGlide.setTarget (semitones); }

    int getLatencySamples() const;

    // Scambio senza allocazione/deallocazione (std::unique_ptr::swap e'
    // noexcept e non invoca il deleter): newShifter in ingresso diventa lo
    // shifter attivo, quello precedente esce in newShifter. Il chiamante
    // (VoicePool, sull'audio thread) deve poi far distruggere il vecchio
    // shifter sul message thread, mai qui — vedi PRD §9.4.
    void swapShifterNoAlloc (std::unique_ptr<PitchShifter>& shifterInOut) noexcept;

    // Somma il segnale shiftato di questa voce dentro mixOutput (gia'
    // inizializzato dal chiamante). Non-op se la voce e' muta.
    // quantizedPlayedNote / continuousInputMidiNote servono alla modalita' Fix.
    void processAdd (const float* monoIn, float* mixOutput, int numSamples,
                      int quantizedPlayedNote, float continuousInputMidiNote);

private:
    std::unique_ptr<PitchShifter> shifter;
    std::vector<float> scratch;
    Glide offsetGlide;
    ShiftMode mode = ShiftMode::move;
    bool muted = true;
};
