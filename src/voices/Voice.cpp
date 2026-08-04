#include "Voice.h"
#include <cmath>

void Voice::prepare (double sampleRate, int maxBlockSize, int stabilityLevel)
{
    shifter = createDefaultPitchShifter();
    shifter->prepare (sampleRate, maxBlockSize, stabilityLevel);
    scratch.assign ((size_t) maxBlockSize, 0.0f);

    offsetGlide.prepare (sampleRate);
    offsetGlide.reset (0.0f);

    ampGlide.prepare (sampleRate);
    ampGlide.setGlideTimeMs (kDeclickMs);
    ampGlide.reset (0.0f); // silenzioso all'avvio, coerente con muted=true di default
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
    // Sessione 12: NON si esce piu' subito perche' muted e' vero — si esce
    // solo quando la dissolvenza ha davvero finito (isSilent()). Cosi' il
    // chiamante puo' smettere di rifornire audio a una voce (fine frase,
    // silenzio, cella tornata vuota su una frase ancora viva) senza tagliare
    // di netto: le ultime chiamate a processAdd, con muted=true, producono
    // ancora suono ma in dissolvenza verso zero.
    if (shifter == nullptr || isSilent())
        return;

    // Sessione 13 (click residui): con un buffer host piu' lungo della
    // rampa (8ms = 353 campioni a 44.1kHz; l'utente ha misurato 4096
    // campioni con MME/DirectX in Ableton) ampGlide.process(numSamples)
    // faceva scattare l'intera dissolvenza in un solo campione — il fix di
    // sessione 12 era di fatto un no-op in quella configurazione. Vedi
    // Glide::processRamp e tests/glide_test.cpp.
    const auto ampRamp = ampGlide.processRamp (numSamples);
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

    // FR-39/FR-41: correzione formantica in funzione dello shift REALMENTE
    // applicato a questa voce (semitonesToApply), non dell'offset grezzo del
    // preset — cosi' funziona identica in Fix e Move, e restera' identica
    // anche in modalita' Play (FR-42) quando esistera', perche' tutte
    // finiscono per produrre lo stesso semitonesToApply. Formula da
    // psola-spec.md §3 (k~0.3): shift in giu' schiarisce (beta>1), shift in
    // su scurisce (beta<1). Si lavora in "semitoni-equivalenti" anziche' nel
    // rapporto beta direttamente cosi' l'offset manuale (FR-41) e' una somma
    // letterale con la correzione automatica, non una moltiplicazione di
    // rapporti — coerente col resto del progetto, che ragiona in semitoni.
    constexpr float kFormantSpreadK = 0.3f;
    const float autoFormantSemitones = -kFormantSpreadK * formantSpread * semitonesToApply;
    const float totalFormantSemitones = autoFormantSemitones + formantOffsetSemitones;
    shifter->setFormantRatio (std::exp2 ((double) totalFormantSemitones / 12.0));

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

    // Guadagno campione-per-campione: ampRamp.rampSamples puo' essere minore
    // di numSamples (la rampa finisce dentro questo blocco), nel qual caso i
    // campioni restanti restano fermi al target (ampRamp.startValue +
    // rampSamples*increment, gia' pari a ampGlide.getCurrentValue()).
    float amp = ampRamp.startValue;
    int i = 0;
    for (; i < ampRamp.rampSamples; ++i)
    {
        mixOutput[i] += scratch[(size_t) i] * amp;
        amp += ampRamp.increment;
    }
    for (; i < numSamples; ++i)
        mixOutput[i] += scratch[(size_t) i] * amp;
}
