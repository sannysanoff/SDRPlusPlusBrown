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
| `GET /status` | Returns JSON with `ready`, `httpListening`, `mainLoopStarted` |
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

- `./sdrpp-cli start` - Start SDR++ with HTTP debug server on port 8080, waits for main loop to start
- `./sdrpp-cli stop` - Stop SDR++ (reports "Was not running" if not running)
- `./sdrpp-cli status` - Returns "up" or "down"
- `./sdrpp-cli build` - Rebuild SDR++ and install to `root_dev/inst/`

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