#include "PlayModeInput.h"

#include <algorithm>

void PlayModeInput::prepare (double sampleRate, int maxBlockSize, int stabilityLevel)
{
    voicePool.prepare (maxNotes, sampleRate, maxBlockSize, stabilityLevel);
    slotNote.fill (-1);
}

void PlayModeInput::reset()
{
    voicePool.reset();
    slotNote.fill (-1);
}

bool PlayModeInput::process (const juce::MidiBuffer& midi,
                              int midiChannel,
                              bool modeActive,
                              const float* monoIn,
                              float* mixOutput,
                              int numSamples,
                              bool inputIsStable,
                              float continuousInputMidiNote,
                              bool applyStabilityChangeNow)
{
    const bool appliedStabilityChange = voicePool.applyPendingStabilityChangeIfSafe (applyStabilityChangeNow);

    std::fill (mixOutput, mixOutput + numSamples, 0.0f);

    // Il tracking delle note premute resta sempre aggiornato, anche a
    // modalita' spenta: vedi il commento su modeActive nell'header.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (! (message.isNoteOn() || message.isNoteOff()))
            continue;

        if (midiChannel != 0 && message.getChannel() != midiChannel)
            continue;

        const int note = message.getNoteNumber();

        if (message.isNoteOn())
        {
            // Nota gia' assegnata a uno slot (ribattuta senza note-off, o
            // doppio note-on): non rubare un secondo slot per la stessa nota.
            if (std::find (slotNote.begin(), slotNote.end(), note) != slotNote.end())
                continue;

            const auto freeSlot = std::find (slotNote.begin(), slotNote.end(), -1);
            if (freeSlot == slotNote.end())
                continue; // FR-25: oltre 8 note simultanee, le eccedenti restano mute

            const int slotIndex = (int) std::distance (slotNote.begin(), freeSlot);
            slotNote[(size_t) slotIndex] = note;

            // FR-22 riusato con bersaglio assoluto = nota MIDI:
            // quantizedPlayedNote=0 (passato in processAdd sotto) + questo
            // offset = la nota voluta, senza bisogno di una terza modalita'
            // di calcolo dentro Voice.
            auto& voice = voicePool.getSlot (slotIndex);
            voice.setMode (ShiftMode::fix);
            voice.setTargetOffsetSemitones ((float) note);
        }
        else // note off
        {
            const auto it = std::find (slotNote.begin(), slotNote.end(), note);
            if (it != slotNote.end())
                slotNote[(size_t) std::distance (slotNote.begin(), it)] = -1;
        }
    }

    if (! modeActive)
    {
        // FR-24: nessun contributo NUOVO, ma le voci eventualmente ancora
        // udibili (transizione Harmonizer<->Play, FR-28) devono sfumare
        // invece di tagliare di netto (sessione 12, fix click) — stesso
        // principio di PhraseScheduler::process. mixOutput e' gia' azzerato
        // sopra: qui si somma solo la coda in dissolvenza, se c'e'.
        for (int i = 0; i < maxNotes; ++i)
        {
            auto& voice = voicePool.getSlot (i);
            voice.setMuted (true);
            if (! voice.isSilent())
                voice.processAdd (monoIn, mixOutput, numSamples, /*quantizedPlayedNote*/ 0, continuousInputMidiNote);
        }
        return appliedStabilityChange;
    }

    for (int i = 0; i < maxNotes; ++i)
    {
        auto& voice = voicePool.getSlot (i);

        if (slotNote[(size_t) i] < 0)
        {
            // Nessuna nota premuta su questo slot: se sta ancora sfumando
            // da un rilascio precedente, lascialo finire (sessione 12).
            voice.setMuted (true);
            if (! voice.isSilent())
                voice.processAdd (monoIn, mixOutput, numSamples, /*quantizedPlayedNote*/ 0, continuousInputMidiNote);
            continue;
        }

        // FR-20 (si applica anche qui, non solo in Harmonizer): senza un
        // ingresso audio stabile il pitch shifter non ha nulla di coerente
        // da trasporre. La nota resta "premuta" (slotNote non cambia), solo
        // il suo audio tace finche' l'ingresso non torna stabile — coerente
        // con lo schema del prodotto (Play richiede comunque una sorgente
        // audio viva da pitchare, non e' un sintetizzatore: vedi il setup
        // di riferimento del PRD, "traccia MIDI vuota + traccia audio").
        if (! inputIsStable)
        {
            voice.setMuted (true);
            if (! voice.isSilent())
                voice.processAdd (monoIn, mixOutput, numSamples, /*quantizedPlayedNote*/ 0, continuousInputMidiNote);
            continue;
        }

        voice.setMuted (false);
        voice.processAdd (monoIn, mixOutput, numSamples, /*quantizedPlayedNote*/ 0, continuousInputMidiNote);
    }

    return appliedStabilityChange;
}
