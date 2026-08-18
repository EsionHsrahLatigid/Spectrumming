#include "BridgeLocator.h"

#if JUCE_MAC
 #include <dlfcn.h>
#elif JUCE_WINDOWS
 #include <windows.h>
#endif

#include <array>

namespace spectrumming::plugin
{
namespace
{
int moduleAnchor = 0;

juce::String stagedPlatformName()
{
#if JUCE_MAC
    return "macos-arm64";
#elif JUCE_WINDOWS
    return "windows-x64";
#else
    return "linux-x64";
#endif
}
} // namespace

juce::File BridgeLocator::resolve()
{
    const auto configured = juce::SystemStats::getEnvironmentVariable(
        "SPECTRUMMING_BRIDGE_PATH", {});
    if(configured.isNotEmpty())
    {
        const juce::File candidate(configured);
        if(candidate.existsAsFile())
            return candidate;
    }

    for(const auto& origin : { currentModuleFile(), juce::File::getCurrentWorkingDirectory() })
        if(const auto candidate = findNear(origin); candidate.existsAsFile())
            return candidate;

    return installedBridge();
}

juce::File BridgeLocator::findNear(const juce::File& origin)
{
    auto directory = origin.existsAsFile() ? origin.getParentDirectory() : origin;
    for(int depth = 0; depth < 12 && directory != juce::File {}; ++depth)
    {
        if(const auto direct = bridgeBelow(directory); direct.existsAsFile())
            return direct;

        const auto nestedStage = directory.getChildFile("artifacts")
                                     .getChildFile("plugin-release")
                                     .getChildFile(stagedPlatformName());
        if(const auto nested = bridgeBelow(nestedStage); nested.existsAsFile())
            return nested;

        const auto parent = directory.getParentDirectory();
        if(parent == directory)
            break;
        directory = parent;
    }
    return {};
}

juce::File BridgeLocator::currentModuleFile()
{
#if JUCE_MAC
    Dl_info information {};
    if(dladdr(static_cast<const void*>(&moduleAnchor), &information) != 0
       && information.dli_fname != nullptr)
        return juce::File(information.dli_fname);
#elif JUCE_WINDOWS
    HMODULE module = nullptr;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                             | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&moduleAnchor), &module) != 0)
    {
        std::array<wchar_t, 32768> path {};
        const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if(length > 0 && length < static_cast<DWORD>(path.size()))
            return juce::File(juce::String(path.data(), static_cast<int>(length)));
    }
#endif
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile);
}

juce::File BridgeLocator::installedBridge()
{
#if JUCE_MAC
    const auto userCandidate = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                   .getChildFile("Applications")
                                   .getChildFile("Spectrumming Bridge.app/Contents/MacOS/Spectrumming Bridge");
    if(userCandidate.existsAsFile())
        return userCandidate;
    return juce::File("/Applications")
        .getChildFile("Spectrumming Bridge.app/Contents/MacOS/Spectrumming Bridge");
#elif JUCE_WINDOWS
    return juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
        .getChildFile("Spectrumming Bridge/Spectrumming Bridge.exe");
#else
    return {};
#endif
}

juce::File BridgeLocator::bridgeBelow(const juce::File& stageRoot)
{
#if JUCE_MAC
    return stageRoot.getChildFile("bridge")
        .getChildFile("Spectrumming Bridge.app/Contents/MacOS/Spectrumming Bridge");
#elif JUCE_WINDOWS
    return stageRoot.getChildFile("bridge").getChildFile("Spectrumming Bridge.exe");
#else
    juce::ignoreUnused(stageRoot);
    return {};
#endif
}
} // namespace spectrumming::plugin
