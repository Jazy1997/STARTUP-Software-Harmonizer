#include "PhraseScheduler.h"
#include "EmptyCellHold.h"
#include <algorithm>
#include <cmath>

void PhraseScheduler::prepare (int hardSlotCapacity, double sampleRate, int maxBlockSize, int stabilityLevel)
{
    voicePool.prepare (hardSlotCapacity, sampleRate, maxBlockSize, stabilityLevel);
    phrases.assign ((size_t) hardSlotCapacity, Phrase {});
    currentVoiceCap = hardSlotCapacity;
    ageCounter = 0;
    numActiveSlotsLastBlock = 0;
    emptyCellHoldSamples = (int) std::lround (kEmptyCellHoldMs * sampleRate / 1000.0);
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

void PhraseScheduler::setVoiceGainLinear (int harmonicVoiceIndex, float gainLinear)
{
    // Stessa logica di setVoiceFormantOffset: proprieta' della COLONNA
    // armonica, applicata a qualunque slot fisico la stia interpretando ora.
    for (auto& p : phrases)
    {
        if (! p.active)
            continue;
        const int slot = p.slotIndices[(size_t) harmonicVoiceIndex];
        if (slot >= 0)
            voicePool.getSlot (slot).setGainLinear (gainLinear);
    }
}

void PhraseScheduler::setVoicePan (int harmonicVoiceIndex, float pan)
{
    for (auto& p : phrases)
    {
        if (! p.active)
            continue;
        const int slot = p.slotIndices[(size_t) harmonicVoiceIndex];
        if (slot >= 0)
            voicePool.getSlot (slot).setPan (pan);
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
    // Non tocca MAI lo stato dei Voice delle slot che sta liberando —
    // deliberatamente, perche' e' chiamata da due contesti con esigenze
    // opposte (vedi i due call site):
    //   - furto d'emergenza (FR-52, allocateFreeSlot): serve lo slot fisico
    //     SUBITO, non c'e' tempo per una dissolvenza. Non e' un problema per
    //     il click perche' il nuovo target arriva via Glide dell'offset
    //     (gia' presente in Voice) sullo stesso shifter che CONTINUA A
    //     GIRARE: e' uno scivolamento di intonazione, non un salto di
    //     ampiezza. Un goCold() qui reintrodurrebbe esattamente il buco di
    //     sessione 27 (vedi Voice.h) sulla voce rubata.
    //   - fine naturale di una frase in rilascio (process(), sotto): li' il
    //     chiamante ha GIA' verificato che tutte le voci sono isSilent() e
    //     ha gia' chiamato goCold() su ciascuna, PRIMA di questa funzione —
    //     e' il punto giusto per il reset (slot davvero restituito al pool,
    //     nessuno lo rifornira' piu').
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
    newPhrase->emptyCellSamples.fill (0);
    newPhrase->warmupSamples.fill (0); // attacco di nota: nessun riscaldamento, vedi B-12

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
                // B-12: voce AGGIUNTA a una frase gia' in suono. Il motore di
                // questo slot e' freddo e restera' muto per la sua latenza
                // dichiarata; senza questa attesa la dissolvenza d'ingresso si
                // consumerebbe dentro quel silenzio e la voce entrerebbe di
                // netto. Vedi Phrase.h. Il ramo del trigger (frase nuova) NON
                // fa questo: li' e' l'attacco di nota, gia' validato.
                p.warmupSamples[(size_t) v] = voicePool.getSlot (slot).getLatencySamples();
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

    std::fill (mixL, mixL + numSamples, 0.0f);
    std::fill (mixR, mixR + numSamples, 0.0f);
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
                    voice.processAdd (monoIn, mixL, mixR, numSamples, quantizedPlayedNote, continuousInputMidiNote);
                    stillFading = true;
                }
            }

            if (! stillFading)
            {
                // Tutte le voci di questa frase sono confermate isSilent():
                // lo slot fisico sta per essere restituito al pool per
                // davvero, nessuno lo rifornira' piu'. E' il punto giusto
                // per il reset esplicito (sessione 27, vedi Voice.h/
                // PhraseScheduler::hardFreePhrase) — a differenza del furto
                // d'emergenza sotto (allocateFreeSlot), qui non c'e' alcuna
                // dissolvenza in corso da interrompere.
                for (int v = 0; v < harmony::numVoices; ++v)
                {
                    const int slotIndex = p.slotIndices[(size_t) v];
                    if (slotIndex >= 0)
                        voicePool.getSlot (slotIndex).goCold();
                }
                hardFreePhrase (p);
            }

            continue;
        }

        for (int v = 0; v < harmony::numVoices; ++v)
        {
            const int slotIndex = p.slotIndices[(size_t) v];
            if (slotIndex < 0)
                continue;

            auto& voice = voicePool.getSlot (slotIndex);

            // FR-19 (sessione 30, B-10): "le voci oltre il numero selezionato
            // sono mute anche se il preset contiene offset per loro". Le due
            // guardie `v >= numRequestedVoices` gia' presenti (triggerNewPhrase
            // e il late-binding sopra) sono puramente ALLOCATIVE: sanno solo
            // aggiungere slot. Senza questo ramo, abbassare il selettore su una
            // frase gia' in suono non spegneva nulla — la riga setMuted(false)
            // in fondo al loop continuava a tenere accese le colonne in
            // eccesso, e tornavano a tacere solo alla morte della frase
            // (freeAllPhrases, cioe' solo a segnale assente: "ferma l'audio,
            // aspetta il silenzio, riparti"). Alzare funzionava invece subito,
            // ed e' esattamente l'asimmetria riportata dall'utente.
            // Letto con FR-17 (un cambio su nota tenuta si applica SUBITO,
            // senza ribattere), il verdetto va dato ogni blocco, qui.
            //
            // Stessa forma del ramo `releasing` sopra, che e' il modo gia'
            // provato di far uscire di scena una voce: dissolvenza di ampiezza
            // (kDeclickMs, anti-click di sessione 12/13 — B-03), e solo a
            // silenzio CONFERMATO il goCold() e la restituzione dello slot
            // (sessione 27/B-04: il reset va fatto quando nessuno rifornira'
            // piu' il motore, non prima). Liberare slotIndices[v] fa smettere
            // isSlotInUse() di proteggere lo slot: torna davvero al pool, cosi'
            // abbassare le voci abbassa davvero la CPU. Se il selettore risale,
            // il late-binding sopra rilega lo slot al blocco successivo.
            if (v >= numRequestedVoices)
            {
                voice.setMuted (true);

                if (! voice.isSilent())
                {
                    voice.processAdd (monoIn, mixL, mixR, numSamples, quantizedPlayedNote, continuousInputMidiNote);
                }
                else
                {
                    voice.goCold();
                    p.slotIndices[(size_t) v] = -1;
                    p.emptyCellSamples[(size_t) v] = 0;
                    p.warmupSamples[(size_t) v] = 0;
                }

                continue; // non conta come voce attiva (FR-53)
            }

            // B-12: riscaldamento di una voce appena AGGIUNTA a questa frase.
            // Resta muta (ampGlide fermo a zero, nessuna dissolvenza sprecata)
            // e si limita ad alimentare il motore, esattamente come fa il ramo
            // della cella vuota qui sotto per non lasciarlo affamato. Quando
            // il conto arriva a zero, il blocco successivo trova un motore gia'
            // pronto e la dissolvenza degli 8 ms coincide con segnale vero.
            if (p.warmupSamples[(size_t) v] > 0)
            {
                p.warmupSamples[(size_t) v] -= numSamples;
                voice.processWarmOnly (monoIn, numSamples, continuousInputMidiNote);
                continue; // non conta come voce attiva finche' non si sente
            }

            const auto& cell = p.frozenOffsets[(size_t) v];

            if (! cell.has_value())
            {
                // FR-17 (sessione 28 — "ribattuto", vedi
                // src/voices/EmptyCellHold.h): la cella e' vuota questo
                // blocco, ma non si muta ISTANTANEAMENTE — Fase 0 (LOG/archivio-s01-s28.md
                // sessione 28) ha misurato che un passaggio breve attraverso
                // un grado non compilato, sulla STESSA frase viva (nessun
                // ri-attacco, nessun cambio di slot), produce con un mute
                // immediato un ciclo fade-out/fade-in udibile come "la
                // stessa nota suona due volte" — non il buco di sessione 27
                // (gia' risolto), un difetto diverso. Sotto soglia la voce
                // resta com'era (ultimo target valido, nessun mute): quando
                // la melodia arriva al prossimo grado compilato il target
                // scatta direttamente su quello, mai "sporcato" da un
                // target intermedio (esattamente la richiesta dell'utente).
                auto& emptySamples = p.emptyCellSamples[(size_t) v];
                const auto hold = stepEmptyCellHold (emptySamples, numSamples, false, emptyCellHoldSamples);
                emptySamples = hold.emptySamplesAfter;

                if (! hold.shouldMuteNow)
                {
                    // Ancora dentro la finestra di attesa: nessun mute,
                    // nessun ri-target — stesso identico trattamento di una
                    // voce con cella piena, solo senza toccare il target.
                    voice.processAdd (monoIn, mixL, mixR, numSamples, quantizedPlayedNote, continuousInputMidiNote);
                    ++activeCount;
                    continue;
                }

                // Soglia superata: stesso comportamento di sessione 27, solo
                // spostato in avanti di emptyCellHoldSamples campioni.
                voice.setMuted (true);
                if (! voice.isSilent())
                    voice.processAdd (monoIn, mixL, mixR, numSamples, quantizedPlayedNote, continuousInputMidiNote);
                else
                    // La frase e' ancora viva, quindi questo stesso slot
                    // fisico puo' tornare a servire questa colonna armonica
                    // al prossimo grado compilato — senza questa chiamata il
                    // motore resterebbe AFFAMATO per tutta la durata della
                    // cella vuota e produrrebbe un buco pari alla sua
                    // latenza dichiarata alla riattivazione (misurato su
                    // materiale reale, vedi LOG/archivio-s01-s28.md sessione 27 e
                    // Voice.h). Nessun contributo al mix: l'uscita di
                    // processWarmOnly e' scartata.
                    voice.processWarmOnly (monoIn, numSamples, continuousInputMidiNote);
                continue;
            }

            p.emptyCellSamples[(size_t) v] = 0; // ritorno a un grado compilato: azzera subito l'attesa
            voice.setMuted (false);
            voice.setTargetOffsetSemitones ((float) *cell);
            voice.processAdd (monoIn, mixL, mixR, numSamples, quantizedPlayedNote, continuousInputMidiNote);
            ++activeCount;
        }
    }

    numActiveSlotsLastBlock.store (activeCount, std::memory_order_relaxed);
    // Diagnostica sessione 12 (FR-43/45/46): se questo sale suonando, il fix
    // dell'allocazione differita sta intervenendo davvero, non solo compilando.
    numLateBindingsTotal.fetch_add (lateBindingsThisBlock, std::memory_order_relaxed);
    return appliedStabilityChange;
}
