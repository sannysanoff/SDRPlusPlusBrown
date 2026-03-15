// Must include EmbeddableWebServer.h FIRST, before anything else that might set EWS_HEADER_ONLY
#include "EmbeddableWebServer.h"

#include "http_debug_server.h"
#include <utils/flog.h>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>
#include <unordered_map>
#include <map>
#include <set>
#include <cstdarg>
#include <core.h>

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

    std::vector<WidgetInfo> widgetRegistry;

    void registerWidget(ImGuiID id, ImGuiItemStatusFlags flags, const ImRect& rect) {
        if (id == 0) return;
        WidgetInfo info;
        info.id = id;
        info.label = "";
        info.flags = flags;
        info.rect = rect;
        widgetRegistry.push_back(info);
    }

    void clearWidgetRegistry() {
        widgetRegistry.clear();
    }

    std::vector<WidgetInfo>& getWidgetRegistry() {
        return widgetRegistry;
    }

}

// Called from ImGui::ItemAdd to register widgets for the debug layout
namespace ImGui {
    void sdrcppRegisterWidget(ImGuiID widgetId, ImGuiItemStatusFlags flags, const ImRect& rect) {
        httpdebug::registerWidget(widgetId, flags, rect);
    }
}

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

        // Windows
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

        // Open popups/menus
        for (int i = 0; i < ctx->OpenPopupStack.Size; i++) {
            ImGuiPopupData& popup = ctx->OpenPopupStack[i];
            if (popup.Window) {
                if (!first) result += ", ";
                result += "{";
                result += "\"type\": \"popup\", ";
                result += "\"name\": \"" + std::string(popup.Window->Name) + "\", ";
                result += "\"id\": " + std::to_string(popup.PopupId) + ", ";
                result += "\"x\": " + std::to_string(popup.Window->Pos.x) + ", ";
                result += "\"y\": " + std::to_string(popup.Window->Pos.y) + ", ";
                result += "\"w\": " + std::to_string(popup.Window->Size.x) + ", ";
                result += "\"h\": " + std::to_string(popup.Window->Size.y);
                result += "}";
                first = false;
            }
        }

        // Widgets from registry
        std::map<ImGuiID, WidgetInfo> uniqueWidgets;
        for (const auto& w : widgetRegistry) {
            if (w.id != 0) {
                uniqueWidgets[w.id] = w;
            }
        }
        for (const auto& it : uniqueWidgets) {
            const WidgetInfo& w = it.second;
            if (!first) result += ", ";
            result += "{";
            result += "\"type\": \"widget\", ";
            result += "\"name\": \"" + w.label + "\", ";
            result += "\"id\": " + std::to_string(w.id) + ", ";
            result += "\"x\": " + std::to_string(w.rect.Min.x) + ", ";
            result += "\"y\": " + std::to_string(w.rect.Min.y) + ", ";
            result += "\"w\": " + std::to_string(w.rect.Max.x - w.rect.Min.x) + ", ";
            result += "\"h\": " + std::to_string(w.rect.Max.y - w.rect.Min.y);
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

namespace httpdebug {
    namespace procfs {
        struct Endpoint {
            std::string path;
            ReadFunc read;
            WriteFunc write;
            Type type;
        };

        inline std::vector<Endpoint> endpoints;
        inline std::mutex endpointsMutex;

        struct PendingRequest {
            std::string path;
            std::string method;
            std::string body;
            int responseId;
        };

        inline std::vector<PendingRequest> pendingRequests;
        inline std::mutex pendingMutex;

        inline std::map<int, ProcResponse> responses;
        inline std::mutex responseMutex;

        int registerEndpoint(const std::string& path, ReadFunc read, WriteFunc write, Type type) {
            std::lock_guard<std::mutex> lock(endpointsMutex);
            endpoints.push_back({ path, read, write, type });
            return endpoints.size();
        }

        void unregister(const std::string& path) {
            std::lock_guard<std::mutex> lock(endpointsMutex);
            endpoints.erase(
                std::remove_if(endpoints.begin(), endpoints.end(),
                               [&](const Endpoint& e) { return e.path == path; }),
                endpoints.end());
        }

        std::vector<std::string> list() {
            std::lock_guard<std::mutex> lock(endpointsMutex);
            std::vector<std::string> result;
            for (const auto& e : endpoints) {
                result.push_back(e.path);
            }
            return result;
        }

        void queueRequest(const std::string& path, const std::string& method, const std::string& body, int responseId) {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingRequests.push_back({ path, method, body, responseId });
        }

        bool getResponse(int responseId, ProcResponse& res) {
            std::lock_guard<std::mutex> lock(responseMutex);
            auto it = responses.find(responseId);
            if (it != responses.end()) {
                res = it->second;
                responses.erase(it);
                return true;
            }
            return false;
        }

        void processQueue() {
            std::vector<PendingRequest> toProcess;
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                toProcess = std::move(pendingRequests);
                pendingRequests.clear();
            }

            for (const auto& req : toProcess) {
                ProcResponse res{ 404, "not found", "text/plain" };

                std::lock_guard<std::mutex> lock(endpointsMutex);
                for (const auto& e : endpoints) {
                    if (e.path == req.path) {
                        if (req.method == "GET" && e.read) {
                            res = { 200, e.read(), "text/plain" };
                        }
                        else if ((req.method == "POST" || req.method == "PUT") && e.write) {
                            e.write(req.body);
                            res = { 200, "ok", "text/plain" };
                        }
                        else {
                            res = { 400, "operation not supported", "text/plain" };
                        }
                        break;
                    }
                }

                std::lock_guard<std::mutex> lock2(responseMutex);
                responses[req.responseId] = res;
            }
        }

    } // namespace procfs
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

    if (strcmp(request->path, "/modules") == 0) {
        std::string json = "{";
        bool first = true;
        for (auto& [name, inst] : core::moduleManager.instances) {
            if (!first) json += ", ";
            std::string modName = inst.module.info ? inst.module.info->name : "unknown";
            json += "\"" + name + "\": \"" + modName + "\"";
            first = false;
        }
        json += "}";
        return responseAllocJSON(json.c_str());
    }

    if (strncmp(request->path, "/proc", 5) == 0) {
        std::string fullPath(request->pathDecoded);
        if (fullPath == "/proc") {
            auto list = httpdebug::procfs::list();
            std::string json = "[";
            for (size_t i = 0; i < list.size(); i++) {
                if (i > 0) json += ", ";
                json += "\"" + list[i] + "\"";
            }
            json += "]";
            return responseAllocJSON(json.c_str());
        }

        std::string path = fullPath.substr(5);

        int responseId = rand();
        std::string method = "GET";
        std::string body = "";

        if (strlen(request->method) > 0) {
            method = request->method;
        }

        if (request->body.length > 0 && request->body.contents) {
            body = std::string(request->body.contents, request->body.length);
        }

        httpdebug::procfs::queueRequest(path, method, body, responseId);

        for (int i = 0; i < 100; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            httpdebug::procfs::ProcResponse res;
            if (httpdebug::procfs::getResponse(responseId, res)) {
                if (res.statusCode == 200) {
                    return responseAllocJSON(res.body.c_str());
                }
                else {
                    return responseAllocJSON(res.body.c_str());
                }
            }
        }

        return responseAllocJSON("{\"error\": \"timeout\"}");
    }

    if (strncmp(request->path, "/ls", 3) == 0 || strncmp(request->pathDecoded, "/ls", 3) == 0) {
        std::lock_guard<std::mutex> lock(httpdebug::procfs::endpointsMutex);
        std::string json = "[";
        for (size_t i = 0; i < httpdebug::procfs::endpoints.size(); i++) {
            const auto& e = httpdebug::procfs::endpoints[i];
            if (i > 0) json += ", ";
            std::string typeStr = "unknown";
            switch (e.type) {
            case httpdebug::procfs::Type::Bool:
                typeStr = "bool";
                break;
            case httpdebug::procfs::Type::Int:
                typeStr = "int";
                break;
            case httpdebug::procfs::Type::Float:
                typeStr = "float";
                break;
            case httpdebug::procfs::Type::String:
                typeStr = "string";
                break;
            default:
                typeStr = "unknown";
                break;
            }
            std::string value = e.read ? e.read() : "";
            json += "{\"path\": \"" + e.path + "\", \"value\": \"" + value + "\", \"type\": \"" + typeStr + "\", \"writable\": " + (e.write ? "true" : "false") + "}";
        }
        json += "]";
        return responseAllocJSON(json.c_str());
    }
#endif

    return responseAlloc404NotFoundHTML(request->path);
}