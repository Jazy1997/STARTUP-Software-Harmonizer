#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ParamIDs
{
    static const juce::String rootNote { "rootNote" };
    static const juce::String presetIndex { "presetIndex" };
    static const juce::String numVoices { "numVoices" };
    static const juce::String dryLevel { "dryLevel" };
    static const juce::String wetLevel { "wetLevel" };
    // FR-77/PRD-UI §6.1: sostituisce dryLevel/wetLevel nel calcolo del
    // guadagno dry/wet effettivo (vedi computeDryWetGains sotto). dryLevel e
    // wetLevel restano dichiarati per sempre (CLAUDE.md regola 6) ma
    // smettono di essere letti in processBlock.
    static const juce::String dryWetMix { "dryWetMix" };
    static const juce::String stabilityLevel { "stabilityLevel" };
    static const juce::String glideTimeMs { "glideTimeMs" };
    static const juce::String maxSimultaneousVoices { "maxSimultaneousVoices" };
    // B-14/D-18: nota MIDI piu' grave che il rilevatore deve agganciare, non
    // la posizione dello strumento in lista — vedi createParameterLayout.
    static const juce::String analysisLowestNote { "analysisLowestNote" };

    static const juce::String formantSpread { "formantSpread" };
    static const juce::String bypass { "bypass" };
    static const juce::String playModeEnabled { "playModeEnabled" };
    static const juce::String keepPhraseTails { "keepPhraseTails" };

    static juce::String voiceFix (int voiceIndex) { return "voiceFix" + juce::String (voiceIndex + 1); }
    static juce::String voiceFormantOffset (int voiceIndex) { return "voiceFormantOffset" + juce::String (voiceIndex + 1); }
    // FR-11/§8.1: gain (dB) e pan per voce — id mai piu' rinominati dopo
    // pubblicazione (CLAUDE.md regola 6), stessa convenzione 1-based.
    static juce::String voiceGain (int voiceIndex) { return "voiceGain" + juce::String (voiceIndex + 1); }
    static juce::String voicePan (int voiceIndex) { return "voicePan" + juce::String (voiceIndex + 1); }
}

namespace
{
    // FR-77/PRD-UI §6.1: crossfade a potenza costante (stessa tecnica gia'
    // in uso per il pan per voce, Voice.cpp, sessione 23) — mix=0 e' tutto
    // dry, mix=1 e' tutto wet. A differenza dei vecchi dryLevel/wetLevel
    // indipendenti, qui i due guadagni non possono stare entrambi al
    // massimo contemporaneamente: e' un vero crossfade, non una somma
    // libera. Usata sia in prepareToPlay (reset delle rampe) sia in
    // processBlock (target delle rampe) — un solo posto dove vive la legge.
    void computeDryWetGains (float mix, float& dryOut, float& wetOut) noexcept
    {
        constexpr float halfPi = juce::MathConstants<float>::halfPi;
        dryOut = std::cos (mix * halfPi);
        wetOut = std::sin (mix * halfPi);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout HarmonizerAudioProcessor::createParameterLayout()
{
    const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Fondamentale e preset sono parametri discreti a passi (FR-35): niente
    // rampe di automazione che attraverserebbero voicing intermedi indesiderati.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::rootNote, 1 }, "Root Note", noteNames, 0));

    // presetIndex e' un intero 1-based su un range FISSO (1..maxPresets), non
    // un AudioParameterChoice: la libreria di preset puo' cambiare dimensione
    // a runtime (aggiunte/rimozioni/riordino da UI), mentre le choices di un
    // parametro APVTS non possono. Il valore coincide gia' concettualmente
    // col futuro CC posizionale (FR-05): valori oltre la libreria attuale
    // vengono ignorati in processBlock, esattamente come da FR-30 per il CC.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::presetIndex, 1 }, "Chord Preset", 1, harmony::PresetLibrary::maxPresets, 1));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::numVoices, 1 }, "Num Voices", 1, harmony::numVoices, 4));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::dryLevel, 1 }, "Dry Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::wetLevel, 1 }, "Wet Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));

    // FR-77/PRD-UI §6.1: nuovo parametro, sostituisce dryLevel/wetLevel nel
    // calcolo del guadagno effettivo (vedi computeDryWetGains). Il default
    // 0.7 e' un punto di partenza scelto per restare vicino al bilanciamento
    // "wet in evidenza" di oggi entro i limiti di un vero crossfade — NON e'
    // un valore calcolato, va confermato all'ascolto (CLAUDE.md regola 12)
    // prima di considerarlo definitivo.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::dryWetMix, 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    // FR-54: 5 posizioni discrete (Fast..Accurate). Il numero di posizioni e'
    // fisso a compile-time (a differenza dei preset armonici), quindi qui una
    // AudioParameterChoice statica va bene.
    {
        juce::StringArray stabilityNames;
        for (auto* name : Stability::names)
            stabilityNames.add (name);

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::stabilityLevel, 1 }, "Stability", stabilityNames, Stability::defaultLevel));
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::glideTimeMs, 1 }, "Glide Time",
        juce::NormalisableRange<float> (0.0f, 200.0f), 30.0f));

    // FR-23: Fix/Move e' selezionabile per singola voce, non globalmente.
    for (int v = 0; v < harmony::numVoices; ++v)
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::voiceFix (v), 1 }, "Voice " + juce::String (v + 1) + " Fix", false));

    // FR-39/FR-40: correzione formantica attiva di default (spread pieno,
    // non a meta') — "da nulla a massima" del PRD, 0 = disattivata.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::formantSpread, 1 }, "Formant Spread",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    // FR-41: offset manuale indipendente per voce, in semitoni-equivalenti
    // (stessa unita' della correzione automatica — si sommano direttamente,
    // vedi Voice::processAdd). Range +-24 st coincide col clamp di beta
    // dentro PsolaShifter (0.25..4.0).
    for (int v = 0; v < harmony::numVoices; ++v)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::voiceFormantOffset (v), 1 }, "Voice " + juce::String (v + 1) + " Formant",
            juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f));

    // FR-11/§8.1: gain per voce in dB, -60 (silenzio, vedi
    // juce::Decibels::decibelsToGain in processBlock) .. +6, default 0 dB —
    // nessun cambiamento rispetto al comportamento preesistente.
    for (int v = 0; v < harmony::numVoices; ++v)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::voiceGain (v), 1 }, "Voice " + juce::String (v + 1) + " Gain",
            juce::NormalisableRange<float> (-60.0f, 6.0f), 0.0f));

    // FR-11/§8.1: pan per voce, -1 (tutto a sinistra) .. +1 (tutto a
    // destra), default 0 (centro) — vedi Voice::processAdd per la legge a
    // potenza costante normalizzata cosi' che il default non cambi il suono.
    for (int v = 0; v < harmony::numVoices; ++v)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::voicePan (v), 1 }, "Voice " + juce::String (v + 1) + " Pan",
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));

    // FR-34/36: automatizzabile come tutti gli altri; il CC di bypass (FR-30)
    // puo' mettere l'automazione host in override per questo parametro,
    // esattamente come per root/preset (vedi processBlock, OverrideManager).
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

    // FR-24/28: interruttore Harmonizer/Play, discreto e automatizzabile
    // come tutti gli altri (FR-34/35). Non e' uno dei 3 CC di FR-30, quindi
    // non passa per OverrideManager: solo automazione host + UI.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::playModeEnabled, 1 }, "Play Mode", false));

    // Vedi Phrase.h/PhraseScheduler::setKeepTails per la semantica completa.
    // Default false (tronca): risoluzione presa in sessione 10 dopo il primo
    // ascolto reale, che ha mostrato l'accumulo di voci/preset col
    // comportamento precedente (sempre "tieni le code").
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::keepPhraseTails, 1 }, "Keep Tails", false));

    // FR-51: tetto configurabile di voci simultanee TRA TUTTE le frasi attive
    // (non le 8 voci di un singolo preset — quello e' "Num Voices" sopra).
    // Il range e' 1..hardSlotCapacity: qui il tetto tecnico coincide col
    // default (32), vedi PhraseScheduler.h.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::maxSimultaneousVoices, 1 }, "Max Simultaneous Voices",
        1, HarmonizerAudioProcessor::hardVoiceSlotCapacity, HarmonizerAudioProcessor::hardVoiceSlotCapacity));

    // B-14 (sessione 32): la nota piu' grave che il rilevatore deve agganciare.
    // Non e' una preferenza estetica — decide la finestra d'analisi di Cycfi Q
    // e quindi quanto tardi l'offset giusto arriva al motore (vedi
    // PitchDetector.h). La UI la presenta come scelta dello STRUMENTO.
    //
    // Si serializza la NOTA, non la posizione nella lista degli strumenti:
    // l'elenco e' ordinato dal piu' acuto al piu' grave perche' l'ordine e'
    // esso stesso l'informazione, quindi uno strumento aggiunto in futuro va
    // INSERITO in mezzo. Con un Choice posizionale (come il CC dei preset,
    // D-03) quell'ordine resterebbe congelato per sempre, perche' un ID
    // pubblicato non cambia mai (regola 6). Vedi D-18.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::analysisLowestNote, 1 }, "Analysis Lowest Note",
        HarmonizerAudioProcessor::minAnalysisLowestNote,
        HarmonizerAudioProcessor::maxAnalysisLowestNote,
        HarmonizerAudioProcessor::defaultAnalysisLowestNote));

    return { params.begin(), params.end() };
}

HarmonizerAudioProcessor::HarmonizerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
    , currentPresetLibrary (std::make_shared<const harmony::PresetLibrary>())
{
}

HarmonizerAudioProcessor::~HarmonizerAudioProcessor() = default;

void HarmonizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Margine oltre il range dichiarato in NFR-03 (32-4096, anche variabile):
    // dimensioniamo sul caso peggiore per non riallocare mai in processBlock.
    constexpr int absoluteMaxBlockSize = 8192;
    const int scratchSize = juce::jmax (samplesPerBlock, absoluteMaxBlockSize);

    monoInputScratch.setSize (1, scratchSize, false, false, true);
    voicesMixScratch.setSize (2, scratchSize, false, false, true);
    playVoicesMixScratch.setSize (2, scratchSize, false, false, true);

    preparedSampleRate = sampleRate;
    lastKnownAnalysisLowestNote = currentAnalysisLowestNote();
    pitchDetector.prepare (sampleRate, lastKnownAnalysisLowestNote);
    onsetDetector.prepare (sampleRate);
    // Sessione 31/32 (B-13, B-14): l'attesa prima di adottare una nota nuova
    // si misura in frame d'analisi del rilevatore, non in millisecondi fissi —
    // cosi' segue da sola lo strumento scelto. Vedi PitchLatch.h.
    pitchLatch.prepare (harmony::PitchLatch::settleSamplesForFrame (pitchDetector.getAnalysisFrameSamples()));

    // Sessione 12: rampa fissa anti-click, indipendente dal glideTimeMs
    // musicale — vedi il commento sul membro in PluginProcessor.h. Il
    // valore iniziale (senza rampa) e' quello attuale dei parametri, cosi'
    // il primo blocco non parte da un salto invece che da una rampa.
    constexpr float kMixDeclickMs = 8.0f;
    // FR-77: il valore iniziale delle rampe viene dal crossfade dryWetMix,
    // non piu' dai due parametri dryLevel/wetLevel indipendenti (che
    // restano dichiarati ma non piu' letti qui — vedi computeDryWetGains).
    float initialDry = 1.0f, initialWet = 0.0f;
    computeDryWetGains (*apvts.getRawParameterValue (ParamIDs::dryWetMix), initialDry, initialWet);
    dryGlide.prepare (sampleRate);
    dryGlide.setGlideTimeMs (kMixDeclickMs);
    dryGlide.reset (initialDry);
    wetGlide.prepare (sampleRate);
    wetGlide.setGlideTimeMs (kMixDeclickMs);
    wetGlide.reset (initialWet);

    if (auto* stabilityParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::stabilityLevel)))
        lastKnownStabilityLevel = stabilityParam->getIndex();
    phraseScheduler.prepare (hardVoiceSlotCapacity, sampleRate, scratchSize, lastKnownStabilityLevel);
    playModeInput.prepare (sampleRate, scratchSize, lastKnownStabilityLevel);

    // SpectralShifter (motore interinale) ha una latenza reale non banale
    // (STFT): dichiararla e' necessario perche' l'host possa compensarla.
    setLatencySamples (phraseScheduler.getLatencySamples());

    // Controlla i cambi di Stability (message thread, FR-56) e ripulisce gli
    // shifter ritirati dallo swap precedente (PRD §9.4) — mai sull'audio thread.
    startTimer (250);
}

void HarmonizerAudioProcessor::releaseResources()
{
    stopTimer();
}

void HarmonizerAudioProcessor::timerCallback()
{
    if (auto* stabilityParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::stabilityLevel)))
    {
        const int currentLevel = stabilityParam->getIndex();
        if (currentLevel != lastKnownStabilityLevel)
        {
            // Costruzione (con allocazione) dei nuovi shifter: message thread.
            // Entrambi i pool (Harmonizer e Play) condividono lo stesso
            // livello di Stability: un solo controllo, due VoicePool.
            phraseScheduler.requestStabilityChange (currentLevel);
            playModeInput.requestStabilityChange (currentLevel);
            lastKnownStabilityLevel = currentLevel;
        }
    }

    // B-14: la nota piu' grave d'analisi. Costruzione (con allocazione) del
    // nuovo rilevatore qui, sul message thread; lo scambio avviene in
    // processBlock ed e' un solo spostamento di puntatore. A differenza di
    // Stability NON si aspetta lo stop del transport: questo cambio non tocca
    // la latenza dichiarata, quindi FR-56 non c'entra.
    const int lowestNote = currentAnalysisLowestNote();
    if (lowestNote != lastKnownAnalysisLowestNote)
    {
        pitchDetector.requestLowestNoteChange (preparedSampleRate, lowestNote);
        lastKnownAnalysisLowestNote = lowestNote;
    }

    // Distrugge gli shifter ritirati dallo scambio precedente: mai sull'audio
    // thread (PRD §9.4).
    phraseScheduler.collectGarbage();
    playModeInput.collectGarbage();
    pitchDetector.collectGarbage();
}

int HarmonizerAudioProcessor::currentAnalysisLowestNote() const
{
    const auto* raw = apvts.getRawParameterValue (ParamIDs::analysisLowestNote);
    if (raw == nullptr)
        return defaultAnalysisLowestNote;

    return juce::jlimit (minAnalysisLowestNote, maxAnalysisLowestNote,
                         (int) std::lround (raw->load()));
}

bool HarmonizerAudioProcessor::canApplyStabilityChangeNow() const
{
    // FR-57: nella versione standalone non esiste transport, il cambio si
    // applica sempre immediatamente.
    if (wrapperType == wrapperType_Standalone)
        return true;

    // FR-56: se il transport sta suonando, il cambio resta in attesa fino
    // allo stop (molti host producono click/buchi se la latenza dichiarata
    // cambia durante la riproduzione).
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            return ! position->getIsPlaying();

    return true; // nessun playhead disponibile: non c'e' modo di saperlo, si applica comunque
}

bool HarmonizerAudioProcessor::isTransportPlaying() const
{
    // Usata solo per il fronte di stop dell'override CC (FR-36): a
    // differenza di canApplyStabilityChangeNow(), qui non si tratta mai lo
    // standalone come caso speciale — il chiamante lo esclude a monte
    // (FR-37, l'override non si revoca mai in standalone).
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            return position->getIsPlaying();

    return false; // nessun playhead: non c'e' automazione da overridare comunque
}

bool HarmonizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != mono && in != stereo)
        return false;

    if (out != mono && out != stereo)
        return false;

    return true;
}

void HarmonizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();

    if (numSamples == 0)
        return;

    // B-14: la nota piu' grave d'analisi e' cambiata. Solo uno spostamento di
    // puntatore (il rilevatore nuovo l'ha costruito il message thread, vedi
    // timerCallback): nessuna allocazione qui. Va fatto PRIMA di alimentare il
    // rilevatore, cosi' il blocco corrente finisce gia' in quello nuovo.
    // Cambia anche il frame d'analisi, quindi l'attesa del latch va rifatta —
    // e' aritmetica intera, e reset() azzera solo dei flag.
    // Le frasi in corso NON si toccano: finche' il rilevatore nuovo non ha
    // riempito la sua finestra, inputIsStable resta falso e PhraseScheduler
    // tiene l'ultimo voicing valido — la voce continua a suonare invece di
    // aprire un buco.
    if (pitchDetector.applyPendingLowestNoteChange())
        pitchLatch.prepare (harmony::PitchLatch::settleSamplesForFrame (pitchDetector.getAnalysisFrameSamples()));

    // 1. Downmix a mono: il prodotto assume una sorgente monofonica in ingresso
    // (PRD §3.1, "Audio in (mono)"). Anche il percorso dry usa questo segnale,
    // cosi' rimane allineato in fase con le voci (FR-58) senza bisogno di una
    // delay line dedicata in questo M0/M1.
    monoInputScratch.setSize (1, numSamples, false, false, true);
    auto* mono = monoInputScratch.getWritePointer (0);

    if (numInputChannels >= 2)
    {
        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (left[i] + right[i]);
    }
    else if (numInputChannels == 1)
    {
        const auto* src = buffer.getReadPointer (0);
        std::copy (src, src + numSamples, mono);
    }
    else
    {
        std::fill (mono, mono + numSamples, 0.0f);
    }

    // FR-24: quando Play e' attivo, la tabella armonica e' completamente
    // disattivata. Non e' uno dei 3 CC di FR-30: solo APVTS/automazione.
    // Letto qui e non piu' a meta' funzione perche' serve gia' al ciclo di
    // campioni qui sotto, che aggiorna l'aggancio.
    const bool playModeEnabled = *apvts.getRawParameterValue (ParamIDs::playModeEnabled) >= 0.5f;

    // B-14 (sessione 32): l'aggancio si aggiorna a ogni STIMA NUOVA del
    // rilevatore, non una volta per blocco dell'host.
    //
    // Fino a s.31 si leggeva `pitchDetector.getMidiNote()` alla fine del
    // blocco e si dava a PitchLatch `numSamples` come tempo trascorso. Con un
    // blocco piu' lungo dell'attesa quel conteggio e' una bugia: una stima
    // vista UNA SOLA VOLTA si vedeva accreditare un blocco intero, e l'attesa
    // diventava un no-op. Misurato: a nota piu' grave C4 e blocco 1024, un
    // singolo frame sbagliato (61.771 dove il vero e' 63.984, 4.4 ms) veniva
    // adottato e produceva un offset di passaggio lungo un blocco — la
    // ricomparsa di B-13 al caso limite.
    //
    // Aggiornando a ogni `pushSample` che ritorna true, il tempo che si passa
    // all'attesa e' quello VERO fra due stime consecutive, e il rifiuto di una
    // stima spuria non dipende piu' dal buffer dell'host. E' anche il modo
    // corretto di leggere Cycfi Q: getMidiNote()/getConfidence() valgono nel
    // momento in cui l'analisi si e' appena conclusa (vedi PitchDetector.h),
    // non a un istante qualsiasi del blocco.
    const bool latchFollowsInput = ! playModeEnabled;

    bool onsetDetectedThisBlock = false;
    for (int i = 0; i < numSamples; ++i)
    {
        const bool estimateUpdated = pitchDetector.pushSample (mono[i]);

        if (onsetDetector.pushSample (mono[i]))
        {
            onsetDetectedThisBlock = true;
            onsetPendingForLatch = true;
        }

        ++samplesSinceLatchUpdate;

        if (estimateUpdated && latchFollowsInput && pitchDetector.hasStableSignal())
        {
            // FR-16/17: isteresi di +-25 cent, e oltre la soglia il salto va
            // DIRETTAMENTE sulla nota d'arrivo dopo una breve conferma — mai
            // per gradi intermedi (B-13). onsetPendingForLatch forza
            // l'aggancio immediato su un vero attacco.
            lastLatchedNote = pitchLatch.update (pitchDetector.getMidiNote(),
                                                 onsetPendingForLatch,
                                                 samplesSinceLatchUpdate);
            onsetPendingForLatch = false;
            samplesSinceLatchUpdate = 0;
        }
    }

    // FR-30/36/37/38: interpreta i CC di questo blocco e risolve la
    // precedenza rispetto all'automazione host. In standalone l'override,
    // una volta attivo, non si revoca mai — non esiste transport da fermare
    // (FR-37): si salta del tutto il rilevamento del fronte di stop.
    const int hostRootPitchClass = (int) *apvts.getRawParameterValue (ParamIDs::rootNote);
    const int hostPresetOneBased = (int) *apvts.getRawParameterValue (ParamIDs::presetIndex);
    const bool hostBypass = *apvts.getRawParameterValue (ParamIDs::bypass) >= 0.5f;

    const auto ccEvents = ccRouter.process (midiMessages);

    if (wrapperType != wrapperType_Standalone)
    {
        const bool isPlayingNow = isTransportPlaying();
        if (wasPlayingLastBlock && ! isPlayingNow)
            overrideManager.clearOverrides(); // FR-36: fronte di stop
        wasPlayingLastBlock = isPlayingNow;
    }

    const auto effective = overrideManager.resolve (ccEvents, hostRootPitchClass, hostPresetOneBased, hostBypass);

    const int rootPitchClass = effective.rootPitchClass;
    const int numActiveVoices = (int) *apvts.getRawParameterValue (ParamIDs::numVoices);
    const int voiceCap = (int) *apvts.getRawParameterValue (ParamIDs::maxSimultaneousVoices);
    // FR-77/PRD-UI §6.1: dryWetMix sostituisce dryLevel/wetLevel nel calcolo
    // del guadagno dry/wet effettivo — crossfade a potenza costante, non
    // piu' due manopole indipendenti che possono sommarsi liberamente.
    // dryLevel/wetLevel restano dichiarati (CLAUDE.md regola 6) ma non sono
    // piu' letti qui.
    float dryLevel = 1.0f, wetLevel = 0.0f;
    computeDryWetGains (*apvts.getRawParameterValue (ParamIDs::dryWetMix), dryLevel, wetLevel);
    const float glideMs = *apvts.getRawParameterValue (ParamIDs::glideTimeMs);

    const float formantSpread = *apvts.getRawParameterValue (ParamIDs::formantSpread);

    const bool keepPhraseTails = *apvts.getRawParameterValue (ParamIDs::keepPhraseTails) >= 0.5f;

    phraseScheduler.setGlideTimeMs (glideMs);
    phraseScheduler.setVoiceCap (voiceCap);
    phraseScheduler.setFormantSpread (formantSpread);
    playModeInput.setFormantSpread (formantSpread); // FR-42 (B-07)
    phraseScheduler.setKeepTails (keepPhraseTails);

    // Il loop qui sotto indicizza il pool di Play con lo stesso v che indicizza
    // le colonne armoniche: sono due costanti INDIPENDENTI che oggi valgono
    // entrambe 8. Se una delle due cambiasse, gain/pan/formanti scriverebbero
    // fuori dal pool di Play senza che nulla lo segnali.
    static_assert (PlayModeInput::maxNotes == harmony::numVoices,
                   "Il pool di Play e le colonne armoniche devono avere la stessa cardinalita': "
                   "il loop di processBlock usa un solo indice per entrambi.");

    for (int v = 0; v < harmony::numVoices; ++v)
    {
        const bool isFix = *apvts.getRawParameterValue (ParamIDs::voiceFix (v)) >= 0.5f;
        phraseScheduler.setVoiceMode (v, isFix ? ShiftMode::fix : ShiftMode::move);

        const float formantOffset = *apvts.getRawParameterValue (ParamIDs::voiceFormantOffset (v));
        phraseScheduler.setVoiceFormantOffset (v, formantOffset);
        playModeInput.setVoiceFormantOffset (v, formantOffset); // FR-42 (B-07)

        // FR-11/§8.1: conversione dB->lineare fatta QUI, non dentro Voice —
        // Voice/voice_test restano privi di dipendenze JUCE (vedi Voice.h).
        // -60dB e' trattato come silenzio esatto (minusInfinityDb, il terzo
        // argomento), non un numero molto piccolo ma diverso da zero.
        const float gainDb = *apvts.getRawParameterValue (ParamIDs::voiceGain (v));
        const float gainLinear = juce::Decibels::decibelsToGain (gainDb, -60.0f);
        phraseScheduler.setVoiceGainLinear (v, gainLinear);
        playModeInput.setVoiceGainLinear (v, gainLinear);

        const float pan = *apvts.getRawParameterValue (ParamIDs::voicePan (v));
        phraseScheduler.setVoicePan (v, pan);
        playModeInput.setVoicePan (v, pan);
    }

    // Snapshot immutabile: sicuro da leggere sull'audio thread (vedi header).
    const auto presetLibrary = getPresetLibrary();
    // Valori oltre la lunghezza attuale della libreria vengono ignorati
    // (FR-30, sia da automazione/UI sia da CC).
    const int presetIndex = juce::jlimit (0, presetLibrary->getNumPresets() - 1, effective.presetOneBased - 1);

    std::array<harmony::Cell, harmony::numVoices> offsets {};
    int quantizedPlayedNote = 0;
    const float continuousInputMidiNote = pitchDetector.getMidiNote();
    const bool inputIsStable = pitchDetector.hasStableSignal();

    // FR-45/46 (sessione 11 — bug trovato dopo il fix dell'isteresi
    // d'intonazione, canto legato ancora silenzioso su D/E dopo C):
    // "c'e' ancora un segnale in ingresso" e "il pitch di QUESTO blocco e'
    // abbastanza confidente" sono due domande diverse. hasStableSignal()
    // (confidenza/periodicita') puo' scendere per pochi blocchi durante
    // uno SCIVOLAMENTO di intonazione fra due note cantate legato, anche se
    // il performer sta ancora chiaramente suonando — usarla per decidere
    // se liberare tutte le frasi (come si faceva prima) svuota lo stato
    // armonico proprio nel mezzo della transizione, e senza un onset a
    // ribattere (canto legato) nulla lo ricostruisce. Il gate di
    // OnsetDetector (livello del segnale, non pitch) e' un proxy migliore
    // per "il performer si e' davvero fermato".
    const bool signalPresent = onsetDetector.isGateOpen();

    // FR-43/45/46 (sessione 12 — note saltate, "senza una logica troppo
    // precisa"): al fronte di DISCESA di signalPresent (vero silenzio appena
    // iniziato), si azzera anche pitchDetector — non solo pitchLatch come
    // gia' avveniva. Senza questo, cycfi::q::pitch_detector conserva
    // l'ultima frequenza/confidenza stimate finche' non ne calcola una
    // nuova da solo: un onset successivo puo' quindi trovare hasStableSignal()
    // gia' vero ma su una nota STANTIA (quella precedente), e la frase nasce
    // congelata sull'accordo sbagliato invece che in attesa del pitch vero.
    // Verificato RT-safe: PitchDetector::reset() assegna due float e chiama
    // cycfi::q::pitch_detector::reset(), che a sua volta e' solo
    // `_frequency = 0.0f` (pitch_detector.hpp) — nessuna allocazione/lock.
    const bool signalJustStopped = signalPresentLastBlock && ! signalPresent;
    if (signalJustStopped)
        pitchDetector.reset();
    signalPresentLastBlock = signalPresent;

    // Diagnostica per l'UI (PRD §8.1): snapshot dell'esito di PitchDetector
    // per questo blocco, indipendente dalla modalita' — utile proprio per
    // capire casi come "con questo synth non riconosce" (punti 2/5 del test
    // di sessione 10) senza dover indovinare le costanti del rilevatore.
    lastDetectedMidiNote.store (continuousInputMidiNote, std::memory_order_relaxed);
    lastDetectedConfidence.store (pitchDetector.getConfidence(), std::memory_order_relaxed);
    lastInputStable.store (inputIsStable, std::memory_order_relaxed);
    lastGateOpen.store (signalPresent, std::memory_order_relaxed);
    lastBlockSize.store (numSamples, std::memory_order_relaxed);

    if (! playModeEnabled && inputIsStable)
    {
        // L'aggancio e' gia' stato aggiornato dal ciclo di campioni qui sopra,
        // a ogni stima nuova del rilevatore (B-14). Qui si legge soltanto: se
        // in questo blocco non e' arrivata alcuna stima nuova — normale a
        // buffer piccoli, dove un blocco e' piu' corto di un frame d'analisi —
        // si riusa l'ultimo aggancio invece di ricalcolarlo su un dato vecchio.
        quantizedPlayedNote = lastLatchedNote;
        offsets = harmony::HarmonyEngine::getOffsets (presetLibrary->getPreset (presetIndex), quantizedPlayedNote, rootPitchClass);
    }
    else if (! signalPresent)
    {
        pitchLatch.reset(); // vero silenzio: il prossimo aggancio riparte pulito
    }
    // else (segnale presente ma pitch non confidente questo blocco):
    // l'aggancio di PitchLatch resta quello dell'ultimo blocco buono,
    // invece di essere azzerato per un calo di confidenza transitorio.

    // FR-24: mentre Play e' attivo, la catena Harmonizer resta "in attesa"
    // con entrambi i segnali forzati a false — la stessa via gia' usata
    // quando il segnale tace (freeAllPhrases): nessuna frase nuova o viva,
    // nessun contributo audio, ma lo swap di Stability continua ad essere
    // applicato in modo uniforme (vedi PhraseScheduler::process).
    const bool harmonizerSignalPresent = (! playModeEnabled) && signalPresent;
    const bool harmonizerInputIsStable = (! playModeEnabled) && inputIsStable;

    // FR-11/§8.1: mix wet STEREO (gain/pan per voce) — canale 0 = L, 1 = R.
    voicesMixScratch.setSize (2, numSamples, false, false, true);
    const bool appliedStabilityChangeHarmonizer = phraseScheduler.process (mono,
        voicesMixScratch.getWritePointer (0), voicesMixScratch.getWritePointer (1), numSamples,
        onsetDetectedThisBlock, harmonizerSignalPresent, harmonizerInputIsStable, quantizedPlayedNote, continuousInputMidiNote,
        offsets, numActiveVoices, canApplyStabilityChangeNow());

    playVoicesMixScratch.setSize (2, numSamples, false, false, true);
    const bool appliedStabilityChangePlay = playModeInput.process (midiMessages, ccRouter.getMidiChannel(), playModeEnabled,
        mono, playVoicesMixScratch.getWritePointer (0), playVoicesMixScratch.getWritePointer (1), numSamples,
        inputIsStable, continuousInputMidiNote, canApplyStabilityChangeNow());

    if (appliedStabilityChangeHarmonizer || appliedStabilityChangePlay)
        setLatencySamples (phraseScheduler.getLatencySamples()); // stessa Stability, stessa latenza per entrambi i pool

    // Le due modalita' sono mutuamente esclusive (FR-24): si sceglie l'una
    // o l'altra, non si sommano.
    const auto* voicesMixL = playModeEnabled ? playVoicesMixScratch.getReadPointer (0) : voicesMixScratch.getReadPointer (0);
    const auto* voicesMixR = playModeEnabled ? playVoicesMixScratch.getReadPointer (1) : voicesMixScratch.getReadPointer (1);

    // Bypass (FR-30): solo dry, esattamente come se Dry=1/Wet=0 — il
    // percorso dry e' gia' il segnale in ingresso non processato, quindi
    // non serve un secondo percorso audio dedicato.
    const float effectiveDryLevel = effective.bypassed ? 1.0f : dryLevel;
    const float effectiveWetLevel = effective.bypassed ? 0.0f : wetLevel;

    // Sessione 12 (fix click): il target del parametro puo' saltare da un
    // blocco all'altro (automazione, CC bypass, il bottone Bypass stesso) —
    // dryGlide/wetGlide smorzano quel salto invece di applicarlo di netto a
    // tutto il buffer. Va chiamato UNA sola volta per blocco (Glide muta
    // stato interno): il valore va calcolato prima del ciclo sui canali, non
    // dentro. Sessione 13: con un buffer host lungo (4096 campioni misurati
    // dall'utente in Ableton, MME/DirectX) una singola chiamata a process()
    // faceva scattare il salto in un solo campione, uguale al bug su
    // ampGlide in Voice.cpp — stesso fix, processRamp campione-per-campione.
    dryGlide.setTarget (effectiveDryLevel);
    wetGlide.setTarget (effectiveWetLevel);
    const auto dryRamp = dryGlide.processRamp (numSamples);
    const auto wetRamp = wetGlide.processRamp (numSamples);

    // FR-11/§8.1: il mix wet e' ora stereo. Con un bus di uscita stereo il
    // canale 0 legge L e il canale 1 legge R; con un bus mono
    // (isBusesLayoutSupported lo consente) non esiste un canale "giusto" da
    // scegliere, quindi si fa la media dei due — l'unico downmix sensato
    // per un segnale che ora porta informazione di pan.
    if (numOutputChannels == 1)
    {
        auto* out = buffer.getWritePointer (0);
        float dry = dryRamp.startValue;
        float wet = wetRamp.startValue;
        const int rampSamples = juce::jmax (dryRamp.rampSamples, wetRamp.rampSamples);
        int i = 0;
        for (; i < rampSamples; ++i)
        {
            out[i] = dry * mono[i] + wet * 0.5f * (voicesMixL[i] + voicesMixR[i]);
            if (i < dryRamp.rampSamples) dry += dryRamp.increment;
            if (i < wetRamp.rampSamples) wet += wetRamp.increment;
        }
        for (; i < numSamples; ++i)
            out[i] = dry * mono[i] + wet * 0.5f * (voicesMixL[i] + voicesMixR[i]);
    }
    else
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            const float* voicesMix = (ch == 1) ? voicesMixR : voicesMixL;
            float dry = dryRamp.startValue;
            float wet = wetRamp.startValue;
            const int rampSamples = juce::jmax (dryRamp.rampSamples, wetRamp.rampSamples);
            int i = 0;
            for (; i < rampSamples; ++i)
            {
                out[i] = dry * mono[i] + wet * voicesMix[i];
                if (i < dryRamp.rampSamples) dry += dryRamp.increment;
                if (i < wetRamp.rampSamples) wet += wetRamp.increment;
            }
            for (; i < numSamples; ++i)
                out[i] = dry * mono[i] + wet * voicesMix[i];
        }
    }
}

juce::AudioProcessorEditor* HarmonizerAudioProcessor::createEditor()
{
    return new HarmonizerAudioProcessorEditor (*this);
}

bool HarmonizerAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String HarmonizerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool HarmonizerAudioProcessor::acceptsMidi() const
{
    return true;
}

bool HarmonizerAudioProcessor::producesMidi() const
{
    return false;
}

bool HarmonizerAudioProcessor::isMidiEffect() const
{
    return false;
}

double HarmonizerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int HarmonizerAudioProcessor::getNumPrograms()
{
    return 1;
}

int HarmonizerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void HarmonizerAudioProcessor::setCurrentProgram (int)
{
}

const juce::String HarmonizerAudioProcessor::getProgramName (int)
{
    return {};
}

void HarmonizerAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void HarmonizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // FR-08: la libreria di preset si serializza dentro lo stato del plugin
    // (sessione host), non solo un riferimento a file esterni. Riordinare la
    // libreria globale non altera percio' i progetti gia' salvati.
    juce::ValueTree root ("HarmonizerState");
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (getPresetLibrary()->toValueTree(), nullptr);

    // FR-31: i numeri CC (e il canale) sono configurazione di routing, non
    // un valore automatizzabile — non vivono nell'APVTS (non avrebbe senso
    // automatizzare "quale CC controlla cosa"), ma vanno comunque salvati
    // con lo stato del plugin perche' l'utente non debba reimpostarli/
    // ri-imparare ad ogni apertura del progetto.
    juce::ValueTree ccSettings ("MidiCcSettings");
    ccSettings.setProperty ("rootCc", ccRouter.getRootCc(), nullptr);
    ccSettings.setProperty ("presetCc", ccRouter.getPresetCc(), nullptr);
    ccSettings.setProperty ("bypassCc", ccRouter.getBypassCc(), nullptr);
    ccSettings.setProperty ("midiChannel", ccRouter.getMidiChannel(), nullptr);
    root.appendChild (ccSettings, nullptr);

    // FR-59/D-20: la scala della finestra segue la sessione, come i CC qui
    // sopra e per la stessa ragione — l'utente non deve rimetterla ad ogni
    // apertura del progetto. Nodo separato da MidiCcSettings perche' e' un'altra
    // categoria di cosa (visualizzazione, non routing) e perche' cosi' una
    // sessione salvata prima di oggi semplicemente non lo ha, senza che il
    // resto della lettura ne risenta.
    juce::ValueTree uiSettings ("UiSettings");
    uiSettings.setProperty ("scalePercent", uiScalePercent.load (std::memory_order_relaxed), nullptr);
    root.appendChild (uiSettings, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmonizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid() || ! root.hasType ("HarmonizerState"))
        return;

    if (auto paramsTree = root.getChildWithName (apvts.state.getType()); paramsTree.isValid())
        apvts.replaceState (paramsTree);

    if (auto libTree = root.getChildWithName ("PresetLibrary"); libTree.isValid())
        editPresetLibrary ([&] (harmony::PresetLibrary& lib) { lib.loadFromValueTree (libTree); });

    if (auto ccTree = root.getChildWithName ("MidiCcSettings"); ccTree.isValid())
    {
        ccRouter.setRootCc    ((int) ccTree.getProperty ("rootCc",    ccRouter.getRootCc()));
        ccRouter.setPresetCc  ((int) ccTree.getProperty ("presetCc",  ccRouter.getPresetCc()));
        ccRouter.setBypassCc  ((int) ccTree.getProperty ("bypassCc",  ccRouter.getBypassCc()));
        ccRouter.setMidiChannel ((int) ccTree.getProperty ("midiChannel", ccRouter.getMidiChannel()));
    }

    // FR-59/D-20. Il nodo manca in ogni sessione salvata prima della sessione
    // 33: in quel caso non si tocca nulla e la finestra resta al valore
    // corrente (il default, 100%). setUiScalePercent clampa, quindi anche un
    // XML modificato a mano non puo' produrre una finestra di dimensione
    // assurda — vedi tests/ui_scale_test.cpp, TEST 4.
    if (auto uiTree = root.getChildWithName ("UiSettings"); uiTree.isValid())
        setUiScalePercent ((int) uiTree.getProperty ("scalePercent", getUiScalePercent()));
}

std::shared_ptr<const harmony::PresetLibrary> HarmonizerAudioProcessor::getPresetLibrary() const noexcept
{
    const juce::SpinLock::ScopedLockType sl (presetLibraryLock);
    return currentPresetLibrary;
}

void HarmonizerAudioProcessor::editPresetLibrary (const std::function<void (harmony::PresetLibrary&)>& mutator)
{
    // Non deve mai essere chiamata dall'audio thread (vedi commento in
    // header); alcuni host possono chiamare setStateInformation da un thread
    // di caricamento diverso dal message thread ma comunque non concorrente
    // con processBlock, quindi qui non si asserisce il message thread in
    // modo rigido — solo la garanzia "mai sull'audio thread" e' richiesta.
    auto mutated = std::make_shared<harmony::PresetLibrary> (*getPresetLibrary());
    mutator (*mutated);

    std::shared_ptr<const harmony::PresetLibrary> old;
    {
        const juce::SpinLock::ScopedLockType sl (presetLibraryLock);
        old = currentPresetLibrary;
        currentPresetLibrary = std::move (mutated);
    }
    // "old" resta viva qui (message thread) finche' non sostituiamo il valore
    // precedente di retiredPresetLibrary: la sua distruzione avviene quindi
    // sul message thread nel caso normale, mai nell'audio thread (PRD §9.4).
    retiredPresetLibrary = std::move (old);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerAudioProcessor();
}
