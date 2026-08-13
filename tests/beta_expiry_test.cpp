// Verifica numerica della scadenza delle build beta (sessione 37, D-26 —
// vedi src/licensing/BetaGate.h).
// Nessuna dipendenza JUCE: si compila ed esegue in meno di un secondo,
// stesso principio di pitch_latch_test.cpp.
//
// Copre l'ARITMETICA della scadenza, non il suo effetto sull'audio: che il wet
// scenda senza click e' garantito dalla rampa di wetGlide, gia' coperta da
// glide_test.cpp (processRamp campione-per-campione), e la verifica
// all'ascolto resta dell'utente.
//
// Compilazione: g++ -O2 -std=c++20 -Isrc tests/beta_expiry_test.cpp -o beta_expiry_test

#include "licensing/BetaGate.h"

#include <cstdio>
#include <cstdint>

namespace
{
    int failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("  %-70s %s\n", description, condition ? "OK" : "FALLITO");
        if (! condition)
            ++failures;
    }

    using Gate = licensing::BetaGate;

    // Un istante fisso qualunque: il valore assoluto non conta, conta solo la
    // differenza con nowEpoch. Non lo si lega a una data reale proprio per non
    // far dipendere il banco dal calendario di chi lo esegue.
    constexpr std::int64_t kBuild = 1'700'000'000;
    constexpr std::int64_t kDay = Gate::secondsPerDay;

    // La durata che spediremo davvero ai tester (HARMONIZER_BETA_DAYS in
    // CMakeLists.txt). Se quel default cambiasse, questo banco continua a
    // valere: verifica la regola, non il numero.
    constexpr int kBetaDays = 30;
}

int main()
{
    std::printf ("\n=== Scadenza delle build beta (BetaGate) ===\n\n");

    std::printf ("[1] La finestra di 30 giorni\n");
    check (! Gate::isExpired (kBuild, kBuild, kBetaDays),
           "appena compilata: non scaduta");
    check (! Gate::isExpired (kBuild, kBuild + 29 * kDay, kBetaDays),
           "dopo 29 giorni: non scaduta");
    check (! Gate::isExpired (kBuild, kBuild + 30 * kDay - 1, kBetaDays),
           "un secondo prima della scadenza: non scaduta");
    check (Gate::isExpired (kBuild, kBuild + 30 * kDay, kBetaDays),
           "all'istante esatto della scadenza: scaduta");
    check (Gate::isExpired (kBuild, kBuild + 30 * kDay + 1, kBetaDays),
           "un secondo dopo la scadenza: scaduta");
    check (Gate::isExpired (kBuild, kBuild + 365 * kDay, kBetaDays),
           "un anno dopo: scaduta");

    std::printf ("\n[2] La scadenza cade dove dice l'aritmetica\n");
    check (Gate::expiryEpoch (kBuild, kBetaDays) == kBuild + 30 * kDay,
           "expiryEpoch = compilazione + giorni * 86400");
    check (Gate::expiryEpoch (kBuild, 0) == kBuild,
           "con days=0 la scadenza coincide con la compilazione");

    std::printf ("\n[3] days=0, il gancio per provare la dissolvenza\n");
    // Serve a verificare all'ascolto che il wet scenda sulla rampa di 8 ms
    // senza aspettare trenta giorni: -DHARMONIZER_BETA=ON con DAYS=0.
    check (Gate::isExpired (kBuild, kBuild, 0),
           "con days=0 la build e' scaduta immediatamente");
    check (Gate::daysRemaining (kBuild, kBuild, 0) == 0,
           "con days=0 non restano giorni");

    std::printf ("\n[4] Orologio indietro: indulgente, e deliberatamente\n");
    // Vedi il commento su isExpired() in BetaGate.h: bloccare un tester
    // dall'orologio sbagliato costa piu' di quanto costi il pirata ingenuo.
    check (! Gate::isExpired (kBuild, kBuild - 1, kBetaDays),
           "un secondo prima della compilazione: NON scaduta");
    check (! Gate::isExpired (kBuild, kBuild - 365 * kDay, kBetaDays),
           "orologio spostato indietro di un anno: NON scaduta (scelta, non svista)");
    check (Gate::daysRemaining (kBuild, kBuild - kDay, kBetaDays) == kBetaDays + 1,
           "con l'orologio indietro di un giorno restano 31 giorni, non un valore negativo");

    std::printf ("\n[5] Il conto alla rovescia per l'avviso nell'editor\n");
    check (Gate::daysRemaining (kBuild, kBuild, kBetaDays) == 30,
           "appena compilata: 30 giorni");
    check (Gate::daysRemaining (kBuild, kBuild + kDay, kBetaDays) == 29,
           "dopo un giorno esatto: 29");
    // Arrotondamento per eccesso: mezza giornata residua non e' "0 giorni".
    check (Gate::daysRemaining (kBuild, kBuild + 29 * kDay + kDay / 2, kBetaDays) == 1,
           "con mezza giornata residua: 1, non 0 (arrotondamento per eccesso)");
    check (Gate::daysRemaining (kBuild, kBuild + 30 * kDay - 1, kBetaDays) == 1,
           "un secondo prima della scadenza: 1");
    check (Gate::daysRemaining (kBuild, kBuild + 30 * kDay, kBetaDays) == 0,
           "alla scadenza: 0");
    check (Gate::daysRemaining (kBuild, kBuild + 999 * kDay, kBetaDays) == 0,
           "molto dopo la scadenza: 0, mai negativo");

    // Le funzioni sono constexpr: le proprieta' portanti si possono provare
    // anche a tempo di compilazione, cosi' una regressione non arriva
    // nemmeno a produrre un eseguibile.
    static_assert (! Gate::isExpired (kBuild, kBuild, kBetaDays));
    static_assert (Gate::isExpired (kBuild, kBuild + 30 * kDay, kBetaDays));
    static_assert (Gate::isExpired (kBuild, kBuild, 0));
    static_assert (! Gate::isExpired (kBuild, kBuild - 365 * kDay, kBetaDays));
    static_assert (Gate::daysRemaining (kBuild, kBuild, kBetaDays) == 30);
    static_assert (Gate::daysRemaining (kBuild, kBuild + 999 * kDay, kBetaDays) == 0);

    std::printf ("\n%s (%d fallimenti)\n\n", failures == 0 ? "TUTTO OK" : "CI SONO FALLIMENTI", failures);
    return failures == 0 ? 0 : 1;
}
