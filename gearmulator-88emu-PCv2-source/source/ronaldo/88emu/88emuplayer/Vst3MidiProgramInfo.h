#pragma once
// Shared metadata for both VST3 component and controller. IDs match the existing
// IMidiMapping layout, preserving projects saved with earlier four-port builds.
namespace emu88Player::vst3Midi
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    constexpr int channelCount = 64;
    constexpr ParamID parameterBase = 0x6d636d00;
    constexpr ParamID programBase = parameterBase + channelCount * kCountCtrlNumber;

    inline bool decode(ParamID id, int& channel, int& controller)
    {
        if(id < parameterBase || id >= programBase + channelCount) return false;
        const auto index = id - parameterBase;
        channel = static_cast<int>(id < programBase ? index / kCountCtrlNumber : id - programBase) + 1;
        controller = id < programBase ? static_cast<int>(index % kCountCtrlNumber) : kCtrlProgramChange;
        return true;
    }
    inline tresult unitInfo(int32 index, UnitInfo& info)
    {
        if(index < 0 || index > channelCount) return kResultFalse;
        info = {};
        info.id = index;
        info.parentUnitId = index == 0 ? kNoParentUnitId : kRootUnitId;
        info.programListId = index == 0 ? kNoProgramListId : static_cast<ProgramListID>(programBase + index - 1);
        const auto name = index == 0 ? juce::String("Root Unit")
            : juce::String("MIDI ") + juce::String::charToString('A' + (index - 1) / 16) + " Ch " + juce::String((index - 1) % 16 + 1);
        Steinberg::UString(info.name, 128).fromAscii(name.toRawUTF8());
        return kResultOk;
    }
    inline tresult listInfo(int32 index, ProgramListInfo& info)
    {
        if(index < 0 || index >= channelCount) return kResultFalse;
        info = {}; info.id = static_cast<ProgramListID>(programBase + index); info.programCount = 128;
        emu88MidiTrace("PROGRAM_LIST index=%d id=%d programs=128", index, info.id);
        UnitInfo unit{}; unitInfo(index + 1, unit);
        std::copy(std::begin(unit.name), std::end(unit.name), std::begin(info.name));
        return kResultOk;
    }
    inline tresult programName(ProgramListID id, int32 index, String128 name)
    {
        if(id < static_cast<ProgramListID>(programBase) || id >= static_cast<ProgramListID>(programBase + channelCount)
            || index < 0 || index >= 128) return kResultFalse;
        Steinberg::UString(name, 128).fromAscii((juce::String("MIDI Program ") + juce::String(index + 1)).toRawUTF8());
        return kResultOk;
    }
    inline tresult unitByBus(MediaType type, BusDirection direction, int32 bus, int32 channel, UnitID& unit)
    {
        if(type != kEvent || direction != kInput || bus < 0 || bus >= 4 || channel < 0 || channel >= 16) return kResultFalse;
        unit = 1 + bus * 16 + channel; return kResultOk;
    }
}
