# SDR++Brown HTTP Debug Protocol

The HTTP debug server is started with `--http <port>` and provides a REST API for controlling SDR++ remotely. All endpoints return JSON responses unless marked otherwise.

---

## 1. Server Status

### `GET /status` (or `GET /`)
Server readiness and state.

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

## 3. Source Selection

### `POST /proc/source/type`
Select the active source module by name.

**Body:** plain text source type name, e.g. `File` or `Airspy`

```
File
```

**Response:**
```
ok
```

---

## 4. Sink Management

Sinks consume audio output from streams. Each registered stream (e.g. `"Radio"`, `"TETRA Demodulator"`) can have **its own** sink independently.

### `GET /sinks`
List all available audio sink providers.

```json
{"sinks": ["None", "NullAudioSink"]}
```

`"None"` is the built-in null sink (discards audio). Additional sink types (PulseAudio, PortAudio, etc.) appear here when their modules are loaded.

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

**Error — unknown sink provider** *(from `setStreamSink` internals, logged only — stream stays on current sink)*

**Usage note:** The NullAudioSink module exposes a `select` command via the generic module channel (`/module/NullAudioSink/command`) which tells it which stream to listen to. The `/sink/select` endpoint handles the core SinkManager assignment. Typical flow:

```bash
# 1. Tell NullAudioSink which stream to consume
curl -X POST http://localhost:8081/module/NullAudioSink/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"select","args":"TETRA Demodulator"}'

# 2. Assign the sink provider to the stream (core API)
curl -X POST http://localhost:8081/sink/select \
  -H 'Content-Type: application/json' \
  -d '{"stream":"TETRA Demodulator","sink":"NullAudioSink"}'
```

---

## 5. VFO Control

### `GET /vfo/set_offset`
Set the center frequency offset of any VFO by its instance name. This shifts the VFO's tuned frequency relative to the source center frequency.

**Query params:**

| Param | Required | Description |
|-------|----------|-------------|
| `name` | Yes | VFO/instance name (URL-encoded, e.g. `TETRA%20Demodulator`) |
| `offset` | No (default: `0`) | Offset in Hz from source center frequency. Negative = lower frequency. |

**Example — tune TETRA VFO to signal at ~468.125 MHz (source center is 468.811597 MHz):**
```
GET /vfo/set_offset?name=TETRA%20Demodulator&offset=-686597
```

**Response:**
```json
{"status": "ok", "vfo": "TETRA Demodulator", "offset_hz": -686597.000000}
```

This is a **core** endpoint — it works for any VFO regardless of which module owns it. The VFO name is the same as the module instance name.

---

## 6. Module Automation

### `GET /modules`
List all loaded module instances and their module types.

```json
{
  "NullAudioSink": "null_audio_sink",
  "File Source": "file_source",
  "TETRA Demodulator": "ch_tetra_demodulator"
}
```

### `POST /module/<instance_name>/command`
Send a command to any module that implements `handleDebugCommand`. The channel is generic — modules opt in by overriding the virtual method.

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

### Commands by module

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
| `get_samples` | *(none)* | Returns the sample counter value (monotonically increasing) |
| `get_status` | *(none)* | Returns status: samples count, sample rate, active status |

**TETRA Demodulator module** (instance name: `"TETRA Demodulator"`):

| Command | Args | Description |
|---------|------|-------------|
| `get_status` | *(none)* | Returns decoder state, sync status, signal quality, MCC/MNC, hyperframe timing, voice service flag |
| `set_mode` | `0` or `1` | Switch decoder mode (0 = osmo-tetra, 1 = network symbols) |

---

## 7. Procfs Endpoints

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

---

## 8. GUI Automation (ImGui Interaction)

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

## 9. Lifecycle

### `GET /stop` (or `GET /exit`)
Gracefully shut down SDR++.

```json
{"status": "exiting"}
```

---

## Complete Example — Automated TETRA Test Pipeline

```bash
# 1. Wait for server readiness
curl -s http://localhost:8080/status

# 2. Select NullAudioSink → consume TETRA stream
curl -X POST http://localhost:8080/module/NullAudioSink/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"select","args":"TETRA Demodulator"}'
# → {"status": "ok", "stream": "TETRA Demodulator"}

# 3. Select File source
curl -X POST http://localhost:8080/proc/source/type -d 'File'
# → ok

# 4. Load WAV file
curl -X POST http://localhost:8080/module/File%20Source/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"set_filename","args":"/path/to/baseband.wav"}'
# → {"status": "ok", "filename": "/path/to/baseband.wav"}

# 5. Tune TETRA VFO to signal frequency
curl "http://localhost:8080/vfo/set_offset?name=TETRA%20Demodulator&offset=-686597"
# → {"status": "ok", "vfo": "TETRA Demodulator", "offset_hz": -686597.0}

# 6. Assign sink provider to TETRA stream
curl -X POST http://localhost:8080/sink/select \
  -H 'Content-Type: application/json' \
  -d '{"stream":"TETRA Demodulator","sink":"NullAudioSink"}'
# → {"status": "ok", "stream": "TETRA Demodulator", "sink": "NullAudioSink"}

# 7. Start playback
curl -s http://localhost:8080/sdr/start
# → {"action": "sdr_start"}

# 8. Monitor TETRA status
curl -X POST http://localhost:8080/module/TETRA%20Demodulator/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"get_status"}'
# → {"decoder_state": "locked", "sync": true, "mcc": 250, "mnc": 13, ...}

# 9. Check audio flow
curl -X POST http://localhost:8080/module/NullAudioSink/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"get_status"}'
# → {"samples": 6048960, "sample_rate": 48000, "status": "active"}

# 10. Stop
curl -s http://localhost:8080/sdr/stop
curl -s http://localhost:8080/stop
```
