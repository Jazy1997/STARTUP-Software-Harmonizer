// Strumento diagnostico OFFLINE per il "click in mezzo alla nota"/instabilita'
// timbrica segnalati dall'utente in sessione 17 dopo aver riascoltato il fix
// di sessione 16 (che ha risolto solo in parte). Vedi handsoff.md sessione 18
// per il meccanismo poi confermato (quantizzazione del periodo intero in
// PsolaShifter::currentPeriod) — questo file resta comunque utile come banco
// di riproduzione OFFLINE (senza DAW) del percorso PitchDetector/
// OnsetDetector/PitchLatch/Voice a numVoices=1.
//
// CLAUDE.md regola 12: non posso ascoltare. Non e' uno strumento pass/fail
// (non e' registrato in ctest: dipende da file WAV esterni non nel repo,
// forniti dall'utente in "SAMPLE TEST/"), e' un MISURATORE — vedi
// SampleAnalysis.h per le funzioni condivise con real_export_probe.cpp
// (stesse tabelle, cosi' le due uscite sono confrontabili riga a riga).
//
// Downmix a mono identico a PluginProcessor::processBlock. Quattro passate:
//   1. runHeld — una voce sola, attivata UNA VOLTA e mai piu' riammutolita
//      per tutta la durata del file — isola il meccanismo di riattivazione
//      (sessione 16, gia' verificato a parte con voice_test.cpp).
//   2. runRetriggered — stesso slot fisico riassegnato ad ogni onset REALE
//      rilevato nel file, con un nuovo offset armonico ad ogni ri-attacco
//      (ciclo rappresentativo, non il preset reale — questa passata testa
//      il MECCANISMO di riattivazione, non l'armonia).
//   3. runLiveHarmony — voce attivata una volta sola, offset che segue DAL
//      VIVO PitchLatch + la colonna V1 VERA del preset "Maj" (root=C),
//      derivata a mano da PresetLibrary.cpp::generateDropVoicingTable
//      (sessione 18): {0,-1,-2,-3,0,-1,-2,0,-1,-2,-3,0}. V1 non e' MAI muta
//      in un accordo a 4+ toni (le celle vuote colpiscono solo v>=numTones).
//   4. runProduction (sessione 18) — la stessa catena ma con un modello a
//      DUE slot che riproduce il rilascio+nuovo-slot di PhraseScheduler
//      (keepTails=false): ad ogni onset la frase precedente sfuma mentre la
//      nuova cresce, esattamente come in produzione a numVoices=1. Duplica
//      la logica di PhraseScheduler invece di linkarla (PhraseScheduler
//      dipende da juce_core via VoicePool/HarmonyPreset.h, non linkabile in
//      un target headless) — rischio noto di divergenza dalla logica reale,
//      da tenere a mente se i numeri non tornano.
//
// Uso: sample_click_finder <file.wav> [stability=2] [formantSpread=1.0] [rootPitchClass=0] [block=4096]
// (block default 4096: e' il block size reale misurato in Ableton in
// sessione 13, non 256 — con un buffer host grande il rilevatore di pitch si
// legge una volta ogni ~93ms invece che ogni ~6ms)
//
// Compilazione (vedi anche il target CMake `sample_click_finder`, NON in ctest):
//   g++ -O2 -std=c++20 -Isrc -Ilibs/signalsmith-stretch -Ilibs/q/q_lib/include
//       -Ilibs/q/infra/include tests/sample_click_finder.cpp src/voices/Voice.cpp
//       src/dsp/PsolaShifter.cpp src/dsp/SpectralShifter.cpp
//       src/dsp/PitchShifterFactory.cpp src/dsp/PitchDetector.cpp
//       src/dsp/OnsetDetector.cpp -o sample_click_finder

#include "voices/Voice.h"
#include "dsp/PitchDetector.h"
#include "dsp/OnsetDetector.h"
#include "harmony/PitchLatch.h"
#include "SampleAnalysis.h"

#include <cstdio>
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <cmath>

namespace
{
    // Diagnostica (sessione 19 — "wobbling"): stampa la traiettoria GREZZA di
    // PitchDetector (esattamente cio' che Voice::processAdd riceve come
    // continuousInputMidiNote, quindi cio' che arriva a
    // PsolaShifter::setInputF0Hz) su un intervallo di tempo, a un dato block
    // size — PRIMA di qualunque cosa Voice/PsolaShifter ci facciano sopra.
    // Serve a distinguere due ipotesi diverse per lo stesso sintomo (jitter
    // di pochi Hz osservato nel wet anche a offset 0/glide 0): (a) il
    // rilevatore stesso produce una stima gia' rumorosa, indipendentemente
    // da quanto spesso la si legge — allora il motore la eredita e basta;
    // (b) la stima e' pulita ma viene letta troppo poco spesso (una volta
    // per blocco host grande) — allora infittire la lettura (sotto-blocchi,
    // sessione 13) la risolverebbe. Confrontare l'uscita a block size diversi
    // sullo STESSO intervallo distingue le due ipotesi.
    void dumpPitchTrace (const std::vector<float>& mono, double sr, int block,
                         double traceStartSec, double traceEndSec)
    {
        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);

        std::printf ("  block=%d — t, midiNote, hz, confidenza, stabile:\n", block);
        int done = 0;
        while (done + block <= (int) mono.size())
        {
            for (int i = 0; i < block; ++i)
                pitchDetector.pushSample (mono[(size_t) (done + i)]);

            const double tSec = (double) done / sr;
            if (tSec >= traceStartSec && tSec <= traceEndSec)
            {
                const float midi = pitchDetector.getMidiNote();
                const double hz = midi >= 0.0f ? 440.0 * std::pow (2.0, ((double) midi - 69.0) / 12.0) : 0.0;
                std::printf ("    %8.4f  %8.3f  %8.1f  %6.3f  %s\n",
                             tSec, midi, hz, pitchDetector.getConfidence(),
                             pitchDetector.hasStableSignal() ? "si" : "no");
            }
            done += block;
        }
    }

    // Diagnostica (sessione 19 — "wobbling"): come runHeld, ma con un f0
    // COSTANTE scelto a mano invece che letto da PitchDetector, e un offset
    // fisso scelto a mano — bypassa COMPLETAMENTE PitchDetector/PitchLatch/
    // Glide. Isola al 100% il comportamento di PsolaShifter sul segnale
    // reale: se un glitch compare anche qui, non puo' venire ne' dal
    // rilevatore di pitch ne' dalla granularita' con cui lo si legge (gia'
    // esclusi misurando la traiettoria grezza di PitchDetector, pulita a
    // ogni block size) — deve venire dal posizionamento degli epoch/dalla
    // sintesi dei grani su questo specifico segnale non impulsivo.
    std::vector<float> runFixedF0 (const std::vector<float>& mono, double sr, float semitones,
                                   int stabilityLevel, float formantSpread, double fixedF0Hz, int block)
    {
        Voice voice;
        voice.prepare (sr, block, stabilityLevel);
        voice.setFormantSpread (formantSpread);
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (semitones);

        const float fixedMidi = (float) (69.0 + 12.0 * std::log2 (fixedF0Hz / 440.0));

        std::vector<float> wet (mono.size(), 0.0f);
        int done = 0;
        while (done + block <= (int) mono.size())
        {
            voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], block, 0, fixedMidi);
            done += block;
        }
        return wet;
    }

    // Passata 1: una sola voce, attivata UNA VOLTA all'inizio e mai piu'
    // riammutolita per l'intera durata del file.
    std::vector<float> runHeld (const std::vector<float>& mono, double sr, float semitones,
                                int stabilityLevel, float formantSpread, int block)
    {
        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);

        Voice voice;
        voice.prepare (sr, block, stabilityLevel);
        voice.setFormantSpread (formantSpread);
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (semitones);

        std::vector<float> wet (mono.size(), 0.0f);

        int done = 0;
        while (done + block <= (int) mono.size())
        {
            for (int i = 0; i < block; ++i)
                pitchDetector.pushSample (mono[(size_t) (done + i)]);

            const float continuousMidi = pitchDetector.getMidiNote();
            voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], block, 0, continuousMidi);
            done += block;
        }
        return wet;
    }

    // Passata 2: OnsetDetector REALE decide quando un nuovo attacco ricicla
    // lo stesso slot fisico (vedi commento di testa del file).
    std::vector<float> runRetriggered (const std::vector<float>& mono, double sr,
                                       int stabilityLevel, float formantSpread, int block)
    {
        static constexpr float kOffsetCycle[] = { 0.0f, -3.0f, -5.0f, -7.0f, 3.0f, -12.0f };
        constexpr int kNumOffsets = (int) (sizeof (kOffsetCycle) / sizeof (kOffsetCycle[0]));

        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);
        OnsetDetector onsetDetector;
        onsetDetector.prepare (sr);

        Voice voice;
        voice.prepare (sr, block, stabilityLevel);
        voice.setFormantSpread (formantSpread);
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (kOffsetCycle[0]);
        int offsetCycleIndex = 0;
        int numRetriggers = 0;

        std::vector<float> wet (mono.size(), 0.0f);

        int done = 0;
        bool firstBlock = true;
        while (done + block <= (int) mono.size())
        {
            bool onsetThisBlock = false;
            for (int i = 0; i < block; ++i)
            {
                pitchDetector.pushSample (mono[(size_t) (done + i)]);
                if (onsetDetector.pushSample (mono[(size_t) (done + i)]))
                    onsetThisBlock = true;
            }
            const float continuousMidi = pitchDetector.getMidiNote();

            if (onsetThisBlock && ! firstBlock)
            {
                voice.setMuted (true);
                while (! voice.isSilent() && done + block <= (int) mono.size())
                {
                    for (int i = 0; i < block; ++i)
                        pitchDetector.pushSample (mono[(size_t) (done + i)]);
                    voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], block, 0, pitchDetector.getMidiNote());
                    done += block;
                }

                if (done + block > (int) mono.size())
                    break;

                offsetCycleIndex = (offsetCycleIndex + 1) % kNumOffsets;
                voice.setMuted (false);
                voice.setTargetOffsetSemitones (kOffsetCycle[offsetCycleIndex]);
                ++numRetriggers;
                continue;
            }

            voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], block, 0, continuousMidi);
            done += block;
            firstBlock = false;
        }

        std::printf ("  (%d ri-attacchi rilevati dall'OnsetDetector reale su questo file)\n", numRetriggers);
        return wet;
    }

    // kMajV1Table (colonna V1 vera del preset Maj) e' definita in
    // SampleAnalysis.h, condivisa con real_export_probe.cpp.

    // Passata 3: voce attivata una volta sola (come la 1), offset che segue
    // DAL VIVO PitchLatch + la tabella V1 vera del preset Maj.
    std::vector<float> runLiveHarmony (const std::vector<float>& mono, double sr,
                                       int stabilityLevel, float formantSpread, int rootPitchClass,
                                       int block)
    {
        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);
        OnsetDetector onsetDetector;
        onsetDetector.prepare (sr);
        harmony::PitchLatch pitchLatch;

        Voice voice;
        voice.prepare (sr, block, stabilityLevel);
        voice.setFormantSpread (formantSpread);
        voice.setMuted (false);

        std::vector<float> wet (mono.size(), 0.0f);
        int done = 0;
        int numDegreeChanges = 0;
        int lastDegree = -1;

        while (done + block <= (int) mono.size())
        {
            bool onsetThisBlock = false;
            for (int i = 0; i < block; ++i)
            {
                pitchDetector.pushSample (mono[(size_t) (done + i)]);
                if (onsetDetector.pushSample (mono[(size_t) (done + i)]))
                    onsetThisBlock = true;
            }
            const float continuousMidi = pitchDetector.getMidiNote();
            const bool signalPresent = onsetDetector.isGateOpen();
            const bool inputIsStable = pitchDetector.hasStableSignal();

            if (! signalPresent)
            {
                pitchLatch.reset();
            }
            else if (inputIsStable)
            {
                const int quantizedNote = pitchLatch.update (continuousMidi, onsetThisBlock);
                const int degree = degreeOf (quantizedNote, rootPitchClass);
                if (degree != lastDegree)
                {
                    ++numDegreeChanges;
                    lastDegree = degree;
                }

                voice.setMuted (false); // V1 non e' mai muta per Maj, vedi sopra
                voice.setTargetOffsetSemitones (kMajV1Table[degree]);
            }

            voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], block, 0, continuousMidi);
            done += block;
        }

        std::printf ("  (%d cambi di grado armonico rilevati durante il file, tabella V1/Maj vera, root pitch class %d)\n",
                     numDegreeChanges, rootPitchClass);
        return wet;
    }

    // Passata 4 (sessione 18): modello a DUE slot fisici che riproduce
    // PhraseScheduler con keepTails=false a numVoices=1 — ad ogni onset la
    // frase precedente sfuma (setMuted(true), alimentata con lo stesso
    // segnale reale finche' non e' isSilent()) mentre una nuova frase cresce
    // sull'ALTRO slot con la cella V1 del nuovo grado, poi le due uscite si
    // sommano — esattamente come farebbe VoicePool con due slot fisici
    // indipendenti. E' la riproduzione piu' fedele alla produzione reale che
    // questo strumento headless (senza juce_core) puo' fare.
    std::vector<float> runProduction (const std::vector<float>& mono, double sr,
                                      int stabilityLevel, float formantSpread, int rootPitchClass,
                                      int block)
    {
        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);
        OnsetDetector onsetDetector;
        onsetDetector.prepare (sr);
        harmony::PitchLatch pitchLatch;

        Voice slots[2];
        slots[0].prepare (sr, block, stabilityLevel);
        slots[1].prepare (sr, block, stabilityLevel);
        slots[0].setFormantSpread (formantSpread);
        slots[1].setFormantSpread (formantSpread);
        int activeSlot = 0; // quello che sta "vivendo" la frase corrente (FR-17 live update)
        bool anyPhraseYet = false;

        std::vector<float> wet (mono.size(), 0.0f);
        int done = 0;
        int numOnsets = 0;

        while (done + block <= (int) mono.size())
        {
            bool onsetThisBlock = false;
            for (int i = 0; i < block; ++i)
            {
                pitchDetector.pushSample (mono[(size_t) (done + i)]);
                if (onsetDetector.pushSample (mono[(size_t) (done + i)]))
                    onsetThisBlock = true;
            }
            const float continuousMidi = pitchDetector.getMidiNote();
            const bool signalPresent = onsetDetector.isGateOpen();
            const bool inputIsStable = pitchDetector.hasStableSignal();

            if (! signalPresent)
            {
                pitchLatch.reset();
                slots[0].setMuted (true);
                slots[1].setMuted (true);
            }
            else if (onsetThisBlock && anyPhraseYet)
            {
                // beginRelease della frase vecchia (sull'altro slot), nuovo
                // slot per la nuova frase (triggerNewPhrase) — stesso schema
                // di PhraseScheduler.cpp con keepTails=false.
                const int oldSlot = activeSlot;
                activeSlot = 1 - activeSlot;
                slots[oldSlot].setMuted (true);
                ++numOnsets;
            }

            if (signalPresent && inputIsStable)
            {
                const int quantizedNote = pitchLatch.update (continuousMidi, onsetThisBlock);
                const int degree = degreeOf (quantizedNote, rootPitchClass);
                slots[activeSlot].setMuted (false);
                slots[activeSlot].setTargetOffsetSemitones (kMajV1Table[degree]);
                anyPhraseYet = true;
            }

            for (auto& s : slots)
                if (! s.isSilent())
                    s.processAdd (&mono[(size_t) done], &wet[(size_t) done], block, 0, continuousMidi);

            done += block;
        }

        std::printf ("  (%d onset reali rilevati, modello a 2 slot fisici, root pitch class %d)\n",
                     numOnsets, rootPitchClass);
        return wet;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("Uso: sample_click_finder <file.wav> [stability=2] [formantSpread=1.0] [rootPitchClass=0] [block=4096] [dumpPrefix] [traceStartSec traceEndSec]\n");
        return 1;
    }

    const std::string path = argv[1];
    const int stabilityLevel = argc > 2 ? std::stoi (argv[2]) : Stability::defaultLevel;
    const float formantSpread = argc > 3 ? std::stof (argv[3]) : 1.0f;
    const int rootPitchClass = argc > 4 ? std::stoi (argv[4]) : 0;
    const int block = argc > 5 ? std::stoi (argv[5]) : 4096;
    // Se fornito, ogni passata salva la propria uscita su "<dumpPrefix>.passataN.wav":
    // permette di rianalizzare l'uscita offline con real_export_probe (stessa
    // strumentazione rigorosa usata sull'export reale), senza duplicare la
    // misura di periodicita'/instabilita' in due tool diversi.
    const std::string dumpPrefix = argc > 6 ? argv[6] : "";
    // Se forniti, esegue SOLO dumpPitchTrace su questo intervallo (a piu'
    // block size, per confronto) e termina — non le 4 passate.
    const bool doTrace = argc > 8;
    const double traceStartSec = doTrace ? std::stod (argv[7]) : 0.0;
    const double traceEndSec   = doTrace ? std::stod (argv[8]) : 0.0;
    // Se fornito (9deg argomento, richiede dumpPrefix): esegue SOLO
    // runFixedF0 (f0 costante, bypassa PitchDetector/PitchLatch/Glide) e
    // salva l'uscita — non le 4 passate ne' la traccia.
    const bool doFixedF0 = argc > 9;
    const double fixedF0Hz = doFixedF0 ? std::stod (argv[9]) : 0.0;

    WavFile wav;
    std::string error;
    if (! readWav (path, wav, error))
    {
        std::printf ("Errore leggendo '%s': %s\n", path.c_str(), error.c_str());
        return 1;
    }

    const auto mono = downmix (wav);
    const double sr = wav.sampleRate;
    const double durationSeconds = (double) mono.size() / sr;

    std::printf ("File: %s\n", path.c_str());
    std::printf ("  %.0f Hz, %d canali, %d-bit, %.2fs (%zu campioni mono)\n",
                 sr, wav.numChannels, wav.bitsPerSample, durationSeconds, mono.size());
    std::printf ("  Parametri di prova: Stability=%s, formantSpread=%.2f, rootPitchClass=%d, block=%d\n",
                 Stability::names[std::clamp (stabilityLevel, 0, Stability::numLevels - 1)],
                 formantSpread, rootPitchClass, block);

    // doFixedF0 richiede piu' argomenti di doTrace (argc>9 implica argc>8):
    // va controllato PRIMA, altrimenti doTrace lo intercetterebbe sempre.
    if (doFixedF0)
    {
        std::printf ("\n=== f0 COSTANTE a %.1fHz, unisono (0 semitoni), bypassa PitchDetector/PitchLatch/Glide ===\n", fixedF0Hz);
        const auto fixedWet = runFixedF0 (mono, sr, 0.0f, stabilityLevel, formantSpread, fixedF0Hz, block);
        if (! dumpPrefix.empty())
        {
            writeWavMono (dumpPrefix + ".fixedf0.wav", fixedWet, sr);
            std::printf ("  uscita salvata come %s.fixedf0.wav\n", dumpPrefix.c_str());
        }
        else
        {
            std::printf ("  ATTENZIONE: nessun dumpPrefix fornito, uscita non salvata su disco\n");
        }
        return 0;
    }

    if (doTrace)
    {
        std::printf ("\n=== TRACCIA GREZZA DI PitchDetector, %.3f-%.3fs, a piu' block size ===\n",
                     traceStartSec, traceEndSec);
        for (int b : { 64, 256, 1024, 4096 })
        {
            dumpPitchTrace (mono, sr, b, traceStartSec, traceEndSec);
        }
        return 0;
    }

    const auto dryEvents = findClicks (mono, sr);

    std::printf ("\nPASSATA 1 — voce singola attivata una volta sola, mai piu' riammutolita, offset\n"
                 "fisso a -5 semitoni (isola il comportamento 'a regime'):\n");
    const auto heldWet = runHeld (mono, sr, -5.0f, stabilityLevel, formantSpread, block);
    const auto heldEvents = findClicks (heldWet, sr);
    printReport ("DRY (segnale sorgente, prima del plugin)", dryEvents);
    printReport ("WET", heldEvents);
    printWetOnly (heldEvents, dryEvents);

    std::printf ("\nPASSATA 2 — stesso slot fisico riassegnato ad ogni onset REALE rilevato nel\n"
                 "file, con un nuovo offset armonico ad ogni ri-attacco (ciclo rappresentativo):\n");
    const auto retriggeredWet = runRetriggered (mono, sr, stabilityLevel, formantSpread, block);
    const auto retriggeredEvents = findClicks (retriggeredWet, sr);
    printReport ("DRY (segnale sorgente, prima del plugin)", dryEvents);
    printReport ("WET", retriggeredEvents);
    printWetOnly (retriggeredEvents, dryEvents);

    std::printf ("\nPASSATA 3 — voce attivata una volta sola, offset che segue DAL VIVO PitchLatch\n"
                 "+ la colonna V1 VERA del preset Maj (sessione 18):\n");
    const auto liveHarmonyWet = runLiveHarmony (mono, sr, stabilityLevel, formantSpread, rootPitchClass, block);
    const auto liveHarmonyEvents = findClicks (liveHarmonyWet, sr);
    printReport ("DRY (segnale sorgente, prima del plugin)", dryEvents);
    printReport ("WET", liveHarmonyEvents);
    printWetOnly (liveHarmonyEvents, dryEvents);

    std::printf ("\nPASSATA 4 (sessione 18) — modello a due slot fisici, riproduce PhraseScheduler\n"
                 "a numVoices=1 con keepTails=false:\n");
    const auto productionWet = runProduction (mono, sr, stabilityLevel, formantSpread, rootPitchClass, block);
    const auto productionEvents = findClicks (productionWet, sr);
    printReport ("DRY (segnale sorgente, prima del plugin)", dryEvents);
    printReport ("WET", productionEvents);
    printWetOnly (productionEvents, dryEvents);

    if (! dumpPrefix.empty())
    {
        writeWavMono (dumpPrefix + ".passata1.wav", heldWet, sr);
        writeWavMono (dumpPrefix + ".passata2.wav", retriggeredWet, sr);
        writeWavMono (dumpPrefix + ".passata3.wav", liveHarmonyWet, sr);
        writeWavMono (dumpPrefix + ".passata4.wav", productionWet, sr);
        std::printf ("\n(uscite salvate come %s.passata{1,2,3,4}.wav)\n", dumpPrefix.c_str());
    }

    return 0;
}
