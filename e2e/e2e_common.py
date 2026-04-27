#!/usr/bin/env python3
"""
E2E Test Common Utilities for SDR++

This module provides all boilerplate code for E2E tests including:
- HTTP communication with SDR++ debug API
- Process management (start, stop, cleanup)
- Configuration helpers (via environment variables)
- Test context managers
- Statistics reporting for CI/aggregator consumption

Environment Variables:
    E2E_HTTP_PORT       - HTTP debug port (default: auto-assigned per test)
    E2E_BUILD_DIR       - Path to build directory
    E2E_ROOT_DEV        - Path to root_dev
    E2E_BINARY          - Binary name (default: ./sdrpp)
    E2E_VERBOSE         - Set to "1" for verbose output, "0" for stats only
    E2E_STARTUP_TIMEOUT - Startup timeout in seconds (default: 20.0)
    E2E_SHUTDOWN_TIMEOUT - Shutdown timeout in seconds (default: 5.0)
"""

import json
import os
import subprocess
import sys
import tempfile
import time
import shutil
from pathlib import Path
from typing import Dict, Any, Optional, List, Callable, TextIO
import urllib.request


# =============================================================================
# Environment Variable Configuration
# =============================================================================

def get_env_int(name: str, default: int) -> int:
    """Get integer from environment variable."""
    val = os.environ.get(name)
    if val is not None:
        try:
            return int(val)
        except ValueError:
            pass
    return default


def get_env_float(name: str, default: float) -> float:
    """Get float from environment variable."""
    val = os.environ.get(name)
    if val is not None:
        try:
            return float(val)
        except ValueError:
            pass
    return default


def get_env_str(name: str, default: str) -> str:
    """Get string from environment variable."""
    return os.environ.get(name, default)


def get_env_bool(name: str, default: bool = False) -> bool:
    """Get boolean from environment variable (1/true/yes = True)."""
    val = os.environ.get(name, "").lower()
    if val in ("1", "true", "yes", "on"):
        return True
    if val in ("0", "false", "no", "off"):
        return False
    return default


# Default paths (can be overridden via env vars)
DEFAULT_BUILD_DIR = get_env_str("E2E_BUILD_DIR", "/Users/san/Fun/SDRPlusPlus/cmake-build-debug")
DEFAULT_ROOT_DEV = get_env_str("E2E_ROOT_DEV", "/Users/san/Fun/SDRPlusPlus/root_dev")
DEFAULT_HTTP_PORT = get_env_int("E2E_HTTP_PORT", 8085)
DEFAULT_BINARY = get_env_str("E2E_BINARY", "./sdrpp_brown")
DEFAULT_STARTUP_TIMEOUT = get_env_float("E2E_STARTUP_TIMEOUT", 20.0)
DEFAULT_SHUTDOWN_TIMEOUT = get_env_float("E2E_SHUTDOWN_TIMEOUT", 5.0)

# Statistics collection mode (set by test runner)
STATS_MODE = get_env_bool("E2E_STATS_MODE", False)


# =============================================================================
# Statistics Reporter
# =============================================================================

class TestStats:
    """Collects and reports test statistics.
    
    In stats mode (E2E_STATS_MODE=1), outputs machine-parseable lines:
        TEST_START|<test_name>
        TEST_RESULT|<test_name>|<PASS|FAIL>|<message>
        TEST_END|<test_name>
        
    In verbose mode, outputs human-readable format.
    """
    
    def __init__(self, verbose: Optional[bool] = None):
        self.verbose = verbose if verbose is not None else not STATS_MODE
        self.stats_mode = not self.verbose
        self.results: List[Dict[str, Any]] = []
        
    def _out(self, message: str) -> None:
        """Output a message (respects stats mode)."""
        if self.verbose:
            print(message)
        # In stats mode, only output machine-parseable lines
        
    def _stat(self, message: str) -> None:
        """Output a machine-parseable stat line (always)."""
        print(message, flush=True)
        
    def section(self, title: str) -> None:
        """Print a section header."""
        if self.verbose:
            print("\n" + "="*60)
            print(title)
            print("="*60)
        else:
            self._stat(f"SECTION|{title}")
    
    def subsection(self, title: str) -> None:
        """Print a subsection header."""
        if self.verbose:
            print(f"\n--- {title} ---")
        else:
            self._stat(f"SUBSECTION|{title}")
    
    def info(self, message: str) -> None:
        """Print info message."""
        self._out(message)
    
    def debug(self, label: str, data: Any) -> None:
        """Print debug data (verbose only)."""
        if self.verbose:
            if isinstance(data, dict):
                print(f"{label}: {json.dumps(data, indent=2)}")
            else:
                print(f"{label}: {data}")
    
    def test_start(self, test_name: str) -> None:
        """Report test start."""
        self._stat(f"TEST_START|{test_name}")
        if self.verbose:
            print(f"\n[{test_name}] ", end='', flush=True)
    
    def test_pass(self, test_name: str, message: str = "") -> None:
        """Report test pass."""
        self._stat(f"TEST_RESULT|{test_name}|PASS|{message}")
        self.results.append({"name": test_name, "status": "PASS", "message": message})
        if self.verbose:
            print(f"PASS")
            if message:
                print(f"  {message}")
    
    def test_fail(self, test_name: str, message: str = "") -> None:
        """Report test fail."""
        self._stat(f"TEST_RESULT|{test_name}|FAIL|{message}")
        self.results.append({"name": test_name, "status": "FAIL", "message": message})
        if self.verbose:
            print(f"FAIL")
            if message:
                print(f"  {message}")
    
    def test_end(self, test_name: str) -> None:
        """Report test end."""
        self._stat(f"TEST_END|{test_name}")
    
    def final_summary(self, total: int, passed: int, failed: int) -> None:
        """Print final summary."""
        self._stat(f"SUMMARY|total={total}|passed={passed}|failed={failed}")
        if self.verbose:
            print("\n" + "="*60)
            print(f"SUMMARY: {total} total, {passed} passed, {failed} failed")
            print("="*60)


# Global stats instance (tests can use this)
stats = TestStats()


def set_stats_mode(enabled: bool = True) -> None:
    """Set statistics mode globally."""
    global stats
    stats = TestStats(verbose=not enabled)


# =============================================================================
# HTTP Communication
# =============================================================================

def http_post(base_url: str, path: str, json_data: Optional[Dict] = None, timeout: float = 5.0) -> Dict:
    """Make HTTP POST request to SDR++ debug API."""
    url = f"{base_url}{path}"
    try:
        data = json.dumps(json_data).encode() if json_data else b''
        req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except Exception as e:
        return {"error": str(e)}


def module_cmd(base_url: str, instance_name: str, cmd: str, args: str = "") -> Dict:
    """Execute a command on a module instance."""
    return http_post(base_url, f"/module/{instance_name.replace(' ', '%20')}/command",
                    json_data={"cmd": cmd, "args": args})


def wait_for_server(base_url: str, timeout: float = DEFAULT_STARTUP_TIMEOUT) -> bool:
    """Wait for SDR++ HTTP debug server to be ready."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            with urllib.request.urlopen(f"{base_url}/status", timeout=0.5) as resp:
                data = json.loads(resp.read().decode())
                if data.get("mainLoopStarted"):
                    return True
        except:
            pass
        time.sleep(0.1)
    return False


def kill_existing_sdrpp(http_port: int) -> None:
    """Kill any existing SDR++ instance using the specified HTTP port."""
    subprocess.run(["pkill", "-f", f"sdrpp.*--http.*{http_port}"], capture_output=True)
    time.sleep(0.5)


# =============================================================================
# Configuration Helpers
# =============================================================================

def get_base_config(build_dir: str = DEFAULT_BUILD_DIR, root_dev: str = DEFAULT_ROOT_DEV) -> Dict[str, Any]:
    """Get base SDR++ configuration with common settings.
    
    Paths are set explicitly to ensure SDR++ can find modules and resources.
    """
    return {
        "frequency": 7100000.0,
        "sampleRate": 48000.0,
        "modulesDirectory": "/Users/san/Fun/SDRPlusPlus/root_dev/inst/lib/sdrpp_brown/plugins",
        "resourcesDirectory": "/Users/san/Fun/SDRPlusPlus/root_dev/inst/share/sdrpp_brown",
        "moduleInstances": {
            "Radio": {"module": "radio", "enabled": True},
            "NullAudioSink": {"module": "null_audio_sink", "enabled": True},
        },
        "streams": {
            "Radio": {"muted": False, "sink": "None", "volume": 1.0}
        },
        "vfoOffsets": {"Radio": 0.0},
    }


def get_radio_config(demod_id: int = 0, bandwidth: float = 12500.0, 
                     demod_name: str = "FM") -> Dict[str, Any]:
    """Get radio module configuration for a specific demodulator mode."""
    return {
        "Radio": {
            "selectedDemodId": demod_id,
            demod_name: {"bandwidth": bandwidth, "snapInterval": 100.0}
        }
    }


def get_lsb_config(frequency: float = 7100000.0, bandwidth: float = 2700.0) -> tuple:
    """Get (main_config, radio_config) for LSB mode testing."""
    main_config = get_base_config()
    main_config["frequency"] = frequency
    radio_config = {
        "Radio": {
            "selectedDemodId": 6,  # LSB
            "LSB": {"bandwidth": bandwidth, "snapInterval": 100.0}
        }
    }
    return main_config, radio_config


# =============================================================================
# Test Context Manager
# =============================================================================

class SDRPPTestContext:
    """Context manager for running SDR++ E2E tests.
    
    Configuration is read from environment variables if not explicitly provided.
    
    Usage:
        with SDRPPTestContext() as ctx:
            ctx.write_configs(main_config, radio_config)
            ctx.start()
            # ... run tests using ctx.module_cmd() ...
    """
    
    _port_counter = 8085  # Class-level port counter
    
    def __init__(
        self,
        http_port: Optional[int] = None,
        build_dir: Optional[str] = None,
        root_dev: Optional[str] = None,
        binary: Optional[str] = None,
        headless: bool = True,
        startup_timeout: Optional[float] = None,
        shutdown_timeout: Optional[float] = None
    ):
        # Use env vars or defaults if not explicitly provided
        self.http_port = http_port if http_port is not None else get_env_int("E2E_HTTP_PORT", SDRPPTestContext._port_counter)
        self.build_dir = build_dir if build_dir is not None else DEFAULT_BUILD_DIR
        self.root_dev = root_dev if root_dev is not None else DEFAULT_ROOT_DEV
        self.binary = binary if binary is not None else DEFAULT_BINARY
        self.headless = headless
        self.startup_timeout = startup_timeout if startup_timeout is not None else DEFAULT_STARTUP_TIMEOUT
        self.shutdown_timeout = shutdown_timeout if shutdown_timeout is not None else DEFAULT_SHUTDOWN_TIMEOUT
        
        # Auto-increment port for next instance if using default
        if http_port is None and "E2E_HTTP_PORT" not in os.environ:
            SDRPPTestContext._port_counter += 1
        
        self.base_url = f"http://localhost:{self.http_port}"
        self.temp_dir: Optional[str] = None
        self.proc: Optional[subprocess.Popen] = None
        self.log_path: Optional[str] = None
        self._started = False
    
    def __enter__(self):
        """Create temp directory and prepare environment."""
        self.temp_dir = tempfile.mkdtemp(prefix="sdrpp_e2e_")
        self.log_path = f"{self.temp_dir}/sdrpp.log"
        kill_existing_sdrpp(self.http_port)
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Stop process and cleanup temp directory."""
        self.stop()
        if self.temp_dir:
            shutil.rmtree(self.temp_dir, ignore_errors=True)
        return False
    
    def write_configs(
        self,
        main_config: Dict[str, Any],
        radio_config: Optional[Dict[str, Any]] = None,
        freq_manager_config: Optional[Dict[str, Any]] = None
    ) -> None:
        """Write configuration files to temp directory."""
        with open(f"{self.temp_dir}/config.json", 'w') as f:
            json.dump(main_config, f, indent=2)
        
        if radio_config:
            with open(f"{self.temp_dir}/radio_config.json", 'w') as f:
                json.dump(radio_config, f, indent=2)
        
        if freq_manager_config:
            with open(f"{self.temp_dir}/frequency_manager_config.json", 'w') as f:
                json.dump(freq_manager_config, f, indent=2)
    
    def start(self, extra_args: Optional[List[str]] = None) -> bool:
        """Start SDR++ process and wait for server to be ready.
        
        Args:
            extra_args: Additional command line arguments
            
        Returns:
            True if started successfully, False otherwise
        """
        if self._started:
            return True
        
        env = os.environ.copy()
        if self.headless:
            env['QT_QPA_PLATFORM'] = 'offscreen'
        
        cmd = [self.binary, '-r', self.temp_dir, '--http', str(self.http_port)]
        if extra_args:
            cmd.extend(extra_args)
        
        self.log_path = f"{self.temp_dir}/sdrpp.log"
        self.log_file = open(self.log_path, 'w')
        
        self.proc = subprocess.Popen(
            cmd,
            stdout=self.log_file,
            stderr=self.log_file,
            env=env,
            cwd=self.build_dir
        )
        
        if not wait_for_server(self.base_url, self.startup_timeout):
            stats.info("FAILED to start SDR++")
            self._print_log_tail(2000)
            self.stop()
            return False
        
        self._started = True
        return True
    
    def stop(self) -> None:
        """Stop SDR++ process gracefully."""
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=self.shutdown_timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
            self.proc = None
            self._started = False
        
        if hasattr(self, 'log_file') and self.log_file:
            self.log_file.close()
    
    def http_post(self, path: str, json_data: Optional[Dict] = None, timeout: float = 5.0) -> Dict:
        """Make HTTP POST request to SDR++ debug API."""
        return http_post(self.base_url, path, json_data, timeout)
    
    def module_cmd(self, instance_name: str, cmd: str, args: str = "") -> Dict:
        """Execute a command on a module instance."""
        return module_cmd(self.base_url, instance_name, cmd, args)
    
    def sleep(self, seconds: float) -> None:
        """Sleep for specified seconds (utility for test timing)."""
        time.sleep(seconds)
    
    def _print_log_tail(self, chars: int = 3000) -> None:
        """Print last N characters of log file."""
        if self.log_path and os.path.exists(self.log_path):
            with open(self.log_path, 'r') as f:
                content = f.read()
                stats.info(f"\n--- SDR++ Log (last {chars} chars) ---")
                stats.info(content[-chars:] if len(content) > chars else content)
    
    def print_log_tail(self, chars: int = 3000) -> None:
        """Public method to print log tail."""
        self._print_log_tail(chars)
    
    def find_in_log(self, pattern: str, context_before: int = 200, context_after: int = 500) -> Optional[str]:
        """Find pattern in log and return surrounding context."""
        if not self.log_path or not os.path.exists(self.log_path):
            return None
        
        with open(self.log_path, 'r') as f:
            content = f.read()
        
        idx = content.find(pattern)
        if idx == -1:
            return None
        
        start = max(0, idx - context_before)
        end = min(len(content), idx + len(pattern) + context_after)
        return content[start:end]


# =============================================================================
# Assertion Helpers
# =============================================================================

def assert_response_ok(resp: Dict, message: str = "Operation") -> tuple:
    """Check if response contains error.
    
    Returns:
        (success: bool, error_message: str)
    """
    if "error" in resp:
        return False, f"{message} failed: {resp['error']}"
    return True, ""


def assert_field_equals(resp: Dict, field: str, expected: Any, message: str = "Field") -> tuple:
    """Check if response field equals expected value.
    
    Returns:
        (success: bool, error_message: str)
    """
    actual = resp.get(field)
    if actual != expected:
        return False, f"{message}: expected {expected}, got {actual}"
    return True, ""


# =============================================================================
# Test Runner (used by __main__)
# =============================================================================

def run_single_test_file(test_file: Path, verbose: bool = False) -> Dict[str, Any]:
    """Run a single test file as a subprocess.
    
    Returns:
        Dict with results: {"filename": str, "total": int, "passed": int, "failed": int, "tests": [...]}
    """
    import re
    
    filename = test_file.name
    result = {
        "filename": filename,
        "total": 0,
        "passed": 0,
        "failed": 0,
        "tests": []
    }
    
    # Run the test file as a subprocess with stats mode enabled
    env = os.environ.copy()
    if not verbose:
        env["E2E_STATS_MODE"] = "1"
    
    try:
        proc = subprocess.run(
            [sys.executable, str(test_file)],
            capture_output=True,
            text=True,
            timeout=300,  # 5 minute timeout per test file
            env=env
        )
        
        output = proc.stdout + proc.stderr
        
        # Parse machine-readable output
        test_start_pattern = re.compile(r'^TEST_START\|(.+)$', re.MULTILINE)
        test_result_pattern = re.compile(r'^TEST_RESULT\|([^|]+)\|([^|]+)\|(.*)$', re.MULTILINE)
        summary_pattern = re.compile(r'^SUMMARY\|total=(\d+)\|passed=(\d+)\|failed=(\d+)$', re.MULTILINE)
        
        # Find all test results
        for match in test_result_pattern.finditer(output):
            test_name = match.group(1)
            status = match.group(2)
            message = match.group(3)
            
            result["total"] += 1
            if status == "PASS":
                result["passed"] += 1
            else:
                result["failed"] += 1
            
            result["tests"].append({
                "name": test_name,
                "status": status,
                "message": message
            })
        
        # If no machine-readable output, check exit code
        if result["total"] == 0:
            result["total"] = 1
            if proc.returncode == 0:
                result["passed"] = 1
                result["tests"].append({"name": "script", "status": "PASS"})
            else:
                result["failed"] = 1
                result["tests"].append({"name": "script", "status": "FAIL", "exit_code": proc.returncode})
        
        # Store output for verbose mode
        result["output"] = output if verbose else ""
        
    except subprocess.TimeoutExpired:
        result["total"] = 1
        result["failed"] = 1
        result["tests"].append({"name": "script", "status": "TIMEOUT"})
    except Exception as e:
        result["total"] = 1
        result["failed"] = 1
        result["tests"].append({"name": "script", "status": "ERROR", "error": str(e)})
    
    return result


def discover_and_run_all(verbose: bool = False) -> tuple:
    """Discover and run all E2E tests.
    
    Returns:
        tuple: (total_tests, passed_tests, failed_tests, results_per_file)
    """
    e2e_dir = Path(__file__).parent
    test_files = sorted([f for f in e2e_dir.glob("test_*.py")])
    
    results_per_file = []
    total_tests = 0
    total_passed = 0
    total_failed = 0
    
    if not verbose:
        print("\n" + "="*70)
        print("SDR++ E2E Test Suite")
        print("="*70)
        print(f"\nFound {len(test_files)} test file(s)\n")
    
    for test_file in test_files:
        if not verbose:
            print(f"Running: {test_file.name}...", end=' ', flush=True)
        
        result = run_single_test_file(test_file, verbose)
        results_per_file.append(result)
        
        total_tests += result["total"]
        total_passed += result["passed"]
        total_failed += result["failed"]
        
        if not verbose:
            status = "PASS" if result["failed"] == 0 else f"FAIL ({result['failed']}/{result['total']})"
            print(status)
        else:
            print(f"\n{'='*70}")
            print(f"Output from {test_file.name}:")
            print(f"{'='*70}")
            print(result.get("output", ""))
    
    return total_tests, total_passed, total_failed, results_per_file


def print_summary(total_tests: int, passed_tests: int, failed_tests: int, 
                  results_per_file: List[Dict]) -> bool:
    """Print formatted summary of test results."""
    print("\n" + "="*70)
    print("TEST SUMMARY")
    print("="*70)
    
    # Per-file breakdown
    print("\nPer-File Results:")
    print("─"*70)
    
    for result in results_per_file:
        filename = result["filename"]
        total = result["total"]
        passed = result["passed"]
        failed = result["failed"]
        
        if failed > 0:
            status = f"FAIL ({failed}/{total} failed)"
        elif "error" in result:
            status = f"ERROR ({result['error']})"
        else:
            status = "PASS"
        
        print(f"  {filename:<35} {passed:>3}/{total:<3} passed  [{status}]")
        
        # Show failed tests for this file
        for test in result.get("tests", []):
            if test["status"] not in ("PASS", "OK"):
                msg = test.get("message", "")
                err = test.get("error", "")
                info = msg or err or test["status"]
                print(f"    └─ {test['name']}: {info}")
    
    # Overall summary
    print("\n" + "─"*70)
    print(f"TOTAL:  {total_tests} test(s)")
    print(f"PASSED: {passed_tests}")
    print(f"FAILED: {failed_tests}")
    print("="*70)
    
    if failed_tests == 0:
        print("✓ All tests passed!")
    else:
        print(f"✗ {failed_tests} test(s) failed")
    
    return failed_tests == 0


def run_all_tests_main():
    """Main entry point when running as __main__."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Run SDR++ E2E tests",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 e2e_common.py              # Run all tests
  python3 e2e_common.py --list       # List test files only
  python3 e2e_common.py --verbose    # Run with full output
  python3 e2e_common.py -f test_lsb  # Run specific test file
        """
    )
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="List test files without running them"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Show verbose output (full test output)"
    )
    parser.add_argument(
        "-f", "--file",
        help="Run only a specific test file (partial name match)"
    )
    
    args = parser.parse_args()
    
    if args.list:
        e2e_dir = Path(__file__).parent
        test_files = sorted([f.name for f in e2e_dir.glob("test_*.py")])
        print("\nE2E Test Files:")
        for i, filename in enumerate(test_files, 1):
            print(f"  {i}. {filename}")
        print(f"\nTotal: {len(test_files)} file(s)")
        return 0
    
    if args.file:
        # Run specific test file
        e2e_dir = Path(__file__).parent
        matching = [f for f in e2e_dir.glob("test_*.py") if args.file in f.name]
        if not matching:
            print(f"Error: No test file matching '{args.file}' found")
            return 1
        if len(matching) > 1:
            print(f"Error: Multiple files match '{args.file}':")
            for f in matching:
                print(f"  {f.name}")
            return 1
        
        result = run_single_test_file(matching[0], verbose=True)
        print(result.get("output", ""))
        return 0 if result["failed"] == 0 else 1
    
    # Run all tests
    total, passed, failed, per_file = discover_and_run_all(verbose=args.verbose)
    success = print_summary(total, passed, failed, per_file)
    
    return 0 if success else 1


# =============================================================================
# Backward Compatibility
# =============================================================================

def run_test(test_fn: Callable[[], bool], test_name: str = "E2E Test") -> bool:
    """Backward-compatible test runner. Uses global stats object.
    
    Tests should use this in their main() function:
        def test_something():
            stats.test_start("test_something")
            # ... test logic ...
            if success:
                stats.test_pass("test_something")
                return True
            else:
                stats.test_fail("test_something", "error message")
                return False
        
        if __name__ == "__main__":
            # Check if run directly (verbose) or via runner (stats mode)
            if not STATS_MODE:
                stats.verbose = True
            result = test_something()
            stats.final_summary(1, 1 if result else 0, 0 if result else 1)
            sys.exit(0 if result else 1)
    """
    stats.test_start(test_name)
    
    try:
        result = test_fn()
        if result:
            stats.test_pass(test_name)
        else:
            stats.test_fail(test_name)
    except Exception as e:
        import traceback
        stats.test_fail(test_name, f"Exception: {e}")
        stats.info(traceback.format_exc())
        result = False
    
    stats.test_end(test_name)
    return result


if __name__ == "__main__":
    sys.exit(run_all_tests_main())
