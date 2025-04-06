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

    im = None  # will be created dynamically

    # Slider axes
    axcolor = 'lightgoldenrodyellow'
    ax_gain = plt.axes([0.15, 0.1, 0.65, 0.03], facecolor=axcolor)
    ax_offset = plt.axes([0.15, 0.05, 0.65, 0.03], facecolor=axcolor)

    s_gain = Slider(ax_gain, 'Gain', 0.1, 10.0, valinit=1.0)
    s_offset = Slider(ax_offset, 'Offset', -100.0, 100.0, valinit=0.0)

    def update(frame):
        global im
        data, width, height = read_data()
        if data is None:
            return [im] if im else []

        # Take logarithm of raw data to compress dynamic range
        log_data = np.log10(np.maximum(data, 1e-20))

        # Apply brightness/contrast on log data
        gain = s_gain.val
        offset = s_offset.val
        adj_data = log_data * gain + offset

        # Normalize with percentiles
        p_low, p_high = np.percentile(adj_data, [5, 95])
        if p_high > p_low:
            norm_data = (adj_data - p_low) / (p_high - p_low)
            norm_data = np.clip(norm_data, 0, 1)
        else:
            norm_data = np.zeros_like(adj_data)

        # Gamma correction
        gamma = 0.5
        norm_data = np.power(norm_data, gamma)

        # Print histogram
        hist, _ = np.histogram(norm_data, bins=20, range=(0,1))
        print("Histogram (normalized data, 20 bins):")
        print(hist)

        # Create imshow on first valid data
        if im is None:
            im = ax.imshow(norm_data, aspect='auto', origin='lower', interpolation='nearest', cmap='inferno')
            plt.colorbar(im, ax=ax)
        else:
            im.set_data(norm_data)

        ax.set_title(f"FFT Heatmap ({width} bins x {height} rows)")
        return [im]

    ani = animation.FuncAnimation(fig, update, interval=500, blit=True)
    plt.show()


