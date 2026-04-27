#!/usr/bin/env python3
"""
E2E Test: Radio Module Modes List

Tests that the `list_demods` debug command returns:
1. Base modes (NFM, WFM, AM, DSB, USB, CW, LSB, RAW) when only radio module is loaded
2. Extended modes (including DSD and OLD DSD) when ch_extravhf_decoder is also loaded

Environment Variables:
    E2E_VERBOSE=1      - Enable verbose output
    E2E_HTTP_PORT=NNNN - Use specific HTTP port
"""

import json
import sys
from e2e_common import (
    SDRPPTestContext, get_base_config, stats, STATS_MODE,
    assert_response_ok
)


def get_modes_config(with_extravhf: bool = False):
    """Get config with or without ch_extravhf_decoder."""
    config = get_base_config()
    if with_extravhf:
        config["moduleInstances"]["VHF Digital Modes"] = {
            "module": "ch_extravhf_decoder", 
            "enabled": True
        }
    return config


def extract_mode_names(modes_response):
    """Extract sorted list of mode names from response."""
    demods = modes_response.get("demods", [])
    return sorted([d["name"] for d in demods])


def run_modes_test(ctx: SDRPPTestContext, expected_modes: list, test_name: str) -> bool:
    """Run a single modes test."""
    stats.test_start(test_name)
    
    if not ctx.start():
        stats.test_fail(test_name, "Failed to start SDR++")
        return False
    
    # Debug: print log section about module loading
    log_section = ctx.find_in_log("ch_extravhf", 200, 500)
    if log_section:
        stats.debug(f"ch_extravhf log", log_section)
    else:
        log_section = ctx.find_in_log("Loading modules", 0, 3000)
        if log_section:
            stats.debug(f"Module loading log", log_section)
    
    # Query list_demods - essence of the test
    resp = ctx.module_cmd("Radio", "list_demods")
    stats.debug("list_demods response", resp)
    
    ok, msg = assert_response_ok(resp, "list_demods")
    if not ok:
        stats.test_fail(test_name, msg)
        return False
    
    mode_names = extract_mode_names(resp)
    stats.info(f"Modes found: {mode_names}")
    
    # Verify expected modes
    if set(mode_names) != set(expected_modes):
        msg = f"Expected {expected_modes}, got {mode_names}"
        stats.test_fail(test_name, msg)
        return False
    
    stats.test_pass(test_name, f"Got {len(mode_names)} expected modes")
    return True


def test_radio_modes_basic():
    """Test 1: Radio module alone should give base modes."""
    stats.subsection("Radio module with base modes only")
    
    config = get_modes_config(with_extravhf=False)
    
    with SDRPPTestContext() as ctx:
        ctx.write_configs(config)
        
        expected_base = ["AM", "CW", "DSB", "LSB", "NFM", "RAW", "USB", "WFM"]
        
        result = run_modes_test(ctx, expected_base, "test_radio_modes_basic")
        
        if result:
            # Extra check: should NOT have DSD modes
            resp = ctx.module_cmd("Radio", "list_demods")
            mode_names = extract_mode_names(resp)
            if "DSD" in mode_names or "oldDSD" in mode_names:
                stats.test_fail("test_radio_modes_basic", "Found DSD modes when ch_extravhf_decoder not loaded")
                return False
        
        return result


def test_radio_modes_with_extravhf():
    """Test 2: Radio + ch_extravhf_decoder should give extended modes."""
    stats.subsection("Radio module with ch_extravhf_decoder")
    
    config = get_modes_config(with_extravhf=True)
    
    with SDRPPTestContext() as ctx:
        ctx.write_configs(config)
        
        expected_extended = ["AM", "CW", "DSB", "DSD", "LSB", "NFM", "oldDSD", "RAW", "USB", "WFM"]
        
        result = run_modes_test(ctx, expected_extended, "test_radio_modes_with_extravhf")
        
        if result:
            # Extra check: MUST have DSD modes
            resp = ctx.module_cmd("Radio", "list_demods")
            mode_names = extract_mode_names(resp)
            if "DSD" not in mode_names or "oldDSD" not in mode_names:
                stats.test_fail("test_radio_modes_with_extravhf", "DSD and oldDSD modes not found when ch_extravhf_decoder loaded")
                return False
        
        return result


def main():
    """Run all radio modes tests."""
    if not STATS_MODE:
        stats.section("Radio Module Modes E2E Test")
        stats.info("Tests list_demods command with and without ch_extravhf_decoder")
    
    test1_passed = test_radio_modes_basic()
    test2_passed = test_radio_modes_with_extravhf()
    
    total = 2
    passed = sum([test1_passed, test2_passed])
    failed = total - passed
    
    if not STATS_MODE:
        stats.section("SUMMARY")
        stats.info(f"Test 1 (base modes): {'PASS' if test1_passed else 'FAIL'}")
        stats.info(f"Test 2 (extended modes): {'PASS' if test2_passed else 'FAIL'}")
    
    stats.final_summary(total, passed, failed)
    
    return test1_passed and test2_passed


if __name__ == "__main__":
    # Enable verbose mode if run directly and not in stats mode
    if not STATS_MODE:
        stats.verbose = True
    
    result = main()
    sys.exit(0 if result else 1)
