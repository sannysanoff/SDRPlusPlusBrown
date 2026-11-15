#include <gui/widgets/menu.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <gui/style.h>
#include "gui/menus/display.h"
#include <core.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// Preset colors offered in the menu header color popup (8 per row)
static const ImU32 menuColorPalette[16] = {
    IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), IM_COL32(0xD9, 0xD9, 0xD9, 0xFF), IM_COL32(0xA6, 0xA6, 0xA6, 0xFF), IM_COL32(0x73, 0x73, 0x73, 0xFF),
    IM_COL32(0x40, 0x40, 0x40, 0xFF), IM_COL32(0x1A, 0x1A, 0x1A, 0xFF), IM_COL32(0x00, 0x00, 0x00, 0xFF), IM_COL32(0x8D, 0x6E, 0x63, 0xFF),
    IM_COL32(0xE5, 0x48, 0x4D, 0xFF), IM_COL32(0xF7, 0x6B, 0x15, 0xFF), IM_COL32(0xFF, 0xB2, 0x24, 0xFF), IM_COL32(0x46, 0xAE, 0xAE, 0xFF),
    IM_COL32(0x46, 0xA7, 0x58, 0xFF), IM_COL32(0x00, 0x91, 0xFF, 0xFF), IM_COL32(0x6E, 0x56, 0xCF, 0xFF), IM_COL32(0xD6, 0x40, 0x9F, 0xFF)
};

// One "color swatch + label + 16 presets" row of the menu color popup
static bool drawMenuColorRow(const char* label, const char* id, ImVec4& col) {
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::ColorEdit3("##custom", (float*)&col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoOptions)) {
        changed = true;
    }
    col.w = 1.0f;
    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    float swatchSize = ImGui::GetFrameHeight() * 0.8f;
    for (int i = 0; i < 16; i++) {
        if ((i % 8) == 0) { ImGui::NewLine(); }
        else { ImGui::SameLine(); }
        ImGui::PushID(i);
        if (ImGui::ColorButton("##palette", ImGui::ColorConvertU32ToFloat4(menuColorPalette[i]), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoBorder, ImVec2(swatchSize, swatchSize))) {
            col = ImGui::ColorConvertU32ToFloat4(menuColorPalette[i]);
            col.w = 1.0f;
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

ImVec4 Menu::hexToColor(const std::string& str, const ImVec4& def) {
    if (str.size() != 7 || str[0] != '#') { return def; }
    if (!std::all_of(str.begin() + 1, str.end(), ::isxdigit)) { return def; }
    return ImVec4(std::stoi(str.substr(1, 2), NULL, 16) / 255.0f, std::stoi(str.substr(3, 2), NULL, 16) / 255.0f, std::stoi(str.substr(5, 2), NULL, 16) / 255.0f, 1.0f);
}

std::string Menu::colorToHex(const ImVec4& col) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", (int)roundf(col.x * 255.0f), (int)roundf(col.y * 255.0f), (int)roundf(col.z * 255.0f));
    return std::string(buf);
}

Menu::Menu() {
}

void Menu::registerEntry(std::string name, void (*drawHandler)(void* ctx), void* ctx, ModuleManager::Instance* inst) {
    MenuItem_t item;
    item.drawHandler = drawHandler;
    item.ctx = ctx;
    item.inst = inst;
    items[name] = item;
    if (!isInOrderList(name)) {
        MenuOption_t opt;
        opt.name = name;
        opt.open = true;
        order.push_back(opt);
    }
}

void Menu::removeEntry(std::string name) {
    items.erase(name);
}

bool Menu::draw(bool updateStates) {

    static auto CollapsingHeader = [](const char *chname, const MenuOption_t* opt = NULL) -> bool {
        auto &style = ImGui::GetStyle();
        auto fp = style.FramePadding;
        style.FramePadding.y += displaymenu::phoneLayout ? style::baseFont->FontSize/3.0 : 0;
        bool overrideColor = (opt != NULL && opt->overrideColor);
        if (overrideColor) {
            // Colors that were never set fall back to the live theme colors
            ImVec4 font = opt->colorsInit ? opt->fontColor : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImVec4 bg = opt->colorsInit ? opt->bgColor : ImGui::GetStyleColorVec4(ImGuiCol_Header);
            font.w = 1.0f;
            bg.w = 1.0f;
            ImGui::PushStyleColor(ImGuiCol_Text, font);
            ImGui::PushStyleColor(ImGuiCol_Header, bg);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(std::min(bg.x + 0.08f, 1.0f), std::min(bg.y + 0.08f, 1.0f), std::min(bg.z + 0.08f, 1.0f), 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(std::min(bg.x + 0.16f, 1.0f), std::min(bg.y + 0.16f, 1.0f), std::min(bg.z + 0.16f, 1.0f), 1.0f));
        }
//        auto rv = ImGui::CollapsingHeader(chname, ImGuiTreeNodeFlags_FramePadding);
        auto rv = ImGui::CollapsingHeader(chname);
        if (overrideColor) { ImGui::PopStyleColor(4); }
        style.FramePadding = fp;
        return rv;
    };
    bool changed = false;
    float menuWidth = ImGui::GetContentRegionAvail().x;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 checkboxOffset = ImVec2(menuWidth - ImGui::GetTextLineHeight() - (6.0f * style::uiScale), - ImGui::GetTextLineHeight() - (10.0f * style::uiScale));

    int displayedCount = 0;
    int rawId = 0;

    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

    for (MenuOption_t& opt : order) {
        rawId++;
        if (items.find(opt.name) == items.end()) {
            continue;
        }
        if (opt.name == draggedMenuName) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", draggedMenuName.c_str());
            ImGui::EndTooltip();
            continue;
        }

        // Draw dragged menu item
        if (displayedCount == insertBefore && !draggedMenuName.empty()) {
            if (updateStates) { ImGui::SetNextItemOpen(false); }
            ImVec2 posMin = ImGui::GetCursorScreenPos();
            ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());
            style::beginDisabled();
            ImRect orignalRect = window->WorkRect;

            CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str(), &draggedOpt);

            if (items[draggedOpt.name].inst != NULL) {
                window->WorkRect = orignalRect;
                ImVec2 pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
                ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
                bool enabled = items[draggedOpt.name].inst->isEnabled();
                ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
                ImGui::SetCursorPos(pos);
            }
            style::endDisabled();
            window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::uiScale);
        }
        displayedCount++;

        MenuItem_t& item = items[opt.name];


        ImRect orginalRect = window->WorkRect;
        if (item.inst != NULL) {
            window->WorkRect = ImRect(orginalRect.Min, ImVec2(orginalRect.Max.x - ImGui::GetTextLineHeight() - (6.0f * style::uiScale), orginalRect.Max.y));
        }

        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());

        headerTops[displayedCount - 1] = posMin.y;
        optionIDs[displayedCount - 1] = rawId - 1;

        bool anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !anyPopupOpen && ImGui::IsMouseHoveringRect(posMin, posMax)) {
            menuClicked = true;
            clickedMenuName = opt.name;
            pressMenuName = opt.name;
            pressPos = ImGui::GetMousePos();
            longPressArmed = false;
        }

        // Long press on the header opens the color popup
        if (pressMenuName == opt.name && !longPressArmed) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                pressMenuName = "";
            }
            else if (fabsf(ImGui::GetMousePos().x - pressPos.x) > (5.0f * style::uiScale) || fabsf(ImGui::GetMousePos().y - pressPos.y) > (5.0f * style::uiScale)) {
                pressMenuName = ""; // The user is dragging the menu, not holding it
            }
            else if (ImGui::GetIO().MouseDownDuration[ImGuiMouseButton_Left] >= 1.0) {
                longPressArmed = true;
                openColorPopup(opt, ImVec2(posMin.x, posMax.y));
            }
        }

        bool menuDragged = (menuClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && draggedMenuName.empty() && clickedMenuName == opt.name && pressMenuName != opt.name);
        if (menuDragged && !locked) {
            draggedMenuName = opt.name;
            draggedId = rawId - 1;
            draggedOpt = opt;
            continue;
        }
        else if (menuDragged) {
            ImGui::SetTooltip("Menu Order Locked!");
        }

        // Draw menu header and menu content. There is a lot of boilerplate because the checkbox has to be drawn before the menu, TODO: fix
        bool suppressToggle = (suppressToggleName == opt.name);
        if (suppressToggle) {
            ImGui::SetNextItemOpen(opt.open, ImGuiCond_Always);
        }
        else if (updateStates) { ImGui::SetNextItemOpen(opt.open); }
        std::string headerId = opt.name + "##sdrpp_main_menu";
        bool headerOpen = CollapsingHeader(headerId.c_str(), &opt);
        if (suppressToggle) {
            // Releasing the long press over the header must not toggle it, restore the open state
            window->DC.StateStorage->SetInt(window->GetID(headerId.c_str()), opt.open ? 1 : 0);
            headerOpen = opt.open;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            openColorPopup(opt, ImVec2(posMin.x, posMax.y));
        }
        if (headerOpen) {
            if (item.inst != NULL) {
                window->WorkRect = orginalRect;
                ImVec2 pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
                ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
                bool enabled = item.inst->isEnabled();
                if (ImGui::Checkbox(("##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                    if (!enabled && core::configManager.conf["moduleInstances"].contains(opt.name)) {
                        core::configManager.acquire();
                        core::configManager.conf["moduleInstances"][opt.name]["enabled"] = false;
                        core::configManager.release(true);
                        core::configManager.save(true);
                    }
                    enabled ? item.inst->enable() : item.inst->disable();
                    changed = true;
                }
                ImGui::SetCursorPos(pos);
            }

            // Check if the state changed
            if (!opt.open && !updateStates) {
                opt.open = true;
                changed = true;
            }

            item.drawHandler(item.ctx);
            ImGui::Spacing();
        }
        else if (item.inst != NULL) {
            window->WorkRect = orginalRect;
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
            ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
            bool enabled = item.inst->isEnabled();
            if (ImGui::Checkbox(("##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                if (!enabled && core::configManager.conf["moduleInstances"].contains(opt.name)) {
                    core::configManager.acquire();
                    core::configManager.conf["moduleInstances"][opt.name]["enabled"] = false;
                    core::configManager.release(true);
                    core::configManager.save(true);
                }
                enabled ? item.inst->enable() : item.inst->disable();
                changed = true;
            }
            ImGui::SetCursorPos(pos);

            if (opt.open && !updateStates) {
                opt.open = false;
                changed = true;
            }
        }
        else if (opt.open && !updateStates) {
            opt.open = false;
            changed = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && menuClicked) {

        if (!draggedMenuName.empty()) {
            // Move menu
            order.erase(order.begin() + draggedId);

            if (insertBefore == displayedCount) {
                order.push_back(draggedOpt);
            }
            else if (insertBeforeName != "") {
                int beforeId = 0;
                for (int i = 0; i < order.size(); i++) {
                    if (order[i].name == insertBeforeName) {
                        beforeId = i;
                        break;
                    }
                }
                order.insert(order.begin() + beforeId, draggedOpt);
            }
            changed = true;
        }

        menuClicked = false;
        draggedMenuName = "";
        insertBeforeName = "";
        insertBefore = -1;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        pressMenuName = "";
        longPressArmed = false;
    }

    // TODO: Figure out why the hell this is needed
    if (insertBefore == displayedCount && !draggedMenuName.empty()) {
        if (updateStates) { ImGui::SetNextItemOpen(false); }
        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());
        style::beginDisabled();
        ImRect orignalRect = window->WorkRect;
        CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str(), &draggedOpt);
        if (items[draggedOpt.name].inst != NULL) {
            window->WorkRect = orignalRect;
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
            ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
            bool enabled = items[draggedOpt.name].inst->isEnabled();
            ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
            ImGui::SetCursorPos(pos);
        }
        style::endDisabled();
        window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::uiScale);
    }

    if (!draggedMenuName.empty()) {
        insertBefore = displayedCount;
        ImVec2 mPos = ImGui::GetMousePos();
        for (int i = 0; i < displayedCount; i++) {
            if (headerTops[i] > mPos.y) {
                insertBefore = i;
                insertBeforeName = order[optionIDs[i]].name;
                break;
            }
        }
    }

    if (drawColorPopup()) {
        changed = true;
    }

    return changed;
}

void Menu::initColorsFromTheme(MenuOption_t& opt) {
    if (opt.colorsInit) { return; }
    opt.fontColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    opt.bgColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
    opt.fontColor.w = 1.0f;
    opt.bgColor.w = 1.0f;
    opt.colorsInit = true;
}

void Menu::openColorPopup(const MenuOption_t& opt, const ImVec2& pos) {
    colorPopupName = opt.name;
    colorPopupPos = pos;
    colorPopupPosPending = true;
    suppressToggleName = opt.name;
    ImGui::OpenPopup(("##sdrpp_menu_color_" + opt.name).c_str());
}

bool Menu::drawColorPopup() {
    if (colorPopupName.empty()) { return false; }

    MenuOption_t* opt = NULL;
    for (MenuOption_t& o : order) {
        if (o.name == colorPopupName) {
            opt = &o;
            break;
        }
    }
    if (opt == NULL) {
        colorPopupName = "";
        colorPopupPosPending = false;
        suppressToggleName = "";
        return false;
    }

    if (colorPopupPosPending) {
        ImGui::SetNextWindowPos(colorPopupPos, ImGuiCond_Always);
        colorPopupPosPending = false;
    }

    if (!ImGui::BeginPopup(("##sdrpp_menu_color_" + colorPopupName).c_str())) {
        colorPopupName = "";
        suppressToggleName = "";
        return false;
    }

    bool changed = false;

    initColorsFromTheme(*opt);

    ImGui::TextUnformatted(opt->name.c_str());
    ImGui::Separator();

    if (ImGui::Checkbox("Override color", &opt->overrideColor)) {
        changed = true;
    }

    ImGui::Spacing();
    if (drawMenuColorRow("Font", "##font", opt->fontColor)) {
        changed = true;
    }
    ImGui::Spacing();
    if (drawMenuColorRow("Background", "##background", opt->bgColor)) {
        changed = true;
    }

    ImGui::EndPopup();
    return changed;
}

bool Menu::isInOrderList(std::string name) {
    for (MenuOption_t opt : order) {
        if (opt.name == name) {
            return true;
        }
    }
    return false;
}
