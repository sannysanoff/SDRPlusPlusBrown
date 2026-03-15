#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <filesystem>

#ifdef __cplusplus
#include "imgui.h"
#include "imgui_internal.h"
#endif

// Forward declarations for EmbeddableWebServer types
struct Server;
struct Request;
struct Connection;
struct Response;

// EWS functions - implemented in http_debug_server_impl.cpp
Server* serverInitWrapper();
void serverDeInitWrapper(Server* server);
void serverStopWrapper(Server* server);
int acceptConnectionsWrapper(Server* server, uint16_t port);

Response* responseAllocJSON(const char* json);
Response* responseAllocJSONWithFormat(const char* format, ...);
Response* responseAlloc404NotFoundHTML(const char* resourcePathOrNull);
char* strdupDecodeGETParam(const char* paramNameIncludingEquals, const struct Request* request, const char* valueIfNotFound);

extern "C" {
struct Response* createResponseForRequest(const struct Request* request, struct Connection* connection);
}

namespace httpdebug {

    inline Server* httpServer = nullptr;
    inline std::thread* ewsThread = nullptr;
    inline std::atomic<bool> httpServerListening{ false };
    inline std::atomic<bool> serverReady{ false };
    inline std::atomic<bool> mainLoopStarted{ false };
    inline std::atomic<bool> shouldExit{ false };

    void startHttpServer(int port);
    void stopHttpServer();
    void signalReady();
    bool isReady();
    void waitForDebugCommand(const std::string& readyFile);
    void signalMainLoopStarted();
    void stopApp();

    inline std::atomic<bool> sdrStartRequest{ false };
    inline std::atomic<bool> sdrStopRequest{ false };
    inline std::atomic<bool> sdrPlaying{ false };

    inline void requestSdrStart() {
        sdrStartRequest.store(true, std::memory_order_release);
    }
    inline void requestSdrStop() {
        sdrStopRequest.store(true, std::memory_order_release);
    }
    inline bool getSdrStartRequest() {
        return sdrStartRequest.exchange(false, std::memory_order_acq_rel);
    }
    inline bool getSdrStopRequest() {
        return sdrStopRequest.exchange(false, std::memory_order_acq_rel);
    }
    inline void setSdrPlaying(bool playing) {
        sdrPlaying.store(playing, std::memory_order_release);
    }
    inline bool isSdrPlaying() {
        return sdrPlaying.load(std::memory_order_acquire);
    }

#ifdef __cplusplus

    struct ImGuiAction {
        enum Type { Click,
                    MouseMove,
                    KeyPress,
                    TypeText,
                    Focus,
                    ClickById } type;
        float x, y;
        int key;
        std::string text;
        ImGuiID targetId;
    };

    inline std::vector<ImGuiAction> pendingActions;
    inline std::mutex actionsMutex;

    void queueClick(float x, float y);
    void queueKeyPress(int key);
    void queueTypeText(const std::string& text);
    void queueMouseMove(float x, float y);
    void queueFocus(ImGuiID id);
    void queueClickById(ImGuiID id);
    bool popAction(ImGuiAction& out);
    std::string getAllWindowsJson();
    std::string getSimpleLayoutJson();

#endif // __cplusplus

} // namespace httpdebug