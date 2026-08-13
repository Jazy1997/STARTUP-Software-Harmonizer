// Verifica che la scadenza beta faccia DAVVERO tacere il wet, e che il dry
// sopravviva (sessione 37, D-26).
//
// PERCHE' ESISTE, separato da beta_expiry_test.cpp: quello copre l'aritmetica
// di BetaGate (quando scade), questo copre il CABLAGGIO (cosa succede all'audio
// quando e' scaduta). Sono due difetti diversi e indipendenti: un'aritmetica
// giusta collegata male spedirebbe agli artisti una versione completa e
// illimitata senza che nulla lo segnali — cioe' esattamente il rischio che la
// scadenza esiste per chiudere. Nessun altro banco lo copre, perche' tutti gli
// altri girano in configurazione NON beta.
//
// QUARTO livello di D-16, come mode_switch_test: il cancello non vive dentro un
// modulo isolato ma in HarmonizerAudioProcessor::processBlock, quindi qui si
// istanzia il PROCESSORE INTERO. Gira in ctest, non nel gate a g++ nudo (A-06).
//
// La build e' resa scaduta A TEMPO DI COMPILAZIONE dal CMakeLists (epoch fisso
// nel passato + DAYS=0), non dall'option HARMONIZER_BETA: il banco misura
// sempre la stessa cosa, indipendentemente da come e' configurata la build e da
// quando lo si esegue.

#include "PluginProcessor.h"
#include "TestSignals.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <vector>
#include <cmath>

namespace
{
    int failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("  %-70s %s\n", description, condition ? "OK" : "FALLITO");
        if (! condition)
            ++failures;
    }

    constexpr double SR = 44100.0;
    constexpr int    kBlock = 256;
    constexpr int    kMaxBlockPrepare = 4096;
    constexpr double kInputF0 = 220.0;            // A3
    constexpr int    kVoiceOffsetSemitones = 4;

    // Stessa scelta di mode_switch_test: tutti e 12 i gradi pieni sulla voce 0,
    // cosi' "wet assente" significa "l'Harmonizer non sta lavorando" e mai
    // "quella cella era vuota". Non e' una raccomandazione d'uso (D-17).
    void configure (HarmonizerAudioProcessor& proc)
    {
        proc.setPlayConfigDetails (2, 2, SR, kMaxBlockPrepare);
        proc.prepareToPlay (SR, kMaxBlockPrepare);

        proc.editPresetLibrary ([] (harmony::PresetLibrary& lib)
        {
            for (int d = 0; d < harmony::numDegrees; ++d)
                lib.setCell (0, d, 0, harmony::Cell (kVoiceOffsetSemitones));
        });

        auto set = [&proc] (const char* id, float v)
        {
            auto* p = proc.apvts.getParameter (id);
            jassert (p != nullptr);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        set ("presetIndex", 1.0f);
        set ("numVoices", 1.0f);
    }

    std::vector<float> run (HarmonizerAudioProcessor& proc, const std::vector<float>& src,
                            int totalSamples, float dryWetMix)
    {
        auto* mix = proc.apvts.getParameter ("dryWetMix");
        mix->setValueNotifyingHost (mix->convertTo0to1 (dryWetMix));

        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;

        std::vector<float> out;
        out.reserve ((size_t) totalSamples);

        for (int pos = 0; pos + kBlock <= totalSamples; pos += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = (pos + i) < (int) src.size() ? src[(size_t) (pos + i)] : 0.0f;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            midi.clear();
            proc.processBlock (buffer, midi);

            for (int i = 0; i < kBlock; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        return out;
    }
}

int main()
{
    // Il processore avvia un Timer in prepareToPlay: senza MessageManager non
    // si costruisce (stessa nota di mode_switch_test).
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("\n=== Cancello della beta sul percorso audio (D-26) ===\n");
    std::printf ("Build resa scaduta a tempo di compilazione: epoch=%lld, giorni=%d\n\n",
                 (long long) HARMONIZER_BETA_BUILD_EPOCH, (int) HARMONIZER_BETA_DAYS);

    const auto carrier = makeVowel (kInputF0, 2.5, SR);
    const int totalSamples = ((int) (2.0 * SR) / kBlock) * kBlock;

    // Finestra di misura ben dopo l'avvio: il rilevatore ha convergiuto e il
    // motore e' a regime. In una build NON scaduta questa e' la finestra in cui
    // mode_switch_test misura wet chiaramente presente, con lo stesso preset e
    // la stessa sorgente — e' quello il riferimento che rende decisiva la
    // misura di silenzio qui sotto.
    const int measureFrom = (int) (1.0 * SR);
    const int measureLen  = (int) (0.5 * SR);

    std::printf ("[1] Il flag e' stato alzato sul message thread\n");
    HarmonizerAudioProcessor procWet;
    configure (procWet);
    check (HarmonizerAudioProcessor::isBetaBuild(),
           "isBetaBuild() e' vera in questa configurazione");
    check (procWet.isBetaExpired(),
           "prepareToPlay ha marcato la build come scaduta");

    std::printf ("\n[2] Tutto wet su una build scaduta: silenzio\n");
    const auto wetOut = run (procWet, carrier, totalSamples, /*dryWetMix*/ 1.0f);
    const double wetRms = rms (wetOut, measureFrom, measureLen);
    std::printf ("      RMS a regime con dryWetMix=1.0 : %.3e\n", wetRms);
    check (allFinite (wetOut), "l'uscita e' tutta finita (nessun NaN/inf)");
    // Silenzio digitale, non "poco": se il cancello fosse scollegato qui ci
    // sarebbe una voce a +4 semitoni ben misurabile.
    //
    // Misurato 4.3e-9, e NON e' wet che sfugge: con dryWetMix=1.0 il crossfade a
    // potenza costante da' dryLevel = cos(pi/2), che in virgola singola non e'
    // zero esatto ma ~6e-8 (vedi computeDryWetGains in PluginProcessor.cpp).
    // 6e-8 per l'RMS della sorgente (0.0992) fa proprio ~6e-9. La soglia a 1e-6
    // sta due ordini di grandezza sopra quel residuo e quattro sotto un wet
    // vero: distingue senza essere fragile.
    check (wetRms < 1.0e-6, "il wet e' silenzio digitale (< 1e-6)");

    std::printf ("\n[3] Tutto dry sulla stessa build scaduta: il segnale passa\n");
    // L'altra meta' della promessa, ed e' quella che protegge i progetti dei
    // tester: scade il WET, non il plugin (principio di FR-68). Un tester che
    // riapre una sessione dopo la scadenza deve ritrovare il suo dry, non una
    // traccia muta.
    HarmonizerAudioProcessor procDry;
    configure (procDry);
    const auto dryOut = run (procDry, carrier, totalSamples, /*dryWetMix*/ 0.0f);
    const double dryRms = rms (dryOut, measureFrom, measureLen);
    const double srcRms = rms (carrier, measureFrom, measureLen);
    std::printf ("      RMS a regime con dryWetMix=0.0 : %.4f (sorgente: %.4f)\n", dryRms, srcRms);
    check (allFinite (dryOut), "l'uscita e' tutta finita (nessun NaN/inf)");
    check (dryRms > 0.5 * srcRms,
           "il dry passa a livello pieno malgrado la scadenza (FR-68)");

    std::printf ("\n%s (%d fallimenti)\n\n", failures == 0 ? "TUTTO OK" : "CI SONO FALLIMENTI", failures);
    return failures == 0 ? 0 : 1;
}
