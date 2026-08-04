# Agent Instructions: Windows Build via windows-mcp

Step-by-step runbook for building this repo (SDR++Brown) on a remote Windows machine through the
`windows-mcp` MCP server. Written from a successful build; `IMPORTANT` marks the steps that make
or break it. Pitfalls are summarized briefly at each step and in the table at the end.

## Prerequisites (on the Windows host)

1. Install `uvx`:

   ```
   powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
   ```

2. Switch off the firewall on the Windows machine (so `windows-mcp` can reach it).
3. Start the windows-mcp server:

   ```
   uvx windows-mcp serve --transport streamable-http --host localhost --port 8000 --allow-insecure-login
   ```

## 1. Verify the connection (from opencode host)

- Call `windows-mcp_DisplayInventory` — returns monitor layout; proves the server is alive.
- `list_mcp_resources` / `list_mcp_resource_templates` return empty; that's normal.
- Run `windows-mcp_PowerShell` with `whoami` / `Get-CimInstance Win32_OperatingSystem` to see the OS.

On a fresh VM: **no git, no cmake, no Visual Studio** — everything below is needed.

## 2. Disable UAC prompts FIRST

Every install needs admin (winget, NSIS installers, HKLM writes). Disable prompting early so all
later elevation is silent.

> **IMPORTANT:** Do this first. It needs exactly ONE elevated `Start-Process -Verb RunAs` (the
> only confirmation you'll ever have to click). After that, elevation is silent.

```powershell
# uac_off.ps1 — run once elevated, have it write a result file you read back
Set-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" -Name "ConsentPromptBehaviorAdmin" -Value 0 -Type DWord
Set-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" -Name "PromptOnSecureDesktop" -Value 0 -Type DWord
"done" | Out-File "$env:TEMP\uac_set.txt"
# run: Start-Process powershell -ArgumentList "-ExecutionPolicy","Bypass","-File","C:\dev\uac_off.ps1" -Verb RunAs -Wait
```

Verify with an elevated probe that prints `elevated=True` (same `-Verb RunAs -Wait` pattern +
read-back of a result file).

> **IMPORTANT:** A non-elevated MCP shell cannot write HKLM — always delegate to an elevated
> child and read a result file. The UAC prompt itself may not visibly appear (auto-approved);
> just check the result file afterwards.

## 3. Install the toolchain (winget)

> **IMPORTANT:** MCP `PowerShell` calls time out at ~30 s. Long installs report "timed out" but
> usually keep running and complete. **Never blindly re-run** — verify completion first
> (`vswhere`, `git --version`) and only retry if actually missing.

```powershell
winget install --id Git.Git --accept-source-agreements --accept-package-agreements --silent
winget install --id 7zip.7zip --accept-source-agreements --accept-package-agreements --silent

winget install --id Microsoft.VisualStudio.2022.BuildTools `
  --accept-source-agreements --accept-package-agreements --silent `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools `
  --includeRecommended --add Microsoft.VisualStudio.Component.VC.CMake.Project `
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
```

Verify VS finished — poll `isComplete` until it returns `1` (the installer runs in the background
for several minutes):

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -products Microsoft.VisualStudio.Product.BuildTools -property isComplete -format value
Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC"
# CMake/Ninja live under Common7\IDE\CommonExtensions\Microsoft\CMake\...
```

> **IMPORTANT:** If winget says "Found an existing package already installed" for BuildTools, an
> earlier timed-out attempt actually completed — check `vswhere isComplete` and move on.

## 4. Clone the repo

```powershell
if(-not (Test-Path "C:\dev")){ New-Item -ItemType Directory "C:\dev" | Out-Null }
git clone --recurse-submodules https://github.com/sannysanoff/SDRPlusPlusBrown "C:\dev\SDRPlusPlusBrown"
```

No submodules are actually registered, so plain `git clone` is fine too.

## 5. External dependencies

Reference recipe: `.github/workflows/build_all.yml`, job `build_windows`.

### 5.1 PothosSDR (volk + airspy/airspyhf/hackrf/rtlsdr/lime/etc.)

> **IMPORTANT:** `7z` cannot unpack the PothosSDR `.exe` (NSIS). Run the installer silently:

```powershell
Invoke-WebRequest -Uri "https://downloads.myriadrf.org/builds/PothosSDR/PothosSDR-2020.01.26-vc14-x64.exe" -OutFile "C:\dev\downloads\pothos.exe"
Start-Process "C:\dev\downloads\pothos.exe" -ArgumentList "/S","/D=C:\Program Files\PothosSDR" -Wait
Test-Path "C:\Program Files\PothosSDR\bin\volk.dll"   # expect True
```

Patch libusb (downgrade to 1.0.23) and librtlsdr (newer build), exactly as CI does:

```powershell
# libusb 1.0.23
Invoke-WebRequest -Uri "https://github.com/libusb/libusb/releases/download/v1.0.23/libusb-1.0.23.7z" -OutFile libusb.7z
7z x libusb.7z -olibusb_old
Copy-Item libusb_old/MS64/dll/libusb-1.0.dll "C:\Program Files\PothosSDR\bin\"
Copy-Item libusb_old/MS64/dll/libusb-1.0.lib "C:\Program Files\PothosSDR\lib\"

# librtlsdr 20240623
Invoke-WebRequest -Uri "https://ftp.osmocom.org/binaries/windows/rtl-sdr/rtl-sdr-64bit-20240623.zip" -OutFile rtl-sdr.zip
7z x rtl-sdr.zip
Copy-Item rtl-sdr-64bit-20240623/librtlsdr.dll "C:\Program Files\PothosSDR\bin\rtlsdr.dll"
```

### 5.2 vcpkg packages (fftw3, glfw3, portaudio, zstd, libusb, itpp, spdlog)

```powershell
git clone https://github.com/microsoft/vcpkg "C:\dev\vcpkg"
Set-Location "C:\dev\vcpkg"; .\bootstrap-vcpkg.bat -disableMetrics
```

> **IMPORTANT:** `vcpkg install` takes 30–60 min and **dies when the MCP shell session ends**.
> Launch it fully detached via WMI and poll a "done" marker — never run it in the foreground:

```powershell
$cmd = 'cd C:\dev\vcpkg; C:\dev\vcpkg\vcpkg.exe install fftw3:x64-windows glfw3:x64-windows portaudio:x64-windows zstd:x64-windows libusb:x64-windows itpp:x64-windows spdlog:x64-windows; (Get-Date -Format o) + " DONE exit=" + $LASTEXITCODE | Out-File C:\dev\vcpkg\run_done.txt -Append'
Set-Content -Path "C:\dev\vcpkg\run2.ps1" -Value $cmd -Encoding UTF8
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = 'powershell.exe -ExecutionPolicy Bypass -NoProfile -File C:\dev\vcpkg\run2.ps1' }
# poll: Get-Content C:\dev\vcpkg\run_done.txt  (also watch buildtrees/*/ *.log mtimes)
```

- Progress check: `Get-ChildItem C:\dev\vcpkg\buildtrees -Directory | ForEach { last log mtime }`
- The first package (fftw3, 4 variants × ~434 files) takes the longest on a VM — be patient.
- vcpkg downloads PowerShell Core 7 for its build scripts; that's expected.

### 5.3 rtaudio (audio sink)

> **IMPORTANT:** plain `cmake ..` hangs trying to auto-detect MSVC. Use the Visual Studio
> generator (no vcvars needed):

```powershell
git clone https://github.com/thestk/rtaudio; Set-Location rtaudio
git checkout 2f2fca4502d506abc50f6d4473b2836d24cfb1e3
New-Item -ItemType Directory build; Set-Location build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="C:\Program Files (x86)\RtAudio"
cmake --build . --config Release
```

> **IMPORTANT:** `cmake --install .` writes to Program Files → run it elevated (`Start-Process
> -Verb RunAs -Wait` on a `.ps1` that runs the install and logs the result).

The installed DLL is `rtaudio.dll` (under `C:\Program Files (x86)\RtAudio\bin`), which is what
`audio_sink` needs at runtime — the `rtaudiod.dll` name in `readme.md` refers to the debug build.

### 5.4 codec2, SDRPlay, perseus/rfnm/fobos/hydrasdr — OPTIONAL

Only needed for full CI parity. Minimal build keeps them OFF (saves hours). If ever needed, CI
builds codec2 via msys2/MinGW (`-DCMAKE_GNUtoMS=ON`) into `C:/Program Files/codec2`.

## 6. Configure the build (minimal scope)

> **IMPORTANT:** Always pass `-G "Visual Studio 17 2022" -A x64`, otherwise cmake hangs.

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
New-Item -ItemType Directory "C:\dev\build"; Set-Location "C:\dev\build"
& $cmake "C:\dev\SDRPlusPlusBrown" `
  -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DCOPY_MSVC_REDISTRIBUTABLES=ON `
  -DOPT_BUILD_M17_DECODER=OFF -DOPT_BUILD_CH_EXTRAVHF_DECODER=ON `
  -DOPT_BUILD_RFNM_SOURCE=OFF -DOPT_BUILD_FOBOSSDR_SOURCE=OFF `
  -DOPT_BUILD_PERSEUS_SOURCE=OFF -DOPT_BUILD_HYDRASDR_SOURCE=OFF `
  -DOPT_BUILD_SDRPLAY_SOURCE=OFF -DOPT_BUILD_BLADERF_SOURCE=OFF `
  -DOPT_BUILD_LIMESDR_SOURCE=OFF -DOPT_BUILD_SOAPY_SOURCE=OFF `
  -DOPT_BUILD_MPEG_ADTS_SINK=OFF
```

> **IMPORTANT:** Keep `OPT_BUILD_M17_DECODER=OFF` unless you've built codec2 first — otherwise the
> build fails with `error C1083: Cannot open include file: 'codec2.h'`.

- Turn OFF any module whose dependency you did not install (CI defaults them ON).
- Configure also downloads mbelib + ETSI TETRA codec (for `ch_extravhf` / `ch_tetra`) into the
  build dir; that's normal and just takes a minute.

## 7. Build (detached)

Run `cmake --build` detached via WMI (same pattern as vcpkg) so MCP timeouts can't kill it:

```powershell
# run_build.ps1
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
Set-Location "C:\dev\build"
& $cmake --build . --config RelWithDebInfo *> C:\dev\build\build.log 2>&1
"exit=" + $LASTEXITCODE | Out-File C:\dev\build\build.log -Append
```

- Poll `build.log` and the `*.dll` count under `C:\dev\build`.
- Success: `exit=0`, `sdrpp.exe` at `C:\dev\build\RelWithDebInfo\sdrpp.exe`.

## 8. Assemble the runtime folder

Modules land in per-module `RelWithDebInfo` dirs; SDR++ expects them flat in `modules/`. Assemble
into a self-contained folder:

```powershell
$rt = "C:\dev\sdrpp_runtime"
New-Item -ItemType Directory $rt; New-Item -ItemType Directory "$rt\modules"

# core exe + MSVC redist + built-in dlls
Copy-Item "C:\dev\build\RelWithDebInfo\*" $rt -Force
Copy-Item "C:\dev\SDRPlusPlusBrown\root\*" $rt -Recurse -Force   # res/

# all module dlls, flattened
Get-ChildItem "C:\dev\build" -Recurse -Filter "*.dll" | Where-Object { $_.FullName -match "\\decoder_modules\\|\\sink_modules\\|\\source_modules\\|\\misc_modules\\" } |
  ForEach-Object { Copy-Item $_.FullName "$rt\modules\" -Force }

# support dlls
Copy-Item "C:\Program Files\PothosSDR\bin\airspy.dll","...airspyhf.dll","...hackrf.dll","...rtlsdr.dll","...libusb-1.0.dll","...libiio.dll","...libad9361.dll","...pthreadVC2.dll" $rt
Copy-Item "C:\dev\vcpkg\installed\x64-windows\bin\itpp.dll" $rt            # ch_extravhf needs it
Copy-Item "C:\Program Files (x86)\RtAudio\bin\rtaudio.dll" $rt             # audio sink
```

> **IMPORTANT:** `itpp.dll` is only on `PATH` inside a vcpkg env — copy it into the runtime dir,
> else `ch_extravhf_decoder` (and `ch_tetra_demodulator`) fail to initialize at startup.

## 9. Run (VM / Mesa caveat)

> **IMPORTANT:** In a VirtualBox/VM with no GL driver you MUST drop Mesa's `opengl32.dll` next to
> `sdrpp.exe` or the GUI won't render (see `readme.md`):

```powershell
Invoke-WebRequest -Uri "https://downloads.fdossena.com/Projects/Mesa3D/Builds/MesaForWindows-x64-20.1.8.7z" -OutFile mesa.7z
7z x mesa.7z -omesa
Copy-Item mesa/opengl32.dll $rt\   # single self-contained file, no companion dlls needed
```

> **IMPORTANT:** In a headless VM with **no audio output device** the app crashes during Radio
> init (`audio_sink` `selectById` does an OOB read of an empty `devList`, AV `c0000005`). The
> default config still targets the `Audio` sink — point the `Radio` stream at the built-in
> `None` (null) sink in the **runtime `config.json`** instead (do NOT change the code default):
>
> ```json
> "streams": { "Radio": { "muted": false, "sink": "None", "volume": 1.0 } }
> ```

Launch and diagnose:

```powershell
Start-Process "$rt\sdrpp.exe" -WorkingDirectory $rt -RedirectStandardOutput "$rt\sdrpp_out.log" -RedirectStandardError "$rt\sdrpp_err.log"
Get-Process sdrpp | Select Id,MainWindowTitle,Responding   # window should appear with title "SDR++Brown v..."
```

> **IMPORTANT — redeploying a rebuilt module DLL:** an MCP `PowerShell` call that does
> `Copy-Item <dll> $rt\modules; Stop-Process sdrpp; Start-Process sdrpp` is WRONG. The old
> process holds the DLL locked, so `Copy-Item` **silently fails** (file in use) and the running
> app keeps the OLD module. Order MUST be separate commands:
> 1. `Stop-Process` (kill) → verify `Get-Process sdrpp` returns nothing.
> 2. `Copy-Item` the rebuilt DLLs into `$rt` / `$rt\modules` → verify `LastWriteTime` updated.
> 3. `Start-Process` the app.
> Always confirm the DLL timestamp on disk matches the fresh build output.

> **IMPORTANT — the window title timestamp is a lie:** `(Built at 10:30:01, Aug 2 2026)` is
> baked into `sdrpp.exe` at link time. Rebuilding only modules (core, file_source, ...) does NOT
> change it, so a running app with that title can still be running the NEW modules. To verify
> freshness, check the module DLL timestamps (`ls $rt\modules\*.dll`), not the title.

> **MCP `Start-Process` "timeout" is normal:** launching sdrpp via `Start-Process -PassThru` often
> returns `MCP error -32001 (timed out)` even though the app started fine (the redirected stdout
> handle keeps the MCP session busy). Don't re-launch — just check `Get-Process sdrpp` in a fresh
> call.

- Expected stderr noise: `missing _INFO_ symbol` for `fftw3f.dll`/`itpp.dll`/`portaudio.dll`/`zstd.dll`
  and `Module 'X' doesn't exist` — these are *copied support DLLs*, not modules; harmless.
- **Log truncation on crash is a red herring:** on Windows `flog` didn't `fflush` stdout, so the
  last line (`FT4 decoder cr...`) was *not* the crash site. Fixed in `core/src/utils/flog.cpp`
  (`fflush(outStream)` in the `_WIN32` branch) — the real crash was the missing audio sink above.
- **Decimal separator bug:** `setlocale(LC_ALL, ".65001")` inherits the system `LC_NUMERIC`, so on a
  comma-decimal Windows install `std::to_string(double)` emits `150000,000000` and breaks the HTTP
  debug JSON. Fix: `setlocale(LC_NUMERIC, "C")` right after (in `core.cpp`).
- When it works, the log ends with `Starting HTTP debug server on port 8080` → `Ready. Main loop
  starts.`, and `http://127.0.0.1:8080/status` returns `{"ready": true, ...}`.

## 10. E2E tests (Python, no .wav needed)

### Environment

- Python is present on the VM (3.14, from `uv`). Verify: `where.exe python`.
- The harness (`e2e/e2e_common.py`) is cross-platform now:
  - `kill_existing_sdrpp()` uses `pkill` on Linux/macOS and a port-matching PowerShell
    `Stop-Process` on Windows (only kills `sdrpp.exe` whose command line contains `--http <port>`).
  - `get_base_config()` falls back to flat `modules/` + `res/` next to the binary (Windows layout)
    after trying the Linux `inst/lib/sdrpp/plugins` paths.
- Set env vars so the harness launches the assembled runtime, then run each test file directly:

```powershell
Set-Location C:\dev\SDRPlusPlusBrown\e2e
$env:E2E_BINARY    = "C:\dev\sdrpp_runtime\sdrpp.exe"
$env:E2E_BUILD_DIR = "C:\dev\sdrpp_runtime"
$env:E2E_ROOT_DEV  = "C:\dev\sdrpp_runtime"
$env:E2E_VERBOSE   = "1"
python test_lsb_startup.py
python test_radio_modes.py
python test_frequency_manager.py
python test_frequency_manager_tetra.py
```

### What passes / what needs .wav

- Start/ping/logging tests (no `.wav` recordings) — all PASS on Windows: `test_lsb_startup.py`,
  `test_radio_modes.py`, `test_frequency_manager.py`, `test_frequency_manager_tetra.py`
  (22/22 as of Aug 2026).
- `.wav`-based tests (`test_dmr_wait_status.py`, `test_dsd_record.py`, `test_tetra_demodulator.py`)
  use repo-relative samples `e2e/recordings/dmr_sample.wav` + `tetra_sample.wav` (committed).
  Run them the same way (set the three `E2E_*` env vars, then `python test_<name>.py`).
- These tests use `File Source` → `set_filename` over HTTP. `file_source` must return properly
  JSON-escaped responses (fixed via `json::dump()`), else a Windows path `C:\dev\...` fails with
  `Invalid \escape` in the harness.

### How the harness works (so a failure makes sense)

- Each test writes `config.json` (+ module configs) into a temp dir, then launches
  `sdrpp.exe -r <tempdir> --http <port>` and polls `/status` for `mainLoopStarted`.
- The config's `moduleInstances` controls which modules are created; base config uses only
  `radio` + `null_audio_sink` with stream sink `"None"` — so tests never touch the real audio
  device (which doesn't exist on this VM anyway).
- Ports auto-increment from 8085 per test process. An already-running manual `sdrpp.exe` on
  port 8080 does not conflict, and `kill_existing_sdrpp()` only kills port-matching processes.

## 11. Crash diagnosis workflow (procdump + cdb)

The VM has no WER dump for sdrpp (exception was in a DLL, and `SetErrorMode` suppresses dialogs),
so to get a real stack trace:

1. Install procdump + WinDbg (contains `cdb.exe`):

```powershell
Invoke-WebRequest -Uri "https://download.sysinternals.com/files/Procdump.zip" -OutFile procdump.zip
Expand-Archive procdump.zip C:\dev\procdump
winget install --id Microsoft.WinDbg -e --accept-source-agreements --accept-package-agreements --silent --disable-interactivity
# cdb.exe at: C:\Program Files\WindowsApps\Microsoft.WinDbg_<ver>_x64__8wekyb3d8bbwe\amd64\cdb.exe
```

2. Launch sdrpp from a `.cmd` so the working dir is the runtime dir, then capture a **mini** dump
   (NOT `-ma`, which is ~2 GB and fills the VM disk!):

```cmd
@echo off
cd /d C:\dev\sdrpp_runtime
C:\dev\procdump\procdump64.exe -accepteula -e -o -x C:\dev\crashdumps C:\dev\sdrpp_runtime\sdrpp.exe
```

3. Analyze (mini dump is enough for the faulting stack):

```powershell
$cdb = "C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2606.22001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe"
& $cdb -z C:\dev\crashdumps\sdrpp.exe_*.dmp -c ".symfix; .reload; !analyze -v; q" 2>&1 | Select-String "STACK_TEXT|FAULTING_SOURCE"
```

- Look at `FAULTING_SOURCE_LINE` + `STACK_TEXT` — this is how the audio_sink `devList[0]` OOB
  was found (not the misleading last flog line).

## 12. Git workflow: keep the two repos in sync

Local macOS repo and the Windows VM repo (`C:\dev\SDRPlusPlusBrown`) must stay identical so
patches apply cleanly. Workflow per fix:

1. Edit locally; `git diff -- <files>`; review.
2. Commit locally.
3. Ship as a patch to the VM: `git format-patch -1 <sha> --stdout`, base64 it, decode on Windows
   via `[IO.File]::WriteAllBytes(...)` into `0001-xxx.patch`, then `git am 0001-xxx.patch`.
4. `git am` needs `user.name`/`user.email` configured (done: `git config user.name/email`).
5. Delete the `.patch` file afterwards.

> **IMPORTANT:** never use `git checkout -- <file>` on a file you have uncommitted edits in,
> and never edit the same file via PowerShell `-replace` / `sed` on one side only — it makes the
> trees diverge and `git apply`/`git am` fail silently. Always patch, then commit on both sides.

## 13. USRP / UHD build (Windows)

UHD is built **from source** (EttusResearch/uhd, tag `v3.15.0.0`) on the Windows host and the
`usrp_source` module is enabled via CMake. Rationale: the UHD 3.15 GitHub release ships **no
Windows binaries** (only `uhd-images` tarballs), and the PothosSDR-bundled UHD 3.15 is used only
as a fallback. Build on branch `windows-usrp-build`.

### 13.1 Prerequisites

- **Boost from vcpkg** (`boost:x64-windows`). NOTE: vcpkg's `boost` port has default features
  that pull in **python3 + sqlite3 + libffi + openssl + expat** even though UHD never uses
  Boost.Python. Build takes 1.5–2 h on a VM. If you want to skip that, install only the needed
  components with `--no-default-features` + `[chrono,date_time,filesystem,program_options,regex,system,thread,serialization]`.
  UHD 3.15 needs Boost ≥ 1.58; vcpkg delivers 1.91 (`BOOST_LIB_VERSION "1_91"`).
  `boost_regex`/`boost_system` are header-only in modern Boost — no `.lib` for them is expected.
- **libusb** from vcpkg (`libusb:x64-windows`) — also in the same install.
- **Python ≥ 3.5 + `mako`** for UHD's codegen. On a VM whose system python is uv-managed
  (3.14, no pip), create a venv and install there:
  ```powershell
  uv venv C:\dev\uhdvenv
  C:\dev\uhdvenv\Scripts\pip.exe install mako requests
  # point UHD's configure at it with -DPYTHON_EXECUTABLE=C:/dev/uhdvenv/Scripts/python.exe
  ```
- Launch boost install detached and poll a done marker (same pattern as §5.2):
  ```powershell
  # boost_install.ps1
  cd C:\dev\vcpkg
  C:\dev\vcpkg\vcpkg.exe install boost:x64-windows libusb:x64-windows
  (Get-Date -Format o) + " BOOST_DONE exit=" + $LASTEXITCODE | Out-File C:\dev\vcpkg\boost_done.txt
  ```
  Watch progress via `Get-ChildItem C:\dev\vcpkg\installed\x64-windows\lib\boost_*.lib | Measure`.

### 13.2 Build UHD 3.15.0.0 from source

```powershell
git clone --depth 1 --branch v3.15.0.0 https://github.com/EttusResearch/uhd C:\dev\uhd
# build_uhd.ps1 (detached, poll C:\dev\uhd\uhd_done.txt):
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
New-Item -ItemType Directory C:\dev\uhd\build -Force | Out-Null
Set-Location C:\dev\uhd\build
& $cmake C:\dev\uhd\host -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DPYTHON_EXECUTABLE=C:/dev/uhdvenv311/Scripts/python.exe" `
  "-DCMAKE_INSTALL_PREFIX=C:/Program Files/uhd" `
  "-DCMAKE_CXX_FLAGS=/EHsc -DBOOST_BIND_GLOBAL_PLACEHOLDERS" `
  "-DBOOST_ALL_DYN_LINK=ON" `
  -DENABLE_TESTS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_UTILS=OFF `
  -DENABLE_PYTHON_API=OFF -DENABLE_DOXYGEN=OFF -DENABLE_MAN_PAGES=OFF
& $cmake --build . --config Release --parallel 4
"UHD_BUILD_DONE exit=" + $LASTEXITCODE | Out-File C:\dev\uhd\uhd_done.txt
```
Then install **elevated** (writes to `C:\Program Files\uhd`):
```powershell
# install_elev.ps1, launched via Start-Process -Verb RunAs -Wait
Set-Location C:\dev\uhd\build
& $cmake --install . --config Release
```

> **IMPORTANT:** UHD's CMake always configures the source root `C:\dev\uhd\host` (not `C:\dev\uhd`),
> and `-DPYTHON_EXECUTABLE` must point into the venv or codegen fails (`no module named mako`).
> The install step writes to `C:\Program Files\uhd` → needs elevation, so run install via the
> elevated pattern if UAC isn't disabled (see §2).

> **IMPORTANT — three CMake flags are mandatory:**
> 1. `-DCMAKE_CXX_FLAGS="/EHsc -DBOOST_BIND_GLOBAL_PLACEHOLDERS"` — without `/EHsc` UHD builds
>    with exceptions off (`<ExceptionHandling/>` is empty), so Boost defines `BOOST_NO_EXCEPTIONS`
>    and `boost/serialization` fails with `throw_exception is not a member of 'boost'` (which also
>    cascades into bogus `boost::math::sign` "not a member of 'boost'" errors in usrp2_impl).
>    Without `-DBOOST_BIND_GLOBAL_PLACEHOLDERS` many files fail with `error C2065: '_1'`.
> 2. `-DBOOST_ALL_DYN_LINK=ON` — vcpkg builds boost as **DLLs** (import libs
>    `boost_thread-vc143-mt-x64-1_91.lib` etc.). Without it UHD's MSVC branch clears the boost
>    component list (static-link path) and the final link fails with
>    `LNK2001: unresolved external boost::thread`-family symbols. With it, auto-linking pulls the
>    import libs.
> 3. Build with `--parallel 4` — full parallelism trips `cl : error D8040: error creating or
>    communicating with child process` + `error C1083 ... Permission denied` on obj files
>    (orphaned cl.exe from a timed-out run fight the next build; kill them first).

> **IMPORTANT — Boost compatibility:** vcpkg ships Boost 1.91 (needed for SDR++), but UHD 3.15's
> bundled `rpclib` (its RPC server) predates Boost 1.87 and won't compile against it:
> - `io_service` / `io_service::strand` were **removed** → migrate to `io_context` /
>   `strand<io_context::executor_type>` (strand ctor takes an executor: `io_.get_executor()`).
> - Single-arg `strand::post(fn)` / `io_service::strand::post(fn)` removed → use free
>   `boost::asio::post(strand_, fn)`.
> - `strand::wrap(fn)` removed → `boost::asio::bind_executor(strand_, fn)`.
> - `tcp::resolver::iterator` removed → `tcp::resolver::results_type`; the async_connect handler
>   now takes `tcp::endpoint`, and `resolver.resolve(host, service)` is preferred over the
>   `{host, service}` init-list overload.
> - `ip::address::from_string` → `boost::asio::ip::make_address`.
> Affected files (all under `lib/deps/rpclib/`): `client.cc`, `server.cc`,
> `detail/server_session.{h,cc}`, `detail/async_writer.h`. Also note UHD needs Python 3.x **with
> `distutils`** (removed in 3.12+, so use Python 3.11 via `uv python install 3.11` + venv), else
> every codegen module check fails with `import platform failed`.

> **IMPORTANT — UHD's own code also needs Boost-1.91 fixes** (besides rpclib):
> - Legacy asio in UHD sources: `lib/transport/{udp_simple,udp_wsa_zero_copy,tcp_zero_copy,udp_zero_copy}.cpp`,
>   `lib/transport/libusb1_base.{hpp,cpp}` (`timeval` needs `<winsock2.h>` before `libusb.h`),
>   `lib/transport/nirio/rpc/rpc_client.{hpp,cpp}` (also `io_service`→`io_context`,
>   `resolver::query`/`iterator`→`results_type`+`begin()->endpoint()`),
>   `lib/usrp/usrp2/{io_impl,mb_eeprom}.cpp`, `lib/usrp/{x300/x300_mb_eeprom,n230/n230_eeprom_manager}.cpp`
>   (`address_v4::from_string`→`make_address_v4`, `to_ulong`→`to_uint`).
> - `boost::filesystem`: `fs::extension()` removed → `path(...).extension().string()`;
>   `fs::change_extension` removed → `path(...).replace_extension("")` (in mpmd/x300/octoclock
>   image loaders — note `filepath` is a `std::string`, wrap in `fs::path` first).
> - `boost::math::sign`/`iround`: files that use them must include
>   `<boost/math/special_functions/{sign,round}.hpp>` explicitly (MSVC gets no transitive include;
>   e.g. usrp2_impl.cpp had them AFTER first use).
> - `std::array` needs an explicit `#include <array>` (lmx2592.cpp etc.).
> - `M_PI`/`M_LN2` are not defined on MSVC — put `#ifndef _USE_MATH_DEFINES / #define` at the TOP
>   of the file (ad9361_device.cpp had it after the first include).

> **IMPORTANT — Boost.Bind placeholders:** Boost 1.87+ no longer defines `_1`, `_2`, ... in the
> global namespace by default, so UHD 3.15 fails with `error C2065: '_1': undeclared identifier`
> (many files use `boost::bind`/`_1`). Fix: pass
> `-DCMAKE_CXX_FLAGS="-DBOOST_BIND_GLOBAL_PLACEHOLDERS"` to UHD's configure.

> **IMPORTANT — MSVC resource errors are transient:** a full-parallel UHD build can die with
> `cl : command line error D8040: error creating or communicating with child process` and/or
> `error C1083 ... Permission denied` on an obj file. That's MSBuild spawning too many `cl.exe`
> children (esp. on a RAM-tight VM), NOT a code error. Fix: rerun with
> `cmake --build . --config Release --parallel 4`. Watch RAM first
> (`Get-CimInstance Win32_OperatingSystem`) — D8040 usually follows low free RAM. **Never relaunch
> a timed-out `Start-Process -NoNewWindow` build** — orphaned `cl.exe` children will fight the
> next build for the same obj files (that's what the `Permission denied` obj errors are). Kill
> leftover `cl`/`msbuild`/`cmake` processes before retrying.

Verify: `C:\Program Files\uhd\lib\cmake\uhd\UHDConfig.cmake`, `C:\Program Files\uhd\include\uhd\version.hpp`
(should say `UHD_VERSION_ABI_STRING "3.15.0"`), `C:\Program Files\uhd\lib\uhd.lib`,
`C:\Program Files\uhd\bin\uhd.dll`.

### 13.3 Module wiring (local repo, `source_modules/usrp_source/CMakeLists.txt`)

MSVC branch uses a `UHD_DIR` variable (defaults to `C:/Program Files/uhd`, falls back to
`C:/Program Files/PothosSDR`), includes `${UHD_DIR}/include`, links `${UHD_DIR}/lib/uhd.lib`,
plus `find_package(Boost REQUIRED)` for headers. `usrp_source` is compiled with
`-DOPT_BUILD_USRP_SOURCE=ON -DUHD_DIR="C:/Program Files/uhd"`.

### 13.4 CI wiring (`.github/workflows/build_all.yml`)

- vcpkg line includes `boost:x64-windows`.
- New steps in `build_windows`: install `mako`+`requests`, download UHD source tarball
  (`v3.15.0.0.tar.gz`), build+install it (7z extract, VS 2022 generator, vcpkg toolchain,
  prefix `C:/Program Files/uhd`, tests/examples/utils/python/doxygen/man OFF), then pass
  `-DOPT_BUILD_USRP_SOURCE=ON -DUHD_DIR="C:/Program Files/uhd"` to the SDR++ configure.
- **Branch gating:** all non-Windows jobs (15 of them) get
  `if: ${{ !contains(github.ref_name, 'windows') }}` so that on a branch whose name contains
  `windows` only the Windows job runs; `build_windows` has no gate; spelling/formatting checks
  are not gated.

### 13.5 Runtime packaging (`make_windows_package.ps1`)

Copies `usrp_source.dll` into `modules/`, `uhd.dll` from `C:/Program Files/uhd/bin`, plus
`boost_*.dll` from both `uhd/bin` and `C:/vcpkg/installed/x64-windows/bin`.

> **IMPORTANT:** PothosSDR's `uhd.dll` statically pulls old `boost_*1_67.dll` names — if the
> vcpkg-built UHD (Boost 1.91) is the one installed at `C:/Program Files/uhd`, ship **that**
> `uhd.dll`, not the Pothos one, or the loader won't find matching boost DLLs.

## Pitfalls at a glance

| Pitfall | Fix |
|---|---|
| MCP calls report "timed out" | Install usually finished; verify (`vswhere`, `git --version`, result file) then continue |
| Non-elevated shell can't write HKLM / Program Files | `Start-Process -Verb RunAs -Wait` on a logging `.ps1`, then read the log |
| CMake hangs on configure | `-G "Visual Studio 17 2022" -A x64` |
| `7z` can't unpack PothosSDR .exe (NSIS) | Run installer: `/S /D=C:\Program Files\PothosSDR` |
| `vcpkg install` killed when MCP session ends | Detached via `Win32_Process.Create` + poll `run_done.txt` |
| Long cmake builds killed by MCP timeout | Run `cmake --build` detached too; poll `build.log` |
| Build fails: `codec2.h` not found | `OPT_BUILD_M17_DECODER=OFF` (needs codec2 built first) |
| ch_extravhf/tetra fail at init | Copy `itpp.dll` into the runtime dir |
| GUI won't render in VM | Mesa `opengl32.dll` next to the exe |
| audio sink won't load | `rtaudio.dll` in the build root |
| Startup abort right after Radio init (AV c0000005) | No audio device → set stream sink `"None"` in runtime `config.json` |
| Crash log line cut off mid-write | Windows `flog` wasn't flushing; `fflush` fix is in `core/src/utils/flog.cpp` |
| HTTP debug returns `150000,000000` (comma) | `setlocale(LC_NUMERIC, "C")` in `core.cpp` (keep `.65001` for Russian text) |
| `-ma` procdump fills the disk (~2 GB) | Use default mini dump; check `Get-PSDrive C` free space first |
| Rebuilt module not in effect (old behavior) | Kill app BEFORE `Copy-Item` the DLL — running process locks it and copy silently fails; verify DLL `LastWriteTime` |
| Window title still "Built at 10:30:01" | Timestamp baked into exe at link time; check module DLL timestamps instead |
| `Start-Process` MCP call "times out" | App usually started fine (stdout handle keeps session busy); verify with `Get-Process sdrpp`, don't relaunch |
| `set_filename` returns `Invalid \escape` | `file_source` built JSON by string concat; fixed via `json::dump()` — Windows paths need `\\` escaping |
| vcpkg `boost:x64-windows` builds python3/sqlite3/libffi/openssl (long) | Default features include Boost.Python; install lean via `--no-default-features` + needed components |
| UHD codegen fails `no module named mako` | `-DPYTHON_EXECUTABLE=C:/dev/uhdvenv/Scripts/python.exe` (uv python has no pip) |
| UHD CMake configures wrong dir / hangs | configure `C:\dev\uhd\host` with `-G "Visual Studio 17 2022" -A x64` |
| `usrp_source` missing at runtime | copy `usrp_source.dll` to `modules/` + `uhd.dll`/`boost_*.dll` (vcpkg-built UHD, not Pothos) |
| usrp_source DLL deps unresolved at runtime | `usrp_source.dll` imports `sdrpp_core.dll` + `uhd.dll` + `volk.dll`; `uhd.dll` imports `libusb-1.0.dll` + `boost_{filesystem,serialization,thread}-vc143-mt-x64-1_91.dll` — all must sit in the runtime root (sdrpp core/volk already there; copy the uhd/boost/libusb ones) |
| sdrpp crashes / first-chance `E06D7363` (C++ exception) right after `Loading .../usrp_source.dll` | **Not a crash** — that's UHD's network/USRP discovery throwing `boost::system::system_error` (send_to error 10049) on a VM with no USRP hardware. The app recovers and prints `USRPSourceModule 'USRP Source': Menu Select!` + `Running post-init for USRP Source`. Verify via `Select-String usrp_source` on the stdout log, not the procdump first-chance noise |
