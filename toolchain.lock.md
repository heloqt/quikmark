# Toolchain Lock

liteMark uses an isolated MSYS2 installation so that its wxWidgets 3.3
dependency does not affect other projects using `D:\msys64`.

## Location

```text
D:\msys64-33
```

Use the `ucrt64` subsystem:

```text
D:\msys64-33\ucrt64
```

Do not mix binaries from `D:\msys64` and `D:\msys64-33`.

## Versions

| Component | Version |
|---|---|
| MSYS2 runtime | 3.6.11-4 |
| UCRT64 GCC/G++ | 16.2.0-6 |
| CMake | 4.4.4 |
| Ninja | 1.13.2 |
| wxWidgets | 3.3.4 |
| md4c | 0.6.0 |
| NSIS | 3.13-2 |
| C++ standard | C++17 |
| Target | x86_64-w64-mingw32 |

## Package commands

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-wxwidgets3.3-msw \
  mingw-w64-ucrt-x86_64-md4c \
  mingw-w64-ucrt-x86_64-nsis
```

The package repositories are pinned in the isolated installation to the
TUNA MSYS2 mirror first.

## Build

```powershell
.\scripts\build.ps1 -Preset mingw-debug
.\scripts\build.ps1 -Preset mingw-release-static
```

Outputs:

```text
build\<preset>\bin\litemark.exe
```
