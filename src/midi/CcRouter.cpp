#include "CcRouter.h"

OverrideManager::CcEvents CcRouter::process (const juce::MidiBuffer& midi) noexcept
{
    OverrideManager::CcEvents events;

    const int channel          = midiChannel.load (std::memory_order_relaxed);
    const int currentRootCc    = rootCc.load (std::memory_order_relaxed);
    const int currentPresetCc  = presetCc.load (std::memory_order_relaxed);
    const int currentBypassCc  = bypassCc.load (std::memory_order_relaxed);

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;

        const int ccNumber = message.getControllerNumber();
        const int ccValue  = message.getControllerValue();

        const auto learning = (LearnTarget) learnTarget.load (std::memory_order_relaxed);
        if (learning != LearnTarget::none)
        {
            // Il MIDI Learn ignora il filtro canale: si impara da
            // QUALUNQUE controller fisico stia inviando ora, prima ancora
            // di aver deciso il canale su cui filtrare in seguito.
            switch (learning)
            {
                case LearnTarget::root:   rootCc.store (ccNumber, std::memory_order_relaxed); break;
                case LearnTarget::preset: presetCc.store (ccNumber, std::memory_order_relaxed); break;
                case LearnTarget::bypass: bypassCc.store (ccNumber, std::memory_order_relaxed); break;
                case LearnTarget::none:   break;
            }
            learnTarget.store ((int) LearnTarget::none, std::memory_order_relaxed);
            continue; // il CC che ha appena insegnato il numero non e' anche un evento
        }

        if (channel != 0 && message.getChannel() != channel)
            continue;

        if (ccNumber == currentRootCc)
        {
            // FR-30: 1-12 validi (1=C..12=B); 0 e valori >12 ignorati.
            if (ccValue >= 1 && ccValue <= 12)
                events.root = { true, ccValue - 1 };
        }
        else if (ccNumber == currentPresetCc)
        {
            // FR-30: 1-N, posizionale; 0 ignorato (non esiste posizione 0).
            // Il clamp sulla lunghezza attuale della libreria avviene a
            // valle, nello stesso punto che gia' clampa il parametro APVTS
            // (stesso comportamento richiesto per i valori oltre la lista).
            if (ccValue >= 1)
                events.preset = { true, ccValue };
        }
        else if (ccNumber == currentBypassCc)
        {
            // FR-30: 0-63 = off, 64-127 = on (soglia del pedale sustain).
            events.bypass = { true, ccValue >= 64 ? 1 : 0 };
        }
    }

    return events;
}
