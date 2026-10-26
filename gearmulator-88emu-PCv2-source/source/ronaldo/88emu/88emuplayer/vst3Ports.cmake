# Product-local adaptation: generate a wrapper in the build directory rather
# than modifying JUCE or changing MIDI behaviour for the other instruments.
set(wrapper "${JUCE_MODULES_DIR}/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
file(READ "${wrapper}" code)
macro(emu88_replace before after)
    string(FIND "${code}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "88emu VST3 adapter: incompatible JUCE wrapper, missing: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" code "${code}")
endmacro()
emu88_replace("#include <juce_audio_processors/format_types/juce_VST3Common.h>"
    "#include <juce_audio_processors/format_types/juce_VST3Common.h>\n#include \"HostMidiPorts.h\"")
emu88_replace("enum { numMIDIChannels = 16 };" "enum { numMIDIChannels = 64 };")
emu88_replace("#include \"HostMidiPorts.h\"" "#include \"HostMidiPorts.h\"\n#include \"MidiTrace.h\"")
emu88_replace("resultID = static_cast<Vst::ParamID> (numMidiCCParams + channel) + parameterToMidiControllerOffset;"
    "resultID = static_cast<Vst::ParamID> (numMidiCCParams + channel) + parameterToMidiControllerOffset;\n            emu88MidiTrace(\"PC_MAP bus=%d channel=%d id=%u\", busIndex, channel, resultID);")
emu88_replace("const auto vstParamID = paramQueue->getParameterId();" "const auto vstParamID = paramQueue->getParameterId();\n                emu88MidiTrace(\"PARAM id=%u points=%d\", vstParamID, paramQueue->getPointCount());")
emu88_replace("return queue->getPoint (index, result.offsetSamples, result.value) == kResultTrue"
    "const auto resultCode = queue->getPoint(index, result.offsetSamples, result.value);\n            emu88MidiTrace(\"PARAM_READ id=%u point=%d result=%d offset=%d normalized=%.17g\", queue->getParameterId(), index, int(resultCode), result.offsetSamples, result.value);\n            return resultCode == kResultTrue")
emu88_replace("addParameterChangeToMidiBuffer (change->offsetSamples, vstParamID, change->value);" [=[{
                                emu88MidiTrace("PARAM_POINT id=%u point=%d offset=%d normalized=%.17g", vstParamID, point, change->offsetSamples, change->value);
                                addParameterChangeToMidiBuffer (change->offsetSamples, vstParamID, change->value);
                            }]=])
emu88_replace("if (midiControllerNumber == Vst::kCtrlProgramChange)" [=[
if (busIndex < 0 || busIndex >= 4 || channel < 0 || channel >= 16
            || midiControllerNumber < 0 || midiControllerNumber > Vst::kCtrlProgramChange)
            return kResultFalse;
        channel = static_cast<Steinberg::int16> (channel + busIndex * 16);
        if (midiControllerNumber == Vst::kCtrlProgramChange)]=])
emu88_replace("channel = jlimit (1, 16, mc.channel + 1);" "channel = mc.channel + 1;")
emu88_replace("if (dir == Vst::kInput)\n                return 1;" "if (dir == Vst::kInput)\n                return 4;")
emu88_replace("if (dir == Vst::kInput && index == 0)" "if (dir == Vst::kInput && index >= 0 && index < 4)")
emu88_replace("toString128 (info.name, TRANS (\"MIDI Input\"));" "toString128 (info.name, String (\"MIDI Input \") + String::charToString ('A' + index));")
emu88_replace("if (index == 0 && dir == Vst::kInput)" "if (index >= 0 && index < 4 && dir == Vst::kInput)")
emu88_replace("isMidiInputBusEnabled = (state != 0);" "isMidiInputBusEnabled[index] = (state != 0);")
emu88_replace("std::atomic<bool> isMidiInputBusEnabled { true };"
    "std::atomic<bool> isMidiInputBusEnabled[4] { true, true, true, true };\n    emu88Player::HostMidiBuffers hostMidiInputs;")
emu88_replace("if (juceVST3EditController->getMidiControllerForParameter (id, channel, ctrlNumber))\n        {" [=[
if (juceVST3EditController->getMidiControllerForParameter (id, channel, ctrlNumber))
        {
            const int port = (channel - 1) / 16;
            if (port < 0 || port >= 4 || !isMidiInputBusEnabled[port])
                return;
            channel = (channel - 1) % 16 + 1;
            emu88MidiTrace("PARAM_DECODE id=%u port=%d channel=%d controller=%d offset=%d normalized=%.17g", id, port, channel, ctrlNumber, offsetSamples, value);
            auto& midiBuffer = hostMidiInputs[port];]=])
emu88_replace("midiBuffer.clear();\n\n        if (data.inputParameterChanges" "midiBuffer.clear();\n        for (auto& input : hostMidiInputs) input.clear();\n\n        if (data.inputParameterChanges")
emu88_replace("if (isMidiInputBusEnabled && data.inputEvents != nullptr)\n            MidiEventList::toMidiBuffer (midiBuffer, *data.inputEvents);" [=[
if (data.inputEvents != nullptr)
        {
            MidiEventList event;
            // Capture only parameter-origin PCs. Do not collapse repeated raw events.
            struct ParameterPC { int port, offset, status, value; };
            std::vector<ParameterPC> parameterPCs;
            for (int port = 0; port < 4; ++port)
                for (const auto metadata : hostMidiInputs[port])
                    if (metadata.numBytes == 2 && (metadata.data[0] & 0xf0) == 0xc0)
                        parameterPCs.push_back({port, metadata.samplePosition, metadata.data[0], metadata.data[1]});
            for (Steinberg::int32 i = 0; i < data.inputEvents->getEventCount(); ++i)
            {
                Vst::Event e{};
                if (data.inputEvents->getEvent(i, e) != kResultOk)
                    continue;
                emu88MidiTrace("INPUT_EVENT index=%d type=%d bus=%d offset=%d enabled=%d", i, e.type, e.busIndex,
                              e.sampleOffset, e.busIndex >= 0 && e.busIndex < 4 ? int(isMidiInputBusEnabled[e.busIndex]) : 0);
                if (e.busIndex < 0 || e.busIndex >= 4 || !isMidiInputBusEnabled[e.busIndex])
                    continue;
                event.clear();
#ifdef EMU88_MIDI_TRACE_FILE
                if (e.type == Vst::Event::kDataEvent)
                {
                    const auto count = static_cast<int>(jmin<Steinberg::uint32>(e.data.size, 256));
                    const auto hex = e.data.bytes != nullptr ? String::toHexString(e.data.bytes, count) : String("null");
                    emu88MidiTrace("RAW_DATA bus=%d subtype=%u size=%u bytes=%s", e.busIndex, e.data.type, e.data.size, hex.toRawUTF8());
                }
#endif
                // VSTHost sends Program Change as a SysEx DataEvent containing
                // Cn pp (or Cn pp 00). JUCE correctly rejects it as SysEx, so
                // recognise this narrow compatibility case before conversion.
                if (e.type == Vst::Event::kDataEvent && e.data.type == Vst::DataEvent::kMidiSysEx
                    && e.data.bytes != nullptr && (e.data.size == 2 || e.data.size == 3)
                    && (e.data.bytes[0] & 0xf0) == 0xc0 && e.data.bytes[1] < 0x80
                    && (e.data.size == 2 || e.data.bytes[2] == 0))
                {
                    const auto duplicate = std::find_if(parameterPCs.begin(), parameterPCs.end(), [&](const ParameterPC& pc)
                    {
                        return pc.port == e.busIndex && pc.offset == e.sampleOffset
                            && pc.status == e.data.bytes[0] && pc.value == e.data.bytes[1];
                    });
                    const bool isDuplicate = duplicate != parameterPCs.end();
                    if (!isDuplicate)
                        hostMidiInputs[e.busIndex].addEvent(e.data.bytes, 2, e.sampleOffset);
                    else
                        parameterPCs.erase(duplicate); // one-to-one matching, not global deduplication
                    emu88MidiTrace("RAW_PC bus=%d offset=%d status=%02x value=%u duplicate=%d", e.busIndex,
                                  e.sampleOffset, e.data.bytes[0], e.data.bytes[1], isDuplicate);
                    continue;
                }
                emu88MidiTrace("EVENT type=%d bus=%d offset=%d", e.type, e.busIndex, e.sampleOffset);
                event.addEvent(e);
                MidiEventList::toMidiBuffer(hostMidiInputs[e.busIndex], event);
            }
        }]=])
emu88_replace("if      (processSetup.symbolicSampleSize == Vst::kSample32) processAudio<float>  (data);" [=[
auto* ports = dynamic_cast<emu88Player::HostMidiPorts*> (pluginInstance);
            if (ports != nullptr) ports->setHostMidiInputs(&hostMidiInputs);
            if      (processSetup.symbolicSampleSize == Vst::kSample32) processAudio<float>  (data);]=])
emu88_replace("else jassertfalse;\n        }\n\n        if (auto* changes" "else jassertfalse;\n            if (ports != nullptr) ports->setHostMidiInputs(nullptr);\n        }\n\n        if (auto* changes")
include("${CMAKE_CURRENT_SOURCE_DIR}/vst3Programs.cmake")
emu88_replace("for (auto& input : hostMidiInputs) input.clear();" [=[for (auto& input : hostMidiInputs) input.clear();
        if ((data.inputEvents != nullptr && data.inputEvents->getEventCount() > 0)
            || (data.inputParameterChanges != nullptr && data.inputParameterChanges->getParameterCount() > 0))
            emu88MidiTrace("PROCESS_INPUT instance=%p samples=%d eventCount=%d parameterQueues=%d controllerLinked=%d",
                          static_cast<void*>(this), data.numSamples, data.inputEvents ? data.inputEvents->getEventCount() : 0,
                          data.inputParameterChanges ? data.inputParameterChanges->getParameterCount() : 0, juceVST3EditController != nullptr);
]=])
set(generated "${CMAKE_CURRENT_BINARY_DIR}/88emu_VST3.cpp")
file(WRITE "${generated}" "${code}")
get_target_property(wrapperSources 88emuplayer_VST3 SOURCES)
list(FILTER wrapperSources EXCLUDE REGEX "juce_audio_plugin_client_VST3\\.cpp$")
set_property(TARGET 88emuplayer_VST3 PROPERTY SOURCES "${wrapperSources};${generated}")
# JUCE also supplies its wrapper as an INTERFACE source. This directory-scoped
# property excludes that inherited copy only for targets in this product.
set_source_files_properties("${wrapper}" PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(88emuplayer_VST3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
