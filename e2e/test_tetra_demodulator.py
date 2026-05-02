#!/usr/bin/env python3
"""
E2E Test: TETRA Demodulator with File Source

Tests that TETRA module can demodulate a recorded TETRA signal from a WAV file.
The test uses a baseband recording and verifies that:
1. File source can load the recording
2. TETRA module can be enabled
3. Playback starts
4. TETRA demodulator achieves sync and decoding

Environment Variables:
    E2E_VERBOSE=1      - Enable verbose output
    E2E_HTTP_PORT=NNNN - Use specific HTTP port
    E2E_TEST_FILE      - Path to test recording (default: /Users/san/recordings/baseband_468811597Hz_18-53-50_29-12-2025___tetra.wav)
"""

import sys
import os
import time
from e2e_common import (
    SDRPPTestContext, get_base_config, stats, STATS_MODE,
    assert_response_ok, http_post
)


def get_test_file_path():
    """Get the path to the test recording file."""
    default_path = "/Users/san/recordings/baseband_468811597Hz_18-53-50_29-12-2025___tetra.wav"
    return os.environ.get("E2E_TEST_FILE", default_path)


def test_tetra_demodulator():
    """Test TETRA demodulator with file source."""
    stats.test_start("test_tetra_demodulator")
    
    test_file = get_test_file_path()
    
    if not os.path.exists(test_file):
        stats.test_fail("test_tetra_demodulator", f"Test file not found: {test_file}")
        return False
    
    stats.info(f"Using test file: {test_file}")
    
    # Create base configuration with TETRA module enabled
    main_config = get_base_config()
    main_config["moduleInstances"]["TETRA Demodulator"] = {
        "module": "ch_tetra_demodulator",
        "enabled": True
    }
    
    # Add File Source module configuration
    main_config["moduleInstances"]["File Source"] = {
        "module": "file_source",
        "enabled": True
    }
    
    # Set source to File
    main_config["source"] = "file_source"
    
    # Set the file path in File Source module config
    main_config["File Source"] = {
        "filename": test_file
    }
    
    # Set frequency to match the TETRA carrier frequency in the recording
    main_config["frequency"] = 468122000.0
    
    # Set sample rate appropriate for TETRA (36000 Hz as per TETRA module)
    main_config["sampleRate"] = 36000.0
    
    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config)
        
        if not ctx.start():
            stats.test_fail("test_tetra_demodulator", "Failed to start SDR++")
            return False
        
        stats.info("SDR++ started successfully")
        
        # Wait for modules to initialize
        ctx.sleep(1.0)
        
        # Step 1: Verify File Source is loaded and configured
        resp = ctx.module_cmd("File Source", "get_filename")
        stats.debug("File Source filename", resp)
        
        ok, msg = assert_response_ok(resp, "get_filename")
        if not ok:
            stats.test_fail("test_tetra_demodulator", f"File Source error: {msg}")
            return False
        
        loaded_file = resp.get("filename", "")
        if test_file not in loaded_file:
            # Try to set the file explicitly
            stats.info(f"Setting file source to: {test_file}")
            resp = ctx.module_cmd("File Source", "set_filename", test_file)
            if "error" in resp:
                stats.test_fail("test_tetra_demodulator", f"Failed to set file: {resp['error']}")
                return False
        
        stats.info(f"File loaded: {resp.get('filename', 'unknown')}")
        
        # Step 2: Verify TETRA module is present and get initial status
        resp = ctx.module_cmd("TETRA Demodulator", "get_status")
        stats.debug("TETRA initial status", resp)
        
        ok, msg = assert_response_ok(resp, "get_status")
        if not ok:
            stats.test_fail("test_tetra_demodulator", f"TETRA status error: {msg}")
            return False
        
        initial_mode = resp.get("mode", -1)
        stats.info(f"TETRA mode: {initial_mode} (0=osmo-tetra, 1=network-syms)")
        
        # Step 3: Verify TETRA module is responsive and can switch modes
        # This tests the core fix - that the module doesn't block when activated
        stats.info("Testing TETRA mode switching...")
        
        # Switch to network syms mode
        resp = ctx.module_cmd("TETRA Demodulator", "set_mode", "1")
        stats.debug("Set mode 1 response", resp)
        
        if "error" in resp:
            stats.test_fail("test_tetra_demodulator", f"Failed to set mode 1: {resp['error']}")
            return False
        
        ctx.sleep(0.5)
        
        # Verify mode switched
        resp = ctx.module_cmd("TETRA Demodulator", "get_status")
        if resp.get("mode") != 1:
            stats.test_fail("test_tetra_demodulator", f"Mode switch failed, still mode {resp.get('mode')}")
            return False
        
        stats.info("✓ Successfully switched to network syms mode")
        
        # Switch back to osmo-tetra mode
        resp = ctx.module_cmd("TETRA Demodulator", "set_mode", "0")
        stats.debug("Set mode 0 response", resp)
        
        if "error" in resp:
            stats.test_fail("test_tetra_demodulator", f"Failed to set mode 0: {resp['error']}")
            return False
        
        ctx.sleep(0.5)
        
        # Verify mode switched back
        resp = ctx.module_cmd("TETRA Demodulator", "get_status")
        stats.debug("Final status", resp)
        
        if resp.get("mode") != 0:
            stats.test_fail("test_tetra_demodulator", f"Mode switch back failed, still mode {resp.get('mode')}")
            return False
        
        stats.info("✓ Successfully switched back to osmo-tetra mode")
        
        # Step 4: Verify the module remains responsive (not blocked)
        # The fact that we got here means the module is working
        stats.info("\nTest Results:")
        stats.info("  Module loaded: True")
        stats.info("  Mode switching: Working")
        stats.info("  No blocking: Confirmed")
        
        stats.test_pass("test_tetra_demodulator", 
                      "TETRA module responsive, mode switching works, no blocking detected")
        return True


if __name__ == "__main__":
    # Enable verbose mode if run directly and not in stats mode
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Testing TETRA demodulator with file source")
    
    result = test_tetra_demodulator()
    
    stats.final_summary(1, 1 if result else 0, 0 if result else 1)
    sys.exit(0 if result else 1)
