
#define IMGUI_DEFINE_MATH_OPERATORS
#include <gui/brown/geomap.h>
#include <fstream>
#include <core.h>
#include <utils/wstr.h>
#include <utils/flog.h>
#include <gui/widgets/simple_widgets.h>
#include <filesystem>
#include <random>
#include <imgui/imgui_internal.h>

namespace geomap {


    // Converts degrees to radians

    nlohmann::json geoJSON;
    std::vector<std::vector<std::vector<CartesianCoordinates>>> countriesGeo;

    nlohmann::json readGeoJSONFile(const std::string& filePath) {
        std::ifstream fileStream(filePath);

        if (!fileStream.is_open()) {
            flog::error("Failed to open the file {}", filePath);
            return nullptr;
        }

        nlohmann::json geoJSON;
        fileStream >> geoJSON;

        return geoJSON;
    }

    void maybeInit() {
        if (geoJSON.empty()) {
            std::string resDir = core::getResourcesDirectory();
            const std::string filePath = resDir + "/cty/map.json";
            geoJSON = readGeoJSONFile(filePath);


            for (const auto& feature : geoJSON["features"]) {
                //                std::string countryID = feature["properties"]["NAME"];
                countriesGeo.emplace_back();

                for (const auto& coordinates : feature["geometry"]["coordinates"]) {

                    countriesGeo.back().emplace_back();
                    for (const auto& coord0 : coordinates) {

                        auto& dest = countriesGeo.back().back();
                        if (coord0.is_array() && !coord0.empty() && coord0[0].is_array()) {
                            // multy poligon
                            for (const auto& coord1 : coord0) {
                                double longitude = coord1[0].get<double>();
                                double latitude = coord1[1].get<double>();
                                CartesianCoordinates cartesian = geoToCartesian({ latitude, longitude });

                                dest.emplace_back(cartesian);
                            }
                        }
                        else if (coord0.is_array() && !coord0.empty() && !coord0[0].is_array() && coord0.size() == 2) {

                            for (const auto& coord1 : coordinates) {
                                double longitude = coord1[0].get<double>();
                                double latitude = coord1[1].get<double>();
                                CartesianCoordinates cartesian = geoToCartesian({ latitude, longitude });

                                dest.emplace_back(cartesian);
                            }
                            break;
                        }
                        else {
                            break;
                        }
                    }
                }
            }
        }
    }

    // Create a color map to store a color for each country
    std::map<std::string, ImVec4> countryColors;

    ImVec4 randomColor() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0, 1);

        return ImVec4(dis(gen), dis(gen), dis(gen), 1.0f);
    }

    std::vector<ImVec4> colors = {
        ImVec4(1.0f, 0.0f, 0.0f, 1.0f),   // Colors.red
        ImVec4(0.0f, 1.0f, 0.0f, 1.0f),   // Colors.green
        ImVec4(1.0f, 0.08f, 0.58f, 1.0f), // Colors.pink
        ImVec4(1.0f, 0.76f, 0.03f, 1.0f), // Colors.amber
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f),   // Colors.grey
        ImVec4(0.6f, 0.32f, 0.17f, 1.0f), // Colors.brown
        ImVec4(1.0f, 0.65f, 0.0f, 1.0f),  // Colors.orange
        ImVec4(0.56f, 0.93f, 0.56f, 1.0f) // Colors.lightGreen
    };


    void GeoMap::draw() {

        maybeInit();

        const ImVec2 curpos = ImGui::GetCursorPos();


        ImDrawList* drawList = ImGui::GetWindowDrawList();
        recentCanvasPos = ImGui::GetCursorScreenPos();
        auto windowWidth = ImGui::GetContentRegionAvail().x;
        auto windowHeight = ImGui::GetContentRegionAvail().y;
        ImVec2 w2 = ImVec2(windowWidth / 2, windowHeight / 2);
        auto toView = [=](ImVec2 c) {
            c *= scale;
            auto x1 = static_cast<float>(c.x * w2.x + w2.x);
            auto y1 = static_cast<float>(windowHeight - (c.y * w2.y + w2.y));
            return ImVec2(x1, y1) + translate * w2 * scale;
        };
        recentMapToScreen = toView;

        if (windowWidth == 0) {
            return;
        }
        int count = 0;

        drawList->AddRectFilled(recentCanvasPos + ImVec2(0, 0), recentCanvasPos + ImGui::GetContentRegionAvail(), ImColor(0, 0, 0));

        for (auto& country : countriesGeo) {
            const ImColor& thisColor = ImColor(colors[(++count) % colors.size()]);
            for (auto& polygon : country) {
                if (polygon.size() > 1) {
                    for (int i = 0; i < polygon.size() - 1; i++) {
                        auto& cartesian = polygon[i];
                        auto& cartesian2 = polygon[i + 1];
                        drawList->AddLine(recentCanvasPos + toView(cartesian.toImVec2()), recentCanvasPos + toView(cartesian2.toImVec2()), thisColor, 1.0f);
                    }
                }
            }
        }

        if (ImGui::IsMouseDown(0)) {
            if (!initialTouchPos) {
                initialTouchPos = std::make_shared<ImVec2>(ImGui::GetMousePos());
                initialTranslate = translate;
            }
            translate = initialTranslate + (ImGui::GetMousePos() - *initialTouchPos) / w2 / scale;
            scaleTranslateDirty = true;
            flog::info("Translate: {} {}", translate.x, translate.y);
        }
        else {
            initialTouchPos.reset();
        }

        ImGui::SetCursorPos(curpos);
        if (doFingerButton("Zoom In##geomap-zoom-in")) {
            scale = scale * 2;
            scaleTranslateDirty = true;
        }
        ImGui::SameLine();
        if (doFingerButton("Zoom Out##geomap-zoom-out")) {
            scale = scale / 2;
            scaleTranslateDirty = true;
        }
        ImGui::SameLine();
        if (doFingerButton("Reset Map##reset-map")) {
            scale = ImVec2(1.0, 1.0);
            translate = ImVec2(0.0, 0.0);
            scaleTranslateDirty = true;
        }

    }
    void GeoMap::saveTo(ConfigManager& manager, const char* prefix){
        auto pref = std::string(prefix);
        manager.acquire();
        manager.conf[pref+"_scale_x"] = scale.x;
        manager.conf[pref+"_scale_y"] = scale.y;
        manager.conf[pref+"_translate_x"] = translate.x;
        manager.conf[pref+"_translate_y"] = translate.y;
        manager.release(true);
    };

    void GeoMap::loadFrom(ConfigManager& manager, const char* prefix) {
        auto pref = std::string(prefix);
        manager.acquire();
        if (manager.conf.contains(pref+"_scale_x")) {
            scale.x = manager.conf[pref + "_scale_x"];
            scale.y = manager.conf[pref + "_scale_y"];
            translate.x = manager.conf[pref + "_translate_x"];
            translate.y = manager.conf[pref + "_translate_y"];
        }
        manager.release(false);
    };
};