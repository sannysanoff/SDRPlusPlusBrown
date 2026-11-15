#pragma once
#include <string>
#include <vector>
#include <map>
#include <imgui/imgui.h>
#include <module.h>

#define MAX_MENU_COUNT 1024

class Menu {
public:
    Menu();

    struct MenuOption_t {
        std::string name;
        bool open;
        bool overrideColor = false;
        bool colorsInit = false;
        ImVec4 fontColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 bgColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    };

    struct MenuItem_t {
        void (*drawHandler)(void* ctx);
        void* ctx;
        ModuleManager::Instance* inst;
    };

    void registerEntry(std::string name, void (*drawHandler)(void* ctx), void* ctx = NULL, ModuleManager::Instance* inst = NULL);
    void removeEntry(std::string name);
    bool draw(bool updateStates);

    // Color helpers used to store menu colors in the config as "#RRGGBB"
    static ImVec4 hexToColor(const std::string& str, const ImVec4& def);
    static std::string colorToHex(const ImVec4& col);

    std::vector<MenuOption_t> order;

    bool locked = false;

private:
    bool isInOrderList(std::string name);
    void openColorPopup(const MenuOption_t& opt, const ImVec2& pos);
    bool drawColorPopup();
    static void initColorsFromTheme(MenuOption_t& opt);

    
    bool menuClicked = false;
    std::string clickedMenuName = "";
    std::string draggedMenuName = "";
    int insertBefore = -1;
    std::string insertBeforeName = "";

    int draggedId = 0;
    MenuOption_t draggedOpt;

    // Long press (or right click) on a menu header opens its color popup
    std::string pressMenuName = "";
    ImVec2 pressPos = ImVec2(0.0f, 0.0f);
    bool longPressArmed = false;
    std::string colorPopupName = "";
    ImVec2 colorPopupPos = ImVec2(0.0f, 0.0f);
    bool colorPopupPosPending = false;
    std::string suppressToggleName = "";

    std::map<std::string, MenuItem_t> items;

    float headerTops[MAX_MENU_COUNT];
    int optionIDs[MAX_MENU_COUNT];
};
