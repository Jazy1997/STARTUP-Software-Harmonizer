#include "OverrideManager.h"

OverrideManager::Effective OverrideManager::resolve (const CcEvents& events,
                                                       int hostRootPitchClass,
                                                       int hostPresetOneBased,
                                                       bool hostBypass) noexcept
{
    if (events.root.present)
    {
        rootState.active = true;
        rootState.value = events.root.value;
    }

    if (events.preset.present)
    {
        presetState.active = true;
        presetState.value = events.preset.value;
    }

    if (events.bypass.present)
    {
        bypassState.active = true;
        bypassState.value = events.bypass.value;
    }

    return Effective {
        rootState.active ? rootState.value : hostRootPitchClass,
        presetState.active ? presetState.value : hostPresetOneBased,
        bypassState.active ? (bypassState.value != 0) : hostBypass
    };
}

void OverrideManager::clearOverrides() noexcept
{
    rootState.active = false;
    presetState.active = false;
    bypassState.active = false;
}
