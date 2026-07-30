#include "PresetLibrary.h"
#include "CsvIo.h"
#include <algorithm>

namespace harmony
{
    namespace
    {
        // Genera una tabella 12x8 impilando le voci sui toni dell'accordo
        // (tones, classi di altezza 0-11 ascendenti) scendendo ciclicamente
        // un'ottava alla volta a partire dal tono piu' vicino sotto (o uguale
        // a) la nota suonata. Le voci oltre il numero di toni restano mute.
        //
        // Algoritmo verificato per calcolo diretto contro l'unico dato di
        // prototipo nel PRD (§2.1): preset Min = {0,3,7,10} (Cm7), d=2 (nota D)
        // -> [-2,-4,-7,-11,null,null,null,null]. Gli altri preset di fabbrica
        // usano toni d'accordo standard di jazz, non verificati sul prototipo:
        // da sostituire con import CSV (FR-03) quando disponibili i dati reali.
        Table generateDropVoicingTable (std::vector<int> tones)
        {
            std::sort (tones.begin(), tones.end());
            const int numTones = (int) tones.size();

            Table table {};

            for (int d = 0; d < numDegrees; ++d)
            {
                int startIdx = -1;
                for (int i = numTones - 1; i >= 0; --i)
                {
                    if (tones[(size_t) i] <= d)
                    {
                        startIdx = i;
                        break;
                    }
                }

                int accumulatedOctave = 0;
                if (startIdx < 0)
                {
                    startIdx = numTones - 1;
                    accumulatedOctave = -12;
                }

                int idx = startIdx;
                for (int v = 0; v < numVoices; ++v)
                {
                    if (v >= numTones)
                    {
                        table[(size_t) d][(size_t) v] = std::nullopt;
                        continue;
                    }

                    const int tone = tones[(size_t) idx] + accumulatedOctave;
                    table[(size_t) d][(size_t) v] = tone - d;

                    if (idx == 0)
                    {
                        idx = numTones - 1;
                        accumulatedOctave -= 12;
                    }
                    else
                    {
                        --idx;
                    }
                }
            }

            return table;
        }
    }

    std::vector<Preset> PresetLibrary::makeFactoryPresets()
    {
        std::vector<Preset> result;
        auto add = [&] (juce::String name, std::vector<int> tones)
        {
            result.push_back ({ juce::Uuid(), std::move (name), generateDropVoicingTable (std::move (tones)) });
        };

        add ("Maj",      { 0, 4, 7, 11 });
        add ("Min",      { 0, 3, 7, 10 }); // verificato su prototipo (d=2, PRD §2.1)
        add ("Dom",      { 0, 4, 7, 10 });
        add ("Sus",      { 0, 5, 7, 10 });
        add ("Half Dim", { 0, 3, 6, 10 });
        add ("Dim",      { 0, 3, 6, 9 });
        add ("Aug7",     { 0, 4, 8, 10 });

        return result;
    }

    PresetLibrary::PresetLibrary() : presets (makeFactoryPresets())
    {
    }

    const Preset& PresetLibrary::getPreset (int index) const noexcept
    {
        jassert (index >= 0 && index < (int) presets.size());
        return presets[(size_t) index];
    }

    int PresetLibrary::addPreset (juce::String name, Table table)
    {
        if ((int) presets.size() >= maxPresets)
            return -1;

        presets.push_back ({ juce::Uuid(), std::move (name), std::move (table) });
        return (int) presets.size() - 1;
    }

    int PresetLibrary::duplicatePreset (int index)
    {
        if (index < 0 || index >= (int) presets.size() || (int) presets.size() >= maxPresets)
            return -1;

        Preset copy = presets[(size_t) index];
        copy.id = juce::Uuid(); // e' un preset distinto (FR-10), non un alias
        copy.name += " copy";
        presets.insert (presets.begin() + index + 1, std::move (copy));
        return index + 1;
    }

    void PresetLibrary::renamePreset (int index, juce::String newName)
    {
        if (index >= 0 && index < (int) presets.size())
            presets[(size_t) index].name = std::move (newName);
    }

    void PresetLibrary::removePreset (int index)
    {
        if (index >= 0 && index < (int) presets.size())
            presets.erase (presets.begin() + index);
    }

    void PresetLibrary::movePreset (int fromIndex, int toIndex)
    {
        if (fromIndex < 0 || fromIndex >= (int) presets.size())
            return;

        toIndex = juce::jlimit (0, (int) presets.size() - 1, toIndex);
        if (fromIndex == toIndex)
            return;

        auto preset = presets[(size_t) fromIndex];
        presets.erase (presets.begin() + fromIndex);
        presets.insert (presets.begin() + toIndex, std::move (preset));
    }

    int PresetLibrary::indexOfId (const juce::Uuid& id) const noexcept
    {
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].id == id)
                return i;
        return -1;
    }

    juce::ValueTree PresetLibrary::toValueTree() const
    {
        juce::ValueTree root ("PresetLibrary");
        for (auto& preset : presets)
        {
            juce::ValueTree node ("Preset");
            node.setProperty ("id", preset.id.toString(), nullptr);
            node.setProperty ("name", preset.name, nullptr);
            node.setProperty ("table", CsvIo::tableToCsv (preset.table), nullptr);
            root.appendChild (node, nullptr);
        }
        return root;
    }

    void PresetLibrary::loadFromValueTree (const juce::ValueTree& tree)
    {
        if (! tree.isValid() || ! tree.hasType ("PresetLibrary"))
            return;

        std::vector<Preset> loaded;
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto node = tree.getChild (i);
            Preset preset;
            preset.id = juce::Uuid (node.getProperty ("id").toString());
            preset.name = node.getProperty ("name").toString();

            if (auto parsed = CsvIo::parseCsv (node.getProperty ("table").toString()))
                preset.table = *parsed;

            loaded.push_back (std::move (preset));
        }

        if (! loaded.empty())
            presets = std::move (loaded);
    }

    juce::File PresetLibrary::getGlobalLibraryFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Harmonizer")
            .getChildFile ("GlobalPresetLibrary.xml");
    }

    bool PresetLibrary::saveAsGlobal() const
    {
        const auto file = getGlobalLibraryFile();
        file.getParentDirectory().createDirectory();

        if (auto xml = toValueTree().createXml())
            return xml->writeTo (file);
        return false;
    }

    bool PresetLibrary::loadGlobal()
    {
        const auto file = getGlobalLibraryFile();
        if (! file.existsAsFile())
            return false;

        if (auto xml = juce::XmlDocument::parse (file))
        {
            loadFromValueTree (juce::ValueTree::fromXml (*xml));
            return true;
        }
        return false;
    }
}
