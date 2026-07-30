#pragma once

#include "HarmonyPreset.h"
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace harmony
{
    // Lista ordinata di preset armonici. La posizione (1-based, getCcValue)
    // e' il futuro valore CC (FR-05): riordinare la rinumera (FR-07), voluto.
    // Ogni preset ha comunque un ID stabile indipendente dalla posizione (FR-10).
    //
    // Questa classe e' un semplice contenitore, copiabile per valore: la
    // sicurezza verso l'audio thread (FR-08/§9.4 del PRD: "si scambia con il
    // puntatore atomico, la vecchia si distrugge sul message thread") e'
    // responsabilita' di chi la possiede (vedi PluginProcessor::editPresetLibrary).
    class PresetLibrary
    {
    public:
        static constexpr int maxPresets = 128; // tetto tecnico (FR-02)

        PresetLibrary(); // popola con i 7 preset di fabbrica (FR-04)

        int getNumPresets() const noexcept { return (int) presets.size(); }
        const Preset& getPreset (int index) const noexcept;
        int getCcValue (int index) const noexcept { return index + 1; }

        // Ritorna l'indice del nuovo preset, o -1 se si e' raggiunto maxPresets.
        int addPreset (juce::String name, Table table);
        int duplicatePreset (int index);
        void renamePreset (int index, juce::String newName);
        void removePreset (int index);

        // Sposta il preset in fromIndex cosi' che finisca in toIndex (FR-06/07).
        void movePreset (int fromIndex, int toIndex);

        int indexOfId (const juce::Uuid& id) const noexcept;

        // FR-08: la libreria si serializza dentro lo stato del plugin.
        juce::ValueTree toValueTree() const;
        void loadFromValueTree (const juce::ValueTree& tree);

        // FR-09: libreria globale su disco, separata dalla sessione. Le due
        // operazioni sono esplicite, mai automatiche: vanno chiamate solo da
        // un'azione diretta dell'utente in UI.
        static juce::File getGlobalLibraryFile();
        bool saveAsGlobal() const;
        bool loadGlobal();

        static std::vector<Preset> makeFactoryPresets();

    private:
        std::vector<Preset> presets;
    };
}
