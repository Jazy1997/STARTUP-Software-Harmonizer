#pragma once

#include "../harmony/HarmonyPreset.h"
#include <array>
#include <cstdint>

// Una frase (FR-43..46): le voci del preset "congelate" al momento del
// trigger. Gli slot fisici (Voice) sono di proprieta' di VoicePool — qui si
// tiene solo quali indici di slot appartengono a questa frase.
//
// Solo la frase piu' recente (isLive) segue in tempo reale i cambi di
// preset/fondamentale finche' la nota che l'ha generata continua a suonare
// (FR-17).
//
// Cosa succede a una frase superata da un nuovo onset — risoluzione della
// tensione FR-17/FR-46 segnalata come [DECISION] nel PRD §6.1, validata
// all'ascolto in sessione 10 e risolta con un bottone utente ("Keep Tails",
// PhraseScheduler::setKeepTails) invece di un comportamento fisso:
//   - Keep Tails OFF (default): la frase si libera SUBITO. Senza l'editor
//     di pattern ritmico (FR-47..49, [V1.1], non ancora costruito) tutte le
//     voci di una frase entrano insieme al trigger — non c'e' mai una "coda"
//     di voci ancora in attesa. Lasciarla viva non farebbe che continuare a
//     ri-armonizzare il segnale live corrente con offset ormai vecchi:
//     verificato all'ascolto in sessione 10 che questo produce un accumulo
//     di voci e preset sovrapposti ad ogni nuovo attacco (bug, non feature).
//   - Keep Tails ON: comportamento precedente, invariato — la frase resta
//     viva (isLive=false, active=true) finche' non viene rubata o il
//     segnale tace. Diventa la scelta creativa giusta SOLO quando esistera'
//     il pattern coi ritardi: li' "tenere le code" significa lasciar finire
//     le voci gia' partite della frase precedente invece di troncarle di
//     netto (vedi l'esempio fornito dall'utente: tre note in corsa scaglio-
//     nata, con la coda dell'ultima tagliata o meno da un nuovo attacco).
//     Oggi, senza ritardi, i due casi "tronca la coda" e "lascia finire"
//     collassano nello stesso caso limite (nessuna voce mai "in coda"),
//     quindi Keep Tails ON oggi equivale semplicemente al vecchio
//     comportamento, non ancora alla ricchezza descritta sopra.
// Sessione 12 (fix scricchiolii/click): prima di questa sessione, "liberare"
// una frase (freePhrase, poi rinominata hardFreePhrase) tagliava di netto
// l'ampiezza di qualunque voce ancora udibile — verificato causa concreta di
// click, sia a fine frase (silenzio) sia quando una frase viene superata da
// un nuovo onset con MENO voci della precedente (le voci non riprese
// smettevano di essere processate, non solo mutate). `releasing` introduce
// un rilascio morbido: la frase resta `active` (protetta da riuso/doppio
// processing) ma tutte le sue voci sfumano a zero (Voice::isSilent(), vedi
// PhraseScheduler::process) prima che la frase venga davvero disattivata.
// L'unico caso che NON passa da qui e' il furto d'emergenza (FR-52, pool
// esaurito): la' serve uno slot fisico SUBITO, e la continuita' e' gia'
// data dal Glide dell'offset sul nuovo target (vedi PhraseScheduler.cpp).
struct Phrase
{
    std::array<int, harmony::numVoices> slotIndices { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<harmony::Cell, harmony::numVoices> frozenOffsets {};
    // FR-17 (sessione 28 — "ribattuto", vedi src/voices/EmptyCellHold.h):
    // quanti campioni consecutivi la cella di ogni colonna e' rimasta vuota
    // su QUESTA frase, da quando l'ultima volta aveva un valore (o dal
    // trigger, se non ne ha mai avuto uno). Azzerato a triggerNewPhrase.
    std::array<int, harmony::numVoices> emptyCellSamples {};
    // B-12 (sessione 30): quando una voce viene AGGIUNTA a una frase gia' in
    // suono (il selettore Voices sale, late-binding in PhraseScheduler), lo
    // slot appena legato ha il motore freddo e resta muto per tutta la
    // latenza dichiarata (~20ms). La dissolvenza anti-click di Voice dura
    // 8ms: partendo subito si esaurisce DENTRO quel silenzio, e quando il
    // segnale vero arriva la voce e' gia' a guadagno pieno — entra di netto.
    // Misurato: ritardo 20.09ms, salita 10%->90% di 3.56ms invece di 8ms.
    // Qui si contano i campioni di riscaldamento ancora dovuti: finche' sono
    // > 0 la voce resta muta e viene alimentata con processWarmOnly (stesso
    // meccanismo di sessione 27/B-04), cosi' la dissolvenza inizia quando il
    // motore ha davvero qualcosa da dire. Zero = voce operativa.
    // NON si applica alle voci nate col trigger di una frase nuova: quello e'
    // l'attacco di nota, un evento diverso e gia' validato all'ascolto.
    std::array<int, harmony::numVoices> warmupSamples {};
    uint64_t age = 0;
    bool active = false;
    bool isLive = false;
    bool releasing = false;
};
