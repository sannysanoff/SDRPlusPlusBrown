#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <core.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <gui/style.h>
#include <filesystem>
#include <fstream>
#include "gui/widgets/simple_widgets.h"
#include "utils/proto/kiwisdr.h"
#include <gui/brown/geomap.h>



struct KiwiSDRMapSelector {

    geomap::GeoMap geoMap;
    std::shared_ptr<json> serversList;
    std::string serverListError;
    std::string serverTestStatus;
    std::string serverTestError;
    bool loadingList = false;
    std::string root;

    struct ServerEntry {
        ImVec2 gps; // -1 .. 1 etc
        std::string name;
        std::string loc;
        std::string url;
        std::string antenna;
        float maxSnr;
        float secondSnr;
        int users, usersmax;
        bool selected = false;
    };

    std::vector<ServerEntry> servers;
    std::string lastTestedServer;
    std::string lastTestedServerLoc;
    std::mutex lastTestedServerMutex;
    const std::string configPrefix;
    bool testInProgress = false;

    ConfigManager* config;

    KiwiSDRMapSelector(const std::string& root, ConfigManager *config, const std::string& configPrefix) : configPrefix(configPrefix) {
        this->root = root;
        this->config = config;
        json def = json({});
        config->load(def);
        geoMap.loadFrom(*config, configPrefix.c_str()); // configPrefix is like "mapselector1_"
    }

    void openPopup() {
        ImGui::OpenPopup((configPrefix+": The KiwiSDR Map").c_str());
    }

    void drawPopup(std::function<void(const std::string&, const std::string&)> onSelected) {
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.125f);
        if (ImGui::BeginPopupModal((configPrefix+": The KiwiSDR Map").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            // Set the modal dialog's width and height
            const ImVec2 ws = ImGui::GetIO().DisplaySize * 0.75f;
            ImGui::SetWindowSize(ws);
            ImGui::BeginChild("##geomap-kiwisdr", ws - ImVec2(0, 50), true, 0);
            geoMap.draw();
            if (geoMap.scaleTranslateDirty) {
                geoMap.saveTo(*config, configPrefix.c_str());
                geoMap.scaleTranslateDirty = false;
            }
            if (!serversList) {
                if (!serverListError.empty()) {
                    ImGui::Text("%s", serverListError.c_str());
                }
                else {
                    ImGui::Text("Loading KiwiSDR servers list..");
                    if (!loadingList) {
                        loadingList = true;
                        std::thread t([&]() {
                            serversList = loadServersList();

                            if (serversList) {
                                for (const auto& entry : *serversList) {
                                    ServerEntry serverEntry;

                                    // Check if all required fields are present
                                    if (entry.contains("gps") && entry.contains("name") && entry.contains("url") &&
                                        entry.contains("snr") && entry.contains("users") && entry.contains("users_max") && entry.contains("offline")) {

                                        if (entry["offline"].get<std::string>() == "no") {
                                            std::string gps_str = entry["gps"].get<std::string>();
                                            geomap::GeoCoordinates geo;
                                            sscanf(gps_str.c_str(), "(%lf, %lf)", &geo.latitude, &geo.longitude);
                                            serverEntry.gps = geomap::geoToCartesian(geo).toImVec2();
                                            serverEntry.name = entry["name"].get<std::string>();
                                            serverEntry.loc = entry["loc"].get<std::string>();
                                            serverEntry.url = entry["url"].get<std::string>();
                                            if (entry.contains("antenna")) {
                                                serverEntry.antenna = entry["antenna"].get<std::string>();
                                            }
                                            sscanf(entry["snr"].get<std::string>().c_str(), "%f,%f", &serverEntry.maxSnr, &serverEntry.secondSnr);
                                            serverEntry.users = atoi(entry["users"].get<std::string>().c_str());
                                            serverEntry.usersmax = atoi(entry["users_max"].get<std::string>().c_str());
                                            servers.push_back(serverEntry);
                                        }
                                    }
                                }
                            }

                            std::sort(servers.begin(), servers.end(), [](const ServerEntry& a, const ServerEntry& b) {
                                return a.maxSnr < b.maxSnr;
                            });


                            loadingList = false;
                        });
                        t.detach();
                    }
                }
            }
            else {
                ImGui::Text("Loaded servers list");
                auto sz = style::baseFont->FontSize;
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                for (auto& s : servers) {
                    if (s.users < s.usersmax) {
                        auto dest = geoMap.recentMapToScreen(s.gps);
                        auto color = ImColor(0.3f, 0.3f, 0.3f);
                        if (s.maxSnr > 22) {
                            color = ImColor(0.0f, 1.0f, 0.0f);
                        }
                        else if (s.maxSnr > 12) {
                            color = ImColor(0.6f, 0.6f, 0.6f);
                        }
                        drawList->AddRectFilled(geoMap.recentCanvasPos + dest - ImVec2(sz / 2, sz / 2), geoMap.recentCanvasPos + dest + ImVec2(sz / 2, sz / 2), color, sz / 4.0f);
                        if (s.selected) {
                            drawList->AddRect(geoMap.recentCanvasPos + dest - ImVec2(sz / 2, sz / 2), geoMap.recentCanvasPos + dest + ImVec2(sz / 2, sz / 2), ImColor(1.0f, 1.0f, 0.0f), sz / 4.0f);
                            drawList->AddRect(geoMap.recentCanvasPos + dest - ImVec2(sz / 2 - 1, sz / 2 - 1), geoMap.recentCanvasPos + dest + ImVec2(sz / 2 - 1, sz / 2 - 1), ImColor(1.0f, 1.0f, 0.0f), sz / 4.0f);
                        }
                        else {
                            drawList->AddRect(geoMap.recentCanvasPos + dest - ImVec2(sz / 2, sz / 2), geoMap.recentCanvasPos + dest + ImVec2(sz / 2, sz / 2), ImColor(0.0f, 0.0f, 0.0f), sz / 4.0f);
                        }
                    }
                }
                if (ImGui::IsMouseClicked(0) && ImGui::GetMouseClickedCount(0) == 1) {
                    auto clickPos = ImGui::GetMousePos() - geoMap.recentCanvasPos;
                    auto radius = sz / 2;
                    for (int q = 0; q < 2; q++) {
                        auto found = false;
                        for (int i = servers.size() - 1; i >= 0; i--) {
                            auto& it = servers[i];
                            auto dest = geoMap.recentMapToScreen(it.gps);
                            auto dist = sqrt(pow(dest.x - clickPos.x, 2) + pow(dest.y - clickPos.y, 2));
                            if (dist < radius) {
                                found = true;
                                for (auto& s : servers) {
                                    s.selected = false;
                                }
                                auto it0 = it;
                                it0.selected = true;
                                servers.emplace_back(it0);
                                servers.erase(servers.begin() + i);
                                break;
                            }
                        }
                        if (found) {
                            break;
                        }
                        radius *= 5;
                    }
                }
                for (auto& s : servers) {
                    if (s.selected) {
                        ImGui::Text("%s", s.name.c_str());
                        ImGui::Text("%s", s.loc.c_str());
                        if (!s.antenna.empty()) {
                            ImGui::Text("ANT: %s", s.antenna.c_str());
                        }
                        if (s.maxSnr > 0) {
                            ImGui::Text("SNR: %d", (int)s.maxSnr);
                        }
                        if (s.usersmax > 0) {
                            ImGui::Text("USR: %d/%d", s.users, s.usersmax);
                        }
                        ImGui::Text("URL: %s", s.url.c_str());
                        ImGui::BeginDisabled(testInProgress);
                        auto doTest = doFingerButton("Test server");
                        ImGui::EndDisabled();
                        if (doTest) {
                            lastTestedServerMutex.lock();
                            lastTestedServer = "";
                            lastTestedServerMutex.unlock();
                            if (!testInProgress) {
                                testInProgress = true;
                                serverTestStatus = "Testing server " + s.url + " ...";
                                std::thread tester([=]() {
                                    KiwiSDRClient testClient;
                                    bool plannedDisconnect = false;
                                    if (s.url.find("http://") == 0) {
                                        auto hostPort = s.url.substr(7);
                                        auto loc = s.loc;
                                        auto lastSlash = hostPort.find("/");
                                        if (lastSlash != std::string::npos) {
                                            hostPort = hostPort.substr(0, lastSlash);
                                        }
                                        serverTestStatus = "Testing server " + hostPort + "...";
                                        testClient.init(hostPort);
                                        bool connected = 0;
                                        bool disconnected = 0;
                                        auto start = currentTimeMillis();
                                        testClient.onConnected = [&]() {
                                            connected = true;
                                            serverTestStatus = "Connected to server " + hostPort + " ...";
                                            start = currentTimeMillis();
                                            testClient.tune(14074000, KiwiSDRClient::TUNE_IQ);
                                        };
                                        testClient.onDisconnected = [&]() {
                                            disconnected = true;
                                            if (plannedDisconnect) {
                                                serverTestStatus = "Got some data. Server OK: " + s.url;
                                                lastTestedServerMutex.lock();
                                                lastTestedServer = hostPort;
                                                lastTestedServerLoc = loc;
                                                lastTestedServerMutex.unlock();
                                            }
                                            else {
                                                serverTestStatus = "Disconnect, no data. Server NOT OK: " + s.url;
                                            }
                                        };
                                        testClient.start();
                                        start = currentTimeMillis();
                                        while (true) {
                                            if (disconnected) {
                                                break;
                                            }
                                            testClient.iqDataLock.lock();
                                            auto bufsize = testClient.iqData.size();
                                            testClient.iqDataLock.unlock();
                                            usleep(100000);
                                            if (bufsize > 0) {
                                                plannedDisconnect = true;
                                                break;
                                            }
                                            if (connected && currentTimeMillis() > start + 5000) {
                                                break;
                                            }
                                        }
                                        testClient.stop();
                                        if (connected) {
                                            while (!disconnected) {
                                                usleep(100000);
                                            }
                                            flog::info("Disconnected ok");
                                        }
                                        else {
                                            usleep(1000000);
                                        }
                                        testInProgress = false;
                                    }
                                    else {
                                        serverTestStatus = "Non-http url " + s.url;
                                    }
                                });
                                tester.detach();
                            }
                        }
                    }
                }
                if (!serverTestStatus.empty()) {
                    ImGui::Text("%s", serverTestStatus.c_str());
                }
                if (!serverTestError.empty()) {
                    ImGui::Text("Server test error: %s", serverTestError.c_str());
                }
            }

            ImGui::EndChild();
            // Display some text in the modal dialog
            //            ImGui::Text("This is a modal dialog box with specified width and height.");

            // Close button
            if (doFingerButton("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            lastTestedServerMutex.lock();
            if (lastTestedServer != "") {
                ImGui::SameLine();
                if (doFingerButton("Use tested server: " + lastTestedServer)) {
                    onSelected(lastTestedServer,lastTestedServerLoc);
                    ImGui::CloseCurrentPopup();
                }
            }
            lastTestedServerMutex.unlock();

            ImGui::EndPopup();
        }
    }

    std::shared_ptr<json> loadServersList() {
        // http://rx.linkfanel.net/kiwisdr_com.js
        try {

            std::string jsoncache = root + "/kiwisdr_source.receiverlist.json";

            auto status = std::filesystem::status(jsoncache);
            if (exists(status)) {
                const std::filesystem::file_time_type last_write_time = std::filesystem::last_write_time(jsoncache);
                auto last_write_time_sys_clock = std::chrono::time_point_cast<std::chrono::system_clock::duration>(last_write_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());

                if (std::chrono::system_clock::now() - last_write_time_sys_clock < std::chrono::hours(1)) {
                    std::ifstream ifs(jsoncache);
                    std::string content((std::istreambuf_iterator<char>(ifs)),
                                        (std::istreambuf_iterator<char>()));
                    return std::make_shared<json>(json::parse(content));
                }
            }

            std::string host = "rx.linkfanel.net";
            auto controlSock = net::connect(host, 80);
            auto controlHttp = net::http::Client(controlSock);

            // Make request
            net::http::RequestHeader rqhdr(net::http::METHOD_GET, "/kiwisdr_com.js", host);
            controlHttp.sendRequestHeader(rqhdr);
            net::http::ResponseHeader rshdr;
            controlHttp.recvResponseHeader(rshdr, 5000);

            flog::debug("Response from {}: {}", host, rshdr.getStatusString());
            std::vector<uint8_t> data(2000000, 0);
            std::string response;
            while (true) {
                auto len = controlSock->recv(data.data(), data.size());
                if (len < 1) {
                    break;
                }
                response += std::string((char*)data.data(), len);
                usleep(100);
            }
            controlSock->close();
            auto BEGIN = "var kiwisdr_com =";
            auto END = "},\n]\n;";
            auto beginIx = response.find(BEGIN);
            if (beginIx == std::string::npos) {
                throw std::runtime_error("Invalid response from server");
            }
            auto endIx = response.find_last_of(END);
            if (endIx == std::string::npos) {
                throw std::runtime_error("Invalid response from server");
            }
            response = response.substr(beginIx + strlen(BEGIN), endIx - strlen(END) - (beginIx + strlen(BEGIN)));
            response += "}]"; // fix trailing comma unsupported by parser

            FILE* toSave = fopen(jsoncache.c_str(), "wt");
            if (toSave) {
                fwrite(response.c_str(), 1, response.size(), toSave);
                fclose(toSave);
            }

            return std::make_shared<json>(json::parse(response));
        }
        catch (std::exception& e) {
            serverListError = e.what();
            return std::shared_ptr<json>();
        }
    }
};
