#!/bin/bash
#
# Run all SDR++ E2E tests
#
# Usage:
#   ./run_all.sh              # Run all tests (statistics mode)
#   ./run_all.sh --list       # List test files only
#   ./run_all.sh --verbose    # Run with verbose output
#   ./run_all.sh -h, --help   # Show help
#   ./run_all.sh -f test_lsb  # Run specific test file
#
# Environment Variables:
#   E2E_VERBOSE=1         # Enable verbose output
#   E2E_HTTP_PORT=8085    # Starting HTTP port (auto-increments)
#   E2E_BUILD_DIR=path    # Path to build directory
#   E2E_ROOT_DEV=path     # Path to root_dev
#   E2E_BINARY=name       # Binary name (default: ./sdrpp)
#   E2E_STATS_MODE=1      # Force statistics-only output
#

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Run the test suite using e2e_common.py
# All environment variables are passed through automatically
python3 "${SCRIPT_DIR}/e2e_common.py" "$@"
