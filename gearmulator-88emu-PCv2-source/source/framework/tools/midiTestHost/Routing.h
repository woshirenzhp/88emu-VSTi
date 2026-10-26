#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <map>
#include <tuple>

namespace midihost
{
struct Route
{
    int track = 0, sourcePort = 0, sourceChannel = 0;
    juce::String name;
    int destination = 0; // Plugin bus >= 0; hardware device = -1 - index.
    int channel = 0;     // 0 keeps the original channel.
    bool muted = false;
};
struct Event { double seconds; int route; juce::MidiMessage message; };
struct Song
{
    std::vector<Route> routes;
    std::vector<Event> events;
    std::vector<std::pair<double, double>> tempos;
    double duration = 0;

    bool read(juce::InputStream& stream, juce::String& error)
    {
        juce::MidiFile file;
        int type = 0;
        if (!file.readFrom(stream, true, &type)) { error = "Invalid MIDI file"; return false; }
        if (type == 2) { error = "SMF type 2 contains independent patterns; export as type 0 or 1."; return false; }
        file.convertTimestampTicksToSeconds();
        routes.clear(); events.clear(); tempos.clear(); duration = 0;
        for (int t = 0; t < file.getNumTracks(); ++t)
        {
            const auto& track = *file.getTrack(t);
            juce::String title = "Track " + juce::String(t + 1);
            for (int i = 0; i < track.getNumEvents(); ++i)
                if (track.getEventPointer(i)->message.isTrackNameEvent())
                    title += " / " + track.getEventPointer(i)->message.getTextFromTextMetaEvent();
            int port = 0;
            std::map<std::pair<int, int>, int> indices;
            for (int i = 0; i < track.getNumEvents(); ++i)
            {
                auto msg = track.getEventPointer(i)->message;
                duration = std::max(duration, msg.getTimeStamp());
                if (msg.isTempoMetaEvent()) tempos.emplace_back(msg.getTimeStamp(), 60.0 / msg.getTempoSecondsPerQuarterNote());
                if (msg.isMetaEvent())
                {
                    if (msg.getMetaEventType() == 0x21 && msg.getMetaEventLength() == 1)
                        port = msg.getMetaEventData()[0];
                    continue;
                }
                const auto key = std::make_pair(port, msg.getChannel());
                auto found = indices.find(key);
                if (found == indices.end())
                {
                    const int index = (int) routes.size();
                    indices[key] = index;
                    routes.push_back({t, port, msg.getChannel(), title, port, 0, false});
                    found = indices.find(key);
                }
                events.push_back({msg.getTimeStamp(), found->second, msg});
            }
        }
        std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.seconds < b.seconds; });
        std::stable_sort(tempos.begin(), tempos.end());
        if (events.empty()) { error = "MIDI file contains no playable events"; return false; }
        return true;
    }

    double bpmAt(double seconds) const
    {
        double bpm = 120;
        for (const auto& tempo : tempos) { if (tempo.first > seconds) break; bpm = tempo.second; }
        return bpm;
    }
};

inline juce::MidiMessage routedMessage(const juce::MidiMessage& original, const Route& route)
{
    auto copy = original;
    if (route.channel != 0 && copy.getChannel() != 0) copy.setChannel(route.channel);
    return copy;
}

// Half-open sample blocks ensure boundary events are delivered exactly once.
template<class Sink>
void dispatch(const Song& song, size_t& cursor, double start, int samples, double sampleRate, Sink&& sink)
{
    const double end = start + samples / sampleRate;
    while (cursor < song.events.size() && song.events[cursor].seconds < end)
    {
        const auto& event = song.events[cursor++];
        const auto& route = song.routes[(size_t) event.route];
        if (!route.muted)
            sink(route.destination, routedMessage(event.message, route),
                 juce::jlimit(0, samples - 1, (int) std::llround((event.seconds - start) * sampleRate)));
    }
}
}
