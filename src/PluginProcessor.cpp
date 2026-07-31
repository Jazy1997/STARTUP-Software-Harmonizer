#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ParamIDs
{
    static const juce::String rootNote { "rootNote" };
    static const juce::String presetIndex { "presetIndex" };
    static const juce::String numVoices { "numVoices" };
    static const juce::String dryLevel { "dryLevel" };
    static const juce::String wetLevel { "wetLevel" };
    static const juce::String stabilityLevel { "stabilityLevel" };
    static const juce::String glideTimeMs { "glideTimeMs" };
    static const juce::String maxSimultaneousVoices { "maxSimultaneousVoices" };

    static const juce::String formantSpread { "formantSpread" };
    static const juce::String bypass { "bypass" };
    static const juce::String playModeEnabled { "playModeEnabled" };

    static juce::String voiceFix (int voiceIndex) { return "voiceFix" + juce::String (voiceIndex + 1); }
    static juce::String voiceFormantOffset (int voiceIndex) { return "voiceFormantOffset" + juce::String (voiceIndex + 1); }
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

    // FR-51: tetto configurabile di voci simultanee TRA TUTTE le frasi attive
    // (non le 8 voci di un singolo preset — quello e' "Num Voices" sopra).
    // Il range e' 1..hardSlotCapacity: qui il tetto tecnico coincide col
    // default (32), vedi PhraseScheduler.h.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::maxSimultaneousVoices, 1 }, "Max Simultaneous Voices",
        1, HarmonizerAudioProcessor::hardVoiceSlotCapacity, HarmonizerAudioProcessor::hardVoiceSlotCapacity));

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
    voicesMixScratch.setSize (1, scratchSize, false, false, true);
    playVoicesMixScratch.setSize (1, scratchSize, false, false, true);

    pitchDetector.prepare (sampleRate);
    onsetDetector.prepare (sampleRate);

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

    // Distrugge gli shifter ritirati dallo scambio precedente: mai sull'audio
    // thread (PRD §9.4).
    phraseScheduler.collectGarbage();
    playModeInput.collectGarbage();
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

    bool onsetDetectedThisBlock = false;
    for (int i = 0; i < numSamples; ++i)
    {
        pitchDetector.pushSample (mono[i]);
        if (onsetDetector.pushSample (mono[i]))
            onsetDetectedThisBlock = true;
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
    const float dryLevel = *apvts.getRawParameterValue (ParamIDs::dryLevel);
    const float wetLevel = *apvts.getRawParameterValue (ParamIDs::wetLevel);
    const float glideMs = *apvts.getRawParameterValue (ParamIDs::glideTimeMs);

    const float formantSpread = *apvts.getRawParameterValue (ParamIDs::formantSpread);

    // FR-24: quando Play e' attivo, la tabella armonica e' completamente
    // disattivata. Non e' uno dei 3 CC di FR-30: solo APVTS/automazione.
    const bool playModeEnabled = *apvts.getRawParameterValue (ParamIDs::playModeEnabled) >= 0.5f;

    phraseScheduler.setGlideTimeMs (glideMs);
    phraseScheduler.setVoiceCap (voiceCap);
    phraseScheduler.setFormantSpread (formantSpread);
    for (int v = 0; v < harmony::numVoices; ++v)
    {
        const bool isFix = *apvts.getRawParameterValue (ParamIDs::voiceFix (v)) >= 0.5f;
        phraseScheduler.setVoiceMode (v, isFix ? ShiftMode::fix : ShiftMode::move);

        const float formantOffset = *apvts.getRawParameterValue (ParamIDs::voiceFormantOffset (v));
        phraseScheduler.setVoiceFormantOffset (v, formantOffset);
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
    if (! playModeEnabled && inputIsStable)
    {
        quantizedPlayedNote = juce::roundToInt (continuousInputMidiNote);
        offsets = harmony::HarmonyEngine::getOffsets (presetLibrary->getPreset (presetIndex), quantizedPlayedNote, rootPitchClass);
    }

    // FR-24: mentre Play e' attivo, la catena Harmonizer resta "in attesa"
    // con inputIsStable forzato a false — la stessa via gia' usata quando
    // il segnale non e' intonato (freeAllPhrases): nessuna frase nuova o
    // viva, nessun contributo audio, ma lo swap di Stability continua ad
    // essere applicato in modo uniforme (vedi PhraseScheduler::process).
    const bool harmonizerInputIsStable = (! playModeEnabled) && inputIsStable;

    voicesMixScratch.setSize (1, numSamples, false, false, true);
    const bool appliedStabilityChangeHarmonizer = phraseScheduler.process (mono, voicesMixScratch.getWritePointer (0), numSamples,
        onsetDetectedThisBlock, harmonizerInputIsStable, quantizedPlayedNote, continuousInputMidiNote,
        offsets, numActiveVoices, canApplyStabilityChangeNow());

    playVoicesMixScratch.setSize (1, numSamples, false, false, true);
    const bool appliedStabilityChangePlay = playModeInput.process (midiMessages, ccRouter.getMidiChannel(), playModeEnabled,
        mono, playVoicesMixScratch.getWritePointer (0), numSamples,
        inputIsStable, continuousInputMidiNote, canApplyStabilityChangeNow());

    if (appliedStabilityChangeHarmonizer || appliedStabilityChangePlay)
        setLatencySamples (phraseScheduler.getLatencySamples()); // stessa Stability, stessa latenza per entrambi i pool

    // Le due modalita' sono mutuamente esclusive (FR-24): si sceglie l'una
    // o l'altra, non si sommano.
    const auto* voicesMix = playModeEnabled ? playVoicesMixScratch.getReadPointer (0) : voicesMixScratch.getReadPointer (0);

    // Bypass (FR-30): solo dry, esattamente come se Dry=1/Wet=0 — il
    // percorso dry e' gia' il segnale in ingresso non processato, quindi
    // non serve un secondo percorso audio dedicato.
    const float effectiveDryLevel = effective.bypassed ? 1.0f : dryLevel;
    const float effectiveWetLevel = effective.bypassed ? 0.0f : wetLevel;

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        auto* out = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            out[i] = effectiveDryLevel * mono[i] + effectiveWetLevel * voicesMix[i];
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
