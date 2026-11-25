[CmdletBinding()]
param(
    [string]$Zig = 'zig',
    [switch]$RunTests,
    [string]$GameFolder,
    [string]$Symtest
)
$ErrorActionPreference = 'Stop'
if ([bool]$GameFolder -ne [bool]$Symtest) { throw 'Supply both -GameFolder and -Symtest for loader validation.' }
$project = $PSScriptRoot
$source = Join-Path $project 'qol_base'
$build = Join-Path $project 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$sources=@('qol_base.c','qol_ui.c','hotkeys.c','replays.c','names.c','settings.c','speed.c','../speed_changer/speed_step.S') | ForEach-Object {Join-Path $source $_}
& $Zig cc -DR5K_QOL_NATIVE -DR5K_QOL_UI -shared -target x86_64-windows-gnu -O2 -Wall -Wextra -Werror @sources -o (Join-Path $build 'Rake5000QOLBase.dll') -luser32 -lgdi32
if($LASTEXITCODE -ne 0){throw 'QOL Tweaks build failed'}
if ($RunTests) {
    & $Zig cc -target x86_64-windows-gnu -O0 -Wall -Wextra -Werror (Join-Path $source 'test_qol_settings.c') -o (Join-Path $build 'test_qol_settings.exe') -luser32
    if($LASTEXITCODE -ne 0){throw 'QOL settings tests build failed'}
    & (Join-Path $build 'test_qol_settings.exe')
    if($LASTEXITCODE -ne 0){throw 'QOL settings tests failed'}
    $grenadeTest=Join-Path $build 'test_grenade_wheel.exe'
    & $Zig cc -target x86_64-windows-gnu -O0 -Wall -Wextra -Werror (Join-Path $project 'modloader_ports/test_grenade_wheel.c') -o $grenadeTest -luser32 -lgdi32
    if($LASTEXITCODE -ne 0){throw 'Grenade wheel test build failed'}
    & $grenadeTest
    if($LASTEXITCODE -ne 0){throw 'Grenade wheel tests failed'}
    $speed=Join-Path $project 'speed_changer'
    & $Zig cc -DR5K_QOL_NATIVE -target x86_64-windows-gnu -O2 -Wall -Wextra -Werror (Join-Path $speed 'test_speed_step.c') (Join-Path $speed 'test_speed_step.S') (Join-Path $speed 'speed_step.S') -o (Join-Path $build 'test_native_speed_step.exe')
    if($LASTEXITCODE -ne 0){throw 'Native speed test build failed'}
    & (Join-Path $build 'test_native_speed_step.exe')
    if($LASTEXITCODE -ne 0){throw 'Native speed test failed'}
    & $Zig cc -DR5K_QOL_NATIVE -target x86_64-windows-gnu -O2 -Wall -Wextra -Werror (Join-Path $speed 'test_speed_settings.c') (Join-Path $speed 'speed_step.S') -o (Join-Path $build 'test_native_speed_settings.exe') -luser32 -lgdi32
    if($LASTEXITCODE -ne 0){throw 'Native speed settings test build failed'}
    & (Join-Path $build 'test_native_speed_settings.exe')
    if($LASTEXITCODE -ne 0){throw 'Native speed settings test failed'}
}
if ($Symtest) {
    & $Symtest $GameFolder (Join-Path $build 'Rake5000QOLBase.dll')
    if ($LASTEXITCODE -ne 0) { throw 'QOL Tweaks loader validation failed.' }
}
