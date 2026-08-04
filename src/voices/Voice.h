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
    Glide ampGlide; // sessione 12: dissolvenza di ampiezza anti-click, vedi setMuted/isSilent
    static constexpr float kDeclickMs = 8.0f;
    ShiftMode mode = ShiftMode::move;
    bool muted = true;
    float formantSpread = 1.0f;         // FR-39 "attiva di default": formula a piena forza
    float formantOffsetSemitones = 0.0f;
};
