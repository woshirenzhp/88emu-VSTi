#include <juce_audio_utils/juce_audio_utils.h>
#define JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY 1
#include <juce_audio_processors/format_types/juce_VST3Headers.h>
#include <juce_audio_processors/utilities/juce_FlagCache.h>
#include <juce_audio_processors/format_types/juce_VST3Common.h>

juce::String testVst3Bridge()
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    struct Mapping final : IMidiMapping
    {
        uint32 PLUGIN_API addRef() override { return 1; }
        uint32 PLUGIN_API release() override { return 1; }
        tresult PLUGIN_API queryInterface(const TUID, void** p) override { *p = nullptr; return kNoInterface; }
        tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID& id) override
        { id = (ParamID) (bus * 10000 + channel * 200 + cc); return kResultTrue; }
    } mapping;
    juce::MidiEventList events;
    struct Control { ParamID id; double value; int offset; };
    std::vector<Control> controls;
    auto callback = [&](ParamID id, double value, int offset) { controls.push_back({id,value,offset}); };
    const juce::uint8 data[] = {0x41, 0x10, 0x42};
    for (int bus : {0, 3})
    {
        juce::StoredMidiMapping stored;
        stored.storeMappings(mapping, bus);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(7, 60, (juce::uint8)100),17);
        midi.addEvent(juce::MidiMessage::controllerEvent(7, 1, 42),23);
        midi.addEvent(juce::MidiMessage::controllerEvent(7, 1, 64),39);
        midi.addEvent(juce::MidiMessage::pitchWheel(7, 8192),41);
        midi.addEvent(juce::MidiMessage::programChange(7, 8),45);
        midi.addEvent(juce::MidiMessage::createSysExMessage(data, sizeof(data)),47);
        juce::MidiEventList::hostToPluginEventList(events, midi, stored, callback, bus);
        Event note{}, sysex{};
        if (events.getEvent(bus == 0 ? 0 : 2,note) != kResultOk || note.busIndex != bus || note.sampleOffset != 17 || note.noteOn.channel != 6)
            return "Native VST3 note bus/channel/offset conversion failed";
        if (events.getEvent(bus == 0 ? 1 : 3,sysex) != kResultOk || sysex.busIndex != bus || sysex.sampleOffset != 47 || sysex.type != Event::kDataEvent)
            return "Native VST3 SysEx bus/offset conversion failed: bus=" + juce::String(sysex.busIndex) + " offset=" + juce::String(sysex.sampleOffset) + " type=" + juce::String(sysex.type) + " count=" + juce::String(events.getEventCount());
    }
    if (events.getEventCount() != 4 || controls.size() != 8) return "Native VST3 event count failed";
    if (controls[0].id != 1201 || controls[4].id != 31201 || controls[0].offset != 23 || controls[1].offset != 39
        || std::abs(controls[0].value - 42.0/127.0)>1e-6 || controls[6].offset != 41 || controls[7].offset != 45)
        return "Per-bus VST3 CC/pitch/program mapping or timestamps failed";
    return {};
}
