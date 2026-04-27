#!/usr/bin/env python3
"""
E2E Test: LSB Bandwidth at Startup

Tests that LSB mode has correct VFO bandwidth (~2.7kHz) not the 100kHz bug.

Environment Variables:
    E2E_VERBOSE=1      - Enable verbose output
    E2E_HTTP_PORT=NNNN - Use specific HTTP port
"""

import sys
from e2e_common import (
    SDRPPTestContext, get_lsb_config, stats, STATS_MODE,
    assert_response_ok
)


def test_lsb_startup():
    """Test LSB bandwidth at startup."""
    stats.test_start("test_lsb_startup")
    
    main_config, radio_config = get_lsb_config(bandwidth=2700.0)
    
    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config, radio_config)
        
        if not ctx.start():
            stats.test_fail("test_lsb_startup", "Failed to start SDR++")
            return False
        
        ctx.sleep(2.0)
        
        # Check demod
        resp = ctx.module_cmd("Radio", "get_demod")
        stats.debug("Demod", resp)
        
        # Check bandwidth - this is the essence of the test
        resp = ctx.module_cmd("Radio", "get_vfo_bandwidth")
        stats.debug("Bandwidth", resp)
        
        ok, msg = assert_response_ok(resp, "get_vfo_bandwidth")
        if not ok:
            stats.test_fail("test_lsb_startup", msg)
            return False
        
        vfo_bw = resp.get("vfo_bandwidth", 0)
        
        # The test: LSB should have ~2.7kHz bandwidth, NOT 100kHz
        if 1700 <= vfo_bw <= 3700:
            stats.test_pass("test_lsb_startup", f"VFO bandwidth: {vfo_bw:.1f} Hz")
            return True
        elif vfo_bw > 50000:
            stats.test_fail("test_lsb_startup", f"100kHz bug detected! VFO bandwidth: {vfo_bw:.1f} Hz")
            return False
        else:
            stats.test_fail("test_lsb_startup", f"Unexpected VFO bandwidth: {vfo_bw:.1f} Hz")
            return False


if __name__ == "__main__":
    # Enable verbose mode if run directly and not in stats mode
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Testing LSB bandwidth at startup")
    
    result = test_lsb_startup()
    
    stats.final_summary(1, 1 if result else 0, 0 if result else 1)
    sys.exit(0 if result else 1)
