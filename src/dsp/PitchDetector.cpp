#include "PitchDetector.h"

#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>
#include <q/support/pitch.hpp>

#include <cmath>

using namespace cycfi::q::literals;

struct PitchDetector::Impl
{
    Impl (double sampleRate, float lowestHz)
        : detector (cycfi::q::frequency { lowestHz }, 1500_Hz, (float) sampleRate, -35_dB)
    {
    }

    cycfi::q::pitch_detector detector;
};

// Soglia di confidenza minima sotto la quale il segnale e' considerato non
// intonato (silenzio, rumore, transiente). Placeholder: FR-20 (fade invece di
// artefatti) e la calibrazione fine restano da fare in M1.
static constexpr float confidenceThreshold = cycfi::q::pitch_detector::min_periodicity;

PitchDetector::PitchDetector() = default;
PitchDetector::~PitchDetector() = default;

float PitchDetector::noteToHz (int midiNote) noexcept
{
    return 440.0f * std::exp2 ((float) (midiNote - 69) / 12.0f);
}

void PitchDetector::prepare (double sampleRate, int lowestNoteMidi)
{
    impl = std::make_unique<Impl> (sampleRate, noteToHz (lowestNoteMidi));
    currentMidiNote = -1.0f;
    currentConfidence = 0.0f;
}

int PitchDetector::getAnalysisFrameSamples() const noexcept
{
    if (! impl)
        return 0;

    // Q avanza la finestra di zero-crossing di meta' alla volta, ed e' li' che
    // is_ready() diventa vero: mezza finestra e' l'intervallo fra due stime.
    return (int) (impl->detector.edges().window_size() / 2);
}

void PitchDetector::reset()
{
    if (impl)
        impl->detector.reset();
    currentMidiNote = -1.0f;
    currentConfidence = 0.0f;
}

bool PitchDetector::pushSample (float sample) noexcept
{
    if (! impl)
        return false;

    const bool updated = impl->detector (sample);
    if (updated)
    {
        currentConfidence = impl->detector.periodicity();
        const float freqHz = impl->detector.get_frequency();
        if (freqHz > 0.0f)
        {
            const cycfi::q::pitch p { cycfi::q::frequency { freqHz } };
            currentMidiNote = cycfi::q::as_float (p);
        }
    }
    return updated;
}

void PitchDetector::requestLowestNoteChange (double sampleRate, int lowestNoteMidi)
{
    // Una richiesta gia' in volo non si sovrascrive: il puntatore appartiene
    // all'audio thread finche' non lo consuma.
    if (pendingReady.load (std::memory_order_acquire))
        return;

    pendingImpl = std::make_unique<Impl> (sampleRate, noteToHz (lowestNoteMidi));
    pendingReady.store (true, std::memory_order_release);
}

bool PitchDetector::applyPendingLowestNoteChange() noexcept
{
    if (! pendingReady.load (std::memory_order_acquire))
        return false;

    // Il posto per il ritirato dev'essere libero: se il message thread non ha
    // ancora raccolto il precedente, cedere qui significherebbe distruggerlo
    // sull'audio thread. Si riprova al blocco successivo.
    if (retiredReady.load (std::memory_order_acquire))
        return false;

    retiredImpl = std::move (impl);
    impl = std::move (pendingImpl);

    pendingReady.store (false, std::memory_order_release);
    retiredReady.store (true, std::memory_order_release);

    // Il nuovo rilevatore parte vuoto: nessuna stima finche' non ha riempito
    // la sua finestra. Il ramo "segnale presente ma pitch non confidente" di
    // PhraseScheduler tiene intanto l'ultimo voicing, quindi non si apre un
    // buco (vedi PhraseScheduler::process).
    currentMidiNote = -1.0f;
    currentConfidence = 0.0f;
    return true;
}

void PitchDetector::collectGarbage()
{
    if (! retiredReady.load (std::memory_order_acquire))
        return;

    retiredImpl.reset(); // distruzione sul message thread
    retiredReady.store (false, std::memory_order_release);
}

bool PitchDetector::hasStableSignal() const noexcept
{
    return currentMidiNote >= 0.0f && currentConfidence >= confidenceThreshold;
}
