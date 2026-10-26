# Replace complete unit/program metadata methods in the first two classes only.
# Their bodies contain nested braces; scan braces instead of relying on a regex.
function(emu88_program_method signature replacement)
    set(source "${code}")
    set(output "")
    set(replacements 0)
    while(TRUE)
        string(FIND "${source}" "${signature}" start)
        if(start EQUAL -1)
            break()
        endif()
        string(SUBSTRING "${source}" 0 ${start} prefix)
        string(SUBSTRING "${source}" ${start} -1 remainder)
        string(FIND "${remainder}" "{" opening)
        math(EXPR cursor "${opening} + 1")
        set(depth 1)
        while(depth GREATER 0)
            string(SUBSTRING "${remainder}" ${cursor} 1 character)
            if(character STREQUAL "{")
                math(EXPR depth "${depth} + 1")
            elseif(character STREQUAL "}")
                math(EXPR depth "${depth} - 1")
            endif()
            math(EXPR cursor "${cursor} + 1")
        endwhile()
        string(APPEND output "${prefix}${replacement}")
        math(EXPR replacements "${replacements} + 1")
        string(SUBSTRING "${remainder}" ${cursor} -1 source)
    endwhile()
    if(replacements EQUAL 0)
        message(FATAL_ERROR "88emu VST3 program adapter: missing method ${signature}")
    endif()
    string(APPEND output "${source}")
    set(code "${output}" PARENT_SCOPE)
endfunction()
emu88_replace("#include \"MidiTrace.h\"" "#include \"MidiTrace.h\"\n#include \"Vst3MidiProgramInfo.h\"")
emu88_program_method("Steinberg::int32 PLUGIN_API getUnitCount() override" [=[Steinberg::int32 PLUGIN_API getUnitCount() override { return 65; }]=])
emu88_program_method("tresult PLUGIN_API getUnitInfo (Steinberg::int32 unitIndex, Vst::UnitInfo& info) override" [=[tresult PLUGIN_API getUnitInfo (Steinberg::int32 unitIndex, Vst::UnitInfo& info) override
    { return emu88Player::vst3Midi::unitInfo(unitIndex, info); }]=])
emu88_program_method("Steinberg::int32 PLUGIN_API getProgramListCount() override" [=[Steinberg::int32 PLUGIN_API getProgramListCount() override { return 64; }]=])
emu88_program_method("tresult PLUGIN_API getProgramListInfo (Steinberg::int32 listIndex, Vst::ProgramListInfo& info) override" [=[tresult PLUGIN_API getProgramListInfo (Steinberg::int32 listIndex, Vst::ProgramListInfo& info) override
    { return emu88Player::vst3Midi::listInfo(listIndex, info); }]=])
emu88_program_method("tresult PLUGIN_API getProgramName (Vst::ProgramListID listId, Steinberg::int32 programIndex, Vst::String128 name) override" [=[tresult PLUGIN_API getProgramName (Vst::ProgramListID listId, Steinberg::int32 programIndex, Vst::String128 name) override
    { return emu88Player::vst3Midi::programName(listId, programIndex, name); }]=])
emu88_program_method("tresult PLUGIN_API getUnitByBus (Vst::MediaType," [=[tresult PLUGIN_API getUnitByBus (Vst::MediaType type, Vst::BusDirection direction, Steinberg::int32 bus, Steinberg::int32 channel, Vst::UnitID& unitId) override
    { return emu88Player::vst3Midi::unitByBus(type, direction, bus, channel, unitId); }]=])
emu88_program_method("tresult PLUGIN_API getUnitByBus (Vst::MediaType type, Vst::BusDirection dir, Steinberg::int32 busIndex," [=[tresult PLUGIN_API getUnitByBus (Vst::MediaType type, Vst::BusDirection direction, Steinberg::int32 bus, Steinberg::int32 channel, Vst::UnitID& unitId) override
    { return emu88Player::vst3Midi::unitByBus(type, direction, bus, channel, unitId); }]=])
emu88_replace([=[parameters.addParameter (new Vst::Parameter (toString ("MIDI PC " + String (c)),
                                     static_cast<Vst::ParamID> (p) + parameterToMidiControllerOffset, nullptr, 0, 0,
                                     0, Vst::kRootUnitId));]=] [=[parameters.addParameter (new Vst::RangeParameter (toString ("MIDI PC " + String (c)),
                                     static_cast<Vst::ParamID> (p) + parameterToMidiControllerOffset, nullptr, 0, 127, 0, 127,
                                     Vst::ParameterInfo::kIsProgramChange | Vst::ParameterInfo::kCanAutomate, c + 1));]=])
# Decode stable IDs in the processing component, independent of a direct pointer
# to the edit controller (which may live behind a host proxy/bridge).
emu88_replace("juceVST3EditController != nullptr && juceVST3EditController->isMidiControllerParamID (vstParamID)"
    "vstParamID >= emu88Player::vst3Midi::parameterBase && vstParamID < emu88Player::vst3Midi::programBase + 64")
emu88_replace("juceVST3EditController->getMidiControllerForParameter (id, channel, ctrlNumber)"
    "emu88Player::vst3Midi::decode(id, channel, ctrlNumber)")
