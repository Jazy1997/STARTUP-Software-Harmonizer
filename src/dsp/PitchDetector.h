#pragma once

#include <atomic>
#include <memory>

// Wrapper monofonico su Cycfi Q (bacf pitch detector). Sorgenti da ottimizzare
// in ordine (FR-14): voce, sax, tromba.
//
// prepare() alloca (message thread / prepareToPlay). pushSample() e i getter
// non allocano mai: rispettano le regole di threading di CLAUDE.md regola 1.
//
// SESSIONE 32 (B-14) — la NOTA PIU' GRAVE non e' un dettaglio di taratura, e'
// il parametro che decide quanto in fretta l'armonia arriva. Cycfi Q ricava la
// finestra d'analisi direttamente da li' (bacf_period_detector:
// `_zc(hysteresis, lowest_freq.period() * 2 * sps)`) e produce una stima ogni
// mezza finestra; a 60 Hz sono 33.4 ms di finestra e una stima ogni 16.7 ms.
// Piu' in alto si mette la nota piu' grave, piu' corta la finestra e piu'
// presto l'offset giusto raggiunge il motore — al prezzo di non agganciare
// nulla sotto quella nota. E' esposta all'utente come scelta dello strumento
// (D-18).
class PitchDetector
{
public:
    PitchDetector();
    ~PitchDetector();

    // lowestNoteMidi: la nota piu' grave che il rilevatore deve saper
    // agganciare. Default 35 (B1, 61.7 Hz) = il comportamento fino a s.31,
    // cosi' un chiamante che non se ne occupa non cambia suono.
    void prepare (double sampleRate, int lowestNoteMidi = kDefaultLowestNoteMidi);
    void reset();

    // Nota piu' grave di default: B1, cioe' i 60 Hz cablati fino a s.31.
    static constexpr int kDefaultLowestNoteMidi = 35;

    static float noteToHz (int midiNote) noexcept;

    // Quanti campioni separano due stime consecutive del rilevatore (mezza
    // finestra d'analisi). E' la scala temporale NATURALE di questo modulo:
    // le attese di chi lo consuma vanno espresse in multipli di questo, non in
    // millisecondi fissi, cosi' seguono da sole la nota piu' grave scelta
    // (vedi PitchLatch::prepare). Zero se prepare() non e' stata chiamata.
    int getAnalysisFrameSamples() const noexcept;

    // ---- Cambio della nota piu' grave a plugin in funzione (B-14) --------
    //
    // Ricostruire il rilevatore ALLOCA (il bitset di Q e' dimensionato sulla
    // finestra), quindi non si puo' fare in processBlock (CLAUDE.md regola 1).
    // Stesso schema gia' usato per il cambio di Stability
    // (VoicePool::requestStabilityChange -> Voice::swapShifterNoAlloc ->
    // collectGarbage), qui pero' senza SpinLock: e' un solo puntatore con un
    // solo scrittore per lato, quindi bastano due flag atomici, e cosi'
    // PitchDetector resta senza dipendenze JUCE (lo compilano anche le sonde).
    //
    // A differenza di Stability, questo cambio NON tocca la latenza
    // dichiarata — il rilevatore non sta nella catena audio — quindi non va
    // subordinato allo stop del transport (FR-56 non si applica).

    // Message thread. Ignora la richiesta se ce n'e' gia' una in volo.
    void requestLowestNoteChange (double sampleRate, int lowestNoteMidi);

    // Audio thread. Scambia i puntatori se c'e' una richiesta pronta e il
    // vecchio rilevatore e' gia' stato raccolto. Nessuna allocazione, nessuna
    // distruzione. Ritorna true se lo scambio e' avvenuto: il chiamante deve
    // rileggere getAnalysisFrameSamples(), perche' e' cambiato.
    bool applyPendingLowestNoteChange() noexcept;

    // Message thread. Distrugge il rilevatore ritirato dallo scambio.
    void collectGarbage();

    // Da chiamare una volta per sample. Ritorna true quando la stima e' stata
    // aggiornata in questo sample (vedi doc Cycfi Q: get_frequency()/periodicity()
    // vanno letti solo quando pushSample() ritorna true).
    bool pushSample (float sample) noexcept;

    // Valida solo se hasStableSignal() e' true nello stesso momento.
    float getMidiNote() const noexcept { return currentMidiNote; }
    float getConfidence() const noexcept { return currentConfidence; }
    bool hasStableSignal() const noexcept;

private:
    // unique_ptr (non optional): il pattern pimpl richiede che l'implicito
    // distruttore generato per unique_ptr<Impl> resti "differito" fino al .cpp
    // dove Impl e' completo — std::optional<T> instanzia i type-trait di T
    // (is_trivially_destructible ecc.) ovunque la classe venga inclusa, e non
    // compila con un tipo incompleto.
    struct Impl;
    std::unique_ptr<Impl> impl;

    // Scritto dal message thread solo quando pendingReady e' false, letto e
    // svuotato dall'audio thread solo quando e' true: un solo scrittore per
    // volta su ciascun puntatore, senza lock. Stessa cosa per retired, a
    // ruoli invertiti.
    std::unique_ptr<Impl> pendingImpl;
    std::unique_ptr<Impl> retiredImpl;
    std::atomic<bool> pendingReady { false };
    std::atomic<bool> retiredReady { false };

    float currentMidiNote = -1.0f;
    float currentConfidence = 0.0f;
};
