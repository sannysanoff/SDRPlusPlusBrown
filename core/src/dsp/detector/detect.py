import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.widgets import Slider

if __name__ == "__main__":


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
    plt.subplots_adjust(bottom=0.25)

    # Initial dummy data
    init_data = np.zeros((20, 1024), dtype=np.float32)
    im = ax.imshow(init_data, aspect='auto', origin='lower', interpolation='nearest', cmap='inferno')
    plt.colorbar(im, ax=ax)
    ax.set_xlabel("Frequency bin")
    ax.set_ylabel("Time (rows)")
    ax.set_title("FFT Heatmap")

    # Slider axes
    axcolor = 'lightgoldenrodyellow'
    ax_gain = plt.axes([0.15, 0.1, 0.65, 0.03], facecolor=axcolor)
    ax_offset = plt.axes([0.15, 0.05, 0.65, 0.03], facecolor=axcolor)

    # Sliders
    s_gain = Slider(ax_gain, 'Gain', 0.1, 10.0, valinit=1.0)
    s_offset = Slider(ax_offset, 'Offset', -100.0, 100.0, valinit=0.0)

    def update(frame):
        data, width, height = read_data()
        if data is None:
            return [im]

        # Take logarithm of raw data to compress dynamic range
        log_data = np.log10(np.maximum(data, 1e-20))  # avoid log(0)

        # Apply brightness/contrast on log data
        gain = s_gain.val
        offset = s_offset.val
        adj_data = log_data * gain + offset

        # Normalize adjusted log data to 0..1
        # Use percentiles to normalize, ignoring extreme outliers
        p_low, p_high = np.percentile(adj_data, [5, 95])
        if p_high > p_low:
            norm_data = (adj_data - p_low) / (p_high - p_low)
            norm_data = np.clip(norm_data, 0, 1)
        else:
            norm_data = np.zeros_like(adj_data)

        # Apply gamma correction to enhance contrast
        gamma = 0.5  # adjust as needed
        norm_data = np.power(norm_data, gamma)

        # Print histogram with 20 buckets
        hist, bin_edges = np.histogram(norm_data, bins=20, range=(0.0, 1.0))
        print("Histogram (normalized data, 20 bins):")
        print(hist)

        im.set_data(norm_data)
        ax.set_title(f"FFT Heatmap ({width} bins x {height} rows)")
        return [im]

    ani = animation.FuncAnimation(fig, update, interval=500, blit=True)
    plt.show()


