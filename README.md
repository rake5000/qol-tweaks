# Rake5000 QOL Tweaks

Windows x64 native module for Door Kickers 2 game build 112 and DK2 Modloader API v3. It includes additional order hotkeys, replay speed, trooper names, game speed controls, and the shared mission settings menu.

## Build and test

Install Zig 0.16.0 and put it on PATH. From PowerShell, run:

```powershell
.\build.ps1 -RunTests
```

The build produces `build/Rake5000QOLBase.dll`. The offline tests require no game installation. To also validate Modloader initialization, supply `-GameFolder` and `-Symtest` with the game directory and the SDK validation executable.

The existing QOL Tweaks package supplies the game UI XML and artwork. For in-game testing, use its existing deployment workflow and restart the game. The DLL name and configuration identities are retained for existing installations.

## License

Rake5000 source, tests, and build scripts use PolyForm Noncommercial 1.0.0. The bundled DK2 Modloader SDK header retains its MIT license.
