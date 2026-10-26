// Harness scaffolding only. The three tested functions are extracted verbatim
// from vsthost-main/SmpVSTHost.cpp at build time (LGPL, Hermann Seib).
#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <map>
#include <set>
#include <algorithm>
enum { kVstMidiType = 1, kVstSysExType = 6, kVstMidiEventIsRealtime = 1 };
struct VstEvent { long type, byteSize, deltaFrames, flags; char data[16]; };
struct VstMidiEvent { long type, byteSize, deltaFrames, flags, noteLength, noteOffset; char midiData[4], detune, noteOffVelocity, reserved1, reserved2; };
struct VstMidiSysexEvent { long type, byteSize, deltaFrames, flags, dumpBytes; intptr_t resvd1; char* sysexDump; intptr_t resvd2; };
struct VstEvents { long numEvents; intptr_t reserved; VstEvent* events[2]; };
struct CMidiMsg {
    std::vector<char> bytes; DWORD stamp = 0;
    BYTE operator[](int n) const { return static_cast<BYTE>(bytes[n]); }
    DWORD GetStamp() const { return stamp; }
    int Length() const { return static_cast<int>(bytes.size()); }
    operator char*() { return bytes.data(); }
    operator DWORD() const { DWORD v = 0; memcpy(&v, bytes.data(), (std::min)(bytes.size(), size_t(3))); return v; }
};
static const int nMaxMidi = 2000, nMaxSysEx = 100;
struct CSmpVSTHost {
    CRITICAL_SECTION cs;
    DWORD midiStorage[nMaxMidi * 2]{}; DWORD* pMidis = midiStorage;
    CMidiMsg sysexStorage[nMaxSysEx]; CMidiMsg* pSysEx = sysexStorage;
    int nMidis = 0, nSysEx = 0;
    CSmpVSTHost() { InitializeCriticalSection(&cs); }
    ~CSmpVSTHost() { DeleteCriticalSection(&cs); }
    void OnMidiIn(CMidiMsg&);
    BYTE* CreateMIDIEvents(int, DWORD, int, int);
    BYTE* CreateMIDISubset(void*, unsigned short);
};
#include "original_midi_functions.h"
static void check(bool b, const char* why) { if (!b) throw std::runtime_error(why); }
static std::string hex(const char* p, int n) {
    static const char digits[] = "0123456789abcdef"; std::string s;
    for (int i = 0; i < n; ++i) { auto b = static_cast<unsigned char>(p[i]); s += digits[b >> 4]; s += digits[b & 15]; }
    return s;
}
int main(int argc, char** argv) {
 try {
    static_assert(sizeof(void*) == 4, "Run the source test as x86, matching legacy host");
    check(argc == 3, "usage: source_test input.txt accepted.txt");
    std::ifstream in(argv[1]); std::ofstream out(argv[2]); check(bool(in) && bool(out), "fixture IO failed");
    struct Input { DWORD stamp; int port; std::string bytes; };
    std::map<std::pair<DWORD, int>, std::vector<Input>> blocks;
    Input item;
    while (in >> item.stamp >> item.port >> item.bytes) blocks[{item.stamp / 5, item.port}].push_back(item);
    int pcs = 0, sysex = 0, all = 0;
    for (const auto& block : blocks) {
        CSmpVSTHost host; std::multiset<std::string> expected;
        for (const auto& e : block.second) {
            CMidiMsg msg; msg.stamp = e.stamp;
            for (size_t i = 0; i < e.bytes.size(); i += 2) msg.bytes.push_back(static_cast<char>(std::stoi(e.bytes.substr(i, 2), nullptr, 16)));
            host.OnMidiIn(msg); expected.insert(e.bytes);
        }
        auto* packed = host.CreateMIDIEvents(256, block.first.first * 5, host.nMidis, host.nSysEx);
        auto* subset = host.CreateMIDISubset(packed, 0xffff);
        check(subset != nullptr, "all-channel subset lost block");
        auto* events = reinterpret_cast<VstEvents*>(subset);
        std::multiset<std::string> actual;
        for (int i = 0; i < events->numEvents; ++i) {
            auto* e = events->events[i]; std::string bytes;
            if (e->type == kVstSysExType) { auto* s = reinterpret_cast<VstMidiSysexEvent*>(e); bytes = hex(s->sysexDump, s->dumpBytes); ++sysex; }
            else { auto* m = reinterpret_cast<VstMidiEvent*>(e); auto status = m->midiData[0] & 0xf0; bytes = hex(m->midiData, status == 0xc0 || status == 0xd0 ? 2 : 3); if (status == 0xc0) ++pcs; }
            actual.insert(bytes); ++all;
            if (e->type == kVstSysExType || bytes.substr(0, 1) == "c") out << block.first.second << ' ' << bytes << '\n';
        }
        check(expected == actual, "source path changed/dropped MIDI bytes");
        auto* zero = host.CreateMIDISubset(packed, 0);
        check((zero ? reinterpret_cast<VstEvents*>(zero)->numEvents : 0) == host.nSysEx, "channel mask must retain only SysEx");
        delete[] zero; delete[] subset; delete[] packed;
    }
    CSmpVSTHost host;
    CMidiMsg pc, sx; pc.bytes = {char(0xc0), 28}; pc.stamp = 1010;
    sx.bytes = {char(0xf0), 0x7e, 0x7f, 9, 1, char(0xf7)}; sx.stamp = 1010;
    host.OnMidiIn(pc); host.OnMidiIn(sx);
    auto* raw = host.CreateMIDIEvents(1024, 1000, host.nMidis, host.nSysEx);
    auto* events = reinterpret_cast<VstEvents*>(raw);
    check(events->events[0]->deltaFrames == 440 && events->events[1]->deltaFrames == 10, "legacy timing behavior changed");
    std::cout << "PASS x86 original source: messages=" << all << " PC=" << pcs << " SysEx=" << sysex << "; exact bytes and zero-channel mask verified\n";
    std::cout << "OBSERVED timing defect at +10ms: PC=440 samples; SysEx=10 samples (441 expected at 44100 Hz)\n";
    delete[] raw; return 0;
 } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
