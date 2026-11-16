#!/usr/bin/env python3
"""
E2E Test: Frequency Manager Bookmark Mode Persistence & Dynamic Modes (#91)

Tests:
1. Toggling between lists preserves demodulation modes (AM, NFM, WFM, DSD) without
   reverting to Unspecified.
2. Modes persist cleanly across application restarts.
3. Dynamic modes (DSD / oldDSD from ch_extravhf_decoder):
   - Initial list with DSD bookmark survives Frequency Manager::postInit() running
     before VHF Digital Modes::postInit().
   - add_bookmark by dynamic mode name ("DSD") stores DemodID 4865 cleanly.
   - Disabling VHF Digital Modes (DSD gone) and saving the list preserves mode=4865
     in config instead of overwriting it with NFM (0) or Unspecified (-1).
   - Re-enabling VHF Digital Modes (DSD back) restores mode="DSD" and applies it.

Environment Variables:
    E2E_VERBOSE=1      - Enable verbose output
    E2E_HTTP_PORT=NNNN - Use specific HTTP port
"""

import json
import os
import sys
from e2e_common import (
    SDRPPTestContext, get_base_config, get_radio_config,
    stats, STATS_MODE,
    assert_response_ok, assert_field_equals
)


def make_main_config(with_extravhf: bool):
    cfg = get_base_config()
    cfg["moduleInstances"]["Frequency Manager"] = {"module": "frequency_manager", "enabled": True}
    cfg["moduleInstances"]["VHF Digital Modes"] = {
        "module": "ch_extravhf_decoder",
        "enabled": with_extravhf
    }
    cfg["menuElements"] = [
        {"name": "Frequency Manager", "open": True}
    ]
    return cfg


def make_fm_config():
    return {
        "selectedList": "ListDSD",
        "bookmarkDisplayMode": 0,
        "lists": {
            "ListAM": {
                "showOnWaterfall": True,
                "bookmarks": {
                    "AirbandTower": {
                        "frequency": 118100000.0,
                        "bandwidth": 10000.0,
                        "mode": 2,
                        "vfo": "Radio"
                    }
                }
            },
            "ListFM": {
                "showOnWaterfall": True,
                "bookmarks": {
                    "BroadcastFM": {
                        "frequency": 100000000.0,
                        "bandwidth": 150000.0,
                        "mode": 1,
                        "vfo": "Radio"
                    }
                }
            },
            "ListDSD": {
                "showOnWaterfall": True,
                "bookmarks": {
                    "DmrRepeater": {
                        "frequency": 438500000.0,
                        "bandwidth": 12500.0,
                        "mode": 4865,
                        "vfo": "Radio"
                    }
                }
            }
        }
    }


def find_bookmark(resp, name):
    return next((bm for bm in resp.get("bookmarks", []) if bm.get("name") == name), None)


def test_frequency_manager_modes():
    if not STATS_MODE:
        stats.section("Testing Frequency Manager Mode Persistence & Dynamic Modes")

    total = 0
    passed = 0

    def check(name, cond, err_msg):
        nonlocal total, passed
        total += 1
        if cond:
            stats.test_pass(name)
            passed += 1
            return True
        stats.test_fail(name, err_msg)
        return False

    main_cfg = make_main_config(with_extravhf=True)
    radio_cfg = get_radio_config(demod_id=0, bandwidth=12500.0, demod_name="FM")
    fm_cfg = make_fm_config()

    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_cfg, radio_cfg, fm_cfg)
        if not ctx.start():
            stats.final_summary(1, 0, 1)
            return False

        # 1. Startup order: Frequency Manager postInit ran BEFORE VHF Digital Modes postInit.
        #    DmrRepeater (mode=4865) in selectedList ("ListDSD") must still resolve to DSD!
        resp = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        dmr = find_bookmark(resp, "DmrRepeater")
        ok = dmr is not None and dmr.get("mode") == "DSD" and dmr.get("mode_id") == 4865
        check("startup_dsd_resolved", ok, f"Expected DmrRepeater mode=DSD id=4865, got {dmr}")

        # 2. Applying DmrRepeater selects DSD in Radio
        ctx.module_cmd("Frequency Manager", "apply_bookmark", "DmrRepeater")
        demod_resp = ctx.module_cmd("Radio", "get_demod")
        check("apply_dsd_demod", demod_resp.get("demod") == "DSD", f"Expected Radio demod=DSD, got {demod_resp}")

        # 3. add_bookmark by dynamic mode name "DSD"
        add_resp = ctx.module_cmd("Frequency Manager", "add_bookmark", "P25Test|446100000|12500|DSD")
        ok = add_resp.get("status") == "ok" and add_resp.get("mode_id") == 4865 and add_resp.get("mode_index", -1) >= 8
        check("add_dsd_by_name", ok, f"Expected mode_id=4865 and valid index, got {add_resp}")

        # 4. Toggle lists: ListDSD -> ListAM -> ListFM -> ListAM -> ListDSD
        ctx.module_cmd("Frequency Manager", "set_current_list", "ListAM")
        ctx.module_cmd("Frequency Manager", "add_bookmark", "AirbandAtis|126400000|10000|AM")
        ctx.module_cmd("Frequency Manager", "set_current_list", "ListFM")
        ctx.module_cmd("Frequency Manager", "add_bookmark", "SimplexNFM|145500000|12500|NFM")

        # Toggle back to ListAM and verify modes did not revert
        ctx.module_cmd("Frequency Manager", "set_current_list", "ListAM")
        resp_am = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        tower = find_bookmark(resp_am, "AirbandTower")
        atis = find_bookmark(resp_am, "AirbandAtis")
        check(
            "toggle_list_am_preserved",
            tower is not None and tower.get("mode") == "AM" and atis is not None and atis.get("mode") == "AM",
            f"Expected AM bookmarks after toggle, got {resp_am}"
        )

        # Apply AirbandAtis and verify Radio switches to AM
        ctx.module_cmd("Frequency Manager", "apply_bookmark", "AirbandAtis")
        demod_am = ctx.module_cmd("Radio", "get_demod")
        check("apply_am_demod", demod_am.get("demod") == "AM", f"Expected Radio demod=AM, got {demod_am}")

        # Toggle to ListFM and verify WFM + NFM preserved
        ctx.module_cmd("Frequency Manager", "set_current_list", "ListFM")
        resp_fm = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        bcast = find_bookmark(resp_fm, "BroadcastFM")
        simplex = find_bookmark(resp_fm, "SimplexNFM")
        check(
            "toggle_list_fm_preserved",
            bcast is not None and bcast.get("mode") == "WFM" and simplex is not None and simplex.get("mode") == "NFM",
            f"Expected WFM & NFM bookmarks after toggle, got {resp_fm}"
        )

        # Wait for debounced ConfigSaveWorker then stop and restart
        ctx.sleep(1.5)
        ctx.stop()

        # 5. Restart with VHF Digital Modes DISABLED (DSD goes away!)
        with open(os.path.join(ctx.temp_dir, "config.json"), "r") as f:
            disk_main = json.load(f)
        disk_main["moduleInstances"]["VHF Digital Modes"]["enabled"] = False
        with open(os.path.join(ctx.temp_dir, "config.json"), "w") as f:
            json.dump(disk_main, f, indent=4)

        if not ctx.start():
            check("restart_without_dsd", False, "Failed to restart without DSD")
            stats.final_summary(total, passed, total - passed)
            return False

        # Switch to ListAM & verify AM persisted across close/reopen
        ctx.module_cmd("Frequency Manager", "set_current_list", "ListAM")
        resp_am2 = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        atis2 = find_bookmark(resp_am2, "AirbandAtis")
        check("restart_am_preserved", atis2 is not None and atis2.get("mode") == "AM", f"Lost AM after restart: {resp_am2}")

        # Switch to ListDSD while DSD is unloaded, mutate another bookmark to trigger saveByName
        ctx.module_cmd("Frequency Manager", "set_current_list", "ListDSD")
        resp_dsd_off = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        dmr_off = find_bookmark(resp_dsd_off, "DmrRepeater")
        p25_off = find_bookmark(resp_dsd_off, "P25Test")
        check(
            "dsd_off_keeps_mode_id",
            dmr_off is not None and dmr_off.get("mode_id") == 4865 and p25_off is not None and p25_off.get("mode_id") == 4865,
            f"Expected mode_id=4865 while DSD unloaded, got {resp_dsd_off}"
        )

        # Trigger saveByName while DSD is absent
        ctx.module_cmd("Frequency Manager", "add_bookmark", "TempBookmark|144800000|12500|NFM")
        ctx.module_cmd("Frequency Manager", "remove_bookmark", "TempBookmark")
        ctx.sleep(1.5)
        ctx.stop()

        # Check config file on disk still has 4865 for DmrRepeater and P25Test
        with open(os.path.join(ctx.temp_dir, "frequency_manager_config.json"), "r") as f:
            disk_fm = json.load(f)
        dsd_bms = disk_fm.get("lists", {}).get("ListDSD", {}).get("bookmarks", {})
        check(
            "disk_dsd_not_corrupted_after_save",
            dsd_bms.get("DmrRepeater", {}).get("mode") == 4865 and dsd_bms.get("P25Test", {}).get("mode") == 4865,
            f"Disk config corrupted when saving while DSD unloaded: {dsd_bms}"
        )

        # 6. Re-enable VHF Digital Modes (DSD comes back!) and verify recovery
        disk_main["moduleInstances"]["VHF Digital Modes"]["enabled"] = True
        with open(os.path.join(ctx.temp_dir, "config.json"), "w") as f:
            json.dump(disk_main, f, indent=4)

        if not ctx.start():
            check("restart_with_dsd_back", False, "Failed to restart with DSD back")
            stats.final_summary(total, passed, total - passed)
            return False

        resp_dsd_back = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        dmr_back = find_bookmark(resp_dsd_back, "DmrRepeater")
        p25_back = find_bookmark(resp_dsd_back, "P25Test")
        ok_back = (
            dmr_back is not None and dmr_back.get("mode") == "DSD" and dmr_back.get("mode_id") == 4865 and
            p25_back is not None and p25_back.get("mode") == "DSD" and p25_back.get("mode_id") == 4865
        )
        check("dsd_back_restores_mode", ok_back, f"Expected DSD restored after re-enable, got {resp_dsd_back}")

        if stats.verbose:
            ctx.print_log_tail(2000)

    stats.final_summary(total, passed, total - passed)
    return passed == total


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
    sys.exit(0 if test_frequency_manager_modes() else 1)
