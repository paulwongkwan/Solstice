# Building
Sunshine binaries are built using [CMake](https://cmake.org) and requires `cmake` > 3.25.

## Building Locally

### Compiler
Use the MinGW-w64 GCC that ships with MSYS2 UCRT64:

| Compiler      | Version |
|:--------------|:--------|
| MinGW-w64 GCC | 14+     |

### Dependencies

Boost `1.89.0` or newer can be supplied as a system package. When a compatible system package is unavailable, CMake
downloads the project-locked Boost release through CPM. Static Boost libraries are preferred on every platform by
default; pass `-DBOOST_USE_STATIC=OFF` to use shared system libraries instead.

Sunshine builds on Windows with MSYS2 UCRT64, which provides the packaged static Boost libraries. Configuring on any
other host fails, because Sunshine supports Windows 11 only.

#### Prebuilt Web UI

Sunshine releases publish the platform-independent Web UI as the `@lizardbyte/sunshine` package to npm and GitHub
Packages. The package contains the production assets under `build/assets/web`, so downstream packagers can install the
Web UI without running Node.js or Vite during the native Sunshine build.

#### Windows

> [!WARNING]
> Cross-compilation is not supported on Windows. You must build on the target architecture.

First, you need to install [MSYS2](https://www.msys2.org).

For AMD64 startup "MSYS2 UCRT64" (or for ARM64 startup "MSYS2 CLANGARM64") then execute the following commands.

##### Update all packages
```bash
pacman -Syu
```

##### Set toolchain variable
For UCRT64:
```bash
export TOOLCHAIN="ucrt-x86_64"
```

For CLANGARM64:
```bash
export TOOLCHAIN="clang-aarch64"
```

##### Install dependencies
```bash
dependencies=(
  "git"
  "mingw-w64-${TOOLCHAIN}-boost"  # Optional
  "mingw-w64-${TOOLCHAIN}-cmake"
  "mingw-w64-${TOOLCHAIN}-cppwinrt"
  "mingw-w64-${TOOLCHAIN}-curl-winssl"
  "mingw-w64-${TOOLCHAIN}-doxygen"  # Optional, for docs
  "mingw-w64-${TOOLCHAIN}-graphviz"  # Optional, for docs
  "mingw-w64-${TOOLCHAIN}-miniupnpc"
  "mingw-w64-${TOOLCHAIN}-onevpl"
  "mingw-w64-${TOOLCHAIN}-openssl"
  "mingw-w64-${TOOLCHAIN}-opus"
  "mingw-w64-${TOOLCHAIN}-toolchain"
  "mingw-w64-${TOOLCHAIN}-qt6-static"
)
if [[ "${MSYSTEM}" == "UCRT64" ]]; then
  dependencies+=(
    "mingw-w64-${TOOLCHAIN}-MinHook"
    "mingw-w64-${TOOLCHAIN}-nsis"
  )
fi
pacman -S "${dependencies[@]}"
```

Static Qt is enabled by default on Windows. Sunshine automatically adds the MSYS2 static Qt prefix at
`${MINGW_PREFIX}/qt6-static` when that package is installed. If an IDE does not inherit `MINGW_PREFIX`, Sunshine
derives the same prefix from the selected compiler. If static Qt is installed in a custom location, specify it with
`-DCMAKE_PREFIX_PATH=/path/to/qt6-static`.

To use dynamic Qt instead, configure with `-DSUNSHINE_USE_STATIC_QT=OFF` and ensure the dynamic Qt package is
available through the normal toolchain prefix.

To create a WiX installer, you also need to install [.NET](https://dotnet.microsoft.com/download).

To build the frontend, install native [Node.js](https://nodejs.org/en/download) for the target architecture. The
MSYS2 Node.js package is not supported because its shared runtime cannot load the native binding required by Rolldown.

### Clone
Ensure [git](https://git-scm.com) is installed on your system, then clone the repository using the following command:

```bash
git clone https://github.com/SunshineStream/Sunshine.git --recurse-submodules
cd sunshine
mkdir build
```

### Build

```bash
cmake -B build -G Ninja -S .
ninja -C build
```

> [!TIP]
> Available build options can be found in
> [options.cmake](https://github.com/SunshineStream/Sunshine/blob/master/cmake/prep/options.cmake).

### Package

```bash
cpack -G NSIS --config ./build/CPackConfig.cmake
cpack -G WIX --config ./build/CPackConfig.cmake
cpack -G ZIP --config ./build/CPackConfig.cmake
```


It may be beneficial to build remotely in some cases. This will enable easier building on different operating systems.

1. Fork the project
2. Activate workflows
3. Trigger the *CI* workflow manually
4. Download the artifacts/binaries from the workflow run summary
