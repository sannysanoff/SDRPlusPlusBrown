# Debugging Guide for SDR++

This document provides debugging and remote control capabilities for SDR++.

## HTTP Debug Server

SDR++ includes an embedded HTTP debug server from [EmbeddableWebServer](https://github.com/hellerf/EmbeddableWebServer) for debugging and remote control.

### Command Line Options

- `--http <port>` - Start HTTP server on port (0 to disable, default 8080)
- `--debug-wait <file>` - Wait for file to exist before continuing (for debugger attachment)

### Endpoints

| Endpoint | Description |
|----------|-------------|
| `GET /status` or `GET /` | Returns JSON with `ready`, `httpListening`, `mainLoopStarted` |
| `GET /stop` or `/exit` | Stop the application |
| `GET /windows` | List ImGui windows (name, id, position, size) |
| `GET /layout` | Dump UI layout with all windows and viewport dimensions |
| `GET /click?x=<>&y=<>` | Queue mouse click at coordinates |
| `GET /clickid?id=<>` | Click on element by ImGui ID |
| `GET /mouse?x=<>&y=<>` | Queue mouse move to coordinates |
| `GET /key?key=<>` | Queue key press (GLFW key code) |
| `GET /type?text=<>` | Queue text input |
| `GET /sdr/start` | Start SDR playback |
| `GET /sdr/stop` | Stop SDR playback |
| `GET /sdr/status` | Get SDR playing status (true/false) |
| `GET /sinks` | List available audio sink providers |
| `GET /streams` | List registered audio streams and current sink assignments |
| `POST /sink/select` | Assign a sink provider to a stream |
| `GET /vfo/set_offset?name=<>&offset=<>` | Set a VFO offset relative to source center frequency |
| `GET /modules` | List all module instances with their module names |
| `GET /module/<instance>/command` | Run a module debug command via query params |
| `POST /module/<instance>/command` | Run a module debug command via JSON body |
| `GET /log` | Retrieve current in-memory SDR++ log batch and clear it |
| `GET /proc` | List all registered procfs endpoints |
| `GET /proc/<path>` | Read from a registered procfs endpoint |
| `POST /proc/<path>` | Write to a registered procfs endpoint |
| `GET /ls` | List procfs endpoints with value, type, and writability |

### Common HTTP Responses

`GET /status` or `GET /` returns server readiness:

```json
{"ready": true, "httpListening": true, "mainLoopStarted": true}
```

`GET /sdr/start`:

```json
{"action": "sdr_start"}
```

`GET /sdr/stop`:

```json
{"action": "sdr_stop"}
```

`GET /sdr/status`:

```json
{"playing": true}
```

`GET /log`:

```json
{"log": "<escaped log content>"}
```

On desktop, `/log` is only available when launched with `SDRPP_ENABLE_MEMORY_LOG=1`.
Each `/log` request drains the current buffer, so the next request only returns newer messages.

## Sink and Stream Control

Sinks consume audio output from streams. Each stream can use its own sink provider.

### Available Sinks and Streams

`GET /sinks` example:

```json
{"sinks": ["None", "NullAudioSink"]}
```

`GET /streams` example:

```json
{"streams": [{"name": "Radio", "sink": "None", "running": false}]}
```

Fields:
- `name`: stream name
- `sink`: currently assigned sink provider
- `running`: whether that audio pipeline is active

### Assign a Sink

`POST /sink/select` body:

```json
{"stream": "Radio", "sink": "NullAudioSink"}
```

Success response:

```json
{"status": "ok", "stream": "Radio", "sink": "NullAudioSink"}
```

Typical flow with `NullAudioSink`:

```bash
# 1. Tell NullAudioSink which stream to consume
curl -X POST http://localhost:8080/module/NullAudioSink/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"select","args":"Radio"}'

# 2. Assign the sink provider to the stream
curl -X POST http://localhost:8080/sink/select \
  -H 'Content-Type: application/json' \
  -d '{"stream":"Radio","sink":"NullAudioSink"}'
```

## VFO Control

`GET /vfo/set_offset?name=<vfo>&offset=<hz>` shifts a VFO relative to the source center frequency.

Example response:

```json
{"status": "ok", "vfo": "Radio", "offset_hz": -253405.0}
```

Use this for prerecorded IQ/baseband when the desired signal is not at the file center.
This is a core endpoint and works for any VFO by instance name.

## Module Automation

### Module Listing

`GET /modules` returns loaded instances and module types:

```json
{
  "File Source": "file_source",
  "NullAudioSink": "null_audio_sink",
  "Radio": "radio"
}
```

### Module Command Channel

Use `POST /module/<instance_name>/command` with JSON:

```json
{"cmd": "set_filename", "args": "/path/to/file.wav"}
```

Or `GET /module/<instance_name>/command?cmd=...&args=...` for simple cases.

This channel is generic. A module must implement `handleDebugCommand` to support it.

### Common Commands by Module

`Radio` instance:

| Command | Args | Description |
|---------|------|-------------|
| `set_demod` | demod name or ID | Switch demodulator |
| `get_demod` | none | Return active demodulator name and ID |
| `set_freq` | frequency in Hz | Tune the Radio module VFO frequency |
| `get_spectrum` | bin count | Return spectrum bins as JSON |

`File Source` instance:

| Command | Args | Description |
|---------|------|-------------|
| `set_filename` | absolute path to WAV/IQ file | Load file source input before `/sdr/start` |
| `get_filename` | none | Return current filename |

`NullAudioSink` instance:

| Command | Args | Description |
|---------|------|-------------|
| `select` | stream name | Select which stream to consume |
| `get_samples` | none | Return monotonically increasing sample counter |
| `get_status` | none | Return sample count, sample rate, active status |

`TETRA Demodulator` instance:

| Command | Args | Description |
|---------|------|-------------|
| `get_status` | none | Return decoder state and sync/quality fields |
| `set_mode` | `0` or `1` | Switch decoder mode |

`Extra V/UHF` instance:

| Command | Args | Description |
|---------|------|-------------|
| `get_dmr_status` | VFO/stream name | Return current DMR-specific status for the selected injected DSD path |
| `wait_dmr_sync_voice` | `name,min_ms,timeout_ms` | Wait for stable accumulated DMR sync/voice before timing out |

### ProcFS (/proc) Endpoint System

SDR++ provides a /proc-style filesystem-like API for reading and writing to module states. All requests are queued and processed in the UI loop (like ImGui actions), ensuring thread safety.

**Core API** (in `http_debug_server.h`):

```cpp
namespace httpdebug::procfs {
    // Register a custom handler
    int registerHandler(const std::string& path, Handler handler);

    // Convenience: register a boolean value (auto GET/POST)
    int registerBool(const std::string& path, bool* value);

    // Convenience: register an integer (read-only if readOnly=true)
    int registerInt(const std::string& path, int* value, bool readOnly = false);

    // Convenience: register a float (read-only if readOnly=true)
    int registerFloat(const std::string& path, float* value, bool readOnly = false);

    // Register a container with dynamic children
    int registerContainer(const std::string& path, ListChildren listChildren, ContainerHandler handler);

    // Unregister by path or handle
    void unregister(const std::string& path);
    void unregister(int handle);

    // List all registered endpoints
    std::vector<std::string> list();
}
```

**Module Auto-Registration:**

All module instances are automatically registered at `/proc/modules/<moduleName>/<instanceName>`. Modules can expose custom handlers by implementing `getInterface("httpEndpoint")`.

**Source Management Endpoints:**

| Endpoint | Description |
|----------|-------------|
| `GET /proc/source/type` | Get current source type (e.g., "File", "RTL-SDR") |
| `POST /proc/source/type` | Set source type (triggers source selection) |
| `GET /proc/source/type:options` | Get JSON array of available source types |
| `GET /proc/source/<param>` | Get source-specific parameter (varies by source) |
| `POST /proc/source/<param>` | Set source-specific parameter |
| `GET /proc/source/<param>:options` | Get JSON array of valid options for parameter |

Source modules register their own endpoints when selected. The `:options` endpoint provides valid values (e.g., available files for File source, device IDs for hardware sources).

**Example Usage:**

```bash
# List all registered endpoints
curl http://localhost:8080/proc

# List all module instances
curl http://localhost:8080/modules

# Get available source types
curl http://localhost:8080/proc/source/type:options

# Get current source type
curl http://localhost:8080/proc/source/type

# Set source type (e.g., to "File")
curl -X POST http://localhost:8080/proc/source/type -d "File"

# Get available files for File source
curl http://localhost:8080/proc/source/filename:options

# Read module info (auto-registered for all modules)
curl 'http://localhost:8080/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse'
# Returns: {"module": "noise_reduction_logmmse", "instance": "...", "hasEndpoints": false}

# Access radio module (if has custom handlers)
curl http://localhost:8080/proc/modules/radio/Radio
```

### Usage Example

```bash
/Users/san/Fun/SDRPlusPlus/root_dev/inst/bin/sdrpp -r /Users/san/Fun/SDRPlusPlus/root_dev --http 8080
# Then access http://localhost:8080/status
```

## EmbeddableWebServer (EWS) Nuances

**Important:** The header file at `core/src/EmbeddableWebServer.h` contains a hardcoded `#define EWS_HEADER_ONLY` (line 72) that breaks the build because EWS_HEADER_ONLY excludes all implementation!

**Solution:** Do NOT define EWS_HEADER_ONLY - include the header directly in `http_debug_server_impl.cpp` without it. The header uses `#ifndef EWS_HEADER_ONLY` to guard implementation, so without the define, all functions are included with `static` linkage. A separate `.cpp` file must compile the header to provide the implementation.

## SDR++ CLI Tool

A convenience script for managing SDR++ during development and testing.

**Location:** `./sdrpp-cli` (in project root)

**Commands:**

- `./sdrpp-cli build` - Rebuild SDR++ and install to `root_dev/inst/` (agent-friendly, no tail/grep needed)
- `./sdrpp-cli start` - Start SDR++ with HTTP debug server on port 8080 (LLM-friendly, no sleep needed)
- `./sdrpp-cli stop` - Stop SDR++ (reports "Was not running" if not running)
- `./sdrpp-cli status` - Returns "up" or "down"

**Key Notes:**
- Always use `./sdrpp-cli build` instead of running cmake/make directly

### Usage in Edit/Test/Debug Loop

```bash
# Build and test changes
./sdrpp-cli build
./sdrpp-cli start
curl http://localhost:8080/status
./sdrpp-cli stop
```

### Key Notes

- Uses a clean config directory (`/tmp/sdrpp_config`) to avoid loading user settings
- Log file: `/tmp/sdrpp_config/sdrpp.log`
- HTTP debug server runs on port 8080 when started
- The `mainLoopStarted` flag in `/status` endpoint indicates the app is fully initialized
- Uses `nohup` to run in background, preventing signal propagation to child process

## Debug Loop Pattern

The typical debugging workflow:

1. **Start the app** with HTTP debug server:
   ```
   SDRPP_ENABLE_MEMORY_LOG=1 ./sdrpp-cli start
   ```

2. **Check status** via HTTP endpoint:
   ```
   curl http://localhost:8080/status
   ```

3. **Check logs** for errors:
   ```
   cat /tmp/sdrpp_config/sdrpp.log
   ```

4. **Interact via HTTP** (list windows, click, keypress, start/stop SDR, etc.):
   ```
   curl http://localhost:8080/layout
   curl http://localhost:8080/sdr/start   # equivalent to ▶ Play button in GUI
   curl http://localhost:8080/sdr/stop
    curl http://localhost:8080/log         # returns current log batch and clears it
    ```
    On desktop, `/log` is for manual agentic research and requires `SDRPP_ENABLE_MEMORY_LOG=1` at launch.
    **Important:** When using `File Source`, you must configure the input file first:
   ```
   curl -X POST http://localhost:8080/module/File%20Source/command -d '{"cmd":"set_filename","args":"/path/to/file.wav"}'
   ```
   Otherwise `/sdr/start` will have no effect.

5. **Stop and rebuild** if needed:
   ```
   ./sdrpp-cli stop
   ./sdrpp-cli build
   ```

The loop is: start → check status/logs → reproduce bug → stop → fix → rebuild → repeat.

## GUI Automation

These endpoints queue actions onto the UI/render thread.

- `GET /windows`: list ImGui windows with names, ids, positions, sizes
- `GET /layout`: dump simplified UI layout and viewport size
- `GET /click?x=<float>&y=<float>`: queue a mouse click
- `GET /mouse?x=<float>&y=<float>`: queue a mouse move
- `GET /key?key=<int>`: queue a key press
- `GET /type?text=<string>`: queue text input
- `GET /clickid?id=<int>`: queue a click by ImGui widget id

## Lifecycle

`GET /stop` or `GET /exit` gracefully shuts down SDR++.

Example response:

```json
{"status": "exiting"}
```

## LLDB for Crashes and Locks

**Always use lldb when SDR++ crashes or hangs.** Do not guess from logs.

### Crash: Delayed-Trigger Pattern

Use `--debug-wait` + delayed background curl to trigger crash after lldb attaches. **Never use interactive lldb** — use a script file:

```bash
kill $(pgrep -f sdrpp) 2>/dev/null; sleep 1
rm -f /tmp/sdrpp_debug_ready
SDRPP_ENABLE_MEMORY_LOG=1 DYLD_LIBRARY_PATH=.../core \
  ./sdrpp -r ./root_dev --http 8080 --debug-wait /tmp/sdrpp_debug_ready \
  > root_dev/sdrpp.log 2>&1 &
SPID=$!; sleep 1; touch /tmp/sdrpp_debug_ready; sleep 8

# Pre-condition system
curl -s -X POST http://localhost:8080/module/Radio/disable; sleep 2

# lldb script file
cat > /tmp/lldb_script.txt << 'EOF'
breakpoint set --name abort
continue
bt all
frame select 0
frame variable
frame select 1
frame variable
frame select 2
frame variable
quit
EOF

# Delayed trigger (background) → lldb (foreground) catches crash
(sleep 8 && curl -s -X POST http://localhost:8080/module/Radio/enable) &
lldb -p $SPID -s /tmp/lldb_script.txt 2>&1
```

### Deadlock/Hang

```bash
lldb -p $(pgrep -f sdrpp)
(lldb) thread interrupt
(lldb) bt all          # look for __psynch_cvwait / __psynch_mutexwait
(lldb) frame select N  # pick blocked thread
(lldb) frame variable
```

### Key Commands

| Command | Use |
|---------|-----|
| `bt all` | all thread backtraces |
| `frame select N` + `frame variable` | locals at frame |
| `thread list` | list all threads |
| `breakpoint set --name abort` | catch abort() |
| `frame variable --ptr *this` | print object |
| `frame variable myVec.__size_` | vector size |

### Real Example: SIGABRT on Radio Enable/Disable

Crash at `radio_module.h:422` — `ifSplitter.init()` called twice because `disable()` calls `stop()` (keeps `_block_init=true`) but `enable()` called `init()` unconditionally. Fix: guard with `hasInput()`, use `setInput()` on re-enable.

### Real Example: AudioSink CoreAudio Deadlock

Two AudioSink instances. `doStop()` on wrong instance → `stopWriter()` set `writerStop` on wrong stream → primary stuck in `swapCV.wait()` → `audio2.closeStream()` deadlocks on `HALB_Mutex`. Fix: shared static `audio2` with refcounting.

## Complete Example

TETRA pipeline example:

```bash
# 1. Wait for server readiness
curl -s http://localhost:8080/status

# 2. Tell NullAudioSink to consume TETRA stream
curl -X POST http://localhost:8080/module/NullAudioSink/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"select","args":"TETRA Demodulator"}'

# 3. Select File source
curl -X POST http://localhost:8080/proc/source/type -d 'File'

# 4. Load baseband file
curl -X POST http://localhost:8080/module/File%20Source/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"set_filename","args":"/path/to/baseband.wav"}'

# 5. Tune the TETRA VFO within the file
curl "http://localhost:8080/vfo/set_offset?name=TETRA%20Demodulator&offset=-686597"

# 6. Assign sink provider to that stream
curl -X POST http://localhost:8080/sink/select \
  -H 'Content-Type: application/json' \
  -d '{"stream":"TETRA Demodulator","sink":"NullAudioSink"}'

# 7. Start playback
curl -s http://localhost:8080/sdr/start

# 8. Check decoder status
curl -X POST http://localhost:8080/module/TETRA%20Demodulator/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"get_status"}'

# 9. Check audio flow
curl -X POST http://localhost:8080/module/NullAudioSink/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"get_status"}'

# 10. Stop playback and app
curl -s http://localhost:8080/sdr/stop
curl -s http://localhost:8080/stop
```

## EXAMPLE bugfix workflow

Use this exact order when fixing SDR++ runtime bugs:

1. **Launch manually first**
   ```bash
   ./sdrpp-cli build
   ./sdrpp-cli start
   curl http://localhost:8080/status
   ```
   Prefer a real running SDR++ process over a Python test at the beginning.
   Manual launch and Python tests must target the same build artifacts.

2. **Reproduce only through HTTP/manual inspection**
   Use curl, `/status`, `/modules`, `/streams`, `/log`, module commands, and procfs endpoints.
   On desktop, enable `/log` explicitly with `SDRPP_ENABLE_MEMORY_LOG=1` when launching SDR++.
   Example:
   ```bash
   curl -X POST http://localhost:8080/module/File%20Source/command \
     -H 'Content-Type: application/json' \
     -d '{"cmd":"set_filename","args":"/path/to/file.wav"}'
   curl -X POST http://localhost:8080/module/Radio/command \
     -H 'Content-Type: application/json' \
     -d '{"cmd":"set_demod","args":"DSD"}'
   curl "http://localhost:8080/vfo/set_offset?name=Radio&offset=-253405"
   curl -X POST http://localhost:8080/module/Extra%20V%2FUHF/command \
     -H 'Content-Type: application/json' \
     -d '{"cmd":"wait_dmr_sync_voice","args":"Radio,2000,10000"}'
   curl http://localhost:8080/sdr/start
   ```
   Reasoning behind each option:
   - `SDRPP_ENABLE_MEMORY_LOG=1`: enables the in-memory `/log` buffer on desktop. Without it, `/log` is intentionally unavailable.
   - `module/File%20Source/command`: targets the `File Source` instance specifically. `%20` is required because the instance name contains a space.
   - `cmd=set_filename`: loads the recording into the source module. `/sdr/start` is not enough by itself; the file source must already know which file to play.
   - `args=/path/to/file.wav`: absolute path is safest for reproducible debugging and avoids dependency on the current working directory.
   - `module/Radio/command`: targets the Radio instance where demodulator selection actually happens.
   - `cmd=set_demod`: switches the Radio module to the injected demodulator by name. This is the public automation hook for demod selection.
   - `args=DSD`: selects the DSD demod that `Extra V/UHF` injects into Radio. This is the real DMR-capable path in this codebase.
   - `/vfo/set_offset?name=Radio&offset=-253405`: retunes within the recorded baseband without changing the file center frequency. For prerecorded IQ, this is usually the correct control when the signal is not at the recording center.
   - `name=Radio`: applies the offset to the Radio VFO, not to the source.
   - `offset=-253405`: example offset that moves from file center `144553405 Hz` to the actual DMR signal near `144300000 Hz`.
   - `module/Extra%20V%2FUHF/command`: uses the decoder-owning module for DMR-specific state instead of polluting the generic Radio interface.
   - `cmd=wait_dmr_sync_voice`: asks for a stable DMR-specific success condition rather than a single noisy snapshot.
   - `args=Radio,2000,10000`: `Radio` selects which injected DSD instance to inspect, `2000` requires about 2 seconds of accumulated DMR voice/sync, and `10000` caps total wait at about 10 seconds.
   - `/sdr/start`: starts playback after source, demodulator, and VFO are correctly configured. Starting earlier can produce misleading results.
   For DMR baseband recordings, changing the source frequency is often the wrong control.
   Keep the file's center frequency and move the Radio VFO with `/vfo/set_offset` to the actual signal.

3. **Capture evidence of the bug or incorrect behavior**
   Examples:
   - crash / process exit
   - wrong HTTP response
   - debug command missing or returning `{}`
   - `/log` missing expected request trace
   - sample counters not moving
   - wrong mode / wrong file type
   - missing sync / missing recorder output
   - `/log` output showing the failure

4. **Only after reproducing manually, write the Python regression test**
   The first version of the test should describe the observed bug, not the future fix.
   If the bug is timing-sensitive, add a stable wait mechanism rather than a one-shot poll.

5. **Commit the test before the fix**
   Use the commit message format:
   ```
   bug found - test case created before fixing (test_name.py)
   ```

6. **Implement the fix**
   Keep the fix minimal and scoped to the reproduced issue.
   Do not redesign unrelated interfaces if a module-specific debug hook is enough.
   Example from this session:
   - do **not** add DMR-specific status to `Radio`
   - add DMR-specific stable wait/status commands to `Extra V/UHF` instead

7. **Launch manually again and verify the real behavior**
   Repeat the exact curl/manual reproduction steps against the fixed build.
   Confirm the original failure is gone on a live SDR++ instance.
   If the bug was timing-sensitive, verify with both:
   - direct status snapshot command
   - stable wait command

8. **Update the Python test to assert the fixed behavior**
   Change the test from “bug is observable” to “correct behavior is enforced”.
   The test harness now defaults to this repo's local `build/` target. If you
   need to override it, point the framework explicitly to the same build with:
   ```bash
   E2E_BUILD_DIR=/path/to/build \
   E2E_ROOT_DEV=/path/to/build/root_dev \
   E2E_BINARY=/path/to/build/sdrpp \
   python3 e2e/test_name.py
   ```

9. **Run the test again and make the second commit**
   The second commit should contain:
   - the product fix
   - the updated passing test
   - any supporting debug documentation changes

10. **Important practical notes**
   - The manual launch and the Python test must use the same build target.
   - For baseband recordings, do not assume the signal is at the file center frequency; use VFO offset when needed.
   - For the Recorder module, verify whether mode is baseband or audio from code, not assumption.
   - In this codebase, `Recorder` uses `mode=0` for baseband and `mode=1` for audio.
   - If decoder state flickers with signal quality, expose a stable wait command instead of polling a single frame.
   - Python test helpers can hide real issues if they use the wrong build target or too-short HTTP timeouts.
   - `/log` on desktop is for manual agentic research only when launched with `SDRPP_ENABLE_MEMORY_LOG=1`; each read drains the current batch.
