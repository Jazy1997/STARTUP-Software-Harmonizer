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
        // shifter->process() — il PitchShifter resta congelato con
        // qualunque contenuto avesse nella sua pipeline interna in quel
        // momento (buffer PSOLA, epoch). Fino a sessione 26 questo metodo
        // resettava lo shifter proprio qui, alla riattivazione — ma
        // sessione 27 (feedback utente: "buchi e click a inizio nota" su un
        // preset a offset fissi con celle vuote su alcuni gradi) ha
        // misurato che quel reset, su uno slot rimasto AFFAMATO durante il
        // silenzio (cella vuota su una frase ancora viva, FR-17 — non lo
        // stesso caso della nota "precedente" su uno slot fisico
        // riassegnato), produce un buco pari all'intera latenza dichiarata
        // del motore (21ms a Balanced) prima che il segnale vero rientri:
        // il motore riparte vuoto invece di restare sincronizzato col
        // segnale in ingresso. Il reset resta corretto e necessario quando
        // lo slot fisico viene DAVVERO restituito al pool (si e' spostato
        // in goCold(), vedi sotto) — qui, alla semplice riattivazione di
        // una frase che e' rimasta viva, non deve piu' scattare: e'
        // compito del chiamante (PhraseScheduler) tenere il motore caldo
        // con processWarmOnly() per tutta la durata della cella vuota,
        // cosi' che alla riattivazione non ci sia nulla da resettare.
        if (! shouldBeMuted && isSilent())
        {
            // Sessione 16 (ripartenza da zero sul click dopo che il fix di
            // sessione 14 e' stato smentito all'ascolto — vedi LOG/archivio-s01-s28.md
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
            // Voice.cpp. Questo meccanismo e' indipendente da quello del
            // motore/reset sopra e resta invariato.
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

    // Sessione 27: tiene "caldo" il motore di una voce silenziosa la cui
    // FRASE resta pero' viva (cella vuota su un grado, FR-17) — gli passa il
    // segnale in ingresso e ne SCARTA l'uscita, senza contribuire al mix e
    // senza toccare ampGlide/gainGlide/panGlide/offsetGlide (nessuno di
    // questi conta se l'uscita non viene usata). Serve perche' un
    // PitchShifter affamato (nessuna chiamata a process() per tutta la
    // durata del silenzio), quando torna attivo, produce silenzio per
    // l'intera sua latenza dichiarata prima che il segnale vero rientri —
    // il buco misurato in LOG/archivio-s01-s28.md sessione 27. RT-safe: stesso percorso
    // di process() dentro processAdd, nessuna allocazione, nessun lock.
    // Chiamare SOLO quando la voce e' effettivamente muta (isSilent()):
    // il chiamante (PhraseScheduler) lo sa gia', non e' verificato qui.
    void processWarmOnly (const float* monoIn, int numSamples, float continuousInputMidiNote);

    // Sessione 34 (B-15, percorso Play) — come processWarmOnly sopra, ma il
    // motore riceve anche il RAPPORTO DI TRASPOSIZIONE e le formanti che
    // questa voce avra' quando tornera' udibile, invece di continuare a
    // girare con quelli della nota precedente.
    //
    // Serve perche' PsolaShifter::synthesise() riempie outBuf IN ANTICIPO
    // (fino a absWrite - maxPeriod): con la sola processWarmOnly a 3
    // argomenti, al momento in cui la dissolvenza parte ci sono gia' ~10 ms
    // di uscita sintetizzata all'intonazione VECCHIA, e la giunzione con
    // quella giusta cade a guadagno pieno — un salto di ampiezza misurato
    // 2.3-5.1 volte quello del regime (PM-3, tests/play_mode_input_test.cpp).
    // L'esperimento di controllo PM-7 (seconda nota alla STESSA altezza:
    // PM-3 ~1.0, prima e dopo) isola il cambio di rapporto come causa unica.
    //
    // ATTENZIONE all'ordine di chiamata: questo metodo consuma
    // justReactivated ed avanza offsetGlide, quindi il chiamante deve aver
    // gia' chiamato setMuted(false) — altrimenti l'aggancio al bersaglio non
    // e' armato e il riscaldamento gira su un'intonazione che SCIVOLA, che e'
    // esattamente il difetto che questo metodo esiste per evitare. Vedi
    // PlayModeInput::process. ampGlide non viene toccato (la sua rampa vive
    // solo in processAdd), quindi la dissolvenza degli 8 ms comincia comunque
    // dopo, sul segnale vero.
    //
    // NON e' usato dalla catena Harmonizer: PhraseScheduler continua a
    // chiamare la versione a 3 argomenti, invariata (D-19 — le suite
    // voice/psola/phrase_scheduler devono restare identiche). Lo stesso
    // miglioramento e' probabilmente disponibile anche per i suoi rami di
    // riscaldamento (celle vuote B-04, late-binding B-12): deliberatamente
    // fuori scope, va misurato prima, vedi BUGS.md/B-15.
    void processWarmOnly (const float* monoIn, int numSamples,
                          int quantizedPlayedNote, float continuousInputMidiNote);

    // Lo slot fisico viene restituito al pool (fine naturale di una frase,
    // tutte le sue voci gia' isSilent(): vedi PhraseScheduler::process()) —
    // da qui in poi nessuno lo rifornisce piu', ne' con processAdd ne' con
    // processWarmOnly. Azzera SUBITO lo stato interno del motore, cosi' una
    // nota futura su questo stesso slot fisico non eredita contenuto
    // residuo di questa (il reset che fino a sessione 26 viveva dentro
    // setMuted, spostato qui — vedi il commento su setMuted sopra e
    // tests/psola_test.cpp Test 9, che continua a verificare reset()
    // direttamente su PsolaShifter, invariato da questo spostamento).
    // NON va chiamato sul furto d'emergenza di una frase ancora viva/in
    // dissolvenza (FR-52, PhraseScheduler::allocateFreeSlot): li' il motore
    // deve continuare a girare senza interruzioni, per costruzione (vedi il
    // commento su PhraseScheduler::hardFreePhrase).
    void goCold() noexcept;

private:
    // Sessione 34 — ESTRAZIONE PURA dal corpo di processAdd: aggancio
    // dell'offset al riattacco, avanzamento di offsetGlide, calcolo dello
    // shift da applicare, correzione formantica, e la corsa del motore dentro
    // `scratch`. Tutto cio' che processAdd faceva PRIMA di mischiare l'uscita
    // nel mix, spostato qui senza cambiare una riga di matematica ne' l'ordine
    // in cui i glide avanzano — cosi' processWarmOnly a 4 argomenti puo'
    // riusarlo scartando `scratch` invece di duplicare la formula.
    void runShifter (const float* monoIn, int numSamples,
                     int quantizedPlayedNote, float continuousInputMidiNote);

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
