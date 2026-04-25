#!/usr/bin/env python3
"""
E2E Test Framework for SDR++

This module provides utilities for launching SDR++ with specific configurations
and querying its debug HTTP API to verify behavior.
"""

import json
import os
import subprocess
import sys
import tempfile
import time
import atexit
from pathlib import Path
from typing import Dict, Any, Optional, Callable
import requests


class SDRPPTestInstance:
    """Manages a test instance of SDR++"""
    
    DEFAULT_DEBUG_PORT = 8080
    DEFAULT_STARTUP_TIMEOUT = 15.0
    DEFAULT_SHUTDOWN_TIMEOUT = 5.0
    
    def __init__(
        self,
        config: Dict[str, Any],
        radio_config: Optional[Dict[str, Any]] = None,
        binary_path: str = "./sdrpp",
        debug_port: int = DEFAULT_DEBUG_PORT,
        working_dir: Optional[str] = None,
        headless: bool = True
    ):
        """
        Initialize SDR++ test instance.
        
        Args:
            config: Main SDR++ configuration dict (config.json)
            radio_config: Radio module configuration dict (radio_config.json)
            binary_path: Path to sdrpp binary
            debug_port: HTTP debug server port
            working_dir: Working directory (temp dir if None)
            headless: Whether to run in headless/offscreen mode
        """
        self.config = config
        self.radio_config = radio_config
        self.binary_path = binary_path
        self.debug_port = debug_port
        self.working_dir = working_dir or tempfile.mkdtemp(prefix="sdrpp_test_")
        self.headless = headless
        self.process: Optional[subprocess.Popen] = None
        self.config_path: Optional[str] = None
        
    def _prepare_config(self):
        """Prepare config files"""
        # Write main config
        config_path = os.path.join(self.working_dir, "config.json")
        with open(config_path, 'w') as f:
            json.dump(self.config, f, indent=2)
        
        # Write radio config if provided
        if self.radio_config:
            radio_config_path = os.path.join(self.working_dir, "radio_config.json")
            with open(radio_config_path, 'w') as f:
                json.dump(self.radio_config, f, indent=2)
        
        self.config_path = config_path
        return config_path
    
    def start(self) -> 'SDRPPTestInstance':
        """Start SDR++ instance"""
        self._prepare_config()
        
        # Prepare environment
        env = os.environ.copy()
        if self.headless:
            # Use offscreen platform
            env['QT_QPA_PLATFORM'] = 'offscreen'
        
        # Build command: sdrpp -r <root> -p <resources> --http <port>
        # The root directory should contain config.json, radio_config.json, etc.
        resources_dir = os.path.join(os.path.dirname(self.binary_path), "..", "res")
        if not os.path.exists(resources_dir):
            resources_dir = "/Users/san/Fun/SDRPlusPlus/root_dev/res"
        
        cmd = [
            self.binary_path,
            '-r', self.working_dir,  # root directory (contains config.json)
            '-p', resources_dir,       # resources directory
            '--http', str(self.debug_port),  # HTTP debug server port
        ]
        
        # Start process
        stdout_path = os.path.join(self.working_dir, 'sdrpp_stdout.log')
        stderr_path = os.path.join(self.working_dir, 'sdrpp_stderr.log')
        
        self.stdout_file = open(stdout_path, 'w')
        self.stderr_file = open(stderr_path, 'w')
        
        self.process = subprocess.Popen(
            cmd,
            cwd=self.working_dir,
            env=env,
            stdout=self.stdout_file,
            stderr=self.stderr_file,
        )
        
        # Register cleanup
        atexit.register(self.stop)
        
        # Wait for debug server to be ready
        self._wait_for_debug_server()
        
        return self
    
    def _wait_for_debug_server(self, timeout: float = DEFAULT_STARTUP_TIMEOUT):
        """Wait for debug HTTP server to be ready"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                response = requests.get(
                    f'http://localhost:{self.debug_port}/',
                    timeout=0.5
                )
                if response.status_code == 200:
                    return
            except requests.exceptions.ConnectionError:
                pass
            except requests.exceptions.Timeout:
                pass
            
            # Check if process died
            if self.process.poll() is not None:
                stdout, stderr = self.get_logs()
                raise RuntimeError(
                    f"SDR++ process exited early with code {self.process.returncode}.\n"
                    f"STDERR:\n{stderr[:2000]}"
                )
            
            time.sleep(0.1)
        
        raise TimeoutError(f"Debug server did not start within {timeout}s")
    
    def stop(self):
        """Stop SDR++ instance"""
        if self.process is None:
            return
        
        # Terminate gracefully first
        self.process.terminate()
        try:
            self.process.wait(timeout=self.DEFAULT_SHUTDOWN_TIMEOUT)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        
        # Close log files
        if hasattr(self, 'stdout_file'):
            self.stdout_file.close()
        if hasattr(self, 'stderr_file'):
            self.stderr_file.close()
        
        self.process = None
        atexit.unregister(self.stop)
    
    def is_running(self) -> bool:
        """Check if SDR++ is still running"""
        if self.process is None:
            return False
        return self.process.poll() is None
    
    def debug_cmd(self, module: str, cmd: str, args: str = "") -> Dict[str, Any]:
        """
        Execute a debug command on a module.
        
        Args:
            module: Module instance name (e.g., "Radio")
            cmd: Command name
            args: Command arguments
            
        Returns:
            JSON response as dict
        """
        url = f'http://localhost:{self.debug_port}/module/{module}'
        
        # Use POST with JSON body
        payload = {'cmd': cmd, 'args': args}
        response = requests.post(url, json=payload, timeout=5.0)
        response.raise_for_status()
        
        return response.json()
    
    def get_logs(self) -> tuple:
        """Get stdout and stderr logs"""
        stdout_path = os.path.join(self.working_dir, 'sdrpp_stdout.log')
        stderr_path = os.path.join(self.working_dir, 'sdrpp_stderr.log')
        
        stdout = ""
        stderr = ""
        
        if os.path.exists(stdout_path):
            with open(stdout_path, 'r') as f:
                stdout = f.read()
        
        if os.path.exists(stderr_path):
            with open(stderr_path, 'r') as f:
                stderr = f.read()
        
        return stdout, stderr
    
    def __enter__(self):
        return self.start()
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
        return False


def create_lsb_radio_config(
    radio_name: str = "Radio",
    frequency: float = 7100000,  # 40m band
    bandwidth: float = 2700,  # 2.7 kHz typical LSB bandwidth
    sample_rate: float = 48000
) -> tuple:
    """
    Create configuration dicts for SDR++ with radio module in LSB mode.
    
    Args:
        radio_name: Name of the radio module instance
        frequency: Center frequency in Hz
        bandwidth: Radio bandwidth in Hz
        sample_rate: Audio sample rate
        
    Returns:
        Tuple of (main_config, radio_config)
    """
    # Main config - minimal for testing
    main_config = {
        "frequency": frequency,
        "sampleRate": sample_rate,
        "moduleInstances": {
            radio_name: {
                "module": "radio",
                "enabled": True
            },
            "Null Sink": {
                "module": "null_audio_sink",
                "enabled": True
            }
        },
        "streams": {
            radio_name: {
                "muted": False,
                "sink": "Null",
                "volume": 1.0
            }
        },
        "vfoOffsets": {
            radio_name: 0.0
        },
        "modulesDirectory": "/Users/san/Fun/SDRPlusPlus/root_dev/inst/lib/sdrpp/plugins",
        "resourcesDirectory": "/Users/san/Fun/SDRPlusPlus/root_dev/res",
    }
    
    # Radio config
    radio_config = {
        radio_name: {
            "selectedDemodId": 6,  # LSB = 6
            "LSB": {
                "bandwidth": bandwidth,
                "snapInterval": 100.0,
                "squelchEnabled": False,
                "squelchLevel": -100.0,
                "squelchMode": "off",
                "highPass": False,
                "deempMode": "None",
                "noiseBlankerEnabled": False,
                "noiseBlankerLevel": 1.0,
                "FMIFNREnabled": False,
            }
        }
    }
    
    return main_config, radio_config


class TestResult:
    """Simple test result container"""
    def __init__(self, name: str, passed: bool, message: str = ""):
        self.name = name
        self.passed = passed
        self.message = message
    
    def __str__(self):
        status = "PASS" if self.passed else "FAIL"
        return f"[{status}] {self.name}: {self.message}"


class TestRunner:
    """Simple test runner"""
    
    def __init__(self):
        self.tests: list = []
        self.results: list = []
    
    def add_test(self, name: str, test_fn: Callable[[], TestResult]):
        """Add a test function"""
        self.tests.append((name, test_fn))
    
    def run(self) -> bool:
        """Run all tests, return True if all passed"""
        print(f"\nRunning {len(self.tests)} tests...\n")
        
        all_passed = True
        for name, test_fn in self.tests:
            try:
                result = test_fn()
            except Exception as e:
                import traceback
                result = TestResult(name, False, f"Exception: {e}\n{traceback.format_exc()}")
            
            self.results.append(result)
            print(result)
            
            if not result.passed:
                all_passed = False
        
        # Summary
        passed_count = sum(1 for r in self.results if r.passed)
        failed_count = len(self.results) - passed_count
        
        print(f"\n{'='*50}")
        print(f"Results: {passed_count} passed, {failed_count} failed")
        print(f"{'='*50}\n")
        
        return all_passed


if __name__ == "__main__":
    print("SDR++ E2E Test Framework")
    print("This module is meant to be imported, not run directly.")
