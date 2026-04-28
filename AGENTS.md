# Debugging Guide for SDR++

This document provides debugging and remote control capabilities for SDR++.

## HTTP Debug Server

SDR++ includes an embedded HTTP debug server from [EmbeddableWebServer](https://github.com/hellerf/EmbeddableWebServer) for debugging and remote control.

### Command Line Options

- `--http <port>` - Start HTTP server on port (0 to disable, default 8080)
- `--debug-wait <file>` - Wait for file to exist before continuing (for debugger attachment)

### Endpoints Overview

| Endpoint | Description |
|----------|-------------|
| `GET /status` | Server readiness and state |
| `GET /stop` or `/exit` | Stop the application |
| `GET /sdr/start` | Start SDR playback |
| `GET /sdr/stop` | Stop SDR playback |
| `GET /sdr/status` | Get SDR playing status (true/false) |
| `GET /sinks` | List available audio sink providers |
| `GET /streams` | List registered audio streams |
| `POST /sink/select` | Assign sink to a stream |
| `GET /vfo/set_offset` | Set VFO offset by instance name |
| `GET /modules` | List all module instances |
| `POST /module/<instance>/command` | Send command to a module |
| `GET /proc` | List all registered procfs endpoints |
| `GET /proc/<path>` | Read from a procfs endpoint |
| `POST /proc/<path>` | Write to a procfs endpoint |
| `GET /windows` | List ImGui windows (name, id, position, size) |
| `GET /layout` | Dump UI layout with all windows |
| `GET /click?x=<>&y=<>` | Queue mouse click at coordinates |
| `GET /clickid?id=<>` | Click on element by ImGui ID |
| `GET /mouse?x=<>&y=<>` | Queue mouse move to coordinates |
| `GET /key?key=<>` | Queue key press (GLFW key code) |
| `GET /type?text=<>` | Queue text input |

---

## 1. Server Status

### `GET /status` (or `GET /`)
Returns JSON with server readiness and state.

```json
{"ready": true, "httpListening": true, "mainLoopStarted": true}
```

| Field | Description |
|-------|-------------|
| `ready` | Server is initialized and accepting requests |
| `httpListening` | HTTP thread is active |
| `mainLoopStarted` | SDR++ main render/processing loop is running |

---

## 2. SDR Playback Control

### `GET /sdr/start`
Start SDR playback (source → demod → audio).

```json
{"action": "sdr_start"}
```

### `GET /sdr/stop`
Stop SDR playback.

```json
{"action": "sdr_stop"}
```

### `GET /sdr/status`
Check if SDR is currently playing.

```json
{"playing": true}
```

---

## 3. Sink Management

Sinks consume audio output from streams. Each registered stream (e.g., `"Radio"`, `"TETRA Demodulator"`) can have **its own** sink independently.

### `GET /sinks`
List all available audio sink providers.

```json
{"sinks": ["None", "NullAudioSink"]}
```

`"None"` is the built-in null sink (discards audio). Additional sink types appear when their modules are loaded.

### `GET /streams`
List all registered audio streams and their current sink assignments.

```json
{"streams": [
  {"name": "Radio", "sink": "None", "running": false}
]}
```

| Field | Description |
|-------|-------------|
| `name` | Stream name (matches module instance name that created it) |
| `sink` | Currently assigned sink provider name |
| `running` | Whether the stream's audio pipeline is active |

### `POST /sink/select`
Assign a sink provider to a specific stream. **Different streams can use different sinks.**

**Body (JSON):**
```json
{"stream": "Radio", "sink": "NullAudioSink"}
```

| Param | Default | Description |
|-------|---------|-------------|
| `stream` | `"Radio"` | Stream name to target |
| `sink` | `"None"` | Sink provider name to assign |

**Success response:**
```json
{"status": "ok", "stream": "Radio", "sink": "NullAudioSink"}
```

**Error — unknown stream:**
```json
{"error": "stream 'Bogus' not found"}
```

---

## 4. VFO Control

### `GET /vfo/set_offset`
Set the center frequency offset of any VFO by its instance name. This shifts the VFO's tuned frequency relative to the source center frequency.

**Query params:**

| Param | Required | Description |
|-------|----------|-------------|
| `name` | Yes | VFO/instance name (URL-encoded, e.g., `TETRA%20Demodulator`) |
| `offset` | No (default: `0`) | Offset in Hz from source center frequency. Negative = lower frequency. |

**Example:**
```
GET /vfo/set_offset?name=TETRA%20Demodulator&offset=-686597
```

**Response:**
```json
{"status": "ok", "vfo": "TETRA Demodulator", "offset_hz": -686597.0}
```

This is a **core** endpoint — it works for any VFO regardless of which module owns it.

---

## 5. Module Automation

### `GET /modules`
List all loaded module instances and their module types.

```json
{
  "NullAudioSink": "null_audio_sink",
  "File Source": "file_source",
  "Radio": "radio"
}
```

### `POST /module/<instance_name>/command`
Send a command to any module that implements `handleDebugCommand`.

**Request body (JSON):**
```json
{"cmd": "set_filename", "args": "/path/to/file.wav"}
```

| Field | Description |
|-------|-------------|
| `cmd` | Command name |
| `args` | Command argument string |

### `GET /module/<instance_name>/command`
Same as POST, but use query params:
```
GET /module/File%20Source/command?cmd=set_filename&args=/path/to/file.wav
```

### Commands by Module

**Radio module** (instance name: `"Radio"`):

| Command | Args | Description |
|---------|------|-------------|
| `set_demod` | demod name or ID | Switch demodulator. Accepts display names (`"FM"`, `"AM"`, `"NFM"`, `"WFM"`, `"LSB"`, `"USB"`, `"DSB"`) or numeric ID |
| `get_demod` | *(none)* | Returns active demodulator name and ID |
| `set_freq` | frequency in Hz | Tune the Radio module's VFO frequency |
| `get_spectrum` | bin count | Returns spectrum data as JSON array of magnitude values |

**File Source module** (instance name: `"File Source"`):

| Command | Args | Description |
|---------|------|-------------|
| `set_filename` | absolute path to WAV file | Load a WAV/IQ file as source |
| `get_filename` | *(none)* | Returns the currently loaded filename |

**NullAudioSink module** (instance name: `"NullAudioSink"`):

| Command | Args | Description |
|---------|------|-------------|
| `select` | stream name | Select which stream's audio to consume |
| `get_samples` | *(none)* | Returns the sample counter value |
| `get_status` | *(none)* | Returns status: samples count, sample rate, active status |

**Frequency Manager module** (instance name: `"Frequency Manager"`):

| Command | Args | Description |
|---------|------|-------------|
| `get_lists` | *(none)* | Returns array of all bookmark list names |
| `get_current_list` | *(none)* | Returns the currently selected list name |
| `set_current_list` | list name | Switch to a different bookmark list |
| `get_bookmarks` | *(none)* | Returns all bookmarks in current list |
| `add_bookmark` | `name\|freq\|bw\|mode` | Add bookmark. Args format: `name\|frequency\|bandwidth\|mode` |
| `remove_bookmark` | bookmark name | Remove bookmark by name from current list |
| `apply_bookmark` | bookmark name | Tune to bookmark frequency/bandwidth/mode |
| `start_scanner` | *(none)* | Start scanning bookmarks |
| `stop_scanner` | *(none)* | Stop scanner |
| `get_scanner_status` | *(none)* | Returns scanning state, current station, bookmark count |

---

## 6. ProcFS Endpoints

The procfs system provides lightweight read/write endpoints registered by modules.

### `GET /proc`
List all registered procfs endpoints.

```json
["/source/type", "/sink/select", "/frequency", "/play/start", "/play/stop"]
```

### `GET /proc/<path>`
Read a procfs endpoint value. Response is plain text.

### `POST /proc/<path>`
Write a value to a procfs endpoint. Body is plain text.

```
POST /proc/source/type
Body: File
```

Returns `ok` on success.

### Core API (in `http_debug_server.h`)

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

---

## 7. GUI Automation (ImGui Interaction)

These endpoints allow automating the graphical interface. They queue actions that are processed on the main render thread.

### `GET /windows`
List all ImGui windows with positions and sizes.

```json
{"windows": [
  {"name": "Source Selector", "id": 12345, "x": 10.0, "y": 20.0, "w": 200.0, "h": 300.0}
]}
```

### `GET /layout`
Get simplified layout of all UI elements (windows, popups, widgets).

```json
{"elements": [
  {"type": "window", "name": "Source Selector", "id": 12345, "x": 10.0, "y": 20.0, "w": 200.0, "h": 300.0}
], "viewport": {"w": 1280, "h": 720}}
```

### `GET /click?x=<float>&y=<float>`
Queue a mouse click at absolute coordinates.

### `GET /mouse?x=<float>&y=<float>`
Queue a mouse move to absolute coordinates.

### `GET /key?key=<int>`
Queue a keyboard key press (ImGuiKey value).

### `GET /type?text=<string>`
Queue text input (URL-encoded).

### `GET /clickid?id=<int>`
Queue a click by ImGui widget ID.

### `GET /ls`
List all registered procfs endpoints with their current values, types, and writability.

```json
[
  {"path": "/source/type", "value": "None", "type": "string", "writable": true}
]
```

---

## 8. Lifecycle

### `GET /stop` (or `GET /exit`)
Gracefully shut down SDR++.

```json
{"status": "exiting"}
```

---

## Usage Example

```bash
/Users/san/Fun/SDRPlusPlus/root_dev/inst/bin/sdrpp_brown -r /Users/san/Fun/SDRPlusPlus/root_dev --http 8080
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

### Using Default User Configuration

To use your default user configuration instead of the clean test config, run SDR++ with `-r [SDRPlusPlusSourceRoot]/root_dev`:

```bash
# Use default user config (instead of /tmp/sdrpp_config)
/Users/san/Fun/SDRPlusPlus/root_dev/inst/bin/sdrpp_brown -r /Users/san/Fun/SDRPlusPlus/root_dev --http 8080
```

This loads settings from `~/Library/Application Support/sdrpp` (macOS), `~/.config/sdrpp` (Linux), or the platform-appropriate user config directory.

## Debug Loop Pattern

The typical debugging workflow:

1. **Start the app** with HTTP debug server:
   ```
   ./sdrpp-cli start
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
   curl http://localhost:8080/sdr/start
   curl http://localhost:8080/sdr/stop
   ```

5. **Stop and rebuild** if needed:
   ```
   ./sdrpp-cli stop
   ./sdrpp-cli build
   ```

The loop is: start → check status/logs → reproduce bug → stop → fix → rebuild → repeat.

## E2E Testing

The `e2e/` directory contains end-to-end tests that verify debug protocol functionality:

| Test File | Description |
|-----------|-------------|
| `e2e/test_lsb_startup.py` | Tests Radio module VFO bandwidth at startup |
| `e2e/test_frequency_manager.py` | Tests Frequency Manager debug commands (bookmarks, lists, scanner) |

Run tests with: `python3 e2e/test_frequency_manager.py`

### Adding Debug Commands to Modules

To add debug commands to a module:

1. Override `handleDebugCommand` in your module class
2. Return JSON string responses
3. Create an E2E test to verify functionality

Example from frequency_manager:
```cpp
std::string handleDebugCommand(const std::string& cmd, const std::string& args) override {
    if (cmd == "get_bookmarks") {
        json bms = json::array();
        for (const auto& [name, bm] : bookmarks) {
            bms.push_back({{"name", name}, {"frequency", bm.frequency}});
        }
        return json{{"bookmarks", bms}}.dump();
    }
    return json{{"error", "unknown command"}}.dump();
}
```