#!/usr/bin/env python3
"""
E2E Test: Frequency Manager with TETRA VFO (non-RadioModuleInterface)

BUG (observed): when the selected VFO is a module that does NOT implement
RadioModuleInterface (e.g. "TETRA Demodulator"), the Frequency Manager
crashes the whole app with a null-pointer dereference:

    FrequencyManagerModule::loadByName -> radio->getDemodIndex() on nullptr
    (radio_module_interface.h:45 / main.cpp:459)

The same null-radio deref exists in the "Add" bookmark button path
(updateModeList / getSelectedDemodId) and in refreshWaterfallBookmarks,
saveByName, importBookmarks, and the bookmark table renderer.

Reproduction config: no "Radio" module instance, so the only VFO is
"TETRA Demodulator" and gui::waterfall.selectedVFO has no
RadioModuleInterface from the very first frame. A bookmark is present in
the selected list so postInit -> loadByName dereferences the null radio.

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
    """Frequency manager config with one bookmark in the shown list."""
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
                    }
                }
            }
        }
    }


def test_frequency_manager_tetra():
    """Test frequency manager survival + bookmark save/load with TETRA VFO."""
    stats.test_start("test_frequency_manager_tetra")

    main_config = get_tetra_only_config()
    fm_config = get_freq_manager_config()

    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config, freq_manager_config=fm_config)

        started = ctx.start()

        # BUG ASSERTION (v1): the app must crash during Frequency Manager
        # postInit -> loadByName -> radio->getDemodIndex(nullptr).
        # The server never becomes ready and the log ends at the FM post-init line.
        log_content = ""
        if ctx.log_path and os.path.exists(ctx.log_path):
            with open(ctx.log_path, 'r') as f:
                log_content = f.read()

        crashed_in_fm_postinit = (
            not started and
            "Running post-init for Frequency Manager" in log_content
        )

        if not crashed_in_fm_postinit:
            stats.test_fail(
                "test_frequency_manager_tetra",
                "Expected crash in Frequency Manager postInit (null RadioModuleInterface "
                "for TETRA VFO), but app did not crash there. "
                f"started={started}, log_tail={log_content[-500:]!r}"
            )
            return False

        stats.test_pass(
            "test_frequency_manager_tetra",
            "BUG reproduced: app crashes in Frequency Manager postInit when the "
            "selected VFO has no RadioModuleInterface (null radio->getDemodIndex)"
        )
        stats.final_summary(1, 1, 0)
        return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
    result = test_frequency_manager_tetra()
    sys.exit(0 if result else 1)
