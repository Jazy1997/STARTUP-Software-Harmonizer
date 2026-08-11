// Strumento diagnostico OFFLINE per il "click in mezzo alla nota"/instabilita'
// timbrica segnalati dall'utente in sessione 17 dopo aver riascoltato il fix
// di sessione 16 (che ha risolto solo in parte). Vedi LOG/archivio-s01-s28.md sessione 18
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
// Uso: sample_click_finder <file.wav> [stability=2] [formantSpread=1.0] [rootPitchClass=0] [block=4096] [dumpPrefix] [traceStartSec traceEndSec] [fixedF0Hz] [fixedF0Semitones=0] [analysisStartSec=0.15] [analysisEndSec=1.90]
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
#include "voices/EmptyCellHold.h"
#include "dsp/PitchDetector.h"
#include "dsp/OnsetDetector.h"
#include "harmony/PitchLatch.h"
#include "SampleAnalysis.h"

#include <cstdio>
#include <cstdint>
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
    //
    // NOTA (sessione 25/26): `semitones` era ignorato, chiamato sempre a
    // 0.0f in main() — bloccava il test a f0/offset fissi in unisono. Ora
    // e' un argomento CLI vero (vedi main()), per riprodurre offline le
    // profondita' di shift di V3/V4 (fino a -10 semitoni) senza dover
    // chiedere altri export all'utente.
    std::vector<float> runFixedF0 (const std::vector<float>& mono, double sr, float semitones,
                                   int stabilityLevel, float formantSpread, double fixedF0Hz, int block)
    {
        Voice voice;
        voice.prepare (sr, block, stabilityLevel);
        voice.setFormantSpread (formantSpread);
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (semitones);

        const float fixedMidi = (float) (69.0 + 12.0 * std::log2 (fixedF0Hz / 440.0));

        // Voice::processAdd e' stereo dalla sessione 23 (gain/pan per voce).
        // Qui gain/pan restano al default (mai toccati): L ed R sono
        // bit-identici per costruzione (T-3, tests/voice_test.cpp) — si
        // scrive comunque un canale R di scarto invece di riusare `wet` per
        // entrambi, cosi' una futura chiamata a setGainLinear/setPan in
        // questo file non produrrebbe silenziosamente un risultato sbagliato
        // senza errore.
        std::vector<float> wet (mono.size(), 0.0f);
        std::vector<float> wetR (mono.size(), 0.0f);
        int done = 0;
        while (done + block <= (int) mono.size())
        {
            voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], &wetR[(size_t) done], block, 0, fixedMidi);
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
        std::vector<float> wetR (mono.size(), 0.0f); // canale di scarto, vedi nota su runFixedF0

        int done = 0;
        while (done + block <= (int) mono.size())
        {
            for (int i = 0; i < block; ++i)
                pitchDetector.pushSample (mono[(size_t) (done + i)]);

            const float continuousMidi = pitchDetector.getMidiNote();
            voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], &wetR[(size_t) done], block, 0, continuousMidi);
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
        std::vector<float> wetR (mono.size(), 0.0f); // canale di scarto, vedi nota su runFixedF0

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
                    voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], &wetR[(size_t) done], block, 0, pitchDetector.getMidiNote());
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

            voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], &wetR[(size_t) done], block, 0, continuousMidi);
            done += block;
            firstBlock = false;
        }

        std::printf ("  (%d ri-attacchi rilevati dall'OnsetDetector reale su questo file)\n", numRetriggers);
        return wet;
    }

    // kMajV1Table (colonna V1 vera del preset Maj) e' definita in
    // SampleAnalysis.h, condivisa con real_export_probe.cpp.

    // Passata 3: voce attivata una volta sola (come la 1), offset che segue
    // DAL VIVO PitchLatch + una tabella V1 (di default quella vera del
    // preset Maj — vedi sotto per Passata 7, che passa il preset custom
    // REALE dell'utente, sessione 28).
    std::vector<float> runLiveHarmony (const std::vector<float>& mono, double sr,
                                       int stabilityLevel, float formantSpread, int rootPitchClass,
                                       int block, const float* table = kMajV1Table)
    {
        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);
        OnsetDetector onsetDetector;
        onsetDetector.prepare (sr);
        harmony::PitchLatch pitchLatch;
        pitchLatch.prepare (harmony::PitchLatch::settleSamplesForFrame (pitchDetector.getAnalysisFrameSamples()));

        Voice voice;
        voice.prepare (sr, block, stabilityLevel);
        voice.setFormantSpread (formantSpread);
        voice.setMuted (false);

        std::vector<float> wet (mono.size(), 0.0f);
        std::vector<float> wetR (mono.size(), 0.0f); // canale di scarto, vedi nota su runFixedF0
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
                const int quantizedNote = pitchLatch.update (continuousMidi, onsetThisBlock, block);
                const int degree = degreeOf (quantizedNote, rootPitchClass);
                if (degree != lastDegree)
                {
                    ++numDegreeChanges;
                    lastDegree = degree;
                }

                voice.setMuted (false); // V1 non e' mai muta per Maj, vedi sopra
                voice.setTargetOffsetSemitones (table[degree]);
            }

            voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], &wetR[(size_t) done], block, 0, continuousMidi);
            done += block;
        }

        std::printf ("  (%d cambi di grado armonico rilevati durante il file, root pitch class %d)\n",
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
        pitchLatch.prepare (harmony::PitchLatch::settleSamplesForFrame (pitchDetector.getAnalysisFrameSamples()));

        Voice slots[2];
        slots[0].prepare (sr, block, stabilityLevel);
        slots[1].prepare (sr, block, stabilityLevel);
        slots[0].setFormantSpread (formantSpread);
        slots[1].setFormantSpread (formantSpread);
        int activeSlot = 0; // quello che sta "vivendo" la frase corrente (FR-17 live update)
        bool anyPhraseYet = false;

        std::vector<float> wet (mono.size(), 0.0f);
        std::vector<float> wetR (mono.size(), 0.0f); // canale di scarto, vedi nota su runFixedF0
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
                const int quantizedNote = pitchLatch.update (continuousMidi, onsetThisBlock, block);
                const int degree = degreeOf (quantizedNote, rootPitchClass);
                slots[activeSlot].setMuted (false);
                slots[activeSlot].setTargetOffsetSemitones (kMajV1Table[degree]);
                anyPhraseYet = true;
            }

            for (auto& s : slots)
                if (! s.isSilent())
                    s.processAdd (&mono[(size_t) done], &wet[(size_t) done], &wetR[(size_t) done], block, 0, continuousMidi);

            done += block;
        }

        std::printf ("  (%d onset reali rilevati, modello a 2 slot fisici, root pitch class %d)\n",
                     numOnsets, rootPitchClass);
        return wet;
    }

    // Passata 5 (sessione 27 — feedback utente: "buchi e click a inizio
    // nota" su un preset personalizzato a offset FISSI con celle vuote su
    // alcuni gradi; diagnosi completa in LOG/archivio-s01-s28.md). A differenza delle
    // Passate 1-4 (che usano sempre kMajV1Table, MAI vuota per costruzione
    // — V1 non e' mai muta in un accordo a 4+ toni), qui la tabella e'
    // PARZIALMENTE compilata come il preset dell'utente: un offset fisso
    // indipendente dal grado, solo su ALCUNI gradi, gli altri vuoti — il
    // ramo "cella vuota su frase viva" (FR-17) che le passate precedenti
    // non esercitano mai.
    //
    // warmEmptyCells seleziona cosa succede al motore durante una cella
    // vuota: true (il fix, Voice::processWarmOnly ad ogni blocco) o false
    // (il comportamento precedente a questa sessione, nessuna chiamata) —
    // permette di confrontare i due direttamente sullo STESSO file reale
    // dell'utente senza ricompilare due volte. refillStartsOut riceve
    // l'indice campione (assoluto, nel buffer restituito) di ogni rientro
    // da una cella vuota a una piena: measureHoles() sotto lo usa per
    // misurare il buco a ciascun rientro.
    std::vector<float> runPartialTable (const std::vector<float>& mono, double sr,
                                        int stabilityLevel, float formantSpread, int rootPitchClass,
                                        int block, bool warmEmptyCells,
                                        std::vector<int>& refillStartsOut)
    {
        // Stesso schema del preset custom dell'utente: offset fisso, solo
        // tonica/terza/quinta (gradi 0/4/7) compilati, gli altri 9 vuoti.
        static constexpr float kOffset = -7.0f;
        static const std::optional<float> kSparseTable[12] =
        {
            kOffset, std::nullopt, std::nullopt, std::nullopt, kOffset, std::nullopt,
            std::nullopt, kOffset, std::nullopt, std::nullopt, std::nullopt, std::nullopt
        };

        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);
        OnsetDetector onsetDetector;
        onsetDetector.prepare (sr);
        harmony::PitchLatch pitchLatch;
        pitchLatch.prepare (harmony::PitchLatch::settleSamplesForFrame (pitchDetector.getAnalysisFrameSamples()));

        Voice voice;
        voice.prepare (sr, block, stabilityLevel);
        voice.setFormantSpread (formantSpread);

        // Vero se il blocco PRECEDENTE aveva questa colonna su una cella
        // vuota di una frase viva (non silenzio vero — quello azzera anche
        // wasEmpty, vedi sotto): serve a decidere se processWarmOnly va
        // chiamata questo blocco E a rilevare la transizione vuoto->pieno.
        bool wasEmpty = false;

        std::vector<float> wet (mono.size(), 0.0f);
        std::vector<float> wetR (mono.size(), 0.0f); // canale di scarto, vedi nota su runFixedF0
        int done = 0;

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
                // Silenzio vero (fine frase per davvero, FR-52/freeAllPhrases
                // nella produzione reale): NON e' il caso che questa passata
                // vuole isolare, wasEmpty si azzera senza scaldare nulla.
                pitchLatch.reset();
                voice.setMuted (true);
                wasEmpty = false;
            }
            else if (inputIsStable)
            {
                const int quantizedNote = pitchLatch.update (continuousMidi, onsetThisBlock, block);
                const int degree = degreeOf (quantizedNote, rootPitchClass);
                const auto& cell = kSparseTable[degree];

                if (! cell.has_value())
                {
                    voice.setMuted (true);
                    wasEmpty = true;
                }
                else
                {
                    if (wasEmpty)
                        refillStartsOut.push_back (done); // rientro da cella vuota: il buco si misura da qui
                    wasEmpty = false;
                    voice.setMuted (false);
                    voice.setTargetOffsetSemitones (*cell);
                }
            }
            // else: segnale presente ma pitch non ancora confidente — si
            // mantiene lo stato del blocco precedente, stessa logica di
            // PhraseScheduler::process() (vedi il commento su quel ramo).

            if (! voice.isSilent())
                voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], &wetR[(size_t) done], block, 0, continuousMidi);
            else if (wasEmpty && warmEmptyCells)
                voice.processWarmOnly (&mono[(size_t) done], block, continuousMidi);

            done += block;
        }

        std::printf ("  (%zu rientri da cella vuota rilevati, tabella a gradi PARZIALI {0,4,7}=%.0f semitoni,\n"
                     "   root pitch class %d, warmEmptyCells=%s)\n",
                     refillStartsOut.size(), kOffset, rootPitchClass,
                     warmEmptyCells ? "true (il fix)" : "false (comportamento precedente a sessione 27)");
        return wet;
    }

    // Misura il buco a ogni rientro (refillStartsOut di runPartialTable):
    // inviluppo RMS a hop fine (2ms) su una finestra locale subito dopo il
    // rientro, confrontato col PROPRIO picco (non un tratto di regime
    // separato — piu' robusto su materiale reale dove il livello a regime
    // varia nota per nota). Stesso principio della misura in H5,
    // tests/voice_test.cpp, qui su audio reale invece che sintetico.
    void measureHoles (const std::vector<float>& wet, double sr, const std::vector<int>& refillStarts)
    {
        const int hop = std::max (1, (int) (0.002 * sr)); // 2ms
        const int winSamples = (int) (0.080 * sr); // 80ms: copre il caso peggiore (Accurate, 30+8ms) con margine

        for (int startSample : refillStarts)
        {
            if (startSample < 0 || startSample + winSamples > (int) wet.size())
                continue;

            const std::vector<float> segment (wet.begin() + startSample, wet.begin() + startSample + winSamples);
            const auto env = envelopeRms (segment, hop);
            if (env.empty())
                continue;

            const double peak = *std::max_element (env.begin(), env.end());
            if (peak <= 1.0e-6)
            {
                std::printf ("    t=%7.3fs  nessun segnale utile nella finestra di misura\n", (double) startSample / sr);
                continue;
            }

            int firstAbove = -1;
            for (size_t i = 0; i < env.size(); ++i)
                if (env[i] >= 0.5 * peak) { firstAbove = (int) i; break; }

            const double holeMs = firstAbove < 0 ? -1.0 : (1000.0 * (double) firstAbove * hop / sr);
            std::printf ("    t=%7.3fs  buco = %6.2f ms\n", (double) startSample / sr, holeMs);
        }
    }

    // =========================================================================
    // FASE 0 (piano "ribattuto", sessione 28 — vedi
    // c:\Users\cazza\.claude\plans\ciao-claude-stiamo-facendo-crispy-hellman.md):
    // PROVA, non fix. Prima di scrivere qualunque debounce del gate (L1), va
    // costruita una prova che usa il codice VERO (OnsetDetector reale, non una
    // ricostruzione a mano) sul materiale reale, e che logga anche gli eventi
    // di allocazione/binding di un modello fedele a PhraseScheduler — non solo
    // le transizioni del gate. Una ricostruzione a mano dell'autore di questo
    // piano, fatta in sola lettura senza poter eseguire il codice vero, aveva
    // trovato UNA sola transizione in 8s su "Test 1" (il gate non si richiude
    // mai): questa passata rifà la stessa misura col codice vero per
    // confermarla o smentirla, PIU' il meccanismo di binding che la
    // ricostruzione a mano non copriva affatto.
    //
    // Due log distinti:
    //   1. ogni transizione del gate (aperto<->chiuso), OnsetDetector VERO,
    //      sample per sample sul segnale grezzo — decide se il Meccanismo A
    //      (chiusura spuria durante una valle naturale dentro una nota tenuta)
    //      e' anche solo POSSIBILE su questo materiale.
    //   2. un modello a N slot fisici con Voice REALI (non finte: isSilent()/
    //      ampGlide/goCold() sono gli stessi meccanismi della produzione),
    //      che replica allocateFreeSlot/triggerNewPhrase/late-binding di
    //      PhraseScheduler.cpp per la SOLA colonna sparsa (kSparseTable,
    //      offset -7 sui gradi 0/4/7 — stessa tabella di Passata 5,
    //      keepTails=false) — logga ogni volta che un fisico slot viene
    //      legato per la PRIMA VOLTA IN ASSOLUTO a questa colonna, e se e'
    //      successo AL TRIGGER (stesso blocco dell'onset — compatibile sia
    //      con Meccanismo A che con un ri-attacco vero) o in LATE BINDING
    //      (blocchi dopo, quando il pitch diventa confidente — la firma del
    //      Meccanismo C).
    //
    // Duplica la logica di PhraseScheduler invece di linkarla (dipende da
    // juce_core via VoicePool/HarmonyPreset.h, non linkabile in un target
    // headless come questo) — stesso rischio noto di divergenza gia'
    // documentato per Passata 4/5 sopra.
    // =========================================================================
    struct GateTransition { int sampleIndex; bool opened; };

    struct SlotBindEvent
    {
        int sampleIndex;
        int physicalSlot;
        bool firstEverUseOfThisSlot;
        bool isLateBinding; // legato blocchi dopo il trigger, non nello stesso blocco dell'onset
    };

    // Sotto-passata 1: transizioni del gate, OnsetDetector VERO.
    std::vector<GateTransition> traceGateTransitions (const std::vector<float>& mono, double sr)
    {
        OnsetDetector onsetDetector;
        onsetDetector.prepare (sr);

        std::vector<GateTransition> transitions;
        bool wasOpen = false;
        for (size_t i = 0; i < mono.size(); ++i)
        {
            onsetDetector.pushSample (mono[i]);
            const bool isOpen = onsetDetector.isGateOpen();
            if (isOpen != wasOpen)
            {
                transitions.push_back ({ (int) i, isOpen });
                wasOpen = isOpen;
            }
        }
        return transitions;
    }

    // Sotto-passata 2: modello a N slot fisici (Voice reali), stessa
    // scansione di allocateFreeSlot, keepTails=false, colonna sparsa.
    struct MiniPhrase
    {
        int slot = -1;
        bool active = false;
        bool isLive = false;
        bool releasing = false;
        uint64_t age = 0;
    };

    std::vector<SlotBindEvent> traceSchedulerBindings (const std::vector<float>& mono, double sr,
                                                        int stabilityLevel, float formantSpread,
                                                        int rootPitchClass, int block, int numPhysicalSlots,
                                                        std::vector<bool>& everUsedOut)
    {
        static constexpr float kOffset = -7.0f;
        static const std::optional<float> kSparseTable[12] =
        {
            kOffset, std::nullopt, std::nullopt, std::nullopt, kOffset, std::nullopt,
            std::nullopt, kOffset, std::nullopt, std::nullopt, std::nullopt, std::nullopt
        };

        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);
        OnsetDetector onsetDetector;
        onsetDetector.prepare (sr);
        harmony::PitchLatch pitchLatch;
        pitchLatch.prepare (harmony::PitchLatch::settleSamplesForFrame (pitchDetector.getAnalysisFrameSamples()));

        std::vector<Voice> slots ((size_t) numPhysicalSlots);
        for (auto& s : slots)
        {
            s.prepare (sr, block, stabilityLevel);
            s.setFormantSpread (formantSpread);
        }
        everUsedOut.assign ((size_t) numPhysicalSlots, false);

        // Capacita' pari al numero di slot fisici, come phrases.size() ==
        // hardSlotCapacity nel codice reale (PhraseScheduler::prepare).
        std::vector<MiniPhrase> phrases ((size_t) numPhysicalSlots);
        uint64_t ageCounter = 0;

        auto isSlotInUse = [&] (int slotIndex)
        {
            for (auto& p : phrases)
                if (p.active && p.slot == slotIndex)
                    return true;
            return false;
        };
        auto allocateFreeSlot = [&] () -> int
        {
            for (int i = 0; i < numPhysicalSlots; ++i)
                if (! isSlotInUse (i))
                    return i;

            MiniPhrase* victim = nullptr;
            for (auto& p : phrases)
                if (p.active && p.releasing && (victim == nullptr || p.age < victim->age))
                    victim = &p;
            if (victim == nullptr)
                for (auto& p : phrases)
                    if (p.active && (victim == nullptr || p.age < victim->age))
                        victim = &p;
            if (victim == nullptr)
                return -1;

            victim->active = false;
            victim->releasing = false;
            victim->slot = -1;

            for (int i = 0; i < numPhysicalSlots; ++i)
                if (! isSlotInUse (i))
                    return i;
            return -1;
        };

        auto bindSlot = [&] (MiniPhrase& p, int slot, int sampleIndex, bool lateBinding, float offsetSemitones,
                             std::vector<SlotBindEvent>& events)
        {
            p.slot = slot;
            const bool firstUse = ! everUsedOut[(size_t) slot];
            everUsedOut[(size_t) slot] = true;
            events.push_back ({ sampleIndex, slot, firstUse, lateBinding });
            slots[(size_t) slot].setMuted (false);
            slots[(size_t) slot].setTargetOffsetSemitones (offsetSemitones);
        };

        std::vector<SlotBindEvent> bindEvents;
        std::vector<float> scratchL ((size_t) block, 0.0f), scratchR ((size_t) block, 0.0f);

        int done = 0;
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
                for (auto& p : phrases)
                    if (p.active) { p.isLive = false; p.releasing = true; }
            }
            else if (onsetThisBlock)
            {
                // keepTails=false: tutte le fasi attive iniziano il rilascio.
                for (auto& p : phrases)
                    if (p.active) { p.isLive = false; p.releasing = true; }

                MiniPhrase* np = nullptr;
                for (auto& p : phrases)
                    if (! p.active) { np = &p; break; }

                if (np != nullptr)
                {
                    np->active = true;
                    np->isLive = true;
                    np->releasing = false;
                    np->slot = -1;
                    np->age = ++ageCounter;

                    const int quantizedNote = pitchLatch.update (continuousMidi, true, block);
                    const int degree = degreeOf (quantizedNote, rootPitchClass);
                    const auto& cell = kSparseTable[degree];
                    if (cell.has_value())
                    {
                        const int slot = allocateFreeSlot();
                        if (slot >= 0)
                            bindSlot (*np, slot, done, false, *cell, bindEvents);
                    }
                }
            }
            else if (inputIsStable)
            {
                const int quantizedNote = pitchLatch.update (continuousMidi, false, block);
                const int degree = degreeOf (quantizedNote, rootPitchClass);
                const auto& cell = kSparseTable[degree];

                for (auto& p : phrases)
                {
                    if (! p.active || ! p.isLive)
                        continue;

                    if (p.slot < 0 && cell.has_value())
                    {
                        const int slot = allocateFreeSlot();
                        if (slot >= 0)
                            bindSlot (p, slot, done, true, *cell, bindEvents);
                    }
                    else if (p.slot >= 0)
                    {
                        if (! cell.has_value())
                            slots[(size_t) p.slot].setMuted (true);
                        else
                        {
                            slots[(size_t) p.slot].setMuted (false);
                            slots[(size_t) p.slot].setTargetOffsetSemitones (*cell);
                        }
                    }
                }
            }

            for (int s = 0; s < numPhysicalSlots; ++s)
                if (! slots[(size_t) s].isSilent())
                    slots[(size_t) s].processAdd (&mono[(size_t) done], scratchL.data(), scratchR.data(),
                                                  block, 0, continuousMidi);

            for (auto& p : phrases)
            {
                if (p.releasing && p.slot >= 0 && slots[(size_t) p.slot].isSilent())
                {
                    slots[(size_t) p.slot].goCold();
                    p.active = false;
                    p.releasing = false;
                    p.slot = -1;
                }
            }

            done += block;
        }

        return bindEvents;
    }

    void runFaseZeroTrace (const std::vector<float>& mono, double sr, int stabilityLevel,
                          float formantSpread, int rootPitchClass, int block, int numPhysicalSlots)
    {
        std::printf ("\n=== FASE 0 (piano ribattuto) — transizioni del gate, OnsetDetector VERO, sample-per-sample ===\n");
        const auto gateTransitions = traceGateTransitions (mono, sr);
        std::printf ("  %zu transizioni in %.3fs di materiale\n", gateTransitions.size(), (double) mono.size() / sr);
        for (size_t i = 0; i < gateTransitions.size(); ++i)
        {
            const auto& t = gateTransitions[i];
            const double tSec = (double) t.sampleIndex / sr;
            if (i == 0)
                std::printf ("    t=%8.4fs  gate %s\n", tSec, t.opened ? "APRE" : "CHIUDE");
            else
            {
                const double durationMs = 1000.0 * (double) (t.sampleIndex - gateTransitions[i - 1].sampleIndex) / sr;
                std::printf ("    t=%8.4fs  gate %s  (stato precedente durato %8.2fms)\n",
                             tSec, t.opened ? "APRE" : "CHIUDE", durationMs);
            }
        }
        if (gateTransitions.empty())
            std::printf ("    (il gate non ha mai cambiato stato sull'intero file)\n");

        std::printf ("\n=== FASE 0 — modello a %d slot fisici (Voice reali), colonna sparsa {0,4,7}=-7st,\n"
                     "    stessa scansione di allocateFreeSlot, keepTails=false, root pitch class %d ===\n",
                     numPhysicalSlots, rootPitchClass);
        std::vector<bool> everUsed;
        const auto bindEvents = traceSchedulerBindings (mono, sr, stabilityLevel, formantSpread, rootPitchClass,
                                                         block, numPhysicalSlots, everUsed);

        int lateBindCount = 0, firstUseCount = 0, firstUseAtTrigger = 0;
        for (auto& e : bindEvents)
        {
            if (e.isLateBinding) ++lateBindCount;
            if (e.firstEverUseOfThisSlot)
            {
                ++firstUseCount;
                if (! e.isLateBinding) ++firstUseAtTrigger;
            }
        }
        std::printf ("  %zu eventi di binding totali (%d al trigger, %d in late-binding);\n"
                     "  %d sono PRIMO USO ASSOLUTO del fisico slot (%d al trigger, %d in late-binding)\n",
                     bindEvents.size(), (int) bindEvents.size() - lateBindCount, lateBindCount,
                     firstUseCount, firstUseAtTrigger, firstUseCount - firstUseAtTrigger);
        for (auto& e : bindEvents)
            std::printf ("    t=%8.4fs  slot=%d  %s%s\n", (double) e.sampleIndex / sr, e.physicalSlot,
                         e.firstEverUseOfThisSlot ? "PRIMO USO" : "riuso (slot gia' visto prima)",
                         e.isLateBinding ? "  [LATE BINDING — Meccanismo C]" : "  [al trigger]");
        if (bindEvents.empty())
            std::printf ("    (nessun binding: la colonna sparsa non e' mai risultata su un grado compilato)\n");
    }

    // =========================================================================
    // Passata 6 (sessione 28 — piano "ribattuto", fix scelto dall'utente:
    // isteresi FR-17, vedi src/voices/EmptyCellHold.h). Stessa tabella sparsa
    // di Passata 5, ma il mute su cella vuota passa dalla funzione pura
    // condivisa con PhraseScheduler.cpp — nessuna duplicazione della
    // decisione stessa, solo dell'orchestrazione attorno (stesso rischio
    // noto di divergenza delle altre passate, che duplicano PhraseScheduler
    // per intero perche' non linkabile qui).
    //
    // holdThresholdSamples=0 riproduce ESATTAMENTE Passata 5 (rete di
    // sicurezza: i rientri/buchi misurati devono combaciare) — un valore
    // maggiore e' il fix. refillStartsOut riceve SOLO i rientri da un mute
    // REALMENTE scattato (soglia superata): un'attesa risolta senza mai
    // mutare non e' un rientro, non c'e' stato alcun buco/re-attacco.
    // =========================================================================
    std::vector<float> runPartialTableWithHold (const std::vector<float>& mono, double sr,
                                                int stabilityLevel, float formantSpread, int rootPitchClass,
                                                int block, int holdThresholdSamples,
                                                std::vector<int>& refillStartsOut, int& numSuppressedOut)
    {
        static constexpr float kOffset = -7.0f;
        static const std::optional<float> kSparseTable[12] =
        {
            kOffset, std::nullopt, std::nullopt, std::nullopt, kOffset, std::nullopt,
            std::nullopt, kOffset, std::nullopt, std::nullopt, std::nullopt, std::nullopt
        };

        PitchDetector pitchDetector;
        pitchDetector.prepare (sr);
        OnsetDetector onsetDetector;
        onsetDetector.prepare (sr);
        harmony::PitchLatch pitchLatch;
        pitchLatch.prepare (harmony::PitchLatch::settleSamplesForFrame (pitchDetector.getAnalysisFrameSamples()));

        Voice voice;
        voice.prepare (sr, block, stabilityLevel);
        voice.setFormantSpread (formantSpread);

        int emptySamples = 0;
        int excursionStartSample = 0;
        bool wasMuted = false; // vero solo se l'isteresi ha DAVVERO fatto scattare un mute
        numSuppressedOut = 0;

        std::vector<float> wet (mono.size(), 0.0f);
        std::vector<float> wetR (mono.size(), 0.0f); // canale di scarto, vedi nota su runFixedF0
        int done = 0;

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
                voice.setMuted (true);
                emptySamples = 0;
                wasMuted = false;
            }
            else if (inputIsStable)
            {
                const int quantizedNote = pitchLatch.update (continuousMidi, onsetThisBlock, block);
                const int degree = degreeOf (quantizedNote, rootPitchClass);
                const auto& cell = kSparseTable[degree];

                if (! cell.has_value())
                {
                    if (emptySamples == 0)
                        excursionStartSample = done; // primo blocco vuoto di una nuova attesa
                    const auto hold = stepEmptyCellHold (emptySamples, block, false, holdThresholdSamples);
                    emptySamples = hold.emptySamplesAfter;
                    if (hold.shouldMuteNow)
                    {
                        wasMuted = true;
                        voice.setMuted (true);
                    }
                }
                else
                {
                    const int excursionSamples = emptySamples > 0 ? (done - excursionStartSample) : 0;
                    if (wasMuted)
                    {
                        refillStartsOut.push_back (done); // rientro da un mute VERO: qui si misura il buco
                        std::printf ("    attesa a t=%7.3fs durata %6.1fms -> MUTATA (soglia superata)\n",
                                     (double) excursionStartSample / sr, 1000.0 * excursionSamples / sr);
                    }
                    else if (emptySamples > 0)
                    {
                        ++numSuppressedOut; // attesa risolta SENZA mai mutare: il fix ha funzionato qui
                        std::printf ("    attesa a t=%7.3fs durata %6.1fms -> SOPPRESSA (mai mutata)\n",
                                     (double) excursionStartSample / sr, 1000.0 * excursionSamples / sr);
                    }
                    emptySamples = 0;
                    wasMuted = false;
                    voice.setMuted (false);
                    voice.setTargetOffsetSemitones (*cell);
                }
            }
            // else: segnale presente ma pitch non ancora confidente — si
            // mantiene lo stato del blocco precedente, stessa logica delle
            // altre passate/di PhraseScheduler::process().

            if (! voice.isSilent())
                voice.processAdd (&mono[(size_t) done], &wet[(size_t) done], &wetR[(size_t) done], block, 0, continuousMidi);
            else if (wasMuted)
                voice.processWarmOnly (&mono[(size_t) done], block, continuousMidi);

            done += block;
        }

        std::printf ("  (holdThresholdSamples=%d [%.1fms]: %zu rientri da mute VERO, %d attese risolte SENZA mutare mai)\n",
                     holdThresholdSamples, 1000.0 * holdThresholdSamples / sr, refillStartsOut.size(), numSuppressedOut);
        return wet;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("Uso: sample_click_finder <file.wav> [stability=2] [formantSpread=1.0] [rootPitchClass=0] [block=4096] [dumpPrefix] [traceStartSec traceEndSec] [fixedF0Hz] [fixedF0Semitones=0] [analysisStartSec=0.15] [analysisEndSec=1.90]\n");
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
    // Sessione 25/26: offset in semitoni per runFixedF0, prima fisso a 0.0f
    // in chiamata (unisono) — ora un argomento CLI vero, per riprodurre
    // offline le profondita' di shift di V3/V4 (fino a -10 semitoni) senza
    // altri export dall'utente. Default 0.0f: comportamento IDENTICO a prima
    // se non specificato.
    const float fixedF0Semitones = argc > 10 ? std::stof (argv[10]) : 0.0f;
    // Sessione 26 (confronto motori): la finestra di analisi era fissa
    // (0.15-1.90s), calibrata sulla latenza del PSOLA (~21ms a Balanced).
    // SpectralShifter ha latenza ~90ms: con la finestra fissa il confronto
    // fra motori sarebbe viziato (il transitorio di uno finirebbe dentro la
    // finestra dell'altro). Ora due argomenti CLI opzionali — default
    // IDENTICI a prima se non specificati, nessun cambiamento per l'uso
    // gia' in produzione di questo strumento.
    const double analysisStartSecArg = argc > 11 ? std::stod (argv[11]) : 0.15;
    const double analysisEndSecArg   = argc > 12 ? std::stod (argv[12]) : 1.90;

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
        std::printf ("\n=== f0 COSTANTE a %.1fHz, offset %.1f semitoni, bypassa PitchDetector/PitchLatch/Glide ===\n",
                     fixedF0Hz, fixedF0Semitones);
        const auto fixedWet = runFixedF0 (mono, sr, fixedF0Semitones, stabilityLevel, formantSpread, fixedF0Hz, block);
        if (! dumpPrefix.empty())
        {
            writeWavMono (dumpPrefix + ".fixedf0.wav", fixedWet, sr);
            std::printf ("  uscita salvata come %s.fixedf0.wav\n", dumpPrefix.c_str());
        }
        else
        {
            std::printf ("  ATTENZIONE: nessun dumpPrefix fornito, uscita non salvata su disco\n");
        }

        // Sessione 25/26 (indagine sul timbro granuloso a offset profondo,
        // V3/V4 del preset Maj): analisi diretta del segmento dove
        // fixedF0Hz e' effettivamente valido — stessa finestra usata in
        // sessione 20, Fase 0 (il plateau C4 di "Test 1 - Basic Silk
        // Horns.wav" e' 0.09-2.01s: fuori da li' il file suona altre note e
        // runFixedF0 e' degradato per costruzione, non per un difetto). Qui
        // la f0 di TARGET e' NOTA per costruzione (fixedF0Hz spostato di
        // fixedF0Semitones): a differenza di voice_reference_probe.cpp non
        // serve un file "Ideal" separato per misurare periodicita'/energia
        // armonica — permette di esplorare offline molte profondita' di
        // shift senza altri export dall'utente.
        {
            const double targetF0Hz = fixedF0Hz * std::exp2 ((double) fixedF0Semitones / 12.0);
            const double analysisStartSec = analysisStartSecArg, analysisEndSec = analysisEndSecArg;
            const int winLenA = (int) (0.030 * sr);
            const int hopA    = (int) (0.010 * sr);

            int framesConsidered = 0, framesUnstable = 0, nHnr = 0;
            double sumHnrTarget = 0.0, sumHnrOriginal = 0.0;

            for (int from = (int) (analysisStartSec * sr); from + winLenA <= (int) (analysisEndSec * sr); from += hopA)
            {
                const auto fs = measureFrame (fixedWet, from, winLenA, sr);
                if (fs.f0Hz <= 0.0) continue;
                ++framesConsidered;
                if (fs.periodicity < 0.90) ++framesUnstable;

                sumHnrTarget   += harmonicEnergyRatio (fixedWet, from, winLenA, sr, targetF0Hz);
                sumHnrOriginal += harmonicEnergyRatio (fixedWet, from, winLenA, sr, fixedF0Hz);
                ++nHnr;
            }

            std::printf ("  Analisi %.2f-%.2fs (target f0=%.1fHz): finestre tonali=%d, instabili "
                         "(periodicita'<0.90)=%d (%.1f%%), HNR medio al target=%.3f, HNR medio al pitch "
                         "ORIGINALE non trasposto (%.1fHz)=%.3f\n",
                         analysisStartSec, analysisEndSec, targetF0Hz, framesConsidered, framesUnstable,
                         framesConsidered > 0 ? 100.0 * framesUnstable / framesConsidered : 0.0,
                         nHnr > 0 ? sumHnrTarget / nHnr : 0.0,
                         fixedF0Hz, nHnr > 0 ? sumHnrOriginal / nHnr : 0.0);
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

    runFaseZeroTrace (mono, sr, stabilityLevel, formantSpread, rootPitchClass, block, /*numPhysicalSlots*/ 4);

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

    // Passata 5 (sessione 27): a differenza di 1-4, la tabella qui ha celle
    // VUOTE su alcuni gradi (kMajV1Table non ce l'ha mai) — il ramo FR-17
    // esatto colpito dal preset custom dell'utente. Eseguita due volte sullo
    // STESSO file: senza il fix (comportamento precedente a questa sessione)
    // e con (Voice::processWarmOnly) — cosi' il buco si vede o non si vede
    // sullo stesso materiale, senza dover ricompilare.
    std::printf ("\nPASSATA 5 (sessione 27) — tabella a gradi PARZIALI (0/4/7 compilati, gli altri\n"
                 "vuoti): il ramo 'cella vuota su frase viva' che le passate 1-4 non esercitano mai.\n");
    std::vector<int> refillsNoFix, refillsWithFix;
    std::printf (" -- SENZA il fix (warmEmptyCells=false) --\n");
    const auto partialNoFixWet = runPartialTable (mono, sr, stabilityLevel, formantSpread, rootPitchClass,
                                                  block, false, refillsNoFix);
    measureHoles (partialNoFixWet, sr, refillsNoFix);
    std::printf (" -- CON il fix (warmEmptyCells=true) --\n");
    const auto partialWithFixWet = runPartialTable (mono, sr, stabilityLevel, formantSpread, rootPitchClass,
                                                    block, true, refillsWithFix);
    measureHoles (partialWithFixWet, sr, refillsWithFix);

    // Passata 6 (sessione 28): stessa tabella sparsa, ma con l'isteresi
    // FR-17 (fix scelto dall'utente dopo Fase 0 — vedi LOG/archivio-s01-s28.md sessione
    // 28) al posto del mute immediato su cella vuota. holdThresholdSamples=0
    // e' una rete di sicurezza (deve riprodurre esattamente i rientri di
    // Passata 5 sopra); 80ms e' il punto di partenza da tarare all'ascolto.
    std::printf ("\nPASSATA 6 (sessione 28) — stessa tabella a gradi PARZIALI, isteresi FR-17 prima\n"
                 "di mutare una colonna gia' legata su una frase viva (src/voices/EmptyCellHold.h):\n");
    std::vector<int> refillsHold0, refillsHold80ms;
    int suppressedHold0 = 0, suppressedHold80ms = 0;
    std::printf (" -- holdThresholdSamples=0 (rete di sicurezza, deve combaciare con Passata 5 SENZA il fix) --\n");
    const auto hold0Wet = runPartialTableWithHold (mono, sr, stabilityLevel, formantSpread, rootPitchClass,
                                                   block, 0, refillsHold0, suppressedHold0);
    measureHoles (hold0Wet, sr, refillsHold0);
    const int holdThresholdSamples80ms = (int) std::lround (0.080 * sr);
    std::printf (" -- holdThresholdSamples=80ms (il fix) --\n");
    const auto hold80msWet = runPartialTableWithHold (mono, sr, stabilityLevel, formantSpread, rootPitchClass,
                                                      block, holdThresholdSamples80ms, refillsHold80ms, suppressedHold80ms);
    measureHoles (hold80msWet, sr, refillsHold80ms);

    // Passata 7 (sessione 28): TABELLA REALE dell'utente (R=-7, 2=-8,
    // 3(maggiore)=-10, il resto mai visitato da questa melodia C4-D4-E4-C4 —
    // vedi LOG/archivio-s01-s28.md). A differenza di Passata 3 (kMajV1Table, shift
    // massimo -3 semitoni) qui lo shift arriva a -10: verifica se la
    // PROFONDITA' dello shift (non il contenuto armonico/FR-17, gia'
    // escluso dai dati dell'utente) e' la variabile che riproduce il buco
    // digitale vero misurato su "Fix ribattuto V1.wav" col codice reale.
    static constexpr float kRealPresetV1Table[12] =
    {
        -7.0f, 0.0f, -8.0f, 0.0f, -10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };
    std::printf ("\nPASSATA 7 (sessione 28) — tabella REALE dell'utente (R=-7,2=-8,3=-10), stesso\n"
                 "modello di Passata 3 (voce continua, real-time PitchDetector/OnsetDetector):\n");
    const auto realPresetWet = runLiveHarmony (mono, sr, stabilityLevel, formantSpread, rootPitchClass,
                                               block, kRealPresetV1Table);

    if (! dumpPrefix.empty())
    {
        writeWavMono (dumpPrefix + ".passata1.wav", heldWet, sr);
        writeWavMono (dumpPrefix + ".passata2.wav", retriggeredWet, sr);
        writeWavMono (dumpPrefix + ".passata3.wav", liveHarmonyWet, sr);
        writeWavMono (dumpPrefix + ".passata4.wav", productionWet, sr);
        writeWavMono (dumpPrefix + ".passata5_noFix.wav", partialNoFixWet, sr);
        writeWavMono (dumpPrefix + ".passata5_withFix.wav", partialWithFixWet, sr);
        writeWavMono (dumpPrefix + ".passata6_hold0.wav", hold0Wet, sr);
        writeWavMono (dumpPrefix + ".passata6_hold80ms.wav", hold80msWet, sr);
        writeWavMono (dumpPrefix + ".passata7_realPreset.wav", realPresetWet, sr);
        std::printf ("\n(uscite salvate come %s.passata{1,2,3,4}.wav, %s.passata5_{noFix,withFix}.wav,\n"
                     " %s.passata6_{hold0,hold80ms}.wav e %s.passata7_realPreset.wav)\n",
                     dumpPrefix.c_str(), dumpPrefix.c_str(), dumpPrefix.c_str(), dumpPrefix.c_str());
    }

    return 0;
}
