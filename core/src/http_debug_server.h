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

    inline void startHttpServer(int port);
    inline void stopHttpServer();
    inline void signalReady();
    inline bool isReady();
    inline void waitForDebugCommand(const std::string& readyFile);
    inline void signalMainLoopStarted();
    inline void stopApp();

#ifdef __cplusplus

    struct ImGuiAction {
        enum Type { Click,
                    MouseMove,
                    KeyPress,
                    TypeText,
                    Focus } type;
        float x, y;
        int key;
        std::string text;
        ImGuiID targetId;
    };

    inline std::vector<ImGuiAction> pendingActions;
    inline std::mutex actionsMutex;

    inline void queueClick(float x, float y);
    inline void queueKeyPress(int key);
    inline void queueTypeText(const std::string& text);
    inline void queueMouseMove(float x, float y);
    inline void queueFocus(ImGuiID id);
    inline bool popAction(ImGuiAction& out);
    inline std::string getAllWindowsJson();

#endif // __cplusplus

} // namespace httpdebug