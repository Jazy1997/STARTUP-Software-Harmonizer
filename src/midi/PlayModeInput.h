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

    // FR-11/§8.1: gain/pan per voce si applicano qui per INDICE DI SLOT
    // (0..maxNotes-1), non per colonna armonica — a differenza di
    // PhraseScheduler non esiste qui un concetto di frase/colonna: gli 8
    // slot del pool dedicato SONO le 8 voci (vedi VoicePool sotto).
    void setVoiceGainLinear (int slotIndex, float gainLinear) { voicePool.getSlot (slotIndex).setGainLinear (gainLinear); }
    void setVoicePan (int slotIndex, float pan) { voicePool.getSlot (slotIndex).setPan (pan); }

    // FR-42: la correzione delle formanti si applica anche in modalita' Play.
    //
    // ATTENZIONE a cosa era davvero rotto (B-07): la correzione AUTOMATICA
    // (FR-39) qui ha sempre funzionato, perche' le Voice partono dal default
    // membro formantSpread=1.0 e in ShiftMode::fix semitonesToApply e' lo
    // shift reale (Voice.cpp: targetAbsoluteMidi - continuousInputMidiNote).
    // Quello che mancava erano i due CONTROLLI: il knob globale Fmt Spread
    // (FR-40) e gli 8 knob Fmt/Voice (FR-41) non arrivavano fin qui, quindi
    // erano inerti in Play mentre gain e pan funzionavano.
    //
    // Spread e' globale -> tutti gli slot, come PhraseScheduler::setFormantSpread.
    void setFormantSpread (float spread)
    {
        for (int i = 0; i < maxNotes; ++i)
            voicePool.getSlot (i).setFormantSpread (spread);
    }

    // L'offset per voce si scrive direttamente sullo slot, e NON come fa
    // PhraseScheduler::setVoiceFormantOffset (che itera le frasi attive per
    // trovare lo slot fisico che interpreta quella colonna armonica): qui non
    // esiste il concetto di frase ne' di colonna, gli 8 slot SONO le 8 voci.
    // Vale quindi la stessa asimmetria gia' documentata sopra per gain e pan:
    // "voce N" e' l'N-esimo slot allocato, non un ruolo musicale, quindi il
    // knob agisce su qualunque nota sia finita in quello slot. E' la scelta
    // coerente con FR-11, non una semantica nuova introdotta qui.
    void setVoiceFormantOffset (int slotIndex, float semitones) { voicePool.getSlot (slotIndex).setFormantOffsetSemitones (semitones); }

    // Da chiamare ogni blocco, ANCHE quando la modalita' Play non e'
    // attiva (modeActive=false): il tracking delle note premute resta
    // aggiornato (riattivare Play non richiede un nuovo note-on) e il
    // cambio di Stability in sospeso continua ad essere applicato in modo
    // uniforme indipendentemente dalla modalita' corrente. mixL/mixR
    // vengono sempre azzerati internamente (stesso contratto di
    // PhraseScheduler::process); con modeActive=false restano a zero —
    // FR-24, nessun contributo audio quando la modalita' non e' attiva.
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
                  float* mixL,
                  float* mixR,
                  int numSamples,
                  bool inputIsStable,
                  float continuousInputMidiNote,
                  bool applyStabilityChangeNow);

private:
    VoicePool voicePool;
    // -1 = slot libero; altrimenti la nota MIDI (0-127) che occupa lo slot.
    std::array<int, maxNotes> slotNote { };

    // B-15 (sessione 34) — riscaldamento del motore prima che la voce si
    // faccia sentire. E' lo stesso meccanismo di Phrase::warmupSamples
    // (B-12, sessione 30), portato qui perche' il percorso Play non l'aveva
    // mai avuto: uno slot senza nota premuta non riceve campioni, quindi il
    // suo PitchShifter resta AFFAMATO (absWrite/absRead/lastEpoch/synthPos
    // congelati, ring pieno dell'audio della nota precedente). Al note-on i
    // primi `latency` campioni in uscita vengono da quel contenuto vecchio, e
    // gli 8 ms di ampGlide si consumano li' dentro: quando il segnale vero
    // rientra la voce e' gia' a guadagno pieno ed entra di netto.
    //
    // Misurato prima del fix in tests/play_mode_input_test.cpp: sulla SECONDA
    // nota il salto d'ampiezza all'attacco vale 3.3-5.1 volte quello del
    // regime (PM-3) e ad Accurate l'intonazione nella finestra di dissolvenza
    // e' ancora quella della nota PRECEDENTE (PM-4: 164.7 Hz contro i 392.0
    // richiesti). E' il "click ogni volta che premo il tasto" riportato
    // dall'utente.
    //
    // Campioni di riscaldamento ancora dovuti su questo slot: finche' sono
    // > 0 la voce resta MUTA e si limita ad alimentare il motore con
    // processWarmOnly, cosi' la dissolvenza inizia quando il motore ha
    // davvero qualcosa da dire. Zero = slot operativo.
    std::array<int, maxNotes> warmupSamples { };

    // Vero quando il motore di questo slot e' stato azzerato (goCold) e da
    // allora non ha piu' ricevuto campioni: al prossimo note-on va
    // riscaldato. Serve anche a garantire che goCold() scatti UNA SOLA VOLTA
    // per rilascio — PitchShifter::reset() azzera buffer interi, non puo'
    // girare ad ogni blocco su 8 slot dentro processBlock (PRD §9.4).
    // Inizializzato a true: dopo prepare() nessuno slot ha mai visto un
    // campione.
    std::array<bool, maxNotes> engineIsCold { };
};
