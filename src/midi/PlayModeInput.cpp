#include "PlayModeInput.h"

#include <algorithm>

void PlayModeInput::prepare (double sampleRate, int maxBlockSize, int stabilityLevel)
{
    voicePool.prepare (maxNotes, sampleRate, maxBlockSize, stabilityLevel);
    slotNote.fill (-1);
    warmupSamples.fill (0);
    engineIsCold.fill (true); // VoicePool::prepare ha appena resettato ogni shifter
}

void PlayModeInput::reset()
{
    voicePool.reset();
    slotNote.fill (-1);
    warmupSamples.fill (0);
    engineIsCold.fill (true); // VoicePool::reset chiama Voice::reset -> shifter->reset()
}

bool PlayModeInput::process (const juce::MidiBuffer& midi,
                              int midiChannel,
                              bool modeActive,
                              const float* monoIn,
                              float* mixL,
                              float* mixR,
                              int numSamples,
                              bool inputIsStable,
                              float continuousInputMidiNote,
                              bool applyStabilityChangeNow)
{
    const bool appliedStabilityChange = voicePool.applyPendingStabilityChangeIfSafe (applyStabilityChangeNow);

    std::fill (mixL, mixL + numSamples, 0.0f);
    std::fill (mixR, mixR + numSamples, 0.0f);

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

            // Sessione 34: fra gli slot liberi si preferisce uno la cui voce
            // sia gia' COMPLETAMENTE spenta, non semplicemente il primo con
            // slotNote == -1.
            //
            // Al note-off lo slot torna libero subito, ma la voce impiega
            // kDeclickMs (8 ms) a spegnersi. Prendendo sempre il primo indice
            // libero, una nota ribattuta in quella finestra riprendeva
            // sistematicamente lo STESSO slot ancora in dissolvenza: li'
            // Voice::justReactivated non viene armato (si arma solo su una
            // voce isSilent(), vedi Voice.h) e l'intonazione SCIVOLA dalla
            // nota vecchia alla nuova in glideTimeMs — 30 ms di default —
            // invece di scattare. Misurato in PM-6: a +10 ms dal ri-attacco
            // suonava ancora la nota rilasciata, e il bersaglio si
            // raggiungeva solo dopo ~60 ms. Su un attacco MIDI netto e' un
            // glissato che nessuno ha chiesto, e tronca a meta' la
            // dissolvenza della nota precedente.
            //
            // Se tutti gli slot liberi stanno ancora sfumando si prende
            // comunque il primo (meglio una scivolata che una nota muta):
            // e' il caso di 8 note rilasciate e ripremute entro 8 ms, e resta
            // un limite noto.
            int slotIndex = -1;
            for (int i = 0; i < maxNotes; ++i)
                if (slotNote[(size_t) i] < 0 && voicePool.getSlot (i).isSilent())
                { slotIndex = i; break; }

            if (slotIndex < 0)
                for (int i = 0; i < maxNotes; ++i)
                    if (slotNote[(size_t) i] < 0)
                    { slotIndex = i; break; }

            if (slotIndex < 0)
                continue; // FR-25: oltre 8 note simultanee, le eccedenti restano mute

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

    for (int i = 0; i < maxNotes; ++i)
    {
        auto& voice = voicePool.getSlot (i);

        // Tre ragioni diverse per cui questo slot non deve farsi sentire,
        // trattate insieme perche' richiedono esattamente lo stesso
        // comportamento (prima erano tre rami identici copiati):
        //   - FR-24: la modalita' Play non e' attiva. Le voci eventualmente
        //     ancora udibili (transizione Harmonizer<->Play, FR-28) devono
        //     sfumare invece di tagliare di netto (sessione 12, fix click).
        //   - nessuna nota premuta su questo slot.
        //   - FR-20: senza un ingresso audio stabile il pitch shifter non ha
        //     nulla di coerente da trasporre. La nota resta "premuta"
        //     (slotNote non cambia), solo il suo audio tace finche' l'ingresso
        //     non torna stabile — coerente con lo schema del prodotto (Play
        //     richiede comunque una sorgente audio viva da pitchare, non e' un
        //     sintetizzatore: vedi il setup di riferimento del PRD, "traccia
        //     MIDI vuota + traccia audio").
        const bool wantsSound = modeActive && slotNote[(size_t) i] >= 0 && inputIsStable;

        if (! wantsSound)
        {
            voice.setMuted (true);

            if (! voice.isSilent())
            {
                // Dissolvenza ancora in corso: il motore continua a ricevere
                // campioni, quindi resta caldo.
                voice.processAdd (monoIn, mixL, mixR, numSamples, /*quantizedPlayedNote*/ 0, continuousInputMidiNote);
            }
            else if (! engineIsCold[(size_t) i])
            {
                // Da qui in poi nessuno rifornisce piu' questo motore. E' il
                // punto giusto per il reset, esattamente come PhraseScheduler
                // lo fa quando uno slot fisico torna al pool (B-04, Voice.h/
                // goCold): senza, il ring resta pieno della coda di questa
                // nota e la PROSSIMA nota su questo slot comincia suonando
                // lei, all'intonazione di questa (misurato in PM-4 prima del
                // fix). engineIsCold garantisce che scatti una volta sola.
                voice.goCold();
                engineIsCold[(size_t) i] = true;
            }

            // Il riscaldamento dovuto e' una CONSEGUENZA dello stato del
            // motore, non un evento da registrare al note-on: cosi' il ciclo
            // di parsing MIDI qui sopra resta ignaro di tutto questo, e il
            // conto e' giusto anche quando la nota resta premuta ma
            // l'ingresso perde stabilita' (FR-20) — al ritorno della
            // stabilita' il motore va riscaldato di nuovo.
            warmupSamples[(size_t) i] = engineIsCold[(size_t) i] ? voice.getLatencySamples() : 0;
            continue;
        }

        if (warmupSamples[(size_t) i] > 0)
        {
            // B-15, stesso meccanismo del fix di B-12: la voce non contribuisce
            // ancora al mix e si limita ad alimentare il motore. Quando il
            // conto arriva a zero, il blocco successivo trova un motore gia'
            // pronto e la dissolvenza degli 8 ms coincide con segnale vero.
            // L'uscita del riscaldamento e' scartata dentro Voice.
            //
            // setMuted(false) va chiamato GIA' QUI, non alla fine del
            // riscaldamento: e' lui ad armare Voice::justReactivated, ed e'
            // justReactivated ad agganciare l'intonazione al bersaglio invece
            // di farcela scivolare sopra in glideTimeMs. Chiamandolo dopo, il
            // motore passerebbe tutto il riscaldamento a sintetizzare in
            // anticipo l'intonazione VECCHIA, e la giunzione con quella giusta
            // cadrebbe a guadagno pieno — il residuo misurato in PM-3
            // (2.3-5.1 volte il regime) che questa versione elimina.
            //
            // Non fa entrare la voce in anticipo: ampGlide avanza solo dentro
            // processAdd (Glide::processRamp vive li'), quindi resta fermo a
            // zero per tutto il riscaldamento e la rampa degli 8 ms comincia
            // davvero al primo processAdd. La chiamata e' idempotente: dal
            // secondo blocco in poi isSilent() e' gia' falso e il target di
            // ampGlide e' gia' 1, quindi non riarma nulla.
            voice.setMuted (false);
            warmupSamples[(size_t) i] -= numSamples;
            voice.processWarmOnly (monoIn, numSamples, /*quantizedPlayedNote*/ 0, continuousInputMidiNote);
            engineIsCold[(size_t) i] = false;
            continue;
        }

        voice.setMuted (false);
        voice.processAdd (monoIn, mixL, mixR, numSamples, /*quantizedPlayedNote*/ 0, continuousInputMidiNote);
        engineIsCold[(size_t) i] = false;
    }

    return appliedStabilityChange;
}
