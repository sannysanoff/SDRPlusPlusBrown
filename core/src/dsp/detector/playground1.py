from pathlib import Path
import librosa
import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import stft
import io
import subprocess
import tempfile
import os # Add os import for cleanup
from PIL import Image

# Get the directory containing the current script (playground1.py)
# Assumes this script is located at core/src/dsp/detector/playground1.py
current_script_dir = Path(__file__).resolve().parent

# Calculate the project root directory by going up 4 levels
# core/src/dsp/detector -> core/src/dsp -> core/src -> core -> project_root
sdrpproot = current_script_dir.parent.parent.parent.parent

# Construct the full path to the test file relative to the project root
file_path = sdrpproot / 'tests/test_files/baseband_14174296Hz_11-08-47_24-02-2024-contest-ssb-small.wav'


# You can print the paths to verify them (optional)
print(f"Project Root: {sdrpproot}")
print(f"Test File Path: {file_path}")
print(f"Does file exist? {file_path.exists()}")


def display_plot_with_imgcat(plt_obj):
    """Saves the current matplotlib plot to a temporary file and displays it using imgcat."""
    tmpfile_path = None # Initialize path variable
    try:
        # Save plot to a temporary file
        with tempfile.NamedTemporaryFile(suffix=".jpg", delete=False) as tmpfile: # Changed suffix
            plt_obj.savefig(tmpfile.name, format='jpg', bbox_inches='tight') # Changed format
            tmpfile_path = tmpfile.name

            # Get and print image dimensions
            try:
                img = Image.open(tmpfile_path)
                width, height = img.size
                print(f"Imgcat: Displaying image {tmpfile_path} ({width}x{height})")
                img.close() # Close the image file handle
            except Exception as e_img:
                print(f"Warning: Could not get image dimensions: {e_img}")

            # Display using imgcat
            subprocess.run(['imgcat', tmpfile_path], check=True)

    except FileNotFoundError:
        print("Error: 'imgcat' command not found. Please install imgcat (e.g., via iTerm2 shell integration).")
        # Fallback or alternative display method could be added here if needed
        # For now, just print the error. If you want the old behavior as fallback:
        # plt_obj.show()
    except subprocess.CalledProcessError as e:
        print(f"Error running imgcat: {e}")
    except Exception as e:
        print(f"An error occurred during plot display: {e}")
    finally:
        # Clean up the temporary file
        if tmpfile_path and os.path.exists(tmpfile_path):
            try:
                os.remove(tmpfile_path)
            except OSError as e:
                print(f"Error removing temporary file {tmpfile_path}: {e}")
        plt_obj.close() # Close the plot figure to free memory


def plot_complex_signal(signal, sr):
    """Plots the spectrogram of a complex signal.

    Args:
      signal: The complex signal data.
      sr: The sample rate of the signal.
    """
    # Compute STFT with return_onesided=False to get full spectrum
    frequencies, times, D = stft(
        signal,
        fs=sr,
        window='hann',
        nperseg=1000,
        noverlap=512,
        return_onesided=False
    )

    # Shift frequencies and STFT to center 0 Hz
    D_shifted = np.fft.fftshift(D, axes=0)
    frequencies_shifted = np.fft.fftshift(frequencies)

    # Compute magnitude in dB
    magnitude_db = 20 * np.log10(np.abs(D_shifted) + 1e-10)

    # Plot
    plt.figure(figsize=(12, 8))
    plt.pcolormesh(times, frequencies_shifted, magnitude_db, shading='gouraud', cmap='viridis')
    plt.colorbar(label='Magnitude (dB)')
    plt.title('Centered Spectrogram of Complex Signal')
    plt.ylabel('Frequency [Hz]')
    plt.xlabel('Time [sec]')
    plt.tight_layout()
    display_plot_with_imgcat(plt)

y, sr = librosa.load(file_path, sr=None, mono=False)

# Separate I and Q data
I = y[0, :].astype(float)
Q = y[1, :].astype(float)
I = I / np.max(np.abs(I))
Q = Q / np.max(np.abs(Q))
complex_signal = I + 1j * Q

plot_complex_signal(complex_signal, sr)
