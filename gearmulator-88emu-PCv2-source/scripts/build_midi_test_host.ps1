param([string]$BuildDirectory = 'temp/midiTestHost')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$previousCompilerOptions = $env:_CL_
Push-Location $projectRoot
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Visual Studio C++ Build Tools are required.' }
    $cmake = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    $version = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    $generator = if (([version]$version).Major -ge 18) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
    # Override JUCE's /Zi so the compiler does not require a matching MSPDB server.
    $env:_CL_ = "$previousCompilerOptions /Z7"
    & $cmake -S source/framework/tools/midiTestHost -B $BuildDirectory -G $generator -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    $msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
    & $msbuild "$BuildDirectory/MidiTestHost_rc_lib.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m:1 /v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Resource build failed.' }
    & $msbuild "$BuildDirectory/MidiTestHost.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m:1 /v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'MIDI Test Host build failed.' }
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    & $ctest --test-dir $BuildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'MIDI Test Host tests failed.' }
    Write-Host "$BuildDirectory/MidiTestHost_artefacts/Release/MIDI Test Host.exe"
}
finally { $env:_CL_ = $previousCompilerOptions; Pop-Location }
