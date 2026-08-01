#!/usr/bin/env python3
"""
E2E Test: Frequency Manager with TETRA VFO (non-RadioModuleInterface)

Fixed behavior under test:
- SDR++ must NOT crash when the selected VFO has no RadioModuleInterface
  (e.g. "TETRA Demodulator"). All frequency-manager radio derefs are
  null-guarded (loadByName / refreshWaterfallBookmarks / saveByName /
  updateModeList / Add button / importBookmarks / table / tooltip).
- Bookmarks store the radio (VFO) name in JSON ("vfo" field). Applying a
  bookmark tunes the radio it was saved for, not the currently selected one.
- A bookmark whose radio no longer exists is reported as unavailable
  (vfo_available=false) and applying it is a safe no-op.

Reproduction config: no "Radio" module instance, so the only VFO is
"TETRA Demodulator" and gui::waterfall.selectedVFO has no
RadioModuleInterface from the very first frame.

Environment Variables:
    E2E_VERBOSE=1      - Enable verbose output
    E2E_HTTP_PORT=NNNN - Use specific HTTP port
"""

import os
import sys
from e2e_common import (
    SDRPPTestContext, get_base_config, stats, STATS_MODE,
    assert_response_ok, assert_field_equals
)


def get_tetra_only_config():
    """Config with TETRA Demodulator as the only VFO-creating module."""
    main_config = get_base_config()
    # Remove Radio: its VFO is the one that normally provides RadioModuleInterface
    main_config["moduleInstances"].pop("Radio", None)
    main_config["streams"].pop("Radio", None)
    main_config["vfoOffsets"].pop("Radio", None)
    main_config["moduleInstances"]["TETRA Demodulator"] = {
        "module": "ch_tetra_demodulator",
        "enabled": True
    }
    main_config["moduleInstances"]["Frequency Manager"] = {
        "module": "frequency_manager",
        "enabled": True
    }
    # Keep the Frequency Manager window open so its menu handler runs
    main_config["menuElements"] = [
        {"name": "Frequency Manager", "open": True}
    ]
    return main_config


def get_freq_manager_config():
    """Frequency manager config:
    - TETRA1: legacy bookmark (no vfo field) -> applies to selected VFO
    - Ghost: bookmark for a radio that does not exist -> must be disabled
    """
    return {
        "selectedList": "General",
        "bookmarkDisplayMode": 1,
        "lists": {
            "General": {
                "showOnWaterfall": True,
                "bookmarks": {
                    "TETRA1": {
                        "frequency": 468122000.0,
                        "bandwidth": 45000.0,
                        "mode": 0
                    },
                    "Ghost": {
                        "frequency": 470000000.0,
                        "bandwidth": 45000.0,
                        "mode": 0,
                        "vfo": "Ghost Radio"
                    }
                }
            }
        }
    }


def find_bookmark(bookmarks, name):
    return next((bm for bm in bookmarks if bm.get("name") == name), None)


def test_frequency_manager_tetra():
    """Test frequency manager survival + radio-aware bookmarks with TETRA VFO."""
    stats.test_start("test_frequency_manager_tetra")

    main_config = get_tetra_only_config()
    fm_config = get_freq_manager_config()

    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config, freq_manager_config=fm_config)

        if not ctx.start():
            stats.test_fail(
                "test_frequency_manager_tetra",
                "SDR++ crashed or failed to start with TETRA-only VFO + FM bookmarks "
                "(null RadioModuleInterface crash was NOT fixed)"
            )
            return False
        stats.test_pass("test_frequency_manager_tetra_start", "SDR++ starts without crashing")

        # Test: get_lists
        stats.subsection("Test: get_lists")
        resp = ctx.module_cmd("Frequency Manager", "get_lists")
        ok, msg = assert_response_ok(resp, "get_lists")
        if not ok or "General" not in resp.get("lists", []):
            stats.test_fail("test_get_lists", msg if msg else "Expected 'General' in lists")
            return False
        stats.test_pass("test_get_lists")

        # Test: legacy bookmark (no vfo) + dead-radio bookmark load without crash
        stats.subsection("Test: get_bookmarks (legacy + dead radio)")
        resp = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        ok, msg = assert_response_ok(resp, "get_bookmarks")
        if not ok:
            stats.test_fail("test_get_bookmarks", msg)
            return False
        bookmarks = resp.get("bookmarks", [])
        tetra1 = find_bookmark(bookmarks, "TETRA1")
        ghost = find_bookmark(bookmarks, "Ghost")
        if tetra1 is None or ghost is None:
            stats.test_fail("test_get_bookmarks", f"Expected TETRA1 and Ghost, got {[b.get('name') for b in bookmarks]}")
            return False
        if ghost.get("vfo") != "Ghost Radio" or ghost.get("vfo_available") is not False:
            stats.test_fail("test_get_bookmarks", f"Ghost should be vfo='Ghost Radio' vfo_available=False, got {ghost}")
            return False
        if tetra1.get("vfo_available") is not True:
            stats.test_fail("test_get_bookmarks", f"Legacy bookmark should be available, got {tetra1}")
            return False
        stats.test_pass("test_get_bookmarks", "Dead-radio bookmark reported unavailable, app alive")

        # Test: add_bookmark stores the selected VFO name
        stats.subsection("Test: add_bookmark stores vfo")
        resp = ctx.module_cmd("Frequency Manager", "add_bookmark", "TETRA2|469000000|45000|0")
        ok, msg = assert_field_equals(resp, "status", "ok", "Add bookmark")
        if not ok:
            stats.test_fail("test_add_bookmark", msg)
            return False
        resp = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        tetra2 = find_bookmark(resp.get("bookmarks", []), "TETRA2")
        if tetra2 is None:
            stats.test_fail("test_add_bookmark", "TETRA2 not found after add")
            return False
        if tetra2.get("vfo") != "TETRA Demodulator":
            stats.test_fail("test_add_bookmark", f"Expected vfo='TETRA Demodulator', got {tetra2.get('vfo')!r}")
            return False
        stats.test_pass("test_add_bookmark", f"vfo={tetra2.get('vfo')!r} stored")

        # Test: apply to dead radio is a safe no-op (no crash)
        stats.subsection("Test: apply_bookmark to dead radio")
        resp = ctx.module_cmd("Frequency Manager", "apply_bookmark", "Ghost")
        if "error" in resp:
            stats.test_fail("test_apply_dead_radio", f"Unexpected error: {resp.get('error')}")
            return False
        # App must still be alive and responsive
        resp = ctx.module_cmd("Frequency Manager", "get_current_list")
        if "current_list" not in resp:
            stats.test_fail("test_apply_dead_radio", "App unresponsive after applying dead-radio bookmark")
            return False
        stats.test_pass("test_apply_dead_radio", "Applying dead-radio bookmark is a safe no-op")

        # Test: apply_bookmark targets the bookmark's own radio
        stats.subsection("Test: apply_bookmark targets stored radio")
        resp = ctx.module_cmd("Frequency Manager", "apply_bookmark", "TETRA2")
        ok, msg = assert_field_equals(resp, "vfo", "TETRA Demodulator", "Apply bookmark")
        if not ok:
            stats.test_fail("test_apply_stored_radio", msg)
            return False
        stats.test_pass("test_apply_stored_radio", "Applied to TETRA Demodulator (stored radio)")

        # Test: persistence across restart (vfo survives save/load)
        stats.subsection("Test: bookmark vfo persists across restart")
        # Config save is async (debounced ~1s on the config-save worker thread);
        # give it time to flush before terminating the process.
        ctx.sleep(2.0)
        ctx.stop()
        if not ctx.start():
            stats.test_fail("test_persistence", "SDR++ failed to restart")
            return False
        resp = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        bookmarks = resp.get("bookmarks", [])
        tetra2 = find_bookmark(bookmarks, "TETRA2")
        ghost = find_bookmark(bookmarks, "Ghost")
        if tetra2 is None:
            stats.test_fail("test_persistence", "TETRA2 lost after restart")
            return False
        if tetra2.get("vfo") != "TETRA Demodulator":
            stats.test_fail("test_persistence", f"vfo not persisted, got {tetra2.get('vfo')!r}")
            return False
        if ghost is None or ghost.get("vfo_available") is not False:
            stats.test_fail("test_persistence", "Ghost should still be unavailable after restart")
            return False
        stats.test_pass("test_persistence", "vfo persisted, dead radio still flagged")

        if stats.verbose:
            ctx.print_log_tail(2000)

        stats.final_summary(7, 7, 0)
        return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
    result = test_frequency_manager_tetra()
    sys.exit(0 if result else 1)
