import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import time
import os

DIM_FILE = "/tmp/array.dim"
BIN_FILE = "/tmp/array.bin"

def read_data():
    # Read dimensions
    try:
        with open(DIM_FILE, "r") as f:
            dims = f.read().strip().split()
            width, height = int(dims[0]), int(dims[1])
    except:
        return None, None, None

    # Read binary data
    try:
        data = np.fromfile(BIN_FILE, dtype=np.float32)
        if data.size != width * height:
            return None, None, None
        data = data.reshape((height, width))
    except:
        return None, None, None

    return data, width, height

def update(frame):
    data, width, height = read_data()
    if data is not None:
        im.set_data(data)
        ax.set_title(f"FFT Heatmap ({width} bins x {height} rows)")
    return [im]

fig, ax = plt.subplots()
# Initialize with zeros
init_data = np.zeros((20, 1024), dtype=np.float32)  # default shape
im = ax.imshow(init_data, aspect='auto', origin='lower', interpolation='nearest', cmap='viridis')
ax.set_title("Waiting for data...")

ani = animation.FuncAnimation(fig, update, interval=500, blit=True)
plt.xlabel("Frequency bin")
plt.ylabel("Time (rows)")
plt.show()
