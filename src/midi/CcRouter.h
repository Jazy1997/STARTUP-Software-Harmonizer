#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "OverrideManager.h"
#include <atomic>

// Interpreta i CC in ingresso secondo FR-30, e gestisce il MIDI Learn
// (FR-33). Non decide la precedenza CC/automazione (quello e' compito di
// OverrideManager, vedi PluginProcessor::processBlock): si limita a
// tradurre i messaggi MIDI grezzi di questo blocco in eventi FR-30-
// conformi, gia' filtrati per canale e validita'.
//
// Configurazione (numeri CC, canale) e stato di apprendimento sono
// std::atomic: scritti dal message thread (UI) o dall'audio thread stesso
// (durante l'apprendimento), letti dall'audio thread ogni blocco — nessun
// lock, nessuna allocazione (CLAUDE.md regola 1).
class CcRouter
{
public:
    enum class LearnTarget { none = -1, root = 0, preset = 1, bypass = 2 };

    // Real-time safe: nessuna allocazione, nessun lock. Da chiamare una
    // volta per blocco in processBlock, con il MidiBuffer ricevuto
    // dall'host. Se un MIDI Learn e' in corso (vedi startLearning()), il
    // primo messaggio CC ricevuto — su QUALUNQUE canale, il filtro canale
    // si applica solo all'uso normale — viene catturato come nuovo numero
    // CC per la funzione richiesta; il suo valore non viene anche
    // interpretato come evento in questo stesso blocco.
    OverrideManager::CcEvents process (const juce::MidiBuffer& midi) noexcept;

    // Da chiamare dal message thread (UI). "none" annulla un apprendimento
    // in corso senza catturare nulla.
    void startLearning (LearnTarget target) noexcept { learnTarget.store ((int) target, std::memory_order_relaxed); }
    LearnTarget getLearnTarget() const noexcept { return (LearnTarget) learnTarget.load (std::memory_order_relaxed); }

    void setRootCc (int ccNumber) noexcept { rootCc.store (ccNumber, std::memory_order_relaxed); }
    void setPresetCc (int ccNumber) noexcept { presetCc.store (ccNumber, std::memory_order_relaxed); }
    void setBypassCc (int ccNumber) noexcept { bypassCc.store (ccNumber, std::memory_order_relaxed); }
    // FR-32: 0 = omni (default), 1..16 = canale specifico.
    void setMidiChannel (int channel) noexcept { midiChannel.store (channel, std::memory_order_relaxed); }

    int getRootCc() const noexcept { return rootCc.load (std::memory_order_relaxed); }
    int getPresetCc() const noexcept { return presetCc.load (std::memory_order_relaxed); }
    int getBypassCc() const noexcept { return bypassCc.load (std::memory_order_relaxed); }
    int getMidiChannel() const noexcept { return midiChannel.load (std::memory_order_relaxed); }

private:
    // Numeri di partenza arbitrari (non collidono con CC standard comuni
    // come sustain/mod wheel): interamente riconfigurabili da UI o MIDI
    // Learn, non un requisito di prodotto.
    std::atomic<int> rootCc      { 20 };
    std::atomic<int> presetCc    { 21 };
    std::atomic<int> bypassCc    { 22 };
    std::atomic<int> midiChannel { 0 }; // omni

    std::atomic<int> learnTarget { (int) LearnTarget::none };
};
