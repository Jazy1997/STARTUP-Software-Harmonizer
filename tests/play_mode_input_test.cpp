// Banco di misura dell'attacco di nota in modalita' Play (sessione 34).
//
// Sintomo riportato all'ascolto dall'utente: in Play, premendo un tasto della
// tastiera MIDI, l'armonizzazione e' corretta ma si sente un CLICK all'inizio,
// e si sente "ogni volta che premo il tasto" — non solo alla primissima nota
// dopo il silenzio.
//
// CLAUDE.md regola 12: non posso ascoltare. Questo file traduce il sintomo in
// misure numeriche, e lo fa PRIMA di scrivere qualunque fix — la lezione di
// D-09 ("verificare col codice vero prima di fidarsi di una ricostruzione in
// sola lettura") e quella di sessione 14 (un fix "plausibile" scritto senza
// misura, poi smentito all'ascolto) valgono entrambe qui.
//
// A differenza di tests/voice_test.cpp (che misura una Voice isolata) e di
// tests/phrase_scheduler_test.cpp (la catena Harmonizer), questo file pilota
// il VERO PlayModeInput con veri juce::MidiBuffer: e' l'unico livello a cui
// esistono gli eventi note-on/note-off, l'allocazione degli slot e la
// decisione di alimentare o no il motore — cioe' il perimetro del difetto.
//
// Metriche (i nomi PM-n sono citati in BUGS.md/B-15 e in LOG/sessione-34.md):
//   PM-1  ritardo dal note-on alla prima uscita udibile
//   PM-2  tempo di salita 10%->90% dell'inviluppo (deve valere kDeclickMs=8ms:
//         se e' molto piu' corto, la dissolvenza si e' consumata nel silenzio
//         del motore freddo e la voce entra "di netto" — B-12)
//   PM-3  massimo salto campione-campione all'attacco, rapportato al regime
//   PM-4  intonazione del wet nella finestra di dissolvenza: e' la nota
//         richiesta o quella precedente?
//   PM-5  stesse misure sulla SECONDA nota, dopo un note-off completo — il
//         caso riportato dall'utente ("ogni volta che premo il tasto")
//   PM-6  nota ribattuta entro la dissolvenza: scatto o scivolata?
//   PM-7  controllo: seconda nota alla STESSA altezza. Non e' un caso da
//         correggere (era gia' pulito), e' l'esperimento che ha isolato il
//         cambio di rapporto di trasposizione come causa di PM-5.
//
// Il cancello duro e' su PM-3 (vedi il blocco CANCELLI in fondo per il perche'
// non su PM-2, che sembrava la scelta ovvia ma misura male su questo
// percorso). PM-1/PM-2/PM-4/PM-6 sono riportate, non gattate. Nessuna misura
// qui puo' dichiarare chiuso il sintomo: serve la conferma all'ascolto
// dell'utente (CLAUDE.md regola 12/14).
//
// Compilazione (vedi anche il target CMake `play_mode_input_test`): linka
// juce_audio_basics per juce::MidiBuffer, quindi sta nel secondo livello di
// D-16 (come phrase_scheduler_test), non nel gate a g++ nudo della CI.

#include "midi/PlayModeInput.h"
#include "TestSignals.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

namespace
{

constexpr double SR = 44100.0;   // stessa SR della configurazione reale dell'utente
constexpr int    kBlock = 256;   // buffer host "normale"; i buffer lunghi sono coperti a parte
constexpr int    kMaxBlockPrepare = 4096;

// Il segnale in ingresso: una sorgente tenuta e stabile, come il setup di
// riferimento del PRD ("traccia MIDI vuota + traccia audio"). In Play il
// plugin non e' un sintetizzatore: senza un ingresso vivo non c'e' nulla da
// trasporre, quindi la sorgente e' presente PRIMA del note-on e resta
// invariata — cosi' l'unica variabile della misura e' il tasto premuto.
constexpr double kInputF0 = 220.0;                       // A3
const float kInputMidi = (float) (69.0 + 12.0 * std::log2 (kInputF0 / 440.0));

double midiToHz (int note) { return 440.0 * std::pow (2.0, ((double) note - 69.0) / 12.0); }

// Energia nelle prime armoniche di f0, su una finestra di Hann (goertzelMag
// di TestSignals.h). Serve a PM-4: su una finestra CORTA e a cavallo di una
// transizione di intonazione, measureF0 (autocorrelazione) non e' affidabile
// — sbaglia ottava e restituisce valori che non corrispondono a nessuna delle
// due note in gioco. La domanda vera pero' non e' "quale f0" ma "quale delle
// DUE note note e' quella che suona", e a quella si risponde confrontando
// direttamente l'energia armonica alle due frequenze, senza stimare nulla.
double harmonicEnergy (const std::vector<float>& x, int from, int len, double sr, double f0)
{
    double e = 0.0;
    for (int h = 1; h <= 5 && f0 * h < 0.45 * sr; ++h)
    {
        const double m = goertzelMag (x, from, len, sr, f0 * (double) h);
        e += m * m;
    }
    return e;
}

// ---------------------------------------------------------------------------
// Inviluppo RMS a finestra scorrevole, causale, e primo attraversamento di
// soglia. Stessa matematica di tests/voice_test.cpp (movingRms/firstCrossing):
// duplicata QUI e non condivisa via TestSignals.h di proposito — voice_test e'
// la rete di regressione di D-19 e non va toccato in una sessione che indaga
// un sintomo nuovo. Se le due copie dovessero divergere, e' questa a doversi
// riallineare a quella.
// ---------------------------------------------------------------------------
std::vector<double> movingRms (const std::vector<float>& x, int win)
{
    std::vector<double> env (x.size(), 0.0);
    double sumSq = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        sumSq += (double) x[i] * (double) x[i];
        if (i >= (size_t) win)
            sumSq -= (double) x[i - (size_t) win] * (double) x[i - (size_t) win];
        const int n = (int) std::min<size_t> (i + 1, (size_t) win);
        env[i] = std::sqrt (sumSq / n);
    }
    return env;
}

int firstCrossing (const std::vector<double>& env, int from, double threshold)
{
    for (size_t i = (size_t) from; i < env.size(); ++i)
        if (env[i] >= threshold)
            return (int) i;
    return -1;
}

// ---------------------------------------------------------------------------
// Pilotaggio del vero PlayModeInput.
// ---------------------------------------------------------------------------
struct ScheduledMidi
{
    int blockIndex;            // il blocco in cui il messaggio entra nel MidiBuffer
    juce::MidiMessage message;
};

struct Capture
{
    std::vector<float> left, right;
};

Capture runPlay (PlayModeInput& play, const std::vector<float>& src, int totalSamples,
                 const std::vector<ScheduledMidi>& events,
                 bool modeActive = true, bool inputIsStable = true)
{
    Capture cap;
    cap.left .assign ((size_t) totalSamples, 0.0f);
    cap.right.assign ((size_t) totalSamples, 0.0f);

    int done = 0, blockIndex = 0;
    while (done + kBlock <= totalSamples)
    {
        juce::MidiBuffer midi;
        for (const auto& e : events)
            if (e.blockIndex == blockIndex)
                midi.addEvent (e.message, 0);

        play.process (midi, /*midiChannel*/ 0, modeActive,
                      &src[(size_t) done], &cap.left[(size_t) done], &cap.right[(size_t) done],
                      kBlock, inputIsStable, kInputMidi, /*applyStabilityChangeNow*/ false);

        done += kBlock;
        ++blockIndex;
    }
    return cap;
}

// ---------------------------------------------------------------------------
// Misure di un singolo attacco, a partire dal campione in cui il note-on e'
// entrato nel buffer MIDI.
// ---------------------------------------------------------------------------
struct AttackMeasure
{
    double delayMs     = -1.0;  // PM-1: note-on -> 10% del regime
    double riseMs      = -1.0;  // PM-2: 10% -> 90% del regime
    double slewRatio   = -1.0;  // PM-3: max salto all'attacco / max salto a regime
    int    onsetSample = -1;    // PM-4: dove comincia la finestra di dissolvenza
    double regimeRms   =  0.0;
    bool   valid       = false;
};

AttackMeasure measureAttack (const std::vector<float>& wet, int noteOnSample,
                             int regimeFromSample, int regimeLen)
{
    AttackMeasure m;
    if (regimeFromSample + regimeLen > (int) wet.size())
        return m;

    const int envWin = (int) (0.005 * SR); // ~5ms, stessa convenzione di voice_test
    const auto env = movingRms (wet, envWin);

    m.regimeRms = rms (wet, regimeFromSample, regimeLen);
    if (m.regimeRms <= 1.0e-9)
        return m;

    const int i10 = firstCrossing (env, noteOnSample, 0.10 * m.regimeRms);
    const int i90 = i10 < 0 ? -1 : firstCrossing (env, i10, 0.90 * m.regimeRms);
    if (i10 < 0 || i90 < 0)
        return m;

    m.delayMs = 1000.0 * (double) (i10 - noteOnSample) / SR;
    m.riseMs  = 1000.0 * (double) (i90 - i10) / SR;

    // PM-3: il salto campione-campione peggiore nei primi 60ms dal NOTE-ON,
    // diviso quello di un tratto gia' a regime. "Un salto di ampiezza
    // discontinuo e' la definizione stessa di un click" (sessione 12): questa
    // e' la misura che nomina il sintomo, ed e' la sola soglia dura del file.
    //
    // La finestra parte dal note-on e non dal primo suono udibile, e dura
    // 60ms: deve contenere la GIUNZIONE fra il contenuto vecchio del ring e il
    // segnale vero, che cade alla latenza dichiarata del motore (fino a ~30ms
    // ad Accurate). Una finestra piu' stretta, ancorata a i10, la mancava ai
    // livelli lenti — ed e' esattamente li' che il difetto e' piu' grosso.
    const int slewWin = (int) (0.060 * SR);
    if (noteOnSample + slewWin < (int) wet.size() && regimeFromSample + slewWin <= (int) wet.size())
    {
        const double slewAttack = maxJump (wet, noteOnSample, slewWin);
        const double slewRegime = maxJump (wet, regimeFromSample, slewWin);
        m.slewRatio = slewRegime > 0.0 ? slewAttack / slewRegime : -1.0;
    }

    m.onsetSample = i10;
    m.valid = true;
    return m;
}

// PM-4: nella finestra in cui la dissolvenza sta salendo, quanta energia c'e'
// sulla nota RICHIESTA rispetto a quella sulla nota PRECEDENTE. Positivo in dB
// = suona la nota giusta; negativo = sta ancora suonando quella di prima.
// 30ms di finestra: copre la dissolvenza (8ms) con margine, e da' al Goertzel
// una risoluzione di ~33 Hz, abbastanza a separare le due note in gioco.
double measurePitchDominanceDb (const std::vector<float>& wet, const AttackMeasure& m,
                                double wantedF0, double previousF0)
{
    const int len = (int) (0.030 * SR);
    if (! m.valid || m.onsetSample < 0 || m.onsetSample + len > (int) wet.size())
        return 0.0;

    const double eWanted   = harmonicEnergy (wet, m.onsetSample, len, SR, wantedF0);
    const double ePrevious = harmonicEnergy (wet, m.onsetSample, len, SR, previousF0);
    if (eWanted <= 0.0 || ePrevious <= 0.0)
        return 0.0;
    return 10.0 * std::log10 (eWanted / ePrevious);
}

void printAttack (const char* label, const AttackMeasure& m,
                  const std::vector<float>& wet, double wantedF0, double previousF0)
{
    if (! m.valid)
    {
        std::printf ("  %-12s  (nessun attacco misurabile: uscita sotto soglia)\n", label);
        return;
    }
    std::printf ("  %-12s  PM-1 %7.2f ms   PM-2 %6.2f ms   PM-3 %6.2f   PM-4 %+7.1f dB\n",
                 label, m.delayMs, m.riseMs, m.slewRatio,
                 measurePitchDominanceDb (wet, m, wantedF0, previousF0));
}

} // namespace

// ---------------------------------------------------------------------------
int main()
{
    int failures = 0;

    // 6 secondi bastano a tutte le sequenze qui sotto, il piu' lungo dei
    // silenzi compreso.
    const auto carrier = makeVowel (kInputF0, 6.0, SR);

    std::printf ("Banco di misura dell'attacco in modalita' Play — SR=%.0f Hz, ingresso %.0f Hz "
                 "(MIDI %.2f), block=%d\n", SR, kInputF0, kInputMidi, kBlock);
    std::printf ("kDeclickMs di Voice = 8.00 ms: e' il valore che PM-2 deve avvicinare.\n");

    // =========================================================================
    // CONTROLLO NEGATIVO (obbligatorio prima di fidarsi delle altre misure,
    // CLAUDE.md regola 13): con la modalita' SPENTA il percorso Play non deve
    // produrre nulla, e con la modalita' accesa ma nessun tasto premuto
    // nemmeno. Se una di queste due misura del segnale, tutte le altre misure
    // di questo file stanno guardando la cosa sbagliata.
    // =========================================================================
    std::printf ("\nCONTROLLO NEGATIVO — nessun contributo senza modalita' attiva / senza tasti premuti\n");
    {
        const int total = (int) (0.5 * SR);

        PlayModeInput offMode;
        offMode.prepare (SR, kMaxBlockPrepare, Stability::defaultLevel);
        const auto capOff = runPlay (offMode, carrier, total,
                                     { { 2, juce::MidiMessage::noteOn (1, 60, 1.0f) } },
                                     /*modeActive*/ false);

        PlayModeInput noKeys;
        noKeys.prepare (SR, kMaxBlockPrepare, Stability::defaultLevel);
        const auto capIdle = runPlay (noKeys, carrier, total, { });

        const double rmsOff  = rms (capOff .left, 0, total);
        const double rmsIdle = rms (capIdle.left, 0, total);
        const bool ok = rmsOff <= 1.0e-9 && rmsIdle <= 1.0e-9;
        if (! ok) ++failures;
        std::printf ("  RMS a modalita' spenta = %.3e, a modalita' accesa senza tasti = %.3e   %s\n",
                     rmsOff, rmsIdle, ok ? "OK" : "FALLITO — la misura stessa non e' affidabile, fermarsi qui");

        if (! allFinite (capOff.left) || ! allFinite (capIdle.left))
        { ++failures; std::printf ("  FALLITO: uscita non finita (NaN/Inf)\n"); }
    }

    // =========================================================================
    // PM-1..PM-4 — prima nota dopo il silenzio, su tutti i livelli di
    // Stability. E' il caso "motore mai alimentato" — B-06 alla lettera, ma
    // sul percorso Play (vedi B-15).
    // =========================================================================
    std::printf ("\nPM-1..PM-4 — PRIMA nota dopo il silenzio (nota MIDI 64, ingresso a MIDI %.2f)\n"
                 "  (PM-4 qui = energia alla nota richiesta contro quella all'ingresso NON trasposto)\n",
                 kInputMidi);
    {
        constexpr int kNote = 64;
        const int noteOnBlock = 4;
        const int noteOnSample = noteOnBlock * kBlock;
        const int total = (int) (1.5 * SR);

        for (int level = 0; level < Stability::numLevels; ++level)
        {
            PlayModeInput play;
            play.prepare (SR, kMaxBlockPrepare, level);
            const auto cap = runPlay (play, carrier, total,
                                      { { noteOnBlock, juce::MidiMessage::noteOn (1, kNote, 1.0f) } });

            // Regime: ben oltre la latenza piu' lunga (Accurate) e la salita.
            const auto m = measureAttack (cap.left, noteOnSample,
                                          noteOnSample + (int) (0.30 * SR), (int) (0.20 * SR));
            printAttack (Stability::names[level], m, cap.left, midiToHz (kNote), kInputF0);

            if (! allFinite (cap.left)) { ++failures; std::printf ("    FALLITO: uscita non finita\n"); }
        }
    }

    // =========================================================================
    // PM-5 — SECONDA nota, dopo un note-off completo e un silenzio lungo.
    // E' il caso segnalato dall'utente ("ogni volta che premo il tasto"): il
    // motore non e' piu' vergine, e' AFFAMATO con dentro la coda della nota
    // precedente. La seconda nota e' anche a un'altezza diversa, cosi' PM-4
    // sa distinguere "intonazione giusta" da "intonazione della nota di prima".
    // =========================================================================
    std::printf ("\nPM-5 — SECONDA nota dopo note-off e silenzio (52 -> silenzio -> 67)\n");
    {
        constexpr int kNoteA = 52, kNoteB = 67;
        const int onABlock  = 4;
        const int offABlock = onABlock  + (int) (0.60 * SR) / kBlock;
        const int onBBlock  = offABlock + (int) (0.60 * SR) / kBlock;
        const int onBSample = onBBlock * kBlock;
        const int total = (int) (2.5 * SR);

        for (int level = 0; level < Stability::numLevels; ++level)
        {
            PlayModeInput play;
            play.prepare (SR, kMaxBlockPrepare, level);
            const auto cap = runPlay (play, carrier, total,
                                      { { onABlock,  juce::MidiMessage::noteOn  (1, kNoteA, 1.0f) },
                                        { offABlock, juce::MidiMessage::noteOff (1, kNoteA) },
                                        { onBBlock,  juce::MidiMessage::noteOn  (1, kNoteB, 1.0f) } });

            const auto m = measureAttack (cap.left, onBSample,
                                          onBSample + (int) (0.30 * SR), (int) (0.20 * SR));
            printAttack (Stability::names[level], m, cap.left, midiToHz (kNoteB), midiToHz (kNoteA));

            if (! allFinite (cap.left)) { ++failures; std::printf ("    FALLITO: uscita non finita\n"); }
        }
        std::printf ("  (PM-4: energia a %.1f Hz = nota richiesta, contro %.1f Hz = nota precedente.\n"
                     "   Negativo = nella dissolvenza sta ancora suonando quella di prima.)\n",
                     midiToHz (kNoteB), midiToHz (kNoteA));
    }

    // =========================================================================
    // PM-7 — ESPERIMENTO DECISIVO sul residuo di PM-5.
    //
    // Dopo il riscaldamento (Fase 1a) la PRIMA nota e' pulita ma la SECONDA
    // no. L'ipotesi: durante il riscaldamento il motore gira ancora con il
    // rapporto di trasposizione della nota PRECEDENTE (processWarmOnly
    // alimenta il motore ma non gli passa il nuovo shift, che arriva solo al
    // primo processAdd), e PsolaShifter::synthesise() riempie outBuf IN
    // ANTICIPO fino a absWrite-maxPeriod: al momento della dissolvenza c'e'
    // gia' una decina di ms di uscita sintetizzata all'intonazione sbagliata,
    // e la giunzione con quella giusta cade a guadagno pieno.
    //
    // Se l'ipotesi e' giusta, ripetere PM-5 con la seconda nota ALLA STESSA
    // ALTEZZA della prima (il rapporto non cambia, quindi non c'e' nessuna
    // intonazione sbagliata da sintetizzare) deve dare PM-3 pulito. Se invece
    // resta alto, l'ipotesi e' sbagliata e la causa e' un'altra: e' una prova,
    // non una conferma di comodo (CLAUDE.md regola 13).
    // =========================================================================
    std::printf ("\nPM-7 — seconda nota alla STESSA altezza della prima (52 -> silenzio -> 52)\n"
                 "  (prova dell'ipotesi sul residuo di PM-5: stesso rapporto, nessun cambio di alpha)\n");
    {
        constexpr int kNote = 52;
        const int onABlock  = 4;
        const int offABlock = onABlock  + (int) (0.60 * SR) / kBlock;
        const int onBBlock  = offABlock + (int) (0.60 * SR) / kBlock;
        const int onBSample = onBBlock * kBlock;
        const int total = (int) (2.5 * SR);

        for (int level = 0; level < Stability::numLevels; ++level)
        {
            PlayModeInput play;
            play.prepare (SR, kMaxBlockPrepare, level);
            const auto cap = runPlay (play, carrier, total,
                                      { { onABlock,  juce::MidiMessage::noteOn  (1, kNote, 1.0f) },
                                        { offABlock, juce::MidiMessage::noteOff (1, kNote) },
                                        { onBBlock,  juce::MidiMessage::noteOn  (1, kNote, 1.0f) } });

            const auto m = measureAttack (cap.left, onBSample,
                                          onBSample + (int) (0.30 * SR), (int) (0.20 * SR));
            printAttack (Stability::names[level], m, cap.left, midiToHz (kNote), kInputF0);

            if (! allFinite (cap.left)) { ++failures; std::printf ("    FALLITO: uscita non finita\n"); }
        }
    }

    // =========================================================================
    // PM-6 — nota RIBATTUTA dentro la dissolvenza di 8ms del rilascio.
    // Al note-off lo slot torna libero SUBITO ma la voce impiega kDeclickMs a
    // spegnersi: se il note-on successivo riprende lo stesso slot mentre
    // isSilent() e' ancora falso, Voice::justReactivated non viene armato e
    // l'intonazione SCIVOLA dalla nota vecchia alla nuova in glideTimeMs
    // invece di scattare. La misura e' l'intonazione subito dopo il ri-attacco.
    // =========================================================================
    std::printf ("\nPM-6 — nota ribattuta entro la dissolvenza (52 -> 67 a un blocco di distanza)\n");
    {
        constexpr int kNoteA = 52, kNoteB = 67;
        const int onABlock  = 4;
        const int offABlock = onABlock + (int) (0.60 * SR) / kBlock;
        const int onBBlock  = offABlock + 1; // ~5.8ms dopo: dentro gli 8ms di kDeclickMs
        const int onBSample = onBBlock * kBlock;
        const int total = (int) (2.0 * SR);

        PlayModeInput play;
        play.prepare (SR, kMaxBlockPrepare, Stability::defaultLevel);
        const auto cap = runPlay (play, carrier, total,
                                  { { onABlock,  juce::MidiMessage::noteOn  (1, kNoteA, 1.0f) },
                                    { offABlock, juce::MidiMessage::noteOff (1, kNoteA) },
                                    { onBBlock,  juce::MidiMessage::noteOn  (1, kNoteB, 1.0f) } });

        // Intonazione su tre finestre successive dal ri-attacco: una scivolata
        // di glideTimeMs (default 30ms) si vede come una progressione, uno
        // scatto come valori gia' sul bersaglio.
        //
        // Il livello va stampato accanto: una finestra quasi silenziosa (la
        // coda della nota precedente si e' spenta e la nuova sta ancora
        // riscaldando il motore) fa restituire a measureF0 un numero che non
        // corrisponde a nulla, e senza il livello sembrerebbe un'intonazione
        // sbagliata invece che assenza di segnale.
        const double expected = midiToHz (kNoteB);
        const double regime = rms (cap.left, onBSample + (int) (0.30 * SR), (int) (0.20 * SR));
        for (double offsetMs : { 10.0, 30.0, 60.0 })
        {
            const int from = onBSample + (int) (offsetMs * 0.001 * SR);
            const double f0 = measureF0 (cap.left, from, 1024, SR);
            const double level = regime > 0.0 ? 100.0 * rms (cap.left, from, 1024) / regime : 0.0;
            std::printf ("  +%5.1f ms dal ri-attacco: %7.1f Hz (%+8.1f cent su %.1f)   livello %5.1f%% del regime\n",
                         offsetMs, f0, f0 > 0.0 ? centsError (f0, expected) : 0.0, expected, level);
        }
        std::printf ("  (%.1f Hz = nota richiesta, %.1f Hz = nota rilasciata un blocco prima)\n",
                     expected, midiToHz (kNoteA));

        if (! allFinite (cap.left)) { ++failures; std::printf ("  FALLITO: uscita non finita\n"); }
    }

    // =========================================================================
    // CANCELLI DI REGRESSIONE — su PM-3, il salto di ampiezza discontinuo che
    // questo progetto chiama click da sessione 12.
    //
    // Perche' PM-3 e non PM-2. Il tempo di salita sembrava la metrica ovvia
    // (e' quella con cui B-12 fu chiuso), ma su questo percorso NON e'
    // affidabile: l'inviluppo RMS dell'uscita PSOLA e' grumoso alla cadenza
    // dei grani, e la "prima nota" misurata a MIDI 64 dava 4.9-10.1 ms mentre
    // la stessa prima nota a MIDI 52 ne dava 2.2 — la misura cambiava con la
    // profondita' dello shift, non con la presenza del difetto (CLAUDE.md
    // regola 13). PM-2 resta stampata come informazione, non come cancello.
    //
    // Tre situazioni, una sola soglia — ma ci sono voluti DUE fix per
    // arrivarci, e la storia va tenuta perche' spiega perche' il primo da
    // solo non bastava (vedi B-15):
    //
    //   A. prima nota, motore mai alimentato. Risolta dal riscaldamento
    //      (PlayModeInput: processWarmOnly per la latenza dichiarata prima di
    //      far partire la dissolvenza). Misurato 2.07-2.12 -> 1.00-1.51.
    //
    //   B. seconda nota alla STESSA altezza. Era gia' pulita prima di
    //      qualunque fix (0.98-1.03): non e' un caso corretto, e' il
    //      CONTROLLO che ha isolato la causa di C.
    //
    //   C. seconda nota ad altezza DIVERSA — il caso che l'utente sentiva
    //      ("ogni volta che premo il tasto"). Il solo riscaldamento non lo
    //      toccava (3.30-5.08 prima, 2.32-5.11 dopo): alimentava il motore ma
    //      non gli passava il nuovo rapporto di trasposizione, che arrivava
    //      solo al primo processAdd. PsolaShifter::synthesise() riempie outBuf
    //      IN ANTICIPO, quindi al momento della dissolvenza c'erano gia' ~10 ms
    //      sintetizzati all'intonazione della nota PRECEDENTE, e la giunzione
    //      con quella giusta cadeva a guadagno pieno. Chiuso dal secondo fix:
    //      processWarmOnly a 4 argomenti (Voice) + setMuted(false) all'INIZIO
    //      del riscaldamento, cosi' justReactivated aggancia il bersaglio
    //      prima che il motore sintetizzi qualunque cosa. Misurato 0.99-1.03.
    // =========================================================================
    std::printf ("\nCANCELLI — PM-3, nessun salto di ampiezza che sporga dal regime\n");
    {
        constexpr double kMaxSlewRatio = 1.8;
        constexpr int kNoteA = 52, kNoteB = 67;
        const int onABlock  = 4;
        const int offABlock = onABlock  + (int) (0.60 * SR) / kBlock;
        const int onBBlock  = offABlock + (int) (0.60 * SR) / kBlock;
        const int total = (int) (2.5 * SR);

        for (int level = 0; level < Stability::numLevels; ++level)
        {
            PlayModeInput crossPitch, samePitch;
            crossPitch.prepare (SR, kMaxBlockPrepare, level);
            samePitch .prepare (SR, kMaxBlockPrepare, level);

            const auto capCross = runPlay (crossPitch, carrier, total,
                                           { { onABlock,  juce::MidiMessage::noteOn  (1, kNoteA, 1.0f) },
                                             { offABlock, juce::MidiMessage::noteOff (1, kNoteA) },
                                             { onBBlock,  juce::MidiMessage::noteOn  (1, kNoteB, 1.0f) } });
            const auto capSame  = runPlay (samePitch, carrier, total,
                                           { { onABlock,  juce::MidiMessage::noteOn  (1, kNoteA, 1.0f) },
                                             { offABlock, juce::MidiMessage::noteOff (1, kNoteA) },
                                             { onBBlock,  juce::MidiMessage::noteOn  (1, kNoteA, 1.0f) } });

            const auto first = measureAttack (capCross.left, onABlock * kBlock,
                                              onABlock * kBlock + (int) (0.30 * SR), (int) (0.20 * SR));
            const auto same  = measureAttack (capSame.left, onBBlock * kBlock,
                                              onBBlock * kBlock + (int) (0.30 * SR), (int) (0.20 * SR));
            const auto cross = measureAttack (capCross.left, onBBlock * kBlock,
                                              onBBlock * kBlock + (int) (0.30 * SR), (int) (0.20 * SR));

            struct Gate { const char* label; const AttackMeasure& m; };
            const Gate gates[] =
            {
                { "prima nota",         first },
                { "2a, stessa altezza", same  },
                { "2a, altra altezza",  cross },
            };

            for (const auto& g : gates)
            {
                const bool ok = g.m.valid && g.m.slewRatio >= 0.0 && g.m.slewRatio <= kMaxSlewRatio;
                if (! ok) ++failures;
                std::printf ("  %-10s %-20s PM-3 = %6.2f  (limite %.1f)  %s\n",
                             Stability::names[level], g.label, g.m.valid ? g.m.slewRatio : -1.0, kMaxSlewRatio,
                             ok ? "OK" : "FALLITO — salto di ampiezza all'attacco");
            }
        }
    }

    std::printf ("\n%s (%d fallimenti)\n", failures == 0 ? "TUTTO OK" : "CI SONO FALLIMENTI", failures);
    return failures == 0 ? 0 : 1;
}
