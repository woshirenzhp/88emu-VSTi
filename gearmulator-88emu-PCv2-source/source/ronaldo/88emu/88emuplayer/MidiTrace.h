#pragma once
// Opt-in diagnostic build only. File I/O is deliberately absent in normal builds.
#ifdef EMU88_MIDI_TRACE_FILE
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
inline void emu88MidiTrace(const char* format, ...)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    static FILE* file = [] {
        const char* overridePath = std::getenv("EMU88_MIDI_TRACE_PATH");
        std::filesystem::path path(overridePath && *overridePath ? overridePath : EMU88_MIDI_TRACE_FILE);
#ifdef _WIN32
        if(path.is_relative())
        {
            HMODULE module = nullptr;
            wchar_t modulePath[32768]{};
            if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(&emu88MidiTrace), &module)
                && GetModuleFileNameW(module, modulePath, 32768) != 0)
            {
                auto directory = std::filesystem::path(modulePath).parent_path();
                // Windows VST3 bundles contain Contents/x86_64-win/plugin.vst3.
                // Put the log beside the outer bundle, or beside a flat DLL.
                for(auto candidate = directory; !candidate.empty(); candidate = candidate.parent_path())
                {
                    if(candidate.extension() == L".vst3") { directory = candidate.parent_path(); break; }
                    if(candidate == candidate.parent_path()) break;
                }
                path = directory / path;
            }
        }
#endif
        std::error_code error;
        if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
#ifdef _WIN32
        auto* opened = _wfopen(path.c_str(), L"a");
#else
        auto* opened = std::fopen(path.c_str(), "a");
#endif
        if(opened) std::fprintf(opened, "\nTRACE_START build=%s %s path=%s\n", __DATE__, __TIME__, path.u8string().c_str());
        return opened;
    }();
    if(!file) return;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::fprintf(file, "time_ms=%lld ", static_cast<long long>(ms));
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
}
#else
inline void emu88MidiTrace(const char*, ...) {}
#endif
