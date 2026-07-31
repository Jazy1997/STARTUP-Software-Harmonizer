#pragma once

#include "../voices/VoicePool.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

// FR-24..28: modalita' Play. Quando e' attiva, la tabella del preset
// armonico e' completamente disattivata (FR-24): le voci non seguono piu'
// il grado armonico della nota suonata, ma inseguono direttamente una nota
// MIDI ricevuta in ingresso, finche' resta premuta.
//
// Architettura deliberatamente SEPARATA da PhraseScheduler, non costruita
// sopra: il modello e' fondamentalmente diverso. PhraseScheduler esiste per
// "un onset genera una frase con voicing congelato che vive di vita propria"
// (FR-43/FR-46) — non c'e' un concetto di "frase" qui, solo "nota premuta ->
// una voce insegue quella nota assoluta finche' non arriva il note-off". E'
// esattamente la logica gia' presente in Voice per la modalita' Fix (FR-22:
// bersaglio = quantizedPlayedNote + offset): con quantizedPlayedNote=0 e
// offset=notaMidi, il bersaglio assoluto e' semplicemente la nota MIDI.
//
// VoicePool dedicato (8 slot fissi, FR-25), separato dai 32 della catena
// Harmonizer: le due modalita' sono mutuamente esclusive in ogni istante
// (FR-24), quindi non serve condividere/contendere lo stesso pool.
class PlayModeInput
{
public:
    static constexpr int maxNotes = 8; // FR-25: fino a 8 note simultanee

    void prepare (double sampleRate, int maxBlockSize, int stabilityLevel);
    void reset();

    // Stesso schema realtime-safe di VoicePool/PhraseScheduler per i cambi
    // di Stability (FR-56/57): message thread costruisce e ripulisce,
    // audio thread applica solo quando sicuro.
    void requestStabilityChange (int newStabilityLevel) { voicePool.requestStabilityChange (newStabilityLevel); }
    void collectGarbage() { voicePool.collectGarbage(); }
    int getLatencySamples() const { return voicePool.getLatencySamples(); }

    // Da chiamare ogni blocco, ANCHE quando la modalita' Play non e'
    // attiva (modeActive=false): il tracking delle note premute resta
    // aggiornato (riattivare Play non richiede un nuovo note-on) e il
    // cambio di Stability in sospeso continua ad essere applicato in modo
    // uniforme indipendentemente dalla modalita' corrente. mixOutput viene
    // sempre azzerato internamente (stesso contratto di PhraseScheduler::
    // process); con modeActive=false resta a zero — FR-24, nessun
    // contributo audio quando la modalita' non e' attiva.
    //
    // midiChannel: 0 = omni, 1..16 = canale specifico (stesso filtro
    // condiviso col controllo CC, vedi CcRouter — un'unica impostazione di
    // canale per tutto il MIDI in ingresso al plugin).
    //
    // Ritorna true se e' stato applicato un cambio di Stability in questa
    // chiamata (il chiamante deve aggiornare setLatencySamples).
    bool process (const juce::MidiBuffer& midi,
                  int midiChannel,
                  bool modeActive,
                  const float* monoIn,
                  float* mixOutput,
                  int numSamples,
                  bool inputIsStable,
                  float continuousInputMidiNote,
                  bool applyStabilityChangeNow);

private:
    VoicePool voicePool;
    // -1 = slot libero; altrimenti la nota MIDI (0-127) che occupa lo slot.
    std::array<int, maxNotes> slotNote { };
};
