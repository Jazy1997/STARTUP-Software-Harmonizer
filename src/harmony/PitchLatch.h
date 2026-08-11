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
    // COME FUNZIONA
    //
    // Entro +-25 cent dalla nota agganciata non si fa nulla. Oltre la soglia,
    // e solo quando l'arrotondamento standard di continuousMidiNote e'
    // davvero diverso dalla nota agganciata, quell'arrotondamento diventa un
    // CANDIDATO. Il candidato viene adottato — di colpo, per quanto lontano
    // sia — dopo che si e' ripetuto uguale per kNoteSettleMs millisecondi.
    //
    // Le due proprieta' che ne derivano sono entrambe requisiti, non effetti
    // collaterali:
    //   1. l'aggancio non passa MAI per una nota intermedia. Va dove il pitch
    //      e' realmente arrivato, o resta dov'era. La tabella armonica e'
    //      indicizzata da questa nota (HarmonyEngine::degreeOf): ogni valore
    //      intermedio restituito qui e' una colonna del preset che il motore
    //      suona davvero, su una nota che il performer non ha mai eseguito;
    //   2. la soglia e' in MILLISECONDI, contati sui campioni del blocco, non
    //      in numero di chiamate. Il comportamento e' quindi lo stesso a 128 e
    //      a 1024 campioni di buffer, che e' un requisito esplicito.
    //
    // SESSIONE 31 — PERCHE' NON SI SALE PIU' DI UN SEMITONO PER CHIAMATA
    //
    // Fino a s.30 l'aggancio si spostava di UN semitono per chiamata, bloccato
    // (min/max) all'arrotondamento standard. La motivazione originale era
    // corretta ma copriva un caso solo: un passo INCONDIZIONATO di un semitono
    // rimbalza avanti e indietro durante uno scivolamento lento, perche' il
    // salto supera quasi sempre la stima attuale e la chiamata successiva lo
    // disfa. Il bloccaggio all'arrotondamento risolveva quello.
    //
    // Il prezzo, non visto allora: un salto vero di N semitoni veniva servito
    // in N chiamate, cioe' N BLOCCHI, e ad ogni passo intermedio la tabella
    // armonica veniva letta e applicata sul serio. Misurato in s.31 sul
    // materiale dell'utente (tests/degree_trace_probe.cpp, "Test 1 - Basic
    // Silk Horns.wav", 4 note): 10 corse di offset applicato invece di 4, con
    // 5 corse di passaggio per 116 ms complessivi a 1024 campioni — e la
    // durata di quelle corse scalava col buffer (20 ms a 128, 464 ms a 4096),
    // cioe' il difetto cambiava forma coi settings dell'host. Le tre
    // transizioni del file mostrano che la stima del rilevatore NON attraversa
    // le note intermedie (salta pulita da 59.97 a 62.23): la spazzata era
    // fabbricata qui dentro. Confermato sull'export reale: il wet suonava
    // 262.8 Hz (= D4 trasposto di -2, la cella b2 del preset) dove il
    // riferimento stava a 196 Hz. Vedi BUGS.md B-13.
    //
    // Il rimbalzo da cui nasceva il passo incrementale non torna: adottando
    // ESATTAMENTE l'arrotondamento della stima, lo scarto residuo dalla nuova
    // nota agganciata e' per costruzione <= mezzo semitono e non c'e' nessun
    // overshoot da disfare. Durante uno scivolamento lento il candidato e'
    // comunque sempre il semitono adiacente, quindi l'aggancio segue la
    // melodia esattamente come prima, un grado alla volta.
    //
    // PERCHE' SERVE ANCHE L'ATTESA, E PERCHE' DURA COSI'
    //
    // Il salto diretto da solo non basta. Sulla terza transizione dello stesso
    // file (E->C) il rilevatore, appena riacquistata confidenza dopo il
    // transiente, riporta 60.696 — 70 cent crescente — per 14.5 ms prima di
    // assestarsi su 60.4. Quell'arrotondamento vale 61, cioe' il grado b2:
    // adottarlo subito significherebbe leggere di nuovo una colonna sbagliata,
    // solo per meno tempo. kNoteSettleMs deve percio' essere piu' lungo della
    // piu' lunga stima sbagliata ma confidente osservata a un cambio di nota
    // (14.5 ms misurati), con un margine — non e' un numero di comodo, ma non
    // e' nemmeno passato per l'ascolto: vedi HANDOFF.md.
    //
    // Il costo e' che un cambio di nota genuino viene adottato fino a
    // kNoteSettleMs piu' tardi. Non e' tempo aggiunto al silenzio: in quella
    // finestra la voce continua a suonare l'ultimo offset valido, che e'
    // esattamente cio' che il ramo della cella vuota (src/voices/EmptyCellHold.h)
    // gia' faceva e che all'ascolto risultava pulito.
    //
    // Risultato coerente con l'esempio fornito dall'utente in s.11: un vibrato
    // di ampiezza +-50 cent su C fa alternare la nota agganciata fra B, C e
    // C# seguendo il vibrato (comportamento voluto, non un difetto) — mentre
    // un'imprecisione entro +-25 cent non altera nulla.
    class PitchLatch
    {
    public:
        // Converte kNoteSettleMs in campioni. Da chiamare in prepareToPlay.
        // Se non viene chiamata, settleSamples resta 0 e l'adozione e'
        // immediata: si perde solo l'attesa, non il salto diretto — un
        // degrado sicuro, mai un ritorno alle note intermedie.
        void prepare (double sampleRate) noexcept
        {
            settleSamples = (int) std::lround ((double) kNoteSettleMs * sampleRate / 1000.0);
            reset();
        }

        // continuousMidiNote: stima continua del pitch, in semitoni MIDI
        // (frazionaria). onAttack: true su un nuovo onset, o al primo
        // aggancio dopo un silenzio/segnale instabile — forza l'aggancio
        // immediato alla nota piu' vicina, senza isteresi e senza attesa (un
        // vero attacco e' una discontinuita' netta, non ha bisogno di
        // tolleranza). numSamples: i campioni del blocco corrente, con cui si
        // misura l'attesa. Ritorna la nota (intera) attualmente agganciata.
        int update (float continuousMidiNote, bool onAttack, int numSamples) noexcept
        {
            const int nearest = (int) std::lround (continuousMidiNote);

            if (! locked || onAttack)
            {
                heldNote = nearest;
                candidateNote = nearest;
                candidateSamples = 0;
                locked = true;
                return heldNote;
            }

            const float deviation = continuousMidiNote - (float) heldNote;

            // Dentro la tolleranza, oppure fuori dalla tolleranza ma con
            // l'arrotondamento ancora fermo sulla nota agganciata (fra 25 e 50
            // cent di scarto): nessun candidato, e l'eventuale attesa in corso
            // si annulla — il pitch e' tornato dov'era.
            if (std::fabs (deviation) <= kHysteresisSemitones || nearest == heldNote)
            {
                candidateNote = heldNote;
                candidateSamples = 0;
                return heldNote;
            }

            if (nearest != candidateNote)
            {
                candidateNote = nearest;
                candidateSamples = 0;
            }

            candidateSamples += numSamples;

            if (candidateSamples >= settleSamples)
            {
                heldNote = candidateNote;
                candidateSamples = 0;
            }

            return heldNote;
        }

        // Da chiamare quando il segnale non e' piu' stabile (silenzio,
        // rumore): al prossimo update() si riparte da un aggancio pulito,
        // invece di riprendere l'isteresi da un valore ormai obsoleto.
        void reset() noexcept
        {
            locked = false;
            candidateSamples = 0;
        }

    private:
        static constexpr float kHysteresisSemitones = 0.25f; // +-25 cent
        // Vedi il commento in testa: piu' lungo dei 14.5 ms di stima
        // sbagliata ma confidente misurati sul materiale reale, con margine.
        static constexpr float kNoteSettleMs = 25.0f;

        int heldNote = 0;
        int candidateNote = 0;
        int candidateSamples = 0;
        int settleSamples = 0;
        bool locked = false;
    };
}
