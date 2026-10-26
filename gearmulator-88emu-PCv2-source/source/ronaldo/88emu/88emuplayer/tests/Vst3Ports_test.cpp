// Loads the built DLL through the public VST3 ABI, not through Processor internals.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_processors/format_types/juce_VST3Headers.h>
#include <iostream>
#include <set>
#include <stdexcept>
#include <array>
#include <vector>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <cstdlib>

using namespace Steinberg;
using namespace Steinberg::Vst;
static void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
#define STACK_UNKNOWN \
    uint32 PLUGIN_API addRef() override { return 1; } \
    uint32 PLUGIN_API release() override { return 1; } \
    tresult PLUGIN_API queryInterface(const TUID, void** p) override { *p = nullptr; return kNoInterface; }

struct Events : IEventList
{
    STACK_UNKNOWN
    std::vector<Event> events;
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events.size()); }
    tresult PLUGIN_API getEvent(int32 i, Event& e) override
    {
        if (i < 0 || i >= getEventCount()) return kResultFalse;
        e = events[i]; return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events.push_back(e); return kResultOk; }
    void note(int port, int pitch, bool on)
    {
        Event e{}; e.busIndex = port; e.sampleOffset = 17;
        e.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
        if (on) { e.noteOn.channel = 0; e.noteOn.pitch = pitch; e.noteOn.velocity = 0.8f; e.noteOn.noteId = -1; }
        else { e.noteOff.channel = 0; e.noteOff.pitch = pitch; e.noteOff.noteId = -1; }
        events.push_back(e);
    }
};
struct Queue : IParamValueQueue
{
    STACK_UNKNOWN
    ParamID id{}; std::vector<std::pair<int32, ParamValue>> points;
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return static_cast<int32>(points.size()); }
    tresult PLUGIN_API getPoint(int32 i, int32& offset, ParamValue& v) override
    { if (i < 0 || i >= getPointCount()) return kResultFalse; offset = points[i].first; v = points[i].second; return kResultOk; }
    tresult PLUGIN_API addPoint(int32 offset, ParamValue v, int32& index) override
    { index = getPointCount(); points.emplace_back(offset, v); return kResultOk; }
};
struct Changes : IParameterChanges
{
    STACK_UNKNOWN
    std::array<Queue, 64> queues;
    int32 count = 0;
    int32 PLUGIN_API getParameterCount() override { return count; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override
    { return i >= 0 && i < count ? &queues[i] : nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override
    { if (count == 64) return nullptr; index = count++; queues[index].id = id; queues[index].points.clear(); return &queues[index]; }
    void cc(IMidiMapping* mapping, int port, CtrlNumber cc, double value)
    {
        ParamID id; require(mapping->getMidiControllerAssignment(port, 0, cc, id) == kResultOk, "CC mapping failed");
        int32 index; auto* q = addParameterData(id, index); require(q != nullptr, "queue full");
        q->addPoint(0, value, index);
    }
};
template<class T> T* query(FUnknown* object)
{
    T* result = nullptr;
    require(object->queryInterface(T::iid, reinterpret_cast<void**>(&result)) == kResultOk, "missing VST3 interface");
    return result;
}

int main(int argc, char** argv)
{
    try
    {
        require(argc >= 2 && argc <= 4, "usage: 88emuVst3_test plugin.vst3 [legacy-accepted.txt [mapped|raw|both]]");
        const std::string inputMode = argc == 4 ? argv[3] : "mapped";
        require(inputMode == "mapped" || inputMode == "raw" || inputMode == "both" || inputMode == "standard" || inputMode == "detached" || inputMode == "multipoint", "invalid input mode");
        juce::ScopedJuceInitialiser_GUI gui;
        juce::DynamicLibrary dll;
        require(dll.open(argv[1]), "cannot load VST3 DLL");
        auto init = reinterpret_cast<bool (*)()>(dll.getFunction("InitDll"));
        auto exit = reinterpret_cast<bool (*)()>(dll.getFunction("ExitDll"));
        require(!init || init(), "InitDll failed");
        auto getFactory = reinterpret_cast<IPluginFactory* (PLUGIN_API*)()>(dll.getFunction("GetPluginFactory"));
        require(getFactory != nullptr, "no factory export");
        auto* factory = getFactory();
        IComponent* component = nullptr;
        for (int32 i = 0; i < factory->countClasses(); ++i)
        {
            PClassInfo info{}; factory->getClassInfo(i, &info);
            if (std::string(info.category) == kVstAudioEffectClass)
                factory->createInstance(info.cid, IComponent::iid, reinterpret_cast<void**>(&component));
        }
        require(component != nullptr, "no instrument component");
        HostApplication host;
        require(component->initialize(&host) == kResultOk, "component init failed");
        require(component->getBusCount(kEvent, kInput) == 4, "expected FOUR MIDI input buses");
        for (int port = 0; port < 4; ++port)
        {
            BusInfo info{};
            require(component->getBusInfo(kEvent, kInput, port, info) == kResultOk && info.channelCount == 16,
                    "expected 16 channels on each input");
            require(component->activateBus(kEvent, kInput, port, true) == kResultOk, "bus activation failed");
        }
        BusInfo bad{};
        require(component->getBusInfo(kEvent, kInput, 4, bad) != kResultOk, "invalid bus accepted");
        TUID controllerId{}; component->getControllerClassId(controllerId);
        IEditController* controller = nullptr;
        require(factory->createInstance(controllerId, IEditController::iid, reinterpret_cast<void**>(&controller)) == kResultOk,
                "cannot create controller");
        require(controller->initialize(&host) == kResultOk, "controller init failed");
        auto* componentConnection = query<IConnectionPoint>(component);
        auto* controllerConnection = query<IConnectionPoint>(controller);
        componentConnection->connect(controllerConnection); controllerConnection->connect(componentConnection);
        auto* mapping = query<IMidiMapping>(controller);
        auto* units = query<IUnitInfo>(controller);
        require(units->getUnitCount() == 65 && units->getProgramListCount() == 64, "expected 64 channel units/program lists plus root");
        std::array<ParamID, 64> standardPC{};
        std::array<std::array<ParamID, 131>, 64> midiIDs{};
        for (int channel = 0; channel < 64; ++channel)
        {
            UnitID unitId;
            require(units->getUnitByBus(kEvent, kInput, channel / 16, channel % 16, unitId) == kResultOk, "missing bus/channel unit");
            UnitInfo unit{}; require(units->getUnitInfo(unitId, unit) == kResultOk && unit.id == unitId, "invalid channel unit");
            ProgramListInfo list{};
            require(units->getProgramListInfo(channel, list) == kResultOk && list.id == unit.programListId && list.programCount == 128, "invalid MIDI program list");
            String128 name{}; require(units->getProgramName(list.id, 127, name) == kResultOk, "program 128 unavailable");
            int matching = 0;
            for (int i = 0; i < controller->getParameterCount(); ++i)
            {
                ParameterInfo info{}; controller->getParameterInfo(i, info);
                if (info.unitId == unitId && (info.flags & ParameterInfo::kIsProgramChange))
                {
                    require(info.stepCount == 127, "PC must have 128 discrete programs");
                    require(std::abs(controller->plainParamToNormalized(info.id, 127) - 1.0) < 1e-9, "PC normalization incorrect");
                    standardPC[channel] = info.id; ++matching;
                }
            }
            require(matching == 1 && standardPC[channel] == static_cast<ParamID>(list.id), "unit must expose one matching PC parameter");
        }
        std::set<ParamID> ids;
        for (int port = 0; port < 4; ++port)
            for (int channel = 0; channel < 16; ++channel)
                for (int cc = 0; cc <= kCtrlProgramChange; ++cc)
                {
                    ParamID id;
                    require(mapping->getMidiControllerAssignment(port, static_cast<int16>(channel), static_cast<CtrlNumber>(cc), id) == kResultOk,
                            "missing controller mapping");
                    require(ids.insert(id).second, "controller IDs collide across ports/channels");
                    midiIDs[port * 16 + channel][cc] = id;
                    if (cc == kCtrlProgramChange) require(id == standardPC[port * 16 + channel], "standard/mapped PC IDs differ");
                }
        ParamID invalid;
        require(mapping->getMidiControllerAssignment(4, 0, 7, invalid) != kResultOk, "invalid controller bus accepted");
        require(mapping->getMidiControllerAssignment(0, 16, 7, invalid) != kResultOk, "invalid channel accepted");
        require(mapping->getMidiControllerAssignment(0, 0, 131, invalid) != kResultOk, "invalid controller accepted");
        MemoryStream savedState;
        require(component->getState(&savedState) == kResultOk && savedState.getSize() > 0, "state save failed");
        savedState.seek(0, IBStream::kIBSeekSet, nullptr);
        require(component->setState(&savedState) == kResultOk, "state restore failed");
        auto* view = controller->createView(ViewType::kEditor);
        require(view != nullptr, "editor unavailable");
        ViewRect rect{};
        require(view->getSize(&rect) == kResultOk && rect.getWidth() > 0 && rect.getHeight() > 0, "editor size invalid");
        view->release();
        auto* processor = query<IAudioProcessor>(component);
        ProcessSetup setup{kRealtime, kSample32, 256, 44100.0};
        require(processor->setupProcessing(setup) == kResultOk, "setup failed");
        component->activateBus(kAudio, kOutput, 0, true);
        require(component->setActive(true) == kResultOk, "activation failed");
        processor->setProcessing(true);
        std::array<float, 256> left{}, right{};
        float* audio[] = {left.data(), right.data()};
        AudioBusBuffers output{}; output.numChannels = 2; output.channelBuffers32 = audio;
        Events events; Changes changes;
        ProcessData data{}; data.processMode = kRealtime; data.symbolicSampleSize = kSample32;
        data.numSamples = 256; data.numOutputs = 1; data.outputs = &output;
        data.inputEvents = &events; data.inputParameterChanges = &changes;
        auto render = [&](int blocks)
        {
            double energy = 0;
            for (int b = 0; b < blocks; ++b)
            {
                require(processor->process(data) == kResultOk, "audio process failed");
                for (int s = 0; s < 256; ++s)
                {
                    require(std::isfinite(left[s]) && std::isfinite(right[s]), "nonfinite audio");
                    energy += left[s] * left[s] + right[s] * right[s];
                }
                events.events.clear(); changes.count = 0;
            }
            return energy;
        };
        render(1800); // Firmware startup, including the Sound Canvas intro.
        const bool detached = inputMode == "detached";
        if (detached) { componentConnection->disconnect(controllerConnection); controllerConnection->disconnect(componentConnection); }
        if (argc >= 3)
        {
            const auto markFixture = [](const char* marker)
            {
                if (const auto* path = std::getenv("EMU88_TEST_TRACE_MARKERS"))
                    if (auto* file = std::fopen(path, "a")) { std::fprintf(file, "\n%s\n", marker); std::fclose(file); }
            };
            markFixture("TEST_FIXTURE_BEGIN");
            // Explicit test adapter: the supplied legacy VSTHost source has no VST3 implementation.
            // Exercise reception of its byte-verified PC/SysEx output, not realtime song playback.
            std::ifstream input(argv[2]); require(bool(input), "cannot open legacy fixture");
            int port = 0, pcCount = 0, sysexCount = 0; std::string hex;
            while (input >> port >> hex)
            {
                require(port >= 0 && port < 4 && hex.size() >= 4 && hex.size() % 2 == 0, "invalid fixture");
                std::vector<uint8> bytes;
                for (size_t i = 0; i < hex.size(); i += 2)
                    bytes.push_back(static_cast<uint8>(std::stoi(hex.substr(i, 2), nullptr, 16)));
                if ((bytes[0] & 0xf0) == 0xc0)
                {
                    if (inputMode != "raw")
                    {
                    ParamID id;
                    id = inputMode == "standard" || detached ? standardPC[port * 16 + (bytes[0] & 15)]
                                                            : midiIDs[port * 16 + (bytes[0] & 15)][kCtrlProgramChange];
                    int32 index; auto* q = changes.addParameterData(id, index);
                    require(q != nullptr, "fixture queue full"); q->addPoint(0, bytes[1] / 127.0, index);
                    if (inputMode == "multipoint") q->addPoint(128, ((bytes[1] + 1) % 128) / 127.0, index);
                    }
                    if (inputMode == "raw" || inputMode == "both")
                    {
                        if (pcCount % 2 == 0) bytes.push_back(0); // VSTHost's padded 3-byte encoding, plus 2-byte coverage
                        Event e{}; e.busIndex = port; e.type = Event::kDataEvent;
                        e.data.type = DataEvent::kMidiSysEx; e.data.size = static_cast<uint32>(bytes.size()); e.data.bytes = bytes.data();
                        events.events.push_back(e);
                    }
                    ++pcCount;
                }
                else if ((bytes[0] & 0xf0) == 0xb0 || (bytes[0] & 0xf0) == 0xe0)
                {
                    const bool bend = (bytes[0] & 0xf0) == 0xe0;
                    auto id = midiIDs[port * 16 + (bytes[0] & 15)][bend ? kPitchBend : bytes[1]];
                    int32 index; auto* q = changes.addParameterData(id, index); require(q != nullptr, "control queue full");
                    q->addPoint(0, bend ? (bytes[1] + 128 * bytes[2]) / 16383.0 : bytes[2] / 127.0, index);
                }
                else
                {
                    require(bytes.front() == 0xf0 && bytes.back() == 0xf7, "fixture sysex framing invalid");
                    Event e{}; e.busIndex = port; e.type = Event::kDataEvent;
                    e.data.type = DataEvent::kMidiSysEx; e.data.size = static_cast<uint32>(bytes.size()); e.data.bytes = bytes.data();
                    events.events.push_back(e); ++sysexCount;
                }
                render(1); // bytes remain alive until process() returns.
            }
            require(pcCount > 0, "fixture lacks PC");
            markFixture("TEST_FIXTURE_END");
            std::cout << "SUBMITTED legacy fixture through TEST VST3 ADAPTER: PC=" << pcCount << " SysEx=" << sysexCount << std::endl;
        }
        else
        for (int port = 0; port < 4; ++port)
        {
            for (int p = 0; p < 4; ++p)
            {
                changes.cc(mapping, p, 120, 0); // all sound off
                changes.cc(mapping, p, 7, p == port ? 1.0 : 0.0);
                changes.cc(mapping, p, 91, 0); changes.cc(mapping, p, 93, 0);
            }
            render(350);
            events.note((port + 1) % 4, 60, true);
            const auto muted = render(100);
            events.note((port + 1) % 4, 60, false);
            render(100);
            events.note(port, 60 + port, true);
            const auto sounding = render(100);
            events.note(port, 60 + port, false);
            std::cout << "Port " << char('A' + port) << ": energy=" << sounding << ", other muted port=" << muted << std::endl;
            require(sounding > 0.001, "port produced no audio (check SC-8850 ROM/config)");
            require(muted < sounding * 0.01 + 1e-8, "MIDI ports or volume controllers are not isolated");
            render(350);
        }
        processor->setProcessing(false); component->setActive(false);
        if (!detached) { componentConnection->disconnect(controllerConnection); controllerConnection->disconnect(componentConnection); }
        units->release(); mapping->release(); componentConnection->release(); controllerConnection->release(); processor->release();
        controller->terminate(); controller->release(); component->terminate(); component->release(); factory->release();
        if (exit) exit();
        std::cout << (argc == 2 ? "PASS: four 16-channel buses, 8384 distinct CC/PC mappings, per-port audio and CC isolation.\n"
                              : "PASS: fixture submitted; inspect diagnostic DELIVER log to verify actual reception.\n");
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
}
