#include "../core/src/gui/widgets/menu.h"
#include "test_runner.h"
#include "test_utils.h"
#include <utils/flog.h>
#include <string>
#include <vector>

// Unit tests for the menu header color config helpers. These are pure functions,
// so no ImGui context is needed.
namespace {

static const ImVec4 defaultColor = ImVec4(-1.0f, -1.0f, -1.0f, 1.0f);

// Decoding a valid hex color must succeed and must survive a save/load round trip
static void checkHexRoundTrip() {
    std::vector<std::string> colors = { "#FFFFFF", "#000000", "#3D84E0", "#A0A0B2", "#E5484D", "#46a758", "#d6409f" };
    for (const std::string& hex : colors) {
        ImVec4 col = Menu::hexToColor(hex, defaultColor);
        if (col.x == defaultColor.x && col.y == defaultColor.y && col.z == defaultColor.z) {
            flog::error("[menu color test] hexToColor rejected valid color {}", hex);
            sdrpp::test::failed = true;
            return;
        }
        if (col.w != 1.0f) {
            flog::error("[menu color test] decoded color is not opaque for {}", hex);
            sdrpp::test::failed = true;
            return;
        }
        std::string saved = Menu::colorToHex(col);
        ImVec4 col2 = Menu::hexToColor(saved, defaultColor);
        if (col2.x != col.x || col2.y != col.y || col2.z != col.z) {
            flog::error("[menu color test] round trip mismatch for {} -> {}", hex, saved);
            sdrpp::test::failed = true;
            return;
        }
    }
}

static void checkHexValues() {
    ImVec4 col = Menu::hexToColor("#FF8000", defaultColor);
    if (col.x != 1.0f) { flog::error("[menu color test] bad red channel {}", col.x); sdrpp::test::failed = true; return; }
    if (col.y <= 0.49f || col.y >= 0.52f) { flog::error("[menu color test] bad green channel {}", col.y); sdrpp::test::failed = true; return; }
    if (col.z != 0.0f) { flog::error("[menu color test] bad blue channel {}", col.z); sdrpp::test::failed = true; return; }

    std::string hex = Menu::colorToHex(ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
    if (hex != "#FF8000") {
        flog::error("[menu color test] colorToHex mismatch, got {}", hex);
        sdrpp::test::failed = true;
    }
}

static void checkInvalidInput() {
    std::vector<std::string> invalid = { "", "#FFF", "#GGGGGG", "FFFFFF", "#FFFFFFFF", "#12345" };
    for (const std::string& hex : invalid) {
        ImVec4 col = Menu::hexToColor(hex, defaultColor);
        if (col.x != defaultColor.x || col.y != defaultColor.y || col.z != defaultColor.z || col.w != defaultColor.w) {
            flog::error("[menu color test] invalid color '{}' was not replaced by the default", hex);
            sdrpp::test::failed = true;
            return;
        }
    }
}

static void checkOptionDefaults() {
    // A menu option that was never colorized must not enable the override
    Menu::MenuOption_t opt;
    opt.name = "Source";
    opt.open = true;
    if (opt.overrideColor || opt.colorsInit) {
        flog::error("[menu color test] default menu option has colors enabled");
        sdrpp::test::failed = true;
        return;
    }

    opt.overrideColor = true;
    opt.colorsInit = true;
    opt.fontColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    opt.bgColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
    if (Menu::colorToHex(opt.fontColor) != "#FF0000" || Menu::colorToHex(opt.bgColor) != "#0000FF") {
        flog::error("[menu color test] menu option colors serialized incorrectly");
        sdrpp::test::failed = true;
    }
}

void runAll() {
    checkHexRoundTrip();
    checkHexValues();
    checkInvalidInput();
    checkOptionDefaults();
    if (!sdrpp::test::failed) { flog::info("[menu color test] all unit checks passed"); }
}

} // namespace

static void setup_menu_color_unit() {
    sdrpp::test::renderLoopHook.verifyResultsFrames = 12;
    sdrpp::test::renderLoopHook.onStableRender = []() { runAll(); };
}

REGISTER_TEST(menu_color_units, ::setup_menu_color_unit);
