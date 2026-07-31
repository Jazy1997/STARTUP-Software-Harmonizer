#include "Voice.h"
#include <cmath>

void Voice::prepare (double sampleRate, int maxBlockSize, int stabilityLevel)
{
    shifter = createDefaultPitchShifter();
    shifter->prepare (sampleRate, maxBlockSize, stabilityLevel);
    scratch.assign ((size_t) maxBlockSize, 0.0f);

    offsetGlide.prepare (sampleRate);
    offsetGlide.reset (0.0f);
}

void Voice::reset()
{
    if (shifter)
        shifter->reset();
}

int Voice::getLatencySamples() const
{
    return shifter ? shifter->getLatencySamples() : 0;
}

void Voice::swapShifterNoAlloc (std::unique_ptr<PitchShifter>& shifterInOut) noexcept
{
    shifter.swap (shifterInOut);
}

void Voice::processAdd (const float* monoIn, float* mixOutput, int numSamples,
                         int quantizedPlayedNote, float continuousInputMidiNote)
{
    if (muted || shifter == nullptr)
        return;

    const float smoothedOffset = offsetGlide.process (numSamples);

    float semitonesToApply;
    if (mode == ShiftMode::fix)
    {
        // FR-22: la voce insegue una nota ASSOLUTA (nota quantizzata +
        // offset), non un rapporto fisso sull'ingresso. Il rapporto va
        // ricalcolato ogni blocco per compensare vibrato/bending in ingresso.
        const float targetAbsoluteMidi = (float) quantizedPlayedNote + smoothedOffset;
        semitonesToApply = targetAbsoluteMidi - continuousInputMidiNote;
    }
    else
    {
        // FR-21: rapporto fisso rispetto all'ingresso — vibrato e portamento
        // dell'esecutore si trasferiscono integralmente sulla voce.
        semitonesToApply = smoothedOffset;
    }

    // Fondamentale del segnale in ingresso, in Hz: serve ai motori a
    // dominio del tempo (PSOLA) per posizionare gli epoch sul periodo
    // reale. E' un calcolo esatto, non un'approssimazione: PitchDetector
    // ricava continuousInputMidiNote proprio da una frequenza in Hz
    // (get_frequency() di Cycfi Q), quindi questo e' l'andata e ritorno
    // dello stesso valore. Le implementazioni che non ne hanno bisogno
    // (SpectralShifter) ignorano la chiamata (default no-op).
    shifter->setInputF0Hz (440.0 * std::exp2 (((double) continuousInputMidiNote - 69.0) / 12.0));

    shifter->setPitchShiftSemitones (semitonesToApply);
    shifter->process (monoIn, scratch.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
        mixOutput[i] += scratch[(size_t) i];
}
