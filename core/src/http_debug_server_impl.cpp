// Must include EmbeddableWebServer.h FIRST, before anything else that might set EWS_HEADER_ONLY
#include "EmbeddableWebServer.h"

#include "http_debug_server.h"
#include <utils/flog.h>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>
#include <unordered_map>
#include <cstdarg>

#ifdef __cplusplus
#include "imgui.h"
#include "imgui_internal.h"
#endif

// Wrapper functions to expose EWS functionality
Server* serverInitWrapper() {
    Server* srv = (Server*)calloc(1, sizeof(Server));
    serverInit(srv);
    return srv;
}

void serverDeInitWrapper(Server* server) {
    serverDeInit(server);
}

void serverStopWrapper(Server* server) {
    serverStop(server);
}

int acceptConnectionsWrapper(Server* server, uint16_t port) {
    return acceptConnectionsUntilStoppedFromEverywhereIPv4(server, port);
}

// Define httpdebug namespace functions here
namespace httpdebug {

    void startHttpServer(int port) {
        if (port <= 0) {
            flog::info("HTTP debug server disabled");
            return;
        }

        httpServer = serverInitWrapper();

        flog::info("Starting HTTP debug server on port {}", port);

        ewsThread = new std::thread([port]() {
            httpServerListening.store(true, std::memory_order_release);
            acceptConnectionsWrapper(httpServer, (uint16_t)port);
        });
    }

    void stopHttpServer() {
        if (httpServer) {
            serverStopWrapper(httpServer);
            if (ewsThread && ewsThread->joinable()) {
                ewsThread->join();
            }
            serverDeInitWrapper(httpServer);
            free(httpServer);
            httpServer = nullptr;
        }
    }

    void signalReady() {
        serverReady.store(true, std::memory_order_release);
    }

    bool isReady() {
        return serverReady.load(std::memory_order_acquire);
    }

    void waitForDebugCommand(const std::string& readyFile) {
        if (readyFile.empty()) {
            return;
        }

        while (!std::filesystem::exists(readyFile)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        flog::info("Debug command received, continuing...");
    }

    void signalMainLoopStarted() {
        mainLoopStarted.store(true, std::memory_order_release);
    }

    void stopApp() {
        shouldExit.store(true, std::memory_order_release);
    }

#ifdef __cplusplus

    void queueClick(float x, float y) {
        std::lock_guard<std::mutex> lock(actionsMutex);
        pendingActions.push_back({ ImGuiAction::Click, x, y, 0, "", 0 });
    }

    void queueKeyPress(int key) {
        std::lock_guard<std::mutex> lock(actionsMutex);
        pendingActions.push_back({ ImGuiAction::KeyPress, 0, 0, key, "", 0 });
    }

    void queueTypeText(const std::string& text) {
        std::lock_guard<std::mutex> lock(actionsMutex);
        pendingActions.push_back({ ImGuiAction::TypeText, 0, 0, 0, text, 0 });
    }

    void queueMouseMove(float x, float y) {
        std::lock_guard<std::mutex> lock(actionsMutex);
        pendingActions.push_back({ ImGuiAction::MouseMove, x, y, 0, "", 0 });
    }

    void queueFocus(ImGuiID id) {
        std::lock_guard<std::mutex> lock(actionsMutex);
        pendingActions.push_back({ ImGuiAction::Focus, 0, 0, 0, "", id });
    }

    bool popAction(ImGuiAction& out) {
        std::lock_guard<std::mutex> lock(actionsMutex);
        if (pendingActions.empty()) return false;
        out = pendingActions.front();
        pendingActions.erase(pendingActions.begin());
        return true;
    }

    std::string getAllWindowsJson() {
        std::string result = "{\"windows\": [";
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        bool first = true;
        for (ImGuiWindow* window : ctx->Windows) {
            if (!first) result += ", ";
            result += "{";
            result += "\"name\": \"" + std::string(window->Name) + "\", ";
            result += "\"id\": " + std::to_string(window->ID) + ", ";
            result += "\"x\": " + std::to_string(window->Pos.x) + ", ";
            result += "\"y\": " + std::to_string(window->Pos.y) + ", ";
            result += "\"w\": " + std::to_string(window->Size.x) + ", ";
            result += "\"h\": " + std::to_string(window->Size.y);
            result += "}";
            first = false;
        }
        result += "]}";
        return result;
    }

    std::string getFullLayoutJson() {
        std::string result = "{\"layout\": {";
        ImGuiContext* ctx = ImGui::GetCurrentContext();

        result += "\"windows\": [";
        bool firstWin = true;
        for (ImGuiWindow* window : ctx->Windows) {
            if (!firstWin) result += ", ";
            result += "{";
            result += "\"name\": \"" + std::string(window->Name) + "\", ";
            result += "\"id\": " + std::to_string(window->ID) + ", ";
            result += "\"x\": " + std::to_string(window->Pos.x) + ", ";
            result += "\"y\": " + std::to_string(window->Pos.y) + ", ";
            result += "\"w\": " + std::to_string(window->Size.x) + ", ";
            result += "\"h\": " + std::to_string(window->Size.y);
            result += "}";
            firstWin = false;
        }
        result += "], ";

        result += "\"viewport\": {\"w\": " + std::to_string(ctx->IO.DisplaySize.x) + ", \"h\": " + std::to_string(ctx->IO.DisplaySize.y) + "}";
        result += "}}";
        return result;
    }

    std::string getSimpleLayoutJson() {
        std::string result = "{\"elements\": [";
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        bool first = true;

        for (ImGuiWindow* window : ctx->Windows) {
            if (!first) result += ", ";
            result += "{";
            result += "\"type\": \"window\", ";
            result += "\"name\": \"" + std::string(window->Name) + "\", ";
            result += "\"id\": " + std::to_string(window->ID) + ", ";
            result += "\"x\": " + std::to_string(window->Pos.x) + ", ";
            result += "\"y\": " + std::to_string(window->Pos.y) + ", ";
            result += "\"w\": " + std::to_string(window->Size.x) + ", ";
            result += "\"h\": " + std::to_string(window->Size.y);
            result += "}";
            first = false;
        }

        result += "], \"viewport\": {";
        result += "\"w\": " + std::to_string(ctx->IO.DisplaySize.x) + ", ";
        result += "\"h\": " + std::to_string(ctx->IO.DisplaySize.y);
        result += "}}";
        return result;
    }

    void queueClickById(ImGuiID id) {
        std::lock_guard<std::mutex> lock(actionsMutex);
        pendingActions.push_back({ ImGuiAction::ClickById, 0, 0, 0, "", id });
    }

#endif // __cplusplus

} // namespace httpdebug

// Implement createResponseForRequest here
struct Response* createResponseForRequest(const struct Request* request, struct Connection* connection) {
    if (strcmp(request->path, "/status") == 0 || strcmp(request->path, "/") == 0) {
        return responseAllocJSONWithFormat(
            "{\"ready\": %s, \"httpListening\": %s, \"mainLoopStarted\": %s}",
            httpdebug::serverReady.load() ? "true" : "false",
            httpdebug::httpServerListening.load() ? "true" : "false",
            httpdebug::mainLoopStarted.load() ? "true" : "false");
    }

#ifdef __cplusplus
    if (strcmp(request->path, "/windows") == 0) {
        return responseAllocJSON(httpdebug::getAllWindowsJson().c_str());
    }

    if (strcmp(request->path, "/click") == 0) {
        char* xParam = strdupDecodeGETParam("x=", request, "0");
        char* yParam = strdupDecodeGETParam("y=", request, "0");
        float x = (float)atof(xParam);
        float y = (float)atof(yParam);
        httpdebug::queueClick(x, y);
        free(xParam);
        free(yParam);
        return responseAllocJSON("{\"action\": \"click\"}");
    }

    if (strcmp(request->path, "/mouse") == 0) {
        char* xParam = strdupDecodeGETParam("x=", request, "0");
        char* yParam = strdupDecodeGETParam("y=", request, "0");
        float x = (float)atof(xParam);
        float y = (float)atof(yParam);
        httpdebug::queueMouseMove(x, y);
        free(xParam);
        free(yParam);
        return responseAllocJSON("{\"action\": \"mouse_move\"}");
    }

    if (strcmp(request->path, "/key") == 0) {
        char* keyParam = strdupDecodeGETParam("key=", request, "0");
        int key = atoi(keyParam);
        httpdebug::queueKeyPress(key);
        free(keyParam);
        return responseAllocJSON("{\"action\": \"key_press\"}");
    }

    if (strcmp(request->path, "/type") == 0) {
        char* textParam = strdupDecodeGETParam("text=", request, "");
        httpdebug::queueTypeText(std::string(textParam));
        free(textParam);
        return responseAllocJSON("{\"action\": \"type\"}");
    }

    if (strcmp(request->path, "/stop") == 0 || strcmp(request->path, "/exit") == 0) {
        httpdebug::stopApp();
        return responseAllocJSON("{\"status\": \"exiting\"}");
    }

    if (strcmp(request->path, "/layout") == 0) {
        return responseAllocJSON(httpdebug::getSimpleLayoutJson().c_str());
    }

    if (strcmp(request->path, "/clickid") == 0) {
        char* idParam = strdupDecodeGETParam("id=", request, "0");
        ImGuiID id = (ImGuiID)atoi(idParam);
        httpdebug::queueClickById(id);
        free(idParam);
        return responseAllocJSON("{\"action\": \"click_id\"}");
    }

    if (strcmp(request->path, "/sdr/start") == 0) {
        httpdebug::requestSdrStart();
        return responseAllocJSON("{\"action\": \"sdr_start\"}");
    }

    if (strcmp(request->path, "/sdr/stop") == 0) {
        httpdebug::requestSdrStop();
        return responseAllocJSON("{\"action\": \"sdr_stop\"}");
    }

    if (strcmp(request->path, "/sdr/status") == 0) {
        return responseAllocJSONWithFormat(
            "{\"playing\": %s}",
            httpdebug::isSdrPlaying() ? "true" : "false");
    }
#endif

    return responseAlloc404NotFoundHTML(request->path);
}