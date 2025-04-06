import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation

DIM_FILE = "/tmp/array.dim"
BIN_FILE = "/tmp/array.bin"

def read_data():
    try:
        with open(DIM_FILE, "r") as f:
            dims = f.read().strip().split()
            width, height = int(dims[0]), int(dims[1])
    except:
        return None, None, None

    try:
        data = np.fromfile(BIN_FILE, dtype=np.float32)
        if data.size != width * height:
            return None, None, None
        data = data.reshape((height, width))
    except:
        return None, None, None

    return data, width, height

fig, ax = plt.subplots()
lines = []

# Initialize empty lines (max 20 rows default)
for _ in range(20):
    line, = ax.plot([], [], lw=1)
    lines.append(line)

ax.set_xlabel("Frequency bin")
ax.set_ylabel("Magnitude")
ax.set_title("FFT Magnitude Series")

def update(frame):
    data, width, height = read_data()
    if data is None:
        return lines

    # Update or hide lines
    for idx, line in enumerate(lines):
        if idx < height:
            line.set_data(np.arange(width), data[idx])
            line.set_visible(True)
        else:
            line.set_visible(False)

    ax.relim()
    ax.autoscale_view()
    return lines

ani = animation.FuncAnimation(fig, update, interval=500, blit=True)
plt.show()
