#include "PhraseScheduler.h"
#include <algorithm>

void PhraseScheduler::prepare (int hardSlotCapacity, double sampleRate, int maxBlockSize, int stabilityLevel)
{
    voicePool.prepare (hardSlotCapacity, sampleRate, maxBlockSize, stabilityLevel);
    phrases.assign ((size_t) hardSlotCapacity, Phrase {});
    currentVoiceCap = hardSlotCapacity;
    ageCounter = 0;
    numActiveSlotsLastBlock = 0;
}

void PhraseScheduler::reset()
{
    voicePool.reset();
    for (auto& p : phrases)
        p = Phrase {};
    ageCounter = 0;
    numActiveSlotsLastBlock = 0;
}

void PhraseScheduler::setVoiceMode (int harmonicVoiceIndex, ShiftMode mode)
{
    // Fix/Move e' una proprieta' della COLONNA armonica (0-7), non della
    // singola frase: si applica a qualunque slot fisico che in questo
    // momento la stia interpretando, in qualunque frase attiva si trovi.
    for (auto& p : phrases)
    {
        if (! p.active)
            continue;
        const int slot = p.slotIndices[(size_t) harmonicVoiceIndex];
        if (slot >= 0)
            voicePool.getSlot (slot).setMode (mode);
    }
}

void PhraseScheduler::setGlideTimeMs (float ms)
{
    for (int i = 0; i < voicePool.getNumSlots(); ++i)
        voicePool.getSlot (i).setGlideTimeMs (ms);
}

void PhraseScheduler::setFormantSpread (float spread)
{
    for (int i = 0; i < voicePool.getNumSlots(); ++i)
        voicePool.getSlot (i).setFormantSpread (spread);
}

void PhraseScheduler::setVoiceFormantOffset (int harmonicVoiceIndex, float semitones)
{
    // Stessa logica di setVoiceMode: e' una proprieta' della COLONNA
    // armonica, non della singola frase — si applica a qualunque slot
    // fisico che in questo momento la stia interpretando.
    for (auto& p : phrases)
    {
        if (! p.active)
            continue;
        const int slot = p.slotIndices[(size_t) harmonicVoiceIndex];
        if (slot >= 0)
            voicePool.getSlot (slot).setFormantOffsetSemitones (semitones);
    }
}

bool PhraseScheduler::isSlotInUse (int slotIndex) const
{
    for (auto& p : phrases)
        if (p.active)
            for (int v = 0; v < harmony::numVoices; ++v)
                if (p.slotIndices[(size_t) v] == slotIndex)
                    return true;
    return false;
}

void PhraseScheduler::hardFreePhrase (Phrase& phrase)
{
    // SOLO furto d'emergenza (FR-52, vedi allocateFreeSlot): serve lo slot
    // fisico SUBITO, non c'e' tempo per una dissolvenza. Non e' un problema
    // per il click perche' il nuovo target arriva via Glide dell'offset
    // (gia' presente in Voice) sullo stesso shifter che continua a girare:
    // e' uno scivolamento di intonazione, non un salto di ampiezza.
    phrase.active = false;
    phrase.isLive = false;
    phrase.releasing = false;
    phrase.slotIndices.fill (-1);
}

void PhraseScheduler::beginRelease (Phrase& phrase)
{
    // Caso normale (fine frase per silenzio, o superata da un nuovo onset):
    // vedi Phrase.h. La frase resta active=true (isSlotInUse() continua a
    // proteggerla) finche' process() non la vede completamente sfumata.
    phrase.isLive = false;
    phrase.releasing = true;
}

void PhraseScheduler::freeAllPhrases()
{
    for (auto& p : phrases)
        if (p.active)
            beginRelease (p);
}

int PhraseScheduler::allocateFreeSlot()
{
    const int numSlots = juce::jmin (currentVoiceCap, voicePool.getNumSlots());

    for (int i = 0; i < numSlots; ++i)
        if (! isSlotInUse (i))
            return i;

    // Nessuno slot libero: preferisci rubare una frase gia' in rilascio
    // (sta gia' sfumando verso il silenzio — interromperla e' meno
    // disruptivo che interrompere una frase ancora piena); altrimenti ruba
    // la frase attiva piu' vecchia PER INTERO (FR-52), non singole voci.
    Phrase* victim = nullptr;
    for (auto& p : phrases)
        if (p.active && p.releasing && (victim == nullptr || p.age < victim->age))
            victim = &p;

    if (victim == nullptr)
        for (auto& p : phrases)
            if (p.active && (victim == nullptr || p.age < victim->age))
                victim = &p;

    if (victim == nullptr)
        return -1;

    hardFreePhrase (*victim);

    for (int i = 0; i < numSlots; ++i)
        if (! isSlotInUse (i))
            return i;

    return -1;
}

void PhraseScheduler::triggerNewPhrase (const std::array<harmony::Cell, harmony::numVoices>& offsets,
                                         int numRequestedVoices)
{
    Phrase* newPhrase = nullptr;
    for (auto& p : phrases)
    {
        if (! p.active)
        {
            newPhrase = &p;
            break;
        }
    }

    if (newPhrase == nullptr)
        return; // non dovrebbe succedere: phrases.size() == maxSimultaneousVoices

    newPhrase->frozenOffsets = offsets;
    newPhrase->age = ++ageCounter;
    newPhrase->active = true;
    newPhrase->isLive = true;
    newPhrase->slotIndices.fill (-1);

    for (int v = 0; v < harmony::numVoices; ++v)
    {
        if (v >= numRequestedVoices || ! offsets[(size_t) v].has_value())
            continue;

        const int slot = allocateFreeSlot();
        if (slot < 0)
            return; // pool esaurito anche dopo il furto: si rinuncia a questa voce

        newPhrase->slotIndices[(size_t) v] = slot;
    }
}

bool PhraseScheduler::process (const float* monoIn,
                                float* mixOutput,
                                int numSamples,
                                bool onsetDetectedThisBlock,
                                bool signalPresent,
                                bool inputIsStable,
                                int quantizedPlayedNote,
                                float continuousInputMidiNote,
                                const std::array<harmony::Cell, harmony::numVoices>& currentOffsetsForTrigger,
                                int numRequestedVoices,
                                bool applyStabilityChangeNow)
{
    const bool appliedStabilityChange = voicePool.applyPendingStabilityChangeIfSafe (applyStabilityChangeNow);
    int lateBindingsThisBlock = 0;

    if (! signalPresent)
    {
        // Vero silenzio (o segnale sotto soglia): qui, e SOLO qui, si libera
        // tutto. Deliberatamente NON legato a inputIsStable (sessione 11) —
        // vedi il commento in PhraseScheduler.h sul perche' i due segnali
        // sono distinti.
        freeAllPhrases();
    }
    else if (onsetDetectedThisBlock)
    {
        // Vedi Phrase.h per la semantica completa di Keep Tails. Con
        // keepTails=false (default) le frasi superate si liberano subito:
        // senza pattern ritmico (voci scaglionate nel tempo) non hanno mai
        // nulla "in coda", quindi non c'e' motivo di lasciarle continuare a
        // ri-armonizzare il segnale live con offset ormai vecchi (bug
        // verificato all'ascolto in sessione 10: accumulo di voci/preset
        // sovrapposti ad ogni nuovo attacco). Con keepTails=true si mantiene
        // il comportamento precedente: la frase resta viva, solo "non piu'
        // live" (smette di seguire i cambi in tempo reale, FR-46).
        for (auto& p : phrases)
        {
            if (! p.active)
                continue;

            if (keepTails)
                p.isLive = false;
            else
                beginRelease (p);
        }

        triggerNewPhrase (currentOffsetsForTrigger, numRequestedVoices);
    }
    else if (inputIsStable)
    {
        // FR-17: la frase piu' recente, se ancora "live", segue dal vivo il
        // preset/fondamentale corrente finche' la stessa nota continua a suonare.
        //
        // Sessione 12 (FR-43/45/46 — note saltate): l'onset spesso vince la
        // corsa contro il rilevatore di pitch (BACF, serve piu' periodi per
        // agganciare con confidenza). Una frase puo' quindi nascere in
        // triggerNewPhrase() con currentOffsetsForTrigger tutto vuoto (nessun
        // pitch ancora noto) -> zero slot allocati, muta per sempre, perche'
        // prima d'ora questo ramo aggiornava solo frozenOffsets senza mai
        // colmare gli slot mancanti. Qui si completa l'allocazione appena il
        // pitch diventa affidabile: stesso identico allocateFreeSlot() del
        // trigger, applicato pero' solo alle voci che hanno un valore ma non
        // hanno ancora uno slot fisico. Nessun timeout, nessuno stato nuovo:
        // se il pitch non arriva mai la frase resta a zero slot e si libera
        // normalmente alla chiusura del gate (ramo sopra).
        for (auto& p : phrases)
        {
            if (! p.active || ! p.isLive)
                continue;

            p.frozenOffsets = currentOffsetsForTrigger;

            for (int v = 0; v < harmony::numVoices; ++v)
            {
                if (v >= numRequestedVoices || ! p.frozenOffsets[(size_t) v].has_value())
                    continue;
                if (p.slotIndices[(size_t) v] >= 0)
                    continue;

                const int slot = allocateFreeSlot();
                if (slot < 0)
                    continue; // pool esaurito anche dopo il furto: si riprova al blocco successivo

                p.slotIndices[(size_t) v] = slot;
                ++lateBindingsThisBlock;
            }
        }
    }
    // else (signalPresent ma ne' onset ne' pitch confidente questo blocco):
    // non si tocca nulla. currentOffsetsForTrigger non e' affidabile in
    // questo blocco (es. durante uno scivolamento di intonazione fra due
    // note cantate legato) — si mantiene l'ultimo voicing valido invece di
    // aggiornarlo con un valore rumoroso o di liberare tutto (il segnale
    // c'e' ancora, il performer non si e' fermato).

    std::fill (mixOutput, mixOutput + numSamples, 0.0f);
    int activeCount = 0;

    for (auto& p : phrases)
    {
        if (! p.active)
            continue;

        // Sessione 12 (fix click): una frase in rilascio sfuma TUTTE le sue
        // voci a prescindere dal contenuto della cella — sta uscendo di
        // scena, non deve piu' interpretare l'armonia, solo finire di
        // sfumare quello che stava gia' suonando. Appena tutte le sue voci
        // sono silenziose (Voice::isSilent()) la frase si libera per davvero.
        if (p.releasing)
        {
            bool stillFading = false;

            for (int v = 0; v < harmony::numVoices; ++v)
            {
                const int slotIndex = p.slotIndices[(size_t) v];
                if (slotIndex < 0)
                    continue;

                auto& voice = voicePool.getSlot (slotIndex);
                voice.setMuted (true);

                if (! voice.isSilent())
                {
                    voice.processAdd (monoIn, mixOutput, numSamples, quantizedPlayedNote, continuousInputMidiNote);
                    stillFading = true;
                }
            }

            if (! stillFading)
                hardFreePhrase (p);

            continue;
        }

        for (int v = 0; v < harmony::numVoices; ++v)
        {
            const int slotIndex = p.slotIndices[(size_t) v];
            if (slotIndex < 0)
                continue;

            const auto& cell = p.frozenOffsets[(size_t) v];
            auto& voice = voicePool.getSlot (slotIndex);

            if (! cell.has_value())
            {
                // FR-17: la cella e' diventata vuota su una frase ancora
                // viva (es. cambio accordo su nota tenuta). Stessa logica di
                // rilascio delle voci sopra, ma per una singola colonna: la
                // frase resta viva, questa voce sfuma senza tagliare di netto.
                voice.setMuted (true);
                if (! voice.isSilent())
                    voice.processAdd (monoIn, mixOutput, numSamples, quantizedPlayedNote, continuousInputMidiNote);
                continue;
            }

            voice.setMuted (false);
            voice.setTargetOffsetSemitones ((float) *cell);
            voice.processAdd (monoIn, mixOutput, numSamples, quantizedPlayedNote, continuousInputMidiNote);
            ++activeCount;
        }
    }

    numActiveSlotsLastBlock.store (activeCount, std::memory_order_relaxed);
    // Diagnostica sessione 12 (FR-43/45/46): se questo sale suonando, il fix
    // dell'allocazione differita sta intervenendo davvero, non solo compilando.
    numLateBindingsTotal.fetch_add (lateBindingsThisBlock, std::memory_order_relaxed);
    return appliedStabilityChange;
}
