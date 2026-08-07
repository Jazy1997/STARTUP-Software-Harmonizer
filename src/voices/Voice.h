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

    // Sessione 12 (fix scricchiolii/click segnalati dall'utente): muted non
    // e' piu' un interruttore che azzera l'uscita di netto — avvia/ferma una
    // breve dissolvenza di ampiezza (ampGlide, durata fissa kDeclickMs, NON
    // legata al glideTimeMs musicale di FR-17: qui serve un tempo costante
    // e breve anti-click, non un tempo di transizione armonica scelto
    // dall'utente). isSilent() dice al chiamante quando puo' smettere di
    // chiamare processAdd senza sentire un salto.
    void setMuted (bool shouldBeMuted) noexcept
    {
        // Sessione 14 (click "a inizio nota", specialmente su note legate):
        // finche' la voce resta silenziosa, processAdd() non chiama piu'
        // shifter->process() (vedi sotto) — il PitchShifter resta congelato
        // con qualunque contenuto avesse nella sua pipeline interna in quel
        // momento (buffer PSOLA, epoch). La dissolvenza anti-click dura solo
        // kDeclickMs (8ms), ma la latenza dichiarata del motore e' SEMPRE
        // piu' lunga (13.6-30ms secondo Stability): la pipeline non fa in
        // tempo a svuotarsi del tutto. Se lo stesso slot fisico viene poi
        // riassegnato a una nuova nota/frase (routine, mai successo un reset
        // fra l'una e l'altra prima d'ora), i primi campioni della nuova
        // nota sono in parte ancora contenuto residuo di quella precedente.
        // Fix: alla transizione silenzio->attiva (non a ogni chiamata: la
        // maggior parte delle chiamate qui non cambia nulla), si resetta lo
        // stato interno dello shifter — verificato numericamente in
        // tests/psola_test.cpp Test 9 (senza reset: scostamento misurabile
        // dal riferimento pulito; con reset: uscita bit-per-bit identica a
        // uno slot mai usato prima). SMENTITO ALL'ASCOLTO dall'utente in
        // sessione 14: il meccanismo resta vero e misurato, ma non era la
        // causa (o non l'unica) del click sentito — vedi handsoff.md
        // sessione 16 per la causa confermata sotto (offsetGlide).
        if (! shouldBeMuted && isSilent())
        {
            if (shifter != nullptr)
                shifter->reset();

            // Sessione 16 (ripartenza da zero sul click dopo che il fix di
            // sessione 14 e' stato smentito all'ascolto — vedi handsoff.md
            // §6): misurato con tests/voice_test.cpp che l'inviluppo di
            // ampiezza NON ha discontinuita' alla riattivazione — la causa
            // reale e' che offsetGlide non veniva mai "agganciato" al nuovo
            // target quando uno slot silenzioso viene riassegnato:
            // l'intonazione della voce restava quella della nota
            // PRECEDENTE su questo stesso slot fisico e ci scivolava sopra
            // in glideTimeMs (FR-17, 30ms di default), invece di scattare
            // subito. Confermato per calcolo con voice_test.cpp
            // (scostamento di ~195 cent dal nuovo target appena dopo il
            // riattacco, prima di questo fix).
            //
            // justReactivated segnala la transizione a processAdd(), che la
            // consuma alla prima chiamata utile agganciando offsetGlide al
            // target CORRENTE (qualunque esso sia in quel momento — non
            // serve conoscerlo qui): funziona sia quando il chiamante
            // imposta il nuovo target PRIMA di chiamare setMuted(false)
            // (PhraseScheduler.cpp) sia quando lo imposta DOPO, in un
            // blocco successivo (PlayModeInput.cpp: target al note-on,
            // setMuted solo quando il segnale torna stabile) — vedi
            // Voice.cpp.
            justReactivated = true;
        }

        muted = shouldBeMuted;
        ampGlide.setTarget (shouldBeMuted ? 0.0f : 1.0f);
    }
    bool isMuted() const noexcept { return muted; }
    // Vero solo quando muted E la dissolvenza ha davvero raggiunto zero: da
    // qui in poi e' sicuro smettere di chiamare processAdd (nessuna dissol-
    // venza ancora in corso, nessun salto udibile se il chiamante si ferma).
    bool isSilent() const noexcept { return muted && ampGlide.isSettled(); }

    void setMode (ShiftMode newMode) noexcept { mode = newMode; }
    void setGlideTimeMs (float ms) noexcept { offsetGlide.setGlideTimeMs (ms); }

    // FR-40: quanto la correzione formantica automatica (FR-39) influisce,
    // 0 = nulla, 1 = piena formula. FR-41: offset manuale in semitoni-
    // equivalenti, indipendente per voce, sommato alla correzione automatica
    // (vedi processAdd). Entrambi globali/per-colonna, non per-frase: la
    // stessa colonna armonica (0-7) li applica a qualunque slot fisico la
    // stia interpretando in questo momento — stesso schema di setMode().
    void setFormantSpread (float spread) noexcept { formantSpread = spread; }
    void setFormantOffsetSemitones (float semitones) noexcept { formantOffsetSemitones = semitones; }

    // FR-11/§8.1: gain e pan per voce, proprieta' della colonna armonica
    // (0-7) esattamente come formantOffsetSemitones sopra — si applicano a
    // qualunque slot fisico la stia interpretando in questo momento (vedi
    // PhraseScheduler::setVoiceGainLinear/setVoicePan). Entrambi passano da
    // un Glide a kDeclickMs: un salto di gain o pan non rampato clicca
    // esattamente come i salti di ampiezza delle sessioni 12/13 — stesso
    // meccanismo, stessa costante di tempo, nessun nuovo tipo di rischio.
    // gainLinear e' gia' lineare quando arriva qui: la conversione da dB
    // (unita' del parametro APVTS) avviene in PluginProcessor::processBlock,
    // cosi' Voice/voice_test restano privi di dipendenze JUCE.
    void setGainLinear (float gainLinear) noexcept { gainGlide.setTarget (gainLinear); }
    // pan in -1..+1 (-1 = tutto a sinistra, 0 = centro, +1 = tutto a destra).
    void setPan (float pan) noexcept { panGlide.setTarget (pan); }

    // Offset armonico grezzo (semitoni) prima del glide (FR-17): la rampa
    // verso il nuovo valore avviene dentro processAdd, non qui. Imposta
    // sempre e solo il TARGET — se questa chiamata segue una riattivazione
    // da silenzio (justReactivated), e' processAdd() a decidere se agganciare
    // subito invece di far scivolare la rampa (vedi Voice.cpp): questo
    // metodo non ha bisogno di saperlo, funziona identico in entrambi i casi
    // e per qualunque ordine di chiamata rispetto a setMuted().
    void setTargetOffsetSemitones (float semitones) noexcept { offsetGlide.setTarget (semitones); }

    int getLatencySamples() const;

    // Scambio senza allocazione/deallocazione (std::unique_ptr::swap e'
    // noexcept e non invoca il deleter): newShifter in ingresso diventa lo
    // shifter attivo, quello precedente esce in newShifter. Il chiamante
    // (VoicePool, sull'audio thread) deve poi far distruggere il vecchio
    // shifter sul message thread, mai qui — vedi PRD §9.4.
    void swapShifterNoAlloc (std::unique_ptr<PitchShifter>& shifterInOut) noexcept;

    // Somma il segnale shiftato di questa voce dentro mixL/mixR (gia'
    // inizializzati dal chiamante) — §8.1: percorso wet stereo, gain/pan per
    // voce (vedi setGainLinear/setPan sopra). Non-op se la voce e' muta.
    // quantizedPlayedNote / continuousInputMidiNote servono alla modalita' Fix.
    void processAdd (const float* monoIn, float* mixL, float* mixR, int numSamples,
                      int quantizedPlayedNote, float continuousInputMidiNote);

private:
    std::unique_ptr<PitchShifter> shifter;
    std::vector<float> scratch;
    Glide offsetGlide;
    Glide ampGlide; // sessione 12: dissolvenza di ampiezza anti-click, vedi setMuted/isSilent
    Glide gainGlide; // FR-11: gain utente per voce, lineare, di default 1.0
    Glide panGlide;  // FR-11: pan utente per voce, -1..+1, di default 0.0 (centro)
    static constexpr float kDeclickMs = 8.0f;
    ShiftMode mode = ShiftMode::move;
    bool muted = true;
    // Sessione 16: vero dalla transizione silenzio->attiva (impostato in
    // setMuted) fino a quando processAdd() lo consuma agganciando offsetGlide
    // al target corrente invece di farlo scivolare — vedi setMuted/processAdd.
    bool justReactivated = false;
    float formantSpread = 1.0f;         // FR-39 "attiva di default": formula a piena forza
    float formantOffsetSemitones = 0.0f;
};
