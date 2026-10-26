#include "Routing.h"
#include <atomic>
#include <fstream>

using namespace juce;
using namespace midihost;

class Engine final : public AudioIODeviceCallback, public AudioPlayHead
{
public:
    AudioDeviceManager devices;
    AudioPluginFormatManager formats;
    CriticalSection mutex;
    std::unique_ptr<AudioPluginInstance> plugin;
    Song song;
    String pluginPath, midiPath;
    Array<MidiDeviceInfo> midiDevices;
    std::vector<std::unique_ptr<MidiOutput>> hardware;
    std::vector<MidiBuffer> buses, hardwareBuffers;
    std::atomic<double> position {0};
    std::atomic<bool> playing {false};
    std::atomic<float> peak {0};
    double rate = 44100;
    int block = 512;

    Engine()
    {
        formats.addDefaultFormats();
        midiDevices = MidiOutput::getAvailableDevices();
        hardware.resize((size_t) midiDevices.size());
        hardwareBuffers.resize(hardware.size());
    }
    ~Engine() override
    {
        devices.removeAudioCallback(this);
        const ScopedLock lock(mutex);
        sendHardwarePanic();
        if (plugin) { plugin->setPlayHead(nullptr); plugin->releaseResources(); }
    }

    String startAudio()
    {
        // JUCE_ASIO=0 excludes ASIO entirely, including its SDK dependency.
        auto error = devices.initialise(0, 2, nullptr, true);
        devices.addAudioCallback(this);
        return error;
    }

    String loadPlugin(const File& file, int type = 0)
    {
        OwnedArray<PluginDescription> descriptions;
        for (int i = 0; i < formats.getNumFormats(); ++i)
            formats.getFormat(i)->findAllTypesForFile(descriptions, file.getFullPathName());
        if (descriptions.isEmpty()) return "No compatible 64-bit VST3 found in this file/bundle.";
        String error;
        auto instance = formats.createPluginInstance(*descriptions[jlimit(0, descriptions.size()-1, type)], rate, block, error);
        if (!instance) return error;
        instance->enableAllBuses();
        if (instance->getTotalNumOutputChannels() == 0) return "Plugin has no audio output.";
        devices.removeAudioCallback(this);
        {
            const ScopedLock lock(mutex);
            playing = false; position = 0; cursor = 0;
            sendHardwarePanic();
            if (plugin) { plugin->setPlayHead(nullptr); plugin->releaseResources(); }
            plugin = std::move(instance);
            pluginPath = file.getFullPathName();
            buses.clear();
            buses.resize((size_t) plugin->getMidiInputBusCount());
            heldNotes.clear(); heldNotes.resize(buses.size());
            for (auto& bus : buses) bus.ensureSize(65536);
            plugin->setMidiInputBusBuffers(buses.data(), (int) buses.size());
            plugin->setPlayHead(this);
            prepare();
            panicPending = true;
        }
        devices.addAudioCallback(this);
        return {};
    }

    String loadMidi(const File& file)
    {
        auto input = file.createInputStream();
        if (!input) return "Cannot open MIDI file.";
        Song next;
        String error;
        if (!next.read(*input, error)) return error;
        const ScopedLock lock(mutex);
        playing = false; position = 0; cursor = 0; panicPending = true;
        sendHardwarePanic();
        song = std::move(next);
        midiPath = file.getFullPathName();
        return {};
    }

    String openHardware(int index)
    {
        if (!isPositiveAndBelow(index, (int) hardware.size())) return "Invalid MIDI output.";
        const ScopedLock lock(mutex);
        if (!hardware[(size_t) index])
        {
            auto output = MidiOutput::openDevice(midiDevices[index].identifier);
            if (!output) return "Cannot open MIDI output: " + midiDevices[index].name;
            output->startBackgroundThread();
            hardware[(size_t) index] = std::move(output);
        }
        return {};
    }

    void transport(int action)
    {
        const ScopedLock lock(mutex);
        if (action == 0) // Play/resume
        {
            if (position >= song.duration) { position = 0; cursor = 0; }
            playing = !song.events.empty();
        }
        else
        {
            playing = false;
            panicPending = true;
            sendHardwarePanic();
            if (action == 2) { position = 0; cursor = 0; }
        }
    }

    void routeChanged()
    {
        // Stop before rerouting so held notes cannot be stranded at the old destination.
        transport(2);
    }

    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSeconds(position.load());
        info.setTimeInSamples((int64) std::llround(position.load() * rate));
        info.setIsPlaying(playing.load());
        info.setBpm(song.bpmAt(position.load()));
        return info;
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override
    {
        const ScopedLock lock(mutex);
        rate = device->getCurrentSampleRate(); block = device->getCurrentBufferSizeSamples();
        if (plugin) plugin->releaseResources();
        prepare();
    }
    void audioDeviceStopped() override
    {
        const ScopedLock lock(mutex);
        playing = false;
        sendHardwarePanic();
        if (plugin) plugin->releaseResources();
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs,
                                         int outputChannels, int samples, const AudioIODeviceCallbackContext&) override
    {
        const ScopedLock lock(mutex);
        for (int ch = 0; ch < outputChannels; ++ch)
            if (outputs[ch]) FloatVectorOperations::clear(outputs[ch], samples);
        float level = 0;
        for (int offset = 0; offset < samples; offset += block)
        {
            const int count = jmin(block, samples - offset);
            render(count);
            // Main output bus only; auxiliary outputs are processed but not summed twice.
            const int mainChannels = plugin ? plugin->getMainBusNumOutputChannels() : 0;
            for (int ch = 0; ch < outputChannels; ++ch)
                if (outputs[ch] && mainChannels > 0)
                {
                    const auto* src = audio.getReadPointer(jmin(ch, mainChannels - 1));
                    for (int i = 0; i < count; ++i)
                    {
                        const float v = std::isfinite(src[i]) ? jlimit(-1.0f, 1.0f, src[i]) : 0.0f;
                        outputs[ch][offset + i] = v;
                        level = jmax(level, std::abs(v));
                    }
                }
        }
        peak = level;
    }

    // Also used by the offline regression smoke test.
    void render(int count)
    {
        for (auto& b : buses) b.clear();
        for (auto& b : hardwareBuffers) b.clear();
        if (panicPending)
        {
            for (size_t bus = 0; bus < buses.size(); ++bus)
                for (int ch = 1; ch <= 16; ++ch)
                {
                    auto& b = buses[bus];
                    for (int note = 0; note < 128; ++note)
                        if (heldNotes[bus][(size_t)ch-1][(size_t)note])
                        {
                            b.addEvent(MidiMessage::noteOff(ch, note), 0);
                            heldNotes[bus][(size_t)ch-1][(size_t)note] = false;
                        }
                    b.addEvent(MidiMessage::controllerEvent(ch, 64, 0), 0);
                    b.addEvent(MidiMessage::allNotesOff(ch), 0);
                    b.addEvent(MidiMessage::allSoundOff(ch), 0);
                }
            panicPending = false;
        }
        if (playing)
        {
            const double start = position;
            dispatch(song, cursor, start, count, rate, [this](int dest, const MidiMessage& msg, int offset)
            {
                if (dest >= 0 && dest < (int) buses.size())
                {
                    buses[(size_t) dest].addEvent(msg, offset);
                    if (msg.isNoteOnOrOff()) heldNotes[(size_t)dest][(size_t)msg.getChannel()-1][(size_t)msg.getNoteNumber()] = msg.isNoteOn();
                }
                if (dest < 0 && -1-dest < (int) hardwareBuffers.size()) hardwareBuffers[(size_t) (-1-dest)].addEvent(msg, offset);
            });
            position = start + count / rate;
            if (position >= song.duration + 0.1) { playing = false; panicPending = true; }
        }
        for (size_t i = 0; i < hardware.size(); ++i)
            if (hardware[i] && !hardwareBuffers[i].isEmpty())
                hardware[i]->sendBlockOfMessages(hardwareBuffers[i], Time::getMillisecondCounterHiRes() + 1.0, rate);
        audio.setSize(audio.getNumChannels(), count, false, false, true);
        audio.clear();
        MidiBuffer ignored;
        if (plugin) plugin->processBlock(audio, ignored);
    }
    float renderedPeak() const { return audio.getMagnitude(0, audio.getNumSamples()); }

private:
    AudioBuffer<float> audio;
    std::vector<std::array<std::array<bool,128>,16>> heldNotes;
    size_t cursor = 0;
    bool panicPending = false;

    void prepare()
    {
        block = jmax(32, block);
        audio.setSize(plugin ? jmax(2, jmax(plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels())) : 2, block);
        if (!plugin) return;
        plugin->setRateAndBufferSizeDetails(rate, block);
        plugin->prepareToPlay(rate, block);
        panicPending = true;
    }
    void sendHardwarePanic()
    {
        for (auto& device : hardware)
            if (device)
            {
                device->clearAllPendingMessages();
                for (int ch = 1; ch <= 16; ++ch)
                {
                    device->sendMessageNow(MidiMessage::controllerEvent(ch, 64, 0));
                    device->sendMessageNow(MidiMessage::allNotesOff(ch));
                    device->sendMessageNow(MidiMessage::allSoundOff(ch));
                }
            }
    }
};

class EditorWindow final : public DocumentWindow
{
public:
    explicit EditorWindow(AudioPluginInstance& plugin)
        : DocumentWindow(plugin.getName(), Colours::darkgrey, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        auto* editor = plugin.createEditorIfNeeded();
        if (!editor) editor = new GenericAudioProcessorEditor(plugin);
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};

class MainComponent final : public Component, private Timer, public FileDragAndDropTarget
{
public:
    Engine engine;
    MainComponent()
    {
        setSize(1100, 700);
        title.setText("MIDI TEST HOST   /   VST3 Instrument & Effect", dontSendNotification);
        title.setFont(Font(23.0f, Font::bold));
        addAndMakeVisible(title);
        addButton(loadPlugin, "Load VST3", [this] { choose(false); });
        addButton(loadMidi, "Load MIDI", [this] { choose(true); });
        addButton(editorButton, "Plugin editor", [this] { if (engine.plugin) { if (!editor) editor = std::make_unique<EditorWindow>(*engine.plugin); editor->setVisible(true); editor->toFront(true); } });
        addButton(audioButton, "Audio settings", [this] { showAudioSettings(); });
        addButton(play, "Play / Resume", [this] { engine.transport(0); });
        addButton(pause, "Pause", [this] { engine.transport(1); });
        addButton(stop, "Stop / Rewind", [this] { engine.transport(2); });
        addButton(panic, "Panic", [this] { engine.transport(1); });
        addButton(save, "Save routes", [this] { chooseRoutes(true); });
        addButton(restore, "Load routes", [this] { chooseRoutes(false); });
        for (auto* label : {&pluginInfo, &fileInfo, &status, &columns, &destinationColumn, &channelColumn, &muteColumn, &hint}) addAndMakeVisible(*label);
        pluginInfo.setText("Plugin: drop a .vst3 bundle here, or click Load VST3", dontSendNotification);
        fileInfo.setText("MIDI: no file loaded", dontSendNotification);
        columns.setText("SOURCE TRACK / PORT / CHANNEL", dontSendNotification);
        destinationColumn.setText("DESTINATION PORT", dontSendNotification);
        channelColumn.setText("CHANNEL", dontSendNotification);
        muteColumn.setText("MUTE", dontSendNotification);
        hint.setText("Port = VST3 event input bus or system MIDI output. Channel: Keep or 1-16. Route changes rewind playback. SysEx keeps its payload.", dontSendNotification);
        hint.setFont(Font(13.0f));
        viewport.setViewedComponent(&routePanel, false);
        addAndMakeVisible(viewport);
        const auto error = engine.startAudio();
        if (error.isNotEmpty()) alert(error);
        timerCallback();
        startTimerHz(10);
    }
    ~MainComponent() override { stopTimer(); editor.reset(); }

    void paint(Graphics& g) override { g.fillAll(Colour(0xff19212c)); }
    void resized() override
    {
        auto area = getLocalBounds().reduced(20);
        title.setBounds(area.removeFromTop(38)); area.removeFromTop(8);
        auto row = area.removeFromTop(34);
        for (auto* button : {&loadPlugin, &loadMidi, &editorButton, &audioButton}) button->setBounds(row.removeFromLeft(160).reduced(0, 2).withTrimmedRight(8));
        pluginInfo.setBounds(area.removeFromTop(28)); fileInfo.setBounds(area.removeFromTop(28));
        area.removeFromTop(10); row = area.removeFromTop(36);
        for (auto* button : {&play, &pause, &stop, &panic, &save, &restore}) button->setBounds(row.removeFromLeft(155).reduced(0, 2).withTrimmedRight(8));
        status.setBounds(area.removeFromTop(32)); hint.setBounds(area.removeFromBottom(36));
        auto header = area.removeFromTop(30);
        columns.setBounds(header.removeFromLeft(430));
        destinationColumn.setBounds(header.removeFromLeft(300));
        channelColumn.setBounds(header.removeFromLeft(160));
        muteColumn.setBounds(header.removeFromLeft(70));
        viewport.setBounds(area);
        routePanel.setSize(jmax(1000, viewport.getWidth()-18), jmax(viewport.getHeight(), (int) rows.size()*42));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto bounds = Rectangle<int>(0, (int)i*42, routePanel.getWidth(), 40);
            rows[i]->source.setBounds(bounds.removeFromLeft(430));
            rows[i]->destination.setBounds(bounds.removeFromLeft(300).reduced(3));
            rows[i]->channel.setBounds(bounds.removeFromLeft(160).reduced(3));
            rows[i]->mute.setBounds(bounds.removeFromLeft(70).reduced(3));
        }
    }
    bool isInterestedInFileDrag(const StringArray& files) override
    {
        for (const auto& file : files) if (File(file).hasFileExtension("mid;midi;vst3")) return true;
        return false;
    }
    void filesDropped(const StringArray& files, int, int) override
    {
        for (const auto& file : files) if (File(file).hasFileExtension("vst3")) open(File(file));
        for (const auto& file : files) if (File(file).hasFileExtension("mid;midi")) open(File(file));
    }
    void open(const File& file)
    {
        String error;
        if (file.hasFileExtension("vst3"))
        {
            editor.reset();
            engine.transport(2);
            error = engine.loadPlugin(file);
            if (error.isEmpty())
                pluginInfo.setText("Plugin: " + engine.plugin->getName() + "   |   " + String(engine.plugin->getMidiInputBusCount()) + " MIDI input ports", dontSendNotification);
        }
        else
        {
            error = engine.loadMidi(file);
            if (error.isEmpty()) fileInfo.setText("MIDI: " + file.getFileName() + "   |   " + String(engine.song.routes.size()) + " routes", dontSendNotification);
        }
        if (error.isNotEmpty()) alert(error);
        rebuildRows();
        timerCallback();
    }

private:
    Label title, pluginInfo, fileInfo, status, columns, destinationColumn, channelColumn, muteColumn, hint;
    TextButton loadPlugin, loadMidi, editorButton, audioButton, play, pause, stop, panic, save, restore;
    Viewport viewport;
    Component routePanel;
    struct Row { Label source; ComboBox destination, channel; ToggleButton mute; };
    std::vector<std::unique_ptr<Row>> rows;
    std::unique_ptr<EditorWindow> editor;
    std::unique_ptr<FileChooser> chooser;

    void addButton(TextButton& b, const String& text, std::function<void()> fn)
    { b.setButtonText(text); b.onClick = std::move(fn); addAndMakeVisible(b); }
    void alert(const String& text) { AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "MIDI Test Host", text); }
    void choose(bool midi)
    {
        chooser = std::make_unique<FileChooser>(midi ? "Open MIDI file" : "Open VST3 bundle", File(), midi ? "*.mid;*.midi" : "*.vst3");
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | (midi ? 0 : FileBrowserComponent::canSelectDirectories),
            [this](const FileChooser& c) { const auto f = c.getResult(); if (f.exists()) open(f); });
    }
    void showAudioSettings()
    {
        auto* selector = new AudioDeviceSelectorComponent(engine.devices, 0, 0, 0, 2, false, false, true, false);
        selector->setSize(600, 450);
        DialogWindow::LaunchOptions options;
        options.dialogTitle = "Audio output (WASAPI / DirectSound, no ASIO)";
        options.dialogBackgroundColour = Colour(0xff263343);
        options.content.setOwned(selector); options.useNativeTitleBar = true;
        options.resizable = false; options.launchAsync();
    }
    void rebuildRows()
    {
        rows.clear();
        for (size_t i = 0; i < engine.song.routes.size(); ++i)
        {
            const auto& route = engine.song.routes[i];
            auto row = std::make_unique<Row>();
            row->source.setText(route.name + " | P" + String(route.sourcePort + 1) + " / " + (route.sourceChannel ? "Ch " + String(route.sourceChannel) : "SysEx/System"), dontSendNotification);
            row->source.setTooltip(row->source.getText());
            for (int bus = 0; engine.plugin && bus < engine.plugin->getMidiInputBusCount(); ++bus)
                row->destination.addItem("Plugin P" + String(bus+1) + ": " + engine.plugin->getMidiInputBusName(bus), bus+1);
            for (int device = 0; device < engine.midiDevices.size(); ++device)
                row->destination.addItem("MIDI OUT: " + engine.midiDevices[device].name, 10000+device);
            int selected = route.destination >= 0 ? route.destination + 1 : 10000 + (-1-route.destination);
            row->destination.setSelectedId(selected, dontSendNotification);
            row->destination.setTextWhenNothingSelected("Unavailable port - select destination");
            row->channel.addItem("Keep channel", 1);
            for (int ch = 1; ch <= 16; ++ch) row->channel.addItem("Channel " + String(ch), ch+1);
            row->channel.setSelectedId(route.channel+1, dontSendNotification);
            row->mute.setToggleState(route.muted, dontSendNotification);
            auto* ptr = row.get();
            row->destination.onChange = [this, i, ptr]
            {
                const int id = ptr->destination.getSelectedId();
                if (id == 0) return;
                engine.routeChanged();
                if (id >= 10000)
                {
                    const auto error = engine.openHardware(id-10000);
                    if (error.isNotEmpty()) { alert(error); rebuildRows(); return; }
                }
                const ScopedLock lock(engine.mutex);
                engine.song.routes[i].destination = id >= 10000 ? -1-(id-10000) : id-1;
            };
            row->channel.onChange = [this, i, ptr] { engine.routeChanged(); const ScopedLock lock(engine.mutex); engine.song.routes[i].channel = ptr->channel.getSelectedId()-1; };
            row->mute.onClick = [this, i, ptr] { engine.routeChanged(); const ScopedLock lock(engine.mutex); engine.song.routes[i].muted = ptr->mute.getToggleState(); };
            routePanel.addAndMakeVisible(row->source); routePanel.addAndMakeVisible(row->destination);
            routePanel.addAndMakeVisible(row->channel); routePanel.addAndMakeVisible(row->mute);
            rows.push_back(std::move(row));
        }
        resized();
    }
    void chooseRoutes(bool saving)
    {
        chooser = std::make_unique<FileChooser>(saving ? "Save routing preset" : "Load routing preset", File(), "*.json");
        chooser->launchAsync((saving ? FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting : FileBrowserComponent::openMode) | FileBrowserComponent::canSelectFiles,
            [this, saving](const FileChooser& c)
            {
                auto file = c.getResult(); if (file == File()) return;
                if (saving)
                {
                    Array<var> data;
                    for (const auto& r : engine.song.routes)
                    {
                        auto* object = new DynamicObject();
                        object->setProperty("track", r.track); object->setProperty("port", r.sourcePort); object->setProperty("sourceChannel", r.sourceChannel);
                        object->setProperty("destination", r.destination); object->setProperty("channel", r.channel); object->setProperty("mute", r.muted);
                        if (r.destination < 0) object->setProperty("deviceId", engine.midiDevices[-1-r.destination].identifier);
                        data.add(var(object));
                    }
                    if (!file.withFileExtension("json").replaceWithText(JSON::toString(var(data)))) alert("Cannot save preset.");
                }
                else
                {
                    const auto data = JSON::parse(file);
                    if (!data.isArray()) { alert("Invalid routing preset."); return; }
                    engine.routeChanged();
                    for (const auto& v : *data.getArray())
                    {
                        if (!v.isObject()) continue;
                        int dest = (int) v["destination"];
                        if (dest < 0)
                        {
                            int device = -1;
                            for (int d = 0; d < engine.midiDevices.size(); ++d) if (engine.midiDevices[d].identifier == v["deviceId"].toString()) device = d;
                            if (device < 0) continue;
                            const auto error = engine.openHardware(device);
                            if (error.isNotEmpty()) { alert(error); continue; }
                            dest = -1-device;
                        }
                        const ScopedLock lock(engine.mutex);
                        for (auto& r : engine.song.routes)
                            if (r.track == (int)v["track"] && r.sourcePort == (int)v["port"] && r.sourceChannel == (int)v["sourceChannel"])
                            { r.destination = dest; r.channel = jlimit(0,16,(int)v["channel"]); r.muted = (bool)v["mute"]; }
                    }
                    rebuildRows();
                }
            });
    }
    void timerCallback() override
    {
        String device = "No audio device";
        if (auto* current = engine.devices.getCurrentAudioDevice())
            device = current->getTypeName() + " / " + current->getName() + "  " + String(current->getCurrentSampleRate(), 0) + " Hz / " + String(current->getCurrentBufferSizeSamples()) + " samples";
        status.setText(String(engine.playing ? "PLAYING  " : "STOPPED  ") + String(engine.position.load(), 2) + " / " + String(engine.song.duration, 2) + " s    |    " + device + "    |    Peak " + String(engine.peak.load(), 2), dontSendNotification);
    }
};

String testVst3Bridge();
static String runTests()
{
    auto require = [](bool condition, const char* message) { if (!condition) throw String(message); };
    try
    {
        Song song;
        song.routes = {{0,0,1,"A",0,3,false}, {1,1,1,"B",1,9,false}, {2,0,2,"Muted",0,0,true}};
        song.events = {{0,0,MidiMessage::noteOn(1,60,(uint8)100)}, {0,1,MidiMessage::noteOn(1,64,(uint8)100)},
                       {0,2,MidiMessage::noteOn(2,70,(uint8)100)}, {0.01,0,MidiMessage::noteOff(1,60)}};
        size_t cursor = 0;
        std::vector<std::tuple<int,int,int>> got;
        auto sink = [&](int bus, const MidiMessage& msg, int offset) { got.emplace_back(bus,msg.getChannel(),offset); };
        dispatch(song,cursor,0,480,48000,sink);
        require(got.size()==2 && got[0]==std::make_tuple(0,3,0) && got[1]==std::make_tuple(1,9,0), "bus/channel isolation or mute failed");
        dispatch(song,cursor,0.01,480,48000,sink);
        require(got.size()==3 && std::get<2>(got.back())==0, "block boundary delivery failed");
        const uint8 bytes[] = {0x41,0x10,0x42,0x12,0x40,0,0x7f,0,0x41};
        auto sysex = MidiMessage::createSysExMessage(bytes,sizeof(bytes));
        const auto routed = routedMessage(sysex,song.routes[0]);
        require(routed.getRawDataSize()==sysex.getRawDataSize() && std::memcmp(routed.getRawData(),sysex.getRawData(),(size_t)sysex.getRawDataSize())==0,"SysEx changed");
        MidiFile file; file.setTicksPerQuarterNote(480);
        MidiMessageSequence tempo, track;
        tempo.addEvent(MidiMessage::tempoMetaEvent(500000),0);
        tempo.addEvent(MidiMessage::tempoMetaEvent(1000000),480);
        const uint8 port[] = {0xff,0x21,0x01,0x03};
        track.addEvent(MidiMessage(port,4),0);
        track.addEvent(MidiMessage::noteOn(2,60,(uint8)90),480);
        track.addEvent(MidiMessage::noteOff(2,60),960);
        file.addTrack(tempo); file.addTrack(track);
        MemoryOutputStream out; require(file.writeTo(out),"SMF write failed");
        MemoryInputStream input(out.getData(),out.getDataSize(),false);
        String error; Song parsed;
        require(parsed.read(input,error),"SMF read failed");
        require(parsed.routes.size()==1 && parsed.routes[0].sourcePort==3 && parsed.routes[0].sourceChannel==2,"port/channel metadata failed");
        require(std::abs(parsed.events[0].seconds-0.5)<1e-6 && std::abs(parsed.events[1].seconds-1.5)<1e-6,"tempo map conversion failed");
        require(parsed.bpmAt(1)==60,"tempo reporting failed");
        const auto bridge = testVst3Bridge();
        require(bridge.isEmpty(), bridge.toRawUTF8());
        return "PASS: port/channel isolation, mute, block boundary, SysEx, SMF port metadata, tempo changes, native VST3 bus indices and per-bus controller mappings/offsets\n";
    }
    catch (const String& error) { return "FAIL: " + error + "\n"; }
}

class HostApplication final : public JUCEApplication
{
public:
    const String getApplicationName() override { return "MIDI Test Host"; }
    const String getApplicationVersion() override { return "1.0.0"; }
    void initialise(const String& commandLine) override
    {
       #if JUCE_WINDOWS
        LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName("Microsoft YaHei UI");
       #endif
        StringArray args; args.addTokens(commandLine,true);
        if (args.size() == 2 && args[0] == "--write-demo")
        {
            MidiFile midi; midi.setTicksPerQuarterNote(480);
            MidiMessageSequence tempo; tempo.addEvent(MidiMessage::tempoMetaEvent(500000),0); midi.addTrack(tempo);
            const int notes[] = {60,62,64,65,67,69,71,72};
            for (int bus = 0; bus < 4; ++bus)
            {
                MidiMessageSequence track;
                track.addEvent(MidiMessage::textMetaEvent(3,"Port " + String(bus+1) + " test"),0);
                const uint8 port[] = {0xff,0x21,1,(uint8)bus};
                track.addEvent(MidiMessage(port,4),0);
                track.addEvent(MidiMessage::programChange(bus+1,bus*8),0);
                for (int n = 0; n < 8; ++n)
                {
                    const int tick = bus*1920+n*240;
                    track.addEvent(MidiMessage::noteOn(bus+1,notes[n],(uint8)80),tick);
                    track.addEvent(MidiMessage::noteOff(bus+1,notes[n]),tick+180);
                }
                midi.addTrack(track);
            }
            auto output = File(args[1].unquoted()).createOutputStream();
            const bool ok = output && output->setPosition(0) && output->truncate().wasOk() && midi.writeTo(*output);
            setApplicationReturnValue(ok ? 0 : 1); quit(); return;
        }
        if (args.size() == 5 && args[0] == "--snapshot")
        {
            MainComponent component;
            component.open(File(args[1].unquoted()));
            component.open(File(args[2].unquoted()));
            const auto image = component.createComponentSnapshot(component.getLocalBounds(),true);
            auto output = File(args[3].unquoted()).createOutputStream();
            PNGImageFormat png;
            const bool ok = output && output->setPosition(0) && output->truncate().wasOk() && png.writeImageToStream(image,*output);
            String report = "Plugin ports: " + String(component.engine.plugin ? component.engine.plugin->getMidiInputBusCount() : 0) + "\nRoutes: " + String(component.engine.song.routes.size()) + "\n";
            if (auto* device = component.engine.devices.getCurrentAudioDevice())
                report += "Audio: " + device->getTypeName() + " / " + device->getName() + "\n";
            File(args[4].unquoted()).replaceWithText(report);
            setApplicationReturnValue(ok ? 0 : 1); quit(); return;
        }
        if (commandLine.contains("--self-test"))
        {
            const auto result = runTests();
            File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("self-test.txt").replaceWithText(result);
            setApplicationReturnValue(result.startsWith("PASS") ? 0 : 1); quit(); return;
        }
        if (args.size() >= 2 && args[0] == "--probe")
        {
            Engine engine;
            String report = engine.loadPlugin(File(args[1].unquoted()));
            bool ok = report.isEmpty();
            if (ok)
            {
                report = "Plugin: " + engine.plugin->getName() + "\nInput buses: " + String(engine.plugin->getMidiInputBusCount()) + "\n";
                for (int bus = 0; bus < engine.plugin->getMidiInputBusCount(); ++bus)
                {
                    report += String(bus+1) + ": " + engine.plugin->getMidiInputBusName(bus) + "\n";
                    engine.song.routes.push_back({bus,bus,1,"Probe",bus,0,false});
                    engine.song.events.push_back({0,bus,MidiMessage::controllerEvent(1,0,0)});
                    engine.song.events.push_back({0,bus,MidiMessage::programChange(1,0)});
                    engine.song.events.push_back({0.001,bus,MidiMessage::noteOn(1,60+bus,(uint8)90)});
                    engine.song.events.push_back({0.2,bus,MidiMessage::noteOff(1,60+bus)});
                }
                std::stable_sort(engine.song.events.begin(),engine.song.events.end(),[](const auto& a,const auto& b){ return a.seconds < b.seconds; });
                engine.song.duration = 0.25;
                engine.transport(0);
                float maximum = 0;
                for (int i = 0; i < 64; ++i) { engine.render(512); maximum = jmax(maximum,engine.renderedPeak()); }
                report += "PASS: 64 processing blocks, per-bus bank/program/note messages; peak=" + String(maximum,6) + "\n";
            }
            File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("plugin-probe.txt").replaceWithText(report);
            setApplicationReturnValue(ok ? 0 : 1); quit(); return;
        }
        window = std::make_unique<Window>();
        for (auto arg : args) { auto file = File(arg.unquoted()); if (file.exists()) window->content->open(file); }
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    class Window final : public DocumentWindow
    {
    public:
        MainComponent* content;
        Window() : DocumentWindow("MIDI Test Host", Colour(0xff19212c), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            content = new MainComponent(); setContentOwned(content,true);
            setResizable(true,false); setResizeLimits(1050,500,2000,1600);
            centreWithSize(getWidth(),getHeight()); setVisible(true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(HostApplication)
