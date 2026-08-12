// Banco di misura del passaggio fra le due modalita' (sessione 35, B-16/B-17).
//
// Due sintomi riportati all'ascolto dall'utente, nello stesso gesto:
//   1. attivando "Play Mode" mentre la sorgente sta suonando si sente un CLICK;
//   2. disattivandolo, l'Harmonizer non riparte piu' — non armonizza secondo la
//      matrice finche' non si ferma e si fa ripartire l'audio.
//
// CLAUDE.md regola 12: non posso ascoltare. Questo file traduce i due sintomi in
// misure numeriche e — come play_mode_input_test in s.34 — lo fa PRIMA di
// scrivere i fix: la tabella deve prima dire che i difetti CI SONO, altrimenti
// non sta misurando quello che credo (regola 13, e la lezione di PM-7).
//
// Perche' a questo livello e non piu' in basso. Il passaggio di modalita' non
// esiste dentro PhraseScheduler ne' dentro PlayModeInput: vive interamente in
// HarmonizerAudioProcessor::processBlock, che decide quale dei due mix wet
// arriva all'uscita e con quali segnali alimentare la catena Harmonizer. Un
// banco che ricostruisse quella decisione misurerebbe la mia ricostruzione, non
// il codice vero (D-09). Quindi qui si istanzia il VERO processore e si muove il
// VERO parametro "playModeEnabled", esattamente come farebbe l'host.
//
// E' il TERZO livello di D-16: linka juce_audio_utils (l'intero plugin, editor
// compreso), quindi gira in ctest ma non nel gate a g++ nudo della CI — che
// resta il debito A-06.
//
// ---------------------------------------------------------------------------
// PERCHE' IL SALTO CAMPIONE-CAMPIONE QUI NON FUNZIONA (e cosa si misura invece)
//
// La misura ovvia sarebbe PM-3 di s.34: il massimo salto fra due campioni
// consecutivi attorno al confine, rapportato al regime. Alla prima passata,
// PRIMA di qualunque fix, quella misura ha dato 1.08 — cioe' "nessun click" —
// mentre nello stesso istante il wet passava da pieno a ZERO ESATTO in un
// campione. Le due cose non sono in contraddizione: il taglio vale |x[b-1]|,
// un campione qualsiasi dell'onda, e il mix wet dell'Harmonizer e' granuloso
// alla cadenza dei grani PSOLA — i suoi salti naturali sono gia' di
// quell'ordine. Il rapporto non distingue "il segnale e' stato troncato" da
// "quel grano era un po' piu' ripido".
//
// Cio' che distingue un taglio da una dissolvenza non e' il salto: e'
// l'INVILUPPO subito dopo. Una dissolvenza di kDeclickMs lascia dietro di se'
// una coda di energia decrescente; un taglio lascia zero, esattamente zero.
// I cancelli stanno quindi sull'energia della coda. Il salto resta stampato
// come diagnostica, non come cancello — stessa sorte di PM-2 in s.34.
// ---------------------------------------------------------------------------
//
// Metriche (i nomi MS-n sono citati in BUGS.md/B-16 e B-17):
//   MS-1  energia nei kDeclickMs dopo l'ACCENSIONE di Play, rapportata al
//         regime dell'Harmonizer: e' la coda dell'Harmonizer che esce di
//         scena. 0 = tagliata di netto. [CANCELLO]
//   MS-2  lo stesso allo SPEGNIMENTO, con una nota Play premuta: e' la coda
//         della voce Play che esce di scena. [CANCELLO]
//   MS-3  quanti ms dallo spegnimento al ritorno del wet dell'Harmonizer,
//         SENZA nessun onset nuovo (la sorgente non si interrompe mai). E'
//         B-17 in un numero solo: prima del fix e' infinito. [CANCELLO]
//   MS-4  salto campione-campione ai due confini — diagnostica, vedi sopra.
//   MS-5  guardia sul percorso dry: il passaggio non deve toccarlo (FR-28,
//         "senza interruzioni del segnale dry").
//   MS-6  salto d'ampiezza sull'ATTACCO della matrice che rientra (PM-3 di
//         s.34 applicata a questo attacco). [CANCELLO]
//   MS-7  tempo di salita di quell'attacco. Molto corto = la dissolvenza si e'
//         consumata nel silenzio di un motore freddo e la voce e' entrata di
//         netto. Soglia tarata sul misurato (rotto 1.66, sano 7.80), NON sulla
//         costante kDeclickMs — vedi il blocco CANCELLI. [CANCELLO]
//   MS-8  il verso SIMMETRICO: la voce Play che entra quando Play si accende
//         con il tasto GIA' premuto, rapportata alla stessa voce che entra da
//         un note-on normale (controllo). [CANCELLO]
//
// MS-6, MS-7 e MS-8 sono nate al secondo giro d'ascolto (l'utente: *"non c'e'
// piu' il click quando attivo Play ma c'e' quando la disattivo e ricomincia a
// suonare la matrice"*): la coda che se ne va e l'attacco che entra sono due
// eventi distinti nello stesso istante, e i primi cancelli guardavano solo il
// primo. Delle due misure sull'attacco, solo MS-7 ha visto il difetto — MS-6
// dava 0.95, cioe' "pulito".
//
// Nessuna misura qui puo' dichiarare chiusi i sintomi: serve la conferma
// all'ascolto dell'utente (CLAUDE.md regola 12/14).

#include "PluginProcessor.h"
#include "TestSignals.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>

namespace
{

constexpr double SR = 44100.0;
constexpr int    kBlock = 256;        // buffer host "normale"
constexpr int    kMaxBlockPrepare = 4096;

// La sorgente: una nota tenuta e stabile, che NON si interrompe mai per tutta
// la durata della prova. E' il cuore della misura di B-17 — senza interruzione
// non c'e' nessun onset nuovo dopo il primo, quindi l'unico evento che puo'
// far ripartire l'Harmonizer e' l'interruttore stesso.
constexpr double kInputF0 = 220.0;    // A3

// Il preset di prova: tutti e 12 i gradi pieni sulla voce 0, offset +4
// semitoni. Non e' una raccomandazione d'uso (D-17 dice l'opposto per gli
// attacchi): serve a togliere di mezzo una variabile — qualunque grado agganci
// il rilevatore, una voce suona. Cosi' "wet assente" significa "l'Harmonizer
// non sta lavorando", mai "quella cella era vuota".
constexpr int kVoiceOffsetSemitones = 4;

// La nota premuta in Play nello scenario B.
constexpr int kPlayNote = 64;   // E4

// Voice::kDeclickMs e' privato e Voice e' rete di regressione di D-19: non lo
// rendo pubblico per una misura. Se la costante la' cambiasse, questa va
// riallineata a mano — e' l'unico punto in cui le due si devono corrispondere.
constexpr double kDeclickMs = 8.0;

// ---------------------------------------------------------------------------
// Inviluppo RMS a finestra scorrevole, causale. Stessa matematica di
// play_mode_input_test/voice_test — duplicata qui per la stessa ragione detta
// la': voice_test e' la rete di regressione di D-19 e non si tocca.
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

// ---------------------------------------------------------------------------
// Pilotaggio del vero HarmonizerAudioProcessor.
// ---------------------------------------------------------------------------
void setParam (juce::AudioProcessorValueTreeState& apvts, const char* id, float plainValue)
{
    auto* p = apvts.getParameter (id);
    jassert (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

struct Timeline
{
    int toggleOnSample  = 0;   // campione in cui playModeEnabled diventa true
    int toggleOffSample = 0;   // ...e in cui torna false
    int noteOnSample    = -1;  // note-on Play, o -1 per "nessun tasto premuto"
    int totalSamples    = 0;
};

struct Capture
{
    std::vector<float> outL, outR;
};

// Fa girare il processore, accendendo e spegnendo Play ai confini di blocco
// indicati. Il note-on eventuale entra nel MidiBuffer del blocco che lo
// contiene, e NON viene mai rilasciato: al toggle OFF il tasto e' ancora
// premuto, che e' il caso di FR-28 ("passaggio con tasti premuti").
Capture run (HarmonizerAudioProcessor& proc, const std::vector<float>& src, const Timeline& tl, float dryWetMix)
{
    setParam (proc.apvts, "dryWetMix", dryWetMix);
    setParam (proc.apvts, "playModeEnabled", 0.0f);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    Capture cap;
    cap.outL.reserve ((size_t) tl.totalSamples);
    cap.outR.reserve ((size_t) tl.totalSamples);

    for (int pos = 0; pos + kBlock <= tl.totalSamples; pos += kBlock)
    {
        // Il parametro cambia AL CONFINE DI BLOCCO, come farebbe un host che
        // riceve il click dell'utente sull'interruttore fra due processBlock.
        if (pos == tl.toggleOnSample)  setParam (proc.apvts, "playModeEnabled", 1.0f);
        if (pos == tl.toggleOffSample) setParam (proc.apvts, "playModeEnabled", 0.0f);

        for (int i = 0; i < kBlock; ++i)
        {
            const float s = (pos + i) < (int) src.size() ? src[(size_t) (pos + i)] : 0.0f;
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }

        midi.clear();
        if (tl.noteOnSample >= pos && tl.noteOnSample < pos + kBlock)
            midi.addEvent (juce::MidiMessage::noteOn (1, kPlayNote, (juce::uint8) 100),
                           tl.noteOnSample - pos);

        proc.processBlock (buffer, midi);

        for (int i = 0; i < kBlock; ++i)
        {
            cap.outL.push_back (buffer.getSample (0, i));
            cap.outR.push_back (buffer.getSample (1, i));
        }
    }

    return cap;
}

// MS-1/MS-2: l'energia della coda nei kDeclickMs successivi al confine,
// rapportata al regime che la precede. Una dissolvenza lineare da 1 a 0 su
// esattamente questa finestra darebbe ~0.58; un taglio da' 0.
double tailRatio (const std::vector<float>& x, int boundarySample, double regimeRms)
{
    const int win = (int) (kDeclickMs * 0.001 * SR);
    if (regimeRms <= 0.0 || boundarySample + win > (int) x.size())
        return -1.0;
    return rms (x, boundarySample, win) / regimeRms;
}

// MS-3: dal campione del toggle OFF, quanti ms prima che l'inviluppo del wet
// risalga al 10% del regime misurato PRIMA dell'accensione di Play. Se non
// risale entro la fine della cattura, ritorna infinito — che e' il sintomo.
double msToWetReturn (const std::vector<float>& x, int fromSample, double regimeRms)
{
    const auto env = movingRms (x, (int) (0.010 * SR));
    const double threshold = 0.10 * regimeRms;
    for (size_t i = (size_t) fromSample; i < env.size(); ++i)
        if (env[i] >= threshold)
            return 1000.0 * (double) ((int) i - fromSample) / SR;
    return std::numeric_limits<double>::infinity();
}

// MS-6: l'ATTACCO della catena Harmonizer che rientra dopo lo spegnimento di
// Play. Qui il salto campione-campione e' la misura giusta — e' PM-3 di s.34
// applicata a questo attacco, e per la stessa ragione: una voce che entra "di
// netto" perche' la sua dissolvenza si e' consumata nel silenzio di un motore
// freddo produce un gradino che sporge dal regime. (Il motivo per cui la stessa
// misura NON funziona sul TAGLIO e' spiegato in testa al file: sono due difetti
// di forma diversa, e vogliono due metriche diverse.)
//
// La finestra parte dal confine e dura 60 ms: deve contenere il rientro, che
// arriva dopo la finestra d'analisi del rilevatore piu' la latenza del motore.
double attackSlewRatio (const std::vector<float>& x, int fromSample, int regimeFromSample)
{
    const int win = (int) (0.060 * SR);
    if (fromSample + win > (int) x.size() || regimeFromSample + win > (int) x.size())
        return -1.0;

    const double atAttack = maxJump (x, fromSample, win);
    const double atRegime = maxJump (x, regimeFromSample, win);
    return atRegime > 0.0 ? atAttack / atRegime : -1.0;
}

// MS-7: il tempo di salita 10%->90% dell'inviluppo al rientro della matrice.
// E' la metrica che chiuse B-12: se la voce entra con la sua dissolvenza
// intatta deve valere kDeclickMs (8 ms). Molto meno significa che la
// dissolvenza si e' consumata nel SILENZIO di un motore freddo, e la voce e'
// entrata di netto quando il motore ha cominciato a produrre — un gradino
// d'inviluppo che il salto campione-campione (MS-6) puo' non vedere, perche'
// cade fra due grani invece che dentro uno.
double riseMsAt (const std::vector<float>& x, int fromSample, double regimeRms)
{
    const auto env = movingRms (x, (int) (0.003 * SR));
    const auto cross = [&env] (int from, double th) -> int
    {
        for (size_t i = (size_t) from; i < env.size(); ++i)
            if (env[i] >= th)
                return (int) i;
        return -1;
    };

    const int i10 = cross (fromSample, 0.10 * regimeRms);
    if (i10 < 0) return -1.0;
    const int i90 = cross (i10, 0.90 * regimeRms);
    if (i90 < 0) return -1.0;
    return 1000.0 * (double) (i90 - i10) / SR;
}

// MS-4 (diagnostica): il salto campione-campione peggiore attorno al confine,
// diviso quello di un tratto a regime. Vedi il blocco in testa al file per il
// perche' NON e' un cancello sul TAGLIO.
double slewRatioAt (const std::vector<float>& x, int boundarySample, int regimeFromSample)
{
    const int pre = (int) (0.002 * SR);
    const int win = (int) (0.020 * SR);
    const int from = std::max (0, boundarySample - pre);
    if (from + win > (int) x.size() || regimeFromSample + win > (int) x.size())
        return -1.0;

    const double atBoundary = maxJump (x, from, win);
    const double atRegime   = maxJump (x, regimeFromSample, win);
    return atRegime > 0.0 ? atBoundary / atRegime : -1.0;
}

void configure (HarmonizerAudioProcessor& proc)
{
    proc.setPlayConfigDetails (2, 2, SR, kMaxBlockPrepare);
    proc.prepareToPlay (SR, kMaxBlockPrepare);

    proc.editPresetLibrary ([] (harmony::PresetLibrary& lib)
    {
        for (int d = 0; d < harmony::numDegrees; ++d)
            lib.setCell (0, d, 0, harmony::Cell (kVoiceOffsetSemitones));
    });
    setParam (proc.apvts, "presetIndex", 1.0f);   // il preset appena riempito
    setParam (proc.apvts, "numVoices", 1.0f);
}

} // namespace

// ---------------------------------------------------------------------------
int main()
{
    // Il processore avvia un Timer in prepareToPlay (aggiornamento di
    // Stability): senza MessageManager non si costruisce.
    juce::ScopedJuceInitialiser_GUI juceInit;

    int failures = 0;

    // 1.0 s perche' la catena Harmonizer entri a regime, 0.5 s in Play, poi
    // 1.5 s per vedere se torna. La sorgente copre tutto e non si interrompe
    // MAI: dopo il primo non esiste nessun altro onset.
    Timeline tl;
    tl.toggleOnSample  = ((int) (1.0 * SR) / kBlock) * kBlock;
    tl.toggleOffSample = ((int) (1.5 * SR) / kBlock) * kBlock;
    tl.totalSamples    = ((int) (3.0 * SR) / kBlock) * kBlock;

    const auto carrier = makeVowel (kInputF0, 3.5, SR);
    const int regimeFrom = tl.toggleOnSample - (int) (0.300 * SR);
    const int regimeLen  = (int) (0.250 * SR);

    std::printf ("Banco di misura del passaggio di modalita' — SR=%.0f Hz, ingresso %.0f Hz, block=%d\n",
                 SR, kInputF0, kBlock);
    std::printf ("Play ON a %.0f ms, OFF a %.0f ms, sorgente MAI interrotta (nessun onset dopo il primo)\n\n",
                 1000.0 * tl.toggleOnSample / SR, 1000.0 * tl.toggleOffSample / SR);

    // -----------------------------------------------------------------------
    // Scenario A — nessun tasto premuto in Play. Isola la catena Harmonizer:
    // la sua coda all'accensione (MS-1) e il suo ritorno allo spegnimento
    // (MS-3). In Play il wet e' silenzio, quindi tutto cio' che si misura
    // dopo il toggle OFF viene dall'Harmonizer e da nient'altro.
    // -----------------------------------------------------------------------
    HarmonizerAudioProcessor procA;
    configure (procA);
    const auto a = run (procA, carrier, tl, /*dryWetMix*/ 1.0f);

    const double regimeA = rms (a.outL, regimeFrom, regimeLen);
    const double ms1 = tailRatio (a.outL, tl.toggleOnSample, regimeA);
    const double ms3 = msToWetReturn (a.outL, tl.toggleOffSample, regimeA);
    const double ms6 = attackSlewRatio (a.outL, tl.toggleOffSample, regimeFrom);
    const double ms7 = riseMsAt (a.outL, tl.toggleOffSample, regimeA);
    const double ms4on = slewRatioAt (a.outL, tl.toggleOnSample, regimeFrom);

    std::printf ("Scenario A — nessun tasto premuto.  Regime Harmonizer: RMS %.5f\n", regimeA);
    std::printf ("  MS-1  coda dell'Harmonizer nei %.0f ms dopo il toggle ON   %6.3f   (0 = tagliata)\n",
                 kDeclickMs, ms1);
    if (std::isinf (ms3))
        std::printf ("  MS-3  ritorno del wet dopo il toggle OFF                  MAI     (entro %.0f ms)\n",
                     1000.0 * (tl.totalSamples - tl.toggleOffSample) / SR);
    else
        std::printf ("  MS-3  ritorno del wet dopo il toggle OFF               %6.1f ms\n", ms3);
    std::printf ("  MS-6  attacco della matrice che rientra                   %6.2f   (regime = 1.00)\n", ms6);
    std::printf ("  MS-7  tempo di salita al rientro                       %6.2f ms  (sano ~%.1f, rotto 1.66)\n", ms7, 7.8);

    // -----------------------------------------------------------------------
    // Scenario B — un tasto premuto in Play, mai rilasciato. E' il caso di
    // FR-28 ("passaggio con tasti premuti") e l'unico in cui esiste una coda
    // Play da tagliare al toggle OFF.
    // -----------------------------------------------------------------------
    Timeline tlB = tl;
    tlB.noteOnSample = ((int) (1.1 * SR) / kBlock) * kBlock;

    HarmonizerAudioProcessor procB;
    configure (procB);
    const auto b = run (procB, carrier, tlB, /*dryWetMix*/ 1.0f);

    // Regime della voce Play: gli ultimi 300 ms prima dello spegnimento, con
    // la nota gia' premuta e stabile da un pezzo.
    const int playRegimeFrom = tl.toggleOffSample - (int) (0.300 * SR);
    const double regimeB = rms (b.outL, playRegimeFrom, regimeLen);
    const double ms2 = tailRatio (b.outL, tl.toggleOffSample, regimeB);
    const double ms4off = slewRatioAt (b.outL, tl.toggleOffSample, playRegimeFrom);

    std::printf ("\nScenario B — nota %d premuta in Play e mai rilasciata.  Regime Play: RMS %.5f\n",
                 kPlayNote, regimeB);
    std::printf ("  MS-2  coda della voce Play nei %.0f ms dopo il toggle OFF  %6.3f   (0 = tagliata)\n",
                 kDeclickMs, ms2);

    // -----------------------------------------------------------------------
    // Scenario C — il caso SIMMETRICO: la nota Play è già premuta PRIMA che
    // Play venga acceso. È l'altra metà di "anti-click in entrata e in
    // uscita": in scenario A l'accensione non aveva nessuna voce Play da far
    // entrare, quindi MS-1 misurava solo la coda che se ne va.
    //
    // Per costruzione dovrebbe essere già coperto dal fix di B-15 (in
    // PlayModeInput il riscaldamento è DERIVATO dallo stato del motore, non
    // registrato al note-on, quindi vale anche quando la nota era premuta da
    // prima). "Dovrebbe" non è una misura: qui si verifica.
    //
    // La finestra parte 12 ms dopo il confine, cioè dopo che la coda
    // dell'Harmonizer si è spenta: altrimenti l'inviluppo è già al livello di
    // regime al confine stesso e la salita non si potrebbe misurare.
    // -----------------------------------------------------------------------
    Timeline tlC = tl;
    tlC.noteOnSample = ((int) (0.7 * SR) / kBlock) * kBlock;   // prima del toggle ON

    HarmonizerAudioProcessor procC;
    configure (procC);
    const auto c = run (procC, carrier, tlC, /*dryWetMix*/ 1.0f);

    const double regimeC = rms (c.outL, playRegimeFrom, regimeLen);
    const double riseC = riseMsAt (c.outL, tl.toggleOnSample + (int) (0.012 * SR), regimeC);

    std::printf ("\nScenario C — nota %d premuta PRIMA di accendere Play.  Regime Play: RMS %.5f\n",
                 kPlayNote, regimeC);

    // MS-8 e' un RAPPORTO, non un tempo assoluto, e il denominatore e' un
    // CONTROLLO nel senso di PM-7 (s.34): la stessa voce Play che entra da un
    // NOTE-ON normale, con Play gia' acceso. E' il percorso che s.34 ha
    // misurato e che l'utente ha confermato all'ascolto chiudendo B-15, quindi
    // e' la definizione operativa di "entrata sana" su questa catena.
    //
    // Perche' un rapporto e non "deve valere kDeclickMs". Al primo giro questo
    // cancello era un tempo assoluto con soglia 5 ms, ed e' FALLITO (4.56)
    // su un percorso che si e' poi rivelato sano: il controllo da' 4.69, cioe'
    // lo stesso numero. Due ragioni per cui il valore assoluto e' piu' basso di
    // 8: una rampa lineare di 8 ms attraversa 10%->90% in 6.4 ms, non in 8; e
    // un inviluppo RMS causale accorcia ancora un po' la salita misurata. Una
    // soglia tarata a memoria sulla costante del codice invece che su una
    // misura di riferimento accusa il codice sbagliato — vedi regola 13.
    const double riseCtrl = riseMsAt (b.outL, tlB.noteOnSample, regimeB);
    const double ms8 = riseCtrl > 0.0 && riseC > 0.0 ? riseC / riseCtrl : -1.0;

    std::printf ("  MS-8  salita della voce Play che entra al toggle ON     %6.2f ms\n", riseC);
    std::printf ("        stessa voce da un note-on normale (controllo)     %6.2f ms  ->  rapporto %.2f\n",
                 riseCtrl, ms8);

    std::printf ("\n  MS-4  (diagnostica) salto campione-campione: toggle ON %.2f, toggle OFF %.2f\n",
                 ms4on, ms4off);

    // -----------------------------------------------------------------------
    // MS-5 — solo dry. FR-28 chiede anche "senza interruzioni del segnale
    // dry". E' una guardia a basso costo: il percorso dry non dipende dalla
    // modalita', ma e' proprio il genere di cosa che si rompe in silenzio
    // quando si tocca il mix.
    // -----------------------------------------------------------------------
    HarmonizerAudioProcessor procDry;
    configure (procDry);
    const auto d = run (procDry, carrier, tl, /*dryWetMix*/ 0.0f);
    const double ms5on  = slewRatioAt (d.outL, tl.toggleOnSample,  regimeFrom);
    const double ms5off = slewRatioAt (d.outL, tl.toggleOffSample, regimeFrom);
    std::printf ("  MS-5  dry: salto al toggle ON %.2f, al toggle OFF %.2f   (atteso ~1.00)\n\n",
                 ms5on, ms5off);

    // -----------------------------------------------------------------------
    // CANCELLI DI REGRESSIONE
    //
    // MS-1/MS-2 a 0.25: una dissolvenza lineare su tutta la finestra darebbe
    // ~0.58, un taglio 0. La soglia sta in mezzo con margine da entrambe le
    // parti — non pretende una forma precisa della rampa, solo che una coda
    // ci sia.
    //
    // MS-3 a 250 ms. Non e' una soglia percettiva: e' "l'Harmonizer riparte da
    // solo, senza che l'utente debba fermare il transport". Il tempo vero e'
    // dominato dalla finestra d'analisi del rilevatore piu' la latenza
    // dichiarata del motore; 250 ms lascia margine a entrambe senza lasciar
    // passare "mai".
    //
    // MS-7 a 5 ms e' tarata EMPIRICAMENTE, non sulla costante del codice:
    // difetto misurato 1.66, sano misurato 7.80, la soglia sta in mezzo. Il
    // valore sano e' sotto kDeclickMs=8 per la stessa ragione spiegata su MS-8
    // (10%->90% di una rampa lineare + inviluppo RMS causale). Leggere "8" e
    // pretenderlo qui e' l'errore che MS-8 ha commesso al primo giro.
    //
    // MS-8 e' un rapporto contro il controllo, quindi la soglia e' pura: 0.7
    // significa "il passaggio non entra sensibilmente piu' brusco di un
    // note-on normale". Misurato 0.97.
    // -----------------------------------------------------------------------
    std::printf ("CANCELLI\n");

    struct Gate { const char* name; double value; double limit; bool lessThan; };
    const Gate gates[] = {
        { "MS-1 (coda al toggle ON)",  ms1, 0.25,  false },
        { "MS-2 (coda al toggle OFF)", ms2, 0.25,  false },
        { "MS-3 (ritorno del wet)",    ms3, 250.0, true  },
        { "MS-6 (attacco al rientro)", ms6, 1.8,   true  },
        { "MS-7 (salita al rientro)",  ms7, 5.0,   false },
        { "MS-8 (salita voce Play/ctrl)", ms8, 0.7, false },
    };

    for (const auto& g : gates)
    {
        const bool ok = g.value >= 0.0 && (g.lessThan ? g.value <= g.limit : g.value >= g.limit);
        if (! ok)
            ++failures;
        std::printf ("  %-28s %10.2f  (limite %s%.2f)  %s\n",
                     g.name, g.value, g.lessThan ? "<= " : ">= ", g.limit, ok ? "OK" : "FALLITO");
    }

    if (! allFinite (a.outL) || ! allFinite (a.outR) || ! allFinite (b.outL) || ! allFinite (b.outR))
    {
        std::printf ("  uscita non finita (NaN/Inf)                                        FALLITO\n");
        ++failures;
    }

    std::printf ("\n%s\n", failures == 0 ? "Tutti i cancelli superati."
                                         : "CANCELLI FALLITI — vedi sopra.");
    return failures == 0 ? 0 : 1;
}
