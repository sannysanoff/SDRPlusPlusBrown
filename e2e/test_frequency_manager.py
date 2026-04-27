#!/usr/bin/env python3
"""
E2E Test: Frequency Manager Debug Protocol

Tests the frequency manager debug commands including:
- get_lists, get_current_list, set_current_list
- get_bookmarks, add_bookmark, remove_bookmark, apply_bookmark
- get_scanner_status, start_scanner, stop_scanner

Environment Variables:
    E2E_VERBOSE=1      - Enable verbose output
    E2E_HTTP_PORT=NNNN - Use specific HTTP port
"""

import json
import sys
from e2e_common import (
    SDRPPTestContext, get_base_config, get_radio_config,
    stats, STATS_MODE,
    assert_response_ok, assert_field_equals
)


def get_freq_manager_config():
    """Get frequency manager config with test data."""
    return {
        "selectedList": "TestList",
        "bookmarkDisplayMode": 0,
        "lists": {
            "TestList": {
                "showOnWaterfall": True,
                "bookmarks": {
                    "TestStation1": {
                        "frequency": 145500000.0,
                        "bandwidth": 12500.0,
                        "mode": 0  # FM
                    },
                    "TestStation2": {
                        "frequency": 446000000.0,
                        "bandwidth": 12500.0,
                        "mode": 0  # FM
                    }
                }
            },
            "EmptyList": {
                "showOnWaterfall": False,
                "bookmarks": {}
            }
        },
        "scanner": {
            "scanIntervalMs": 100.0,
            "listenTimeSec": 5.0,
            "noiseFloor": -120.0,
            "signalMarginDb": 6.0,
            "squelchEnabled": False
        }
    }


def test_frequency_manager():
    """Test frequency manager debug protocol - all 12 test cases."""
    if not STATS_MODE:
        stats.section("Testing Frequency Manager Debug Protocol")
    
    main_config = get_base_config()
    main_config["moduleInstances"]["Frequency Manager"] = {"module": "frequency_manager", "enabled": True}
    radio_config = get_radio_config(demod_id=0, bandwidth=12500.0, demod_name="FM")
    freq_manager_config = get_freq_manager_config()
    
    all_passed = True
    total_tests = 0
    passed_tests = 0
    
    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config, radio_config, freq_manager_config)
        
        if not ctx.start():
            stats.final_summary(0, 0, 1)
            return False
        
        ctx.sleep(2.0)
        
        # Test 1: Get lists
        stats.subsection("Test 1: get_lists")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "get_lists")
        stats.debug("Response", resp)
        ok, msg = assert_response_ok(resp, "get_lists")
        if not ok or "TestList" not in resp.get("lists", []):
            stats.test_fail("test_1_get_lists", msg if msg else "Expected 'TestList' in lists")
            all_passed = False
        else:
            stats.test_pass("test_1_get_lists")
            passed_tests += 1
        
        # Test 2: Get current list
        stats.subsection("Test 2: get_current_list")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "get_current_list")
        stats.debug("Response", resp)
        ok, msg = assert_field_equals(resp, "current_list", "TestList", "Current list")
        if not ok:
            stats.test_fail("test_2_get_current_list", msg)
            all_passed = False
        else:
            stats.test_pass("test_2_get_current_list")
            passed_tests += 1
        
        # Test 3: Get bookmarks
        stats.subsection("Test 3: get_bookmarks")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        stats.debug("Response", resp)
        ok, msg = assert_response_ok(resp, "get_bookmarks")
        bookmarks = resp.get("bookmarks", [])
        if not ok or len(bookmarks) != 2:
            stats.test_fail("test_3_get_bookmarks", msg if msg else f"Expected 2 bookmarks, got {len(bookmarks)}")
            all_passed = False
        else:
            stats.test_pass("test_3_get_bookmarks")
            passed_tests += 1
        
        # Test 4: Add bookmark
        stats.subsection("Test 4: add_bookmark")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "add_bookmark", "NewStation|433500000|12500|WFM")
        stats.debug("Response", resp)
        ok, msg = assert_field_equals(resp, "status", "ok", "Add bookmark")
        if not ok:
            stats.test_fail("test_4_add_bookmark", msg)
            all_passed = False
        else:
            stats.test_pass("test_4_add_bookmark")
            passed_tests += 1
        
        # Test 5: Verify bookmark was added
        stats.subsection("Test 5: Verify added bookmark")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "get_bookmarks")
        bookmarks = resp.get("bookmarks", [])
        new_station = next((bm for bm in bookmarks if bm.get("name") == "NewStation"), None)
        if new_station is None:
            stats.test_fail("test_5_verify_bookmark", "NewStation not found in bookmarks")
            all_passed = False
        elif abs(new_station.get("frequency", 0) - 433500000.0) > 0.1:
            stats.test_fail("test_5_verify_bookmark", f"Expected frequency=433500000, got {new_station.get('frequency')}")
            all_passed = False
        else:
            stats.test_pass("test_5_verify_bookmark")
            passed_tests += 1
        
        # Test 6: Apply bookmark
        stats.subsection("Test 6: apply_bookmark")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "apply_bookmark", "TestStation1")
        stats.debug("Response", resp)
        ok, msg = assert_field_equals(resp, "status", "ok", "Apply bookmark")
        if not ok:
            stats.test_fail("test_6_apply_bookmark", msg)
            all_passed = False
        else:
            stats.test_pass("test_6_apply_bookmark")
            passed_tests += 1
        
        # Test 7: Set current list
        stats.subsection("Test 7: set_current_list")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "set_current_list", "EmptyList")
        stats.debug("Response", resp)
        ok, msg = assert_field_equals(resp, "status", "ok", "Set current list")
        if not ok:
            stats.test_fail("test_7_set_current_list", msg)
            all_passed = False
        else:
            # Verify list was changed
            resp = ctx.module_cmd("Frequency Manager", "get_current_list")
            ok, msg = assert_field_equals(resp, "current_list", "EmptyList", "Current list after change")
            if not ok:
                stats.test_fail("test_7_set_current_list", msg)
                all_passed = False
            else:
                stats.test_pass("test_7_set_current_list")
                passed_tests += 1
        
        # Test 8: Try to start scanner on empty list (should fail)
        stats.subsection("Test 8: start_scanner on empty list (should fail)")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "start_scanner")
        stats.debug("Response", resp)
        if "error" not in resp:
            stats.test_fail("test_8_start_scanner_empty", "Expected error when starting scanner with no bookmarks")
            all_passed = False
        else:
            stats.test_pass("test_8_start_scanner_empty")
            passed_tests += 1
        
        # Switch back to TestList for remaining tests
        ctx.module_cmd("Frequency Manager", "set_current_list", "TestList")
        
        # Test 9: Get scanner status (not scanning)
        stats.subsection("Test 9: get_scanner_status (not scanning)")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "get_scanner_status")
        stats.debug("Response", resp)
        ok, msg = assert_field_equals(resp, "scanning", False, "Scanner status")
        if not ok:
            stats.test_fail("test_9_scanner_status", msg)
            all_passed = False
        else:
            stats.test_pass("test_9_scanner_status")
            passed_tests += 1
        
        # Test 10: Remove bookmark
        stats.subsection("Test 10: remove_bookmark")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "remove_bookmark", "NewStation")
        stats.debug("Response", resp)
        ok, msg = assert_field_equals(resp, "status", "ok", "Remove bookmark")
        if not ok:
            stats.test_fail("test_10_remove_bookmark", msg)
            all_passed = False
        else:
            # Verify bookmark was removed
            resp = ctx.module_cmd("Frequency Manager", "get_bookmarks")
            bookmarks = resp.get("bookmarks", [])
            new_station = next((bm for bm in bookmarks if bm.get("name") == "NewStation"), None)
            if new_station is not None:
                stats.test_fail("test_10_remove_bookmark", "NewStation still exists after removal")
                all_passed = False
            else:
                stats.test_pass("test_10_remove_bookmark")
                passed_tests += 1
        
        # Test 11: Error cases
        stats.subsection("Test 11: Error cases")
        total_tests += 1
        errors = []
        
        # Invalid list name
        resp = ctx.module_cmd("Frequency Manager", "set_current_list", "NonExistentList")
        if "error" not in resp:
            errors.append("Expected error for non-existent list")
        
        # Invalid bookmark name for apply
        resp = ctx.module_cmd("Frequency Manager", "apply_bookmark", "NonExistentBookmark")
        if "error" not in resp:
            errors.append("Expected error for non-existent bookmark")
        
        if errors:
            stats.test_fail("test_11_error_cases", "; ".join(errors))
            all_passed = False
        else:
            stats.test_pass("test_11_error_cases")
            passed_tests += 1
        
        # Test 12: Unknown command
        stats.subsection("Test 12: Unknown command")
        total_tests += 1
        resp = ctx.module_cmd("Frequency Manager", "invalid_command")
        if "error" not in resp:
            stats.test_fail("test_12_unknown_command", "Expected error for unknown command")
            all_passed = False
        else:
            stats.test_pass("test_12_unknown_command")
            passed_tests += 1
        
        # Print log before cleanup (only in verbose mode)
        if stats.verbose:
            ctx.print_log_tail(3000)
        
        failed_tests = total_tests - passed_tests
        stats.final_summary(total_tests, passed_tests, failed_tests)
        
        return all_passed


if __name__ == "__main__":
    # Enable verbose mode if run directly and not in stats mode
    if not STATS_MODE:
        stats.verbose = True
    
    result = test_frequency_manager()
    sys.exit(0 if result else 1)
