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
// (FR-17); tutte le altre restano congelate per sempre a quando sono state
// superate da un nuovo onset (FR-46). Risoluzione della tensione FR-17/FR-46
// segnalata come [DECISION] nel PRD §6.1 — da validare all'ascolto.
struct Phrase
{
    std::array<int, harmony::numVoices> slotIndices { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<harmony::Cell, harmony::numVoices> frozenOffsets {};
    uint64_t age = 0;
    bool active = false;
    bool isLive = false;
};
