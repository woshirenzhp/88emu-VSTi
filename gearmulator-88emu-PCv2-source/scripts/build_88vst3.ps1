param([string]$BuildDirectory = 'temp/cmake_88vst3', [switch]$BuildTests, [string]$MidiTraceFile = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Visual Studio C++ Build Tools are required.' }
    $cmake = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    $version = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    $is2026 = ([version]$version).Major -ge 18
    $generator = if ($is2026) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
    $toolset = if ($is2026) { 'v145' } else { 'v143' }
    $arguments = @('-S', '.', '-B', $BuildDirectory, '-G', $generator, '-A', 'x64', '-T', $toolset,
        '-Dgearmulator_BUILD_JUCEPLUGIN=ON', '-Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON',
        '-Dgearmulator_BUILD_JUCEPLUGIN_VST2=OFF', '-Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF',
        '-Dgearmulator_SYNTH_88EMU=ON', '-Dgearmulator_COMPONENT_DSPBRIDGE=OFF',
        '-Dgearmulator_BUILD_INSTALLER=OFF', '-DBUILD_TESTING=ON', "-DEMU88_MIDI_TRACE_FILE:STRING=$MidiTraceFile")
    foreach ($synth in @('OSIRUS','OSTIRUS','VAVRA','XENIA','NODALRED2X','JE8086')) {
        $arguments += "-Dgearmulator_SYNTH_$synth=OFF"
    }
    & $cmake @arguments
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed. See doc/88emu_vst3_dependencies.json for required dependency revisions.' }
    $targets = @('88emuplayer_VST3')
    if ($BuildTests) { $targets += '88emuVst3_test' }
    & $cmake --build $BuildDirectory --config Release --target @targets -j 4
    if ($LASTEXITCODE -ne 0) { throw 'VST3 build failed.' }
    Write-Host 'VST3 bundle: bin/plugins/Release/VST3/88emuPlayer.vst3'
}
finally { Pop-Location }
