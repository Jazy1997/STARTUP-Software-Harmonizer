#pragma once

#include <algorithm>
#include <cmath>

namespace harmony
{
    // Isteresi sulla nota usata per il lookup nella tabella armonica
    // (sessione 11): senza questa classe, la nota "suonata" era un semplice
    // arrotondamento ricalcolato da zero ogni blocco (juce::roundToInt),
    // senza memoria di quale nota fosse gia' agganciata — qualunque vibrato
    // o imprecisione d'intonazione che attraversasse un confine di semitono
    // faceva scattare un cambio di colonna nella tabella. Senza un nuovo
    // onset a "ribattere" la frase (canto legato), quel cambio non si
    // consolidava mai in modo stabile: verificato all'ascolto in sessione 11
    // (note cantate legate, C->D->E, solo la prima si armonizzava).
    //
    // Logica pura, nessuna dipendenza JUCE (stesso principio di
    // OverrideManager): testabile senza aprire una DAW, vedi
    // tests/pitch_latch_test.cpp.
    //
    // Entro +-25 cent dalla nota agganciata non si fa nulla. Oltre la
    // soglia, ci si sposta di UN semitono per volta verso la nota piu'
    // vicina alla stima corrente — mai oltre: il passo e' sempre bloccato
    // (min/max) al risultato di un arrotondamento standard di
    // continuousMidiNote, cosi' non si "supera" mai la nota di destinazione
    // rispetto a dove il pitch e' realmente arrivato.
    //
    // Questo bloccaggio e' essenziale, non un dettaglio implementativo: un
    // semplice passo incondizionato (heldNote +-1 ogni volta che si supera
    // la soglia) rimbalza avanti e indietro ad ogni blocco durante uno
    // scivolamento lento — il salto di un intero semitono supera quasi
    // sempre la stima attuale del pitch, la nuova nota agganciata risulta
    // percio' "oltre" la soglia nella direzione opposta, e la chiamata
    // successiva la disfa immediatamente. Bloccando il passo al risultato
    // di un arrotondamento standard, l'aggancio segue fedelmente
    // l'arrotondamento a semitono piu' vicino non appena ci si allontana
    // dalla zona di tolleranza, senza mai anticiparlo ne' disfarlo.
    //
    // Risultato coerente con l'esempio fornito dall'utente: un vibrato di
    // ampiezza +-50 cent su C fa alternare la nota agganciata fra B, C e
    // C# seguendo il vibrato (comportamento voluto, non un difetto) —
    // mentre un'imprecisione entro +-25 cent non altera nulla.
    class PitchLatch
    {
    public:
        // continuousMidiNote: stima continua del pitch, in semitoni MIDI
        // (frazionaria). onAttack: true su un nuovo onset, o al primo
        // aggancio dopo un silenzio/segnale instabile — forza l'aggancio
        // immediato alla nota piu' vicina, senza isteresi (un vero attacco
        // e' una discontinuita' netta, non ha bisogno di tolleranza).
        // Ritorna la nota (intera) attualmente agganciata.
        int update (float continuousMidiNote, bool onAttack) noexcept
        {
            if (! locked || onAttack)
            {
                heldNote = (int) std::lround (continuousMidiNote);
                locked = true;
                return heldNote;
            }

            const float deviation = continuousMidiNote - (float) heldNote;
            const int nearest = (int) std::lround (continuousMidiNote);

            if (deviation > kHysteresisSemitones)
                heldNote = std::min (heldNote + 1, nearest);
            else if (deviation < -kHysteresisSemitones)
                heldNote = std::max (heldNote - 1, nearest);

            return heldNote;
        }

        // Da chiamare quando il segnale non e' piu' stabile (silenzio,
        // rumore): al prossimo update() si riparte da un aggancio pulito,
        // invece di riprendere l'isteresi da un valore ormai obsoleto.
        void reset() noexcept { locked = false; }

    private:
        static constexpr float kHysteresisSemitones = 0.25f; // +-25 cent

        int heldNote = 0;
        bool locked = false;
    };
}
