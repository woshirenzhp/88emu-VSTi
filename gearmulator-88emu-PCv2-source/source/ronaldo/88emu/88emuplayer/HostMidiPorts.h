#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace emu88Player
{
    // The JUCE MidiBuffer API has no port field. The product's VST3 wrapper
    // preserves all four event buses here, including their sample offsets.
    using HostMidiBuffers = std::array<juce::MidiBuffer, 4>;
    struct HostMidiPorts
    {
        virtual ~HostMidiPorts() = default;
        virtual void setHostMidiInputs(const HostMidiBuffers* inputs) = 0;
    };
}
