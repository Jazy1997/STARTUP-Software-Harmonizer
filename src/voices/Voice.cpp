#include "Voice.h"
#include <algorithm>
#include <cmath>
#include <utility>

void Voice::prepare (double sampleRate, int maxBlockSize, int stabilityLevel)
{
    shifter = createDefaultPitchShifter();
    shifter->prepare (sampleRate, maxBlockSize, stabilityLevel);
    scratch.assign ((size_t) maxBlockSize, 0.0f);

    offsetGlide.prepare (sampleRate);
    offsetGlide.reset (0.0f);

    ampGlide.prepare (sampleRate);
    ampGlide.setGlideTimeMs (kDeclickMs);
    ampGlide.reset (0.0f); // silenzioso all'avvio, coerente con muted=true di default

    gainGlide.prepare (sampleRate);
    gainGlide.setGlideTimeMs (kDeclickMs);
    gainGlide.reset (1.0f); // FR-11: default 0 dB = 1.0 lineare, nessun cambiamento

    panGlide.prepare (sampleRate);
    panGlide.setGlideTimeMs (kDeclickMs);
    panGlide.reset (0.0f); // FR-11: default centro
}

void Voice::reset()
{
    if (shifter)
        shifter->reset();
}

int Voice::getLatencySamples() const
{
    return shifter ? shifter->getLatencySamples() : 0;
}

void Voice::swapShifterNoAlloc (std::unique_ptr<PitchShifter>& shifterInOut) noexcept
{
    shifter.swap (shifterInOut);
}

void Voice::processAdd (const float* monoIn, float* mixL, float* mixR, int numSamples,
                         int quantizedPlayedNote, float continuousInputMidiNote)
{
    // Sessione 12: NON si esce piu' subito perche' muted e' vero — si esce
    // solo quando la dissolvenza ha davvero finito (isSilent()). Cosi' il
    // chiamante puo' smettere di rifornire audio a una voce (fine frase,
    // silenzio, cella tornata vuota su una frase ancora viva) senza tagliare
    // di netto: le ultime chiamate a processAdd, con muted=true, producono
    // ancora suono ma in dissolvenza verso zero.
    if (shifter == nullptr || isSilent())
        return;

    // Sessione 16 (fix dell'intonazione stantia al riattacco, vedi
    // Voice.h/setMuted per la diagnosi completa): consumato qui, non dentro
    // setTargetOffsetSemitones, perche' l'ordine con cui i due chiamanti
    // impostano target/mute e' diverso (PhraseScheduler.cpp: setMuted POI
    // setTarget; PlayModeInput.cpp: setTarget al note-on, POI setMuted
    // quando il segnale torna stabile, anche blocchi dopo) — processAdd()
    // e' l'unico punto che arriva sempre PER ULTIMO in entrambi i casi,
    // quando il target giusto e' gia' stato impostato per certo.
    // offsetGlide.reset(getTarget()) aggancia current al target CORRENTE,
    // qualunque esso sia in questo momento: nessuna scivolata dalla nota
    // precedente occupante lo stesso slot fisico.
    if (justReactivated)
    {
        offsetGlide.reset (offsetGlide.getTarget());
        justReactivated = false;
    }

    // Sessione 13 (click residui): con un buffer host piu' lungo della
    // rampa (8ms = 353 campioni a 44.1kHz; l'utente ha misurato 4096
    // campioni con MME/DirectX in Ableton) ampGlide.process(numSamples)
    // faceva scattare l'intera dissolvenza in un solo campione — il fix di
    // sessione 12 era di fatto un no-op in quella configurazione. Vedi
    // Glide::processRamp e tests/glide_test.cpp.
    const auto ampRamp = ampGlide.processRamp (numSamples);
    const float smoothedOffset = offsetGlide.process (numSamples);

    float semitonesToApply;
    if (mode == ShiftMode::fix)
    {
        // FR-22: la voce insegue una nota ASSOLUTA (nota quantizzata +
        // offset), non un rapporto fisso sull'ingresso. Il rapporto va
        // ricalcolato ogni blocco per compensare vibrato/bending in ingresso.
        const float targetAbsoluteMidi = (float) quantizedPlayedNote + smoothedOffset;
        semitonesToApply = targetAbsoluteMidi - continuousInputMidiNote;
    }
    else
    {
        // FR-21: rapporto fisso rispetto all'ingresso — vibrato e portamento
        // dell'esecutore si trasferiscono integralmente sulla voce.
        semitonesToApply = smoothedOffset;
    }

    // FR-39/FR-41: correzione formantica in funzione dello shift REALMENTE
    // applicato a questa voce (semitonesToApply), non dell'offset grezzo del
    // preset — cosi' funziona identica in Fix e Move, e restera' identica
    // anche in modalita' Play (FR-42) quando esistera', perche' tutte
    // finiscono per produrre lo stesso semitonesToApply. Formula da
    // psola-spec.md §3 (k~0.3): shift in giu' schiarisce (beta>1), shift in
    // su scurisce (beta<1). Si lavora in "semitoni-equivalenti" anziche' nel
    // rapporto beta direttamente cosi' l'offset manuale (FR-41) e' una somma
    // letterale con la correzione automatica, non una moltiplicazione di
    // rapporti — coerente col resto del progetto, che ragiona in semitoni.
    constexpr float kFormantSpreadK = 0.3f;
    const float autoFormantSemitones = -kFormantSpreadK * formantSpread * semitonesToApply;
    const float totalFormantSemitones = autoFormantSemitones + formantOffsetSemitones;
    shifter->setFormantRatio (std::exp2 ((double) totalFormantSemitones / 12.0));

    // Fondamentale del segnale in ingresso, in Hz: serve ai motori a
    // dominio del tempo (PSOLA) per posizionare gli epoch sul periodo
    // reale. E' un calcolo esatto, non un'approssimazione: PitchDetector
    // ricava continuousInputMidiNote proprio da una frequenza in Hz
    // (get_frequency() di Cycfi Q), quindi questo e' l'andata e ritorno
    // dello stesso valore. Le implementazioni che non ne hanno bisogno
    // (SpectralShifter) ignorano la chiamata (default no-op).
    shifter->setInputF0Hz (440.0 * std::exp2 (((double) continuousInputMidiNote - 69.0) / 12.0));

    shifter->setPitchShiftSemitones (semitonesToApply);
    shifter->process (monoIn, scratch.data(), numSamples);

    // FR-11: gain e pan utente per voce, con lo stesso trattamento anti-click
    // campione-per-campione gia' usato per ampRamp (sessioni 12/13 — un
    // guadagno che scatta invece di rampare clicca, a prescindere da cosa lo
    // sta guidando: fine frase, un knob trascinato, o l'automazione host).
    const auto gainRamp = gainGlide.processRamp (numSamples);
    const auto panRamp = panGlide.processRamp (numSamples);

    // Legge di pan a potenza costante, normalizzata cosi' che pan=0 dia
    // gL=gR=1.0 (non 0.707/0.707): con gain e pan al default lo stereo
    // risultante e' identico campione per campione al vecchio mix mono
    // duplicato sui due canali — nessun calo di ~3dB da spiegare all'ascolto
    // quando questo lavoro e' stato introdotto. Calcolata solo agli estremi
    // della rampa (due sin/cos per blocco, non per campione) e interpolata
    // linearmente: su una rampa di 8ms non e' distinguibile dalla curva vera.
    static constexpr float kPiOverFour = 0.78539816339744830961f;
    static constexpr float kSqrt2 = 1.41421356237309504880f;
    const auto panToLR = [] (float pan) noexcept
    {
        // Tre casi espliciti (centro ed estremi), non lasciati al calcolo
        // trigonometrico: kPiOverFour e' un'APPROSSIMAZIONE in float di
        // pi/4, quindi 2*kPiOverFour non e' bit-esattamente pi/2 —
        // std::cos(theta) a pan=+1 non torna mai esattamente 0.0f (nella
        // pratica ~1e-8 per il segnale di prova, vedi tests/voice_test.cpp
        // T-1), e std::cos/std::sin non sono comunque garantiti bit-esatti
        // fra loro allo stesso argomento (implementazioni indipendenti). Un
        // utente che porta il pan a un estremo si aspetta silenzio VERO
        // sul canale opposto, non un residuo a -150dB; il pan di default e'
        // inoltre esattamente 0.0f (mai toccato finche' l'utente non sposta
        // il knob): qui si garantiscono gL/gR ESATTI nei tre casi, cosi'
        // pan centrale produce uno stereo bit-per-bit identico al vecchio
        // mix mono (T-3) e pan hard-left/right un canale ESATTAMENTE zero
        // (T-1) — non solo "vicino".
        if (pan == 0.0f)  return std::pair<float, float> { 1.0f, 1.0f };
        if (pan <= -1.0f) return std::pair<float, float> { kSqrt2, 0.0f };
        if (pan >= 1.0f)  return std::pair<float, float> { 0.0f, kSqrt2 };
        const float theta = (pan + 1.0f) * kPiOverFour;
        return std::pair<float, float> { kSqrt2 * std::cos (theta), kSqrt2 * std::sin (theta) };
    };
    const auto [gLStart, gRStart] = panToLR (panRamp.startValue);
    const auto [gLEnd, gREnd] = panToLR (panRamp.startValue + panRamp.increment * (float) panRamp.rampSamples);
    const float gLIncrement = panRamp.rampSamples > 0 ? (gLEnd - gLStart) / (float) panRamp.rampSamples : 0.0f;
    const float gRIncrement = panRamp.rampSamples > 0 ? (gREnd - gRStart) / (float) panRamp.rampSamples : 0.0f;

    // Guadagno campione-per-campione: ogni rampa.rampSamples puo' essere
    // minore di numSamples (finisce dentro questo blocco), nel qual caso i
    // campioni restanti restano fermi al valore raggiunto — stesso schema
    // gia' in uso per ampRamp da solo, qui esteso a quattro rampe insieme.
    const int rampSamples = std::max ({ ampRamp.rampSamples, gainRamp.rampSamples, panRamp.rampSamples });
    float amp = ampRamp.startValue;
    float gain = gainRamp.startValue;
    float gL = gLStart;
    float gR = gRStart;
    int i = 0;
    for (; i < rampSamples; ++i)
    {
        const float s = scratch[(size_t) i] * amp * gain;
        mixL[i] += s * gL;
        mixR[i] += s * gR;
        if (i < ampRamp.rampSamples) amp += ampRamp.increment;
        if (i < gainRamp.rampSamples) gain += gainRamp.increment;
        if (i < panRamp.rampSamples) { gL += gLIncrement; gR += gRIncrement; }
    }
    for (; i < numSamples; ++i)
    {
        const float s = scratch[(size_t) i] * amp * gain;
        mixL[i] += s * gL;
        mixR[i] += s * gR;
    }
}
