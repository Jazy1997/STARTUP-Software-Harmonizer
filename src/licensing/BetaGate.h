#pragma once

#include <cstdint>

// ============================================================================
//  ATTENZIONE — QUESTO NON E' IL LICENSING DI M6.
//
//  Non e' il LicenseManager di FR-63..FR-70, non gli somiglia e non va esteso
//  per diventarlo. E' un cancello TEMPORANEO con un solo scopo: le build date
//  in mano agli artisti beta (sessione 37, D-26) smettono di produrre wet dopo
//  un numero fisso di giorni dalla compilazione, cosi' una copia che gira non
//  resta una versione completa e illimitata per sempre.
//
//  Quando arrivera' il licensing vero (A-01, M6) questo file VA CANCELLATO,
//  insieme all'option HARMONIZER_BETA in CMakeLists.txt e ai due punti di
//  innesto in PluginProcessor.cpp. Non va fatto evolvere: FR-64 chiede la
//  verifica offline di una licenza firmata crittograficamente, che e' un altro
//  problema con un'altra forma.
//
//  LIMITE, da non fraintendere: chiunque sposti indietro l'orologio di sistema
//  riottiene il wet. E' un deterrente, non un DRM — vedi isExpired() sotto per
//  perche' e' stata scelta deliberatamente la strada indulgente.
// ============================================================================

namespace licensing
{
    // Aritmetica pura, senza JUCE e senza chiamate di sistema: l'ora non viene
    // letta qui ma passata dal chiamante. E' cio' che rende questo header
    // testabile in un millisecondo (tests/beta_expiry_test.cpp) e cio' che
    // tiene std::time() fuori dall'audio thread — CLAUDE.md regola 1 / PRD
    // §9.4 vietano una chiamata di sistema in processBlock. Chi chiama legge
    // l'ora sul message thread e pubblica il risultato in un flag atomico.
    struct BetaGate
    {
        static constexpr std::int64_t secondsPerDay = 86400;

        static constexpr std::int64_t expiryEpoch (std::int64_t buildEpoch, int days) noexcept
        {
            return buildEpoch + (std::int64_t) days * secondsPerDay;
        }

        // Scaduto quando l'ora corrente raggiunge la scadenza. Il confronto e'
        // >= e non >: con days=0 la build e' scaduta subito, che e' il modo di
        // provare la dissolvenza del wet senza aspettare trenta giorni
        // (vedi la tabella di verifica in LOG/sessione-37.md).
        //
        // OROLOGIO INDIETRO — scelta deliberata: se nowEpoch e' PRIMA della
        // data di compilazione, la build NON e' considerata scaduta. Trattare
        // quel caso come "scaduto" prenderebbe il pirata ingenuo ma
        // bloccherebbe anche un tester col semplice orologio sbagliato, e per
        // questi destinatari il secondo caso e' piu' probabile del primo:
        // spostare indietro l'orologio di una macchina da produzione rompe
        // iLok e la maggior parte dei plugin commerciali installati, quindi
        // non e' una strada che un musicista percorre per sbaglio ne' per
        // comodita'. Falso blocco di un tester vero = feedback perso, che e'
        // esattamente cio' che questa beta deve produrre.
        static constexpr bool isExpired (std::int64_t buildEpoch, std::int64_t nowEpoch, int days) noexcept
        {
            return nowEpoch >= expiryEpoch (buildEpoch, days);
        }

        // Giorni pieni che restano, arrotondati PER ECCESSO: finche' rimane un
        // istante utile la risposta e' almeno 1, e 0 significa "scaduta", non
        // "ultimo giorno". Serve all'avviso nell'editor: il tester vede il
        // conto alla rovescia e la scadenza non lo cogliera' di sorpresa
        // facendogli scrivere una segnalazione di bug che non e' un bug.
        static constexpr int daysRemaining (std::int64_t buildEpoch, std::int64_t nowEpoch, int days) noexcept
        {
            const std::int64_t remaining = expiryEpoch (buildEpoch, days) - nowEpoch;
            if (remaining <= 0)
                return 0;

            return (int) ((remaining + secondsPerDay - 1) / secondsPerDay);
        }
    };
}
