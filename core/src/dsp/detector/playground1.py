from pathlib import Path

# Get the directory containing the current script (playground1.py)
# Assumes this script is located at core/src/dsp/detector/playground1.py
current_script_dir = Path(__file__).resolve().parent

# Calculate the project root directory by going up 4 levels
# core/src/dsp/detector -> core/src/dsp -> core/src -> core -> project_root
sdrpproot = current_script_dir.parent.parent.parent.parent

# Construct the full path to the test file relative to the project root
file_path = sdrpproot / 'tests/test_files/baseband_14174296Hz_11-08-47_24-02-2024-contest-ssb-small.wav'

# If you need string representations of the paths later:
# sdrpproot_str = str(sdrpproot)
# file_path_str = str(file_path)

# You can print the paths to verify them (optional)
# print(f"Project Root: {sdrpproot}")
# print(f"Test File Path: {file_path}")
# print(f"Does file exist? {file_path.exists()}")
