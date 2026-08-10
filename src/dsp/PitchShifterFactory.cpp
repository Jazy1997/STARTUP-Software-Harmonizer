#include "PitchShifter.h"
#include "PsolaShifter.h"
#include "SpectralShifter.h"

// Unico punto di scelta del motore attivo dietro l'interfaccia astratta
// (FR-62, CLAUDE.md regola 2): Voice.cpp e VoicePool.cpp chiamano solo
// questa factory, mai un costruttore concreto direttamente.
//
// Default: PSOLA proprietario (rischio piu' alto del PRD §13, ora
// integrato e verificato numericamente — vedi tests/psola_test.cpp e
// LOG/archivio-s01-s28.md). SpectralShifter (motore interinale STFT) resta compilato e
// funzionante come fallback: per tornare ad esso basta definire
// HARMONIZER_USE_SPECTRAL_SHIFTER, senza toccare nessun altro file.
std::unique_ptr<PitchShifter> createDefaultPitchShifter()
{
#if defined (HARMONIZER_USE_SPECTRAL_SHIFTER) && HARMONIZER_USE_SPECTRAL_SHIFTER
    return std::make_unique<SpectralShifter>();
#else
    return std::make_unique<PsolaShifter>();
#endif
}
