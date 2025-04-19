using WAV
using DSP
import StatsBase # Change from 'using' to 'import'
using FFTW
using Plots
gr()  # Use GR backend
using Printf
using Base.Filesystem  # for rm, ispath
using Images
using Statistics  # for median
using StatsBase   # for histogram

# --- Path Setup ---
current_script_dir = @__DIR__
sdrpproot = dirname(dirname(dirname(dirname(current_script_dir))))
file_path = joinpath(sdrpproot, "tests", "test_files",  "baseband_14174296Hz_11-08-47_24-02-2024-contest-ssb-small.wav")
@printf("Project Root: %s\n", abspath(sdrpproot))
@printf("Test File Path: %s\n", abspath(file_path))
@printf("Does file exist? %s\n", isfile(file_path))

# --- Main Processing ---
println("Loading WAV...")
y, sr = wavread(file_path)
yf = Float64.(y)
I, Q = yf[:,1], yf[:,2]
I ./= maximum(abs.(I)); Q ./= maximum(abs.(Q))
sig = I .+ im .* Q

# --- Imgcat Display Function ---
function display_plot_with_imgcat(plot_obj)
    println("Saving png..")
    tmpfile_path = ""
    try
        base_tmp = tempname(; cleanup=false)
        tmpfile_path = base_tmp * ".png"
        savefig(plot_obj, tmpfile_path)

        # Try to read and report dimensions
        try
            img = Images.load(tmpfile_path)
            h, w = size(img)
            println("Imgcat: Displaying $tmpfile_path ($w x $h)")
        catch e_dim
            @warn "Could not read image dims: $e_dim"
        end

        run(`imgcat $tmpfile_path`)
    catch e
        if e isa ProcessFailedException || occursin("executable file not found", lowercase(sprint(showerror, e)))
            println("Error: 'imgcat' not found or failed. Install imgcat or check PATH.")
        else
            println("Error displaying plot:")
            showerror(stdout, e); println()
        end
    finally
        if !isempty(tmpfile_path) && ispath(tmpfile_path)
            try rm(tmpfile_path) catch _ end
        end
    end
end

# --- Signal Extraction Function ---
function extract_signal(sig::Vector{ComplexF64}, fs::Float64,
                        fmin::Float64, fmax::Float64,
                        tstart::Float64, tend::Float64)
    if tstart >= tend; error("start >= end"); end
    if fmin >= fmax; error("fmin >= fmax"); end
    bw = fmax - fmin
    # Nyquist checks omitted for brevity

    i1 = max(1, floor(Int, tstart*fs)+1)
    i2 = min(length(sig), floor(Int, tend*fs)+1)
    seg = sig[i1:i2]
    N = length(seg)
    println(@sprintf("Segment: %d samples from %.3fs to %.3fs", N, (i1-1)/fs, (i2-1)/fs))

    # Frequency shift
    fctr = (fmin + fmax)/2
    tvec = (0:N-1)/fs
    seg_shift = seg .* exp.(-im*2*pi*fctr .* tvec)

    # Resample to bandwidth
    newfs = bw
    ratio = newfs/fs
    println(@sprintf("Resampling from %.1f→%.1f Hz (ratio %.4f)", fs, newfs, ratio))
    out = abs(ratio-1)<1e-6 ? seg_shift : DSP.resample(seg_shift, ratio)
    println(@sprintf("Resampled length %d @ %.1f Hz", length(out), newfs))
    return out, newfs
end

# --- Spectrogram + Data Function ---
function compute_spectrogram(sig::Vector{ComplexF64}, fs::Float64)
    nperseg = max(16, floor(Int, fs/10))
    nover = nperseg ÷ 2
    win = DSP.hanning(nperseg)
    println("STFT...")
    S = DSP.stft(sig, nperseg, nover; fs=fs, window=win)
    freqs = FFTW.fftfreq(nperseg, fs)
    Ssh = FFTW.fftshift(S, 1)
    fsh = FFTW.fftshift(freqs)
    mag_db = 20*log10.(abs.(Ssh) .+ 1e-10)
    mag_lin = abs.(Ssh)
    step = nperseg - nover
    nframes = size(mag_lin, 2)
    times = ((0:nframes-1).*step .+ nperseg/2)/fs
    return mag_db, mag_lin, times, fsh
end


# Helper function for finding peaks (used by sliding_window_peak_analysis)
# Finds peaks in data based on prominence above median and local maximum condition.
function find_peaks_in_window(window_data::AbstractVector{<:Real}, min_prominence_db::Real, local_half_width::Int)
    n = length(window_data)
    if n < 3
        return Int[] # Cannot find local maxima in very short vectors
    end
    # Estimate noise floor using median
    noise_floor = median(window_data)
    # Threshold requires the signal to be significantly above the median
    threshold = noise_floor + min_prominence_db

    # Find potential peaks (local maxima above threshold)
    potential_indices = Int[]
    for k in 2:n-1
        # Check if it's a local maximum and above the threshold
        if window_data[k] > threshold && window_data[k] > window_data[k-1] && window_data[k] > window_data[k+1]
            push!(potential_indices, k)
        end
    end

    # Filter based on being the maximum in a wider local window
    peak_indices = Int[]
    for k in potential_indices
        start_idx = max(1, k - local_half_width)
        end_idx = min(n, k + local_half_width)
        # Check if the potential peak is the true maximum in its neighborhood
        if window_data[k] == maximum(view(window_data, start_idx:end_idx))
            push!(peak_indices, k)
        end
    end
    # Sort peaks by index, just in case
    sort!(peak_indices)
    return peak_indices # Indices are 1-based relative to window start
end

# Analyzes a 1D signal with a sliding window to find periodic peaks.
# Returns the period (in number of samples/indices) and phase of the peaks for each window.
function sliding_window_peak_analysis(signal::Vector{<:Number}, window_size::Int, step_size::Int;
                                      min_prominence_db::Real=6.0, # Min height above median in dB for a peak
                                      local_half_width::Int=2,     # Half-width for local max check
                                      max_period_std_ratio::Real=0.4) # Max allowed std_dev/mean_period for regularity
    n_signal = length(signal)
    # Calculate the number of full windows that fit
    n_windows = (n_signal >= window_size) ? floor(Int, (n_signal - window_size) / step_size) + 1 : 0

    if n_windows == 0
        return Float64[], Float64[] # Return empty arrays if no windows fit
    end

    periods = fill(NaN, n_windows)
    phases = fill(NaN, n_windows)

    for i in 1:n_windows
        # Calculate start and end indices for the current window
        start_idx = (i - 1) * step_size + 1
        end_idx = start_idx + window_size - 1

        window_data = view(signal, start_idx:end_idx) # Use view for efficiency

        # Find peaks within the current window
        peak_indices_in_window = find_peaks_in_window(window_data, min_prominence_db, local_half_width)

        if length(peak_indices_in_window) >= 2
            diffs = diff(peak_indices_in_window)
            if !isempty(diffs)
                mean_diff = mean(diffs)
                # Check regularity: either only one diff (2 peaks) or std dev is small relative to mean
                is_regular = length(diffs) == 1 || (mean_diff > 0 && std(diffs) / mean_diff < max_period_std_ratio)

                if is_regular
                    period = mean_diff
                    periods[i] = period

                    # Calculate phase based on the first peak's position (1-based index in window)
                    first_peak_idx = peak_indices_in_window[1]
                    # Offset relative to the start of the window (0-based index)
                    offset = first_peak_idx - 1
                    # Phase: (offset % period) / period * 2pi
                    # Ensure period is positive to avoid NaN from modulo with non-positive
                    if period > 0
                        phase = (offset % period) / period * 2 * pi
                        phases[i] = phase
                    end
                end
            end
        end
        # If < 2 peaks or irregular spacing, periods[i] and phases[i] remain NaN
    end

    return periods, phases
end


function try2()
    sub, subfs = extract_signal(sig, Float64(sr), 0.0, 2.5e4, 0.0, 3.0)

    mag_db, mag_lin, times, fsh = compute_spectrogram(sub, subfs)

    # Determine the full frequency range for consistent x-axes
    fmin_global, fmax_global = minimum(fsh), maximum(fsh)

    first_slice_db = mag_db[:, 10]
    plt_slice = plot(fsh, first_slice_db;
                     xlabel="Frequency [Hz]", ylabel="Magnitude [dB]",
                     xformatter = x -> @sprintf("%.0f", x), # Format x-ticks as integers/fixed-point
                     # title="First Time Slice of Spectrogram", # Title removed from top
                     xticks = 50, # Suggest more ticks on the x-axis
                     bottom_margin=15Plots.Plots.mm, # Add margin at the bottom for the title
                     label="", size=(3600, 400),
                     xlims=(fmin_global, fmax_global)) # Set x-axis limits
    annotate!(plt_slice, [(0.5, -0.15, Plots.text("Time Slice at index 20 of Spectrogram", :center, 10))]; annotation_clip=false) # Add title annotation below the plot

    periods, phases = sliding_window_peak_analysis(first_slice_db, 250, 1; min_prominence_db=6.0, local_half_width=3)
    @printf("Sliding Window Peak Analysis Results (Period [indices], Phase [radians]): of array shape=%s\n", size(first_slice_db))
    # Iterate and print each result pair
    for i in 1:length(periods)
        # Calculate the starting frequency of the window
        # step_size is 1, so window i starts at index i in first_slice_db
        start_freq = fsh[i] # Get frequency corresponding to the start index of the window
        @printf("  Freq[%d] Start %.1f Hz: Period = %.2f indices, Phase = %.3f rad\n", i, start_freq, periods[i], phases[i])
    end

    # Detect peaks in the slice using custom logic
    MIN_PEAK_RATIO = 3 # Threshold factor relative to noise floor
    PEAK_WINDOW_HALF_WIDTH = 2 # Samples on each side to check for max
    nfreq = length(first_slice_db)
    # Estimate noise floor using median
    thr = median(first_slice_db)
    # Find peak indices based on local maxima and threshold
    potential_peak_indices = [ k for k in 2:nfreq-1
                     if first_slice_db[k] > thr + 20*log10(MIN_PEAK_RATIO) && # Compare in dB domain
                        first_slice_db[k] > first_slice_db[k-1] && first_slice_db[k] > first_slice_db[k+1] ]

    # Filter peaks: keep only those that are the maximum in their local window
    peak_indices = [ k for k in potential_peak_indices
                     if first_slice_db[k] == maximum(first_slice_db[max(1, k-PEAK_WINDOW_HALF_WIDTH):min(nfreq, k+PEAK_WINDOW_HALF_WIDTH)]) ]
    peak_vals = first_slice_db[peak_indices]
    peak_freqs = fsh[peak_indices]
    # Add peaks to the plot
    scatter!(plt_slice, peak_freqs, peak_vals; markercolor=:red, markersize=3, label="Peaks")


    display_plot_with_imgcat(plt_slice)

    plt = heatmap(fsh, times, mag_db';
        xlabel="Freq [Hz]", ylabel="Time [s]",
        # title="Extracted Spectrogram", # Title removed from top
        xformatter = x -> @sprintf("%.0f", x), # Format x-ticks as integers/fixed-point
        xticks = 50, # Suggest more ticks on the x-axis
        legend = false, # Disable the legend
        bottom_margin=15Plots.Plots.mm, # Add margin at the bottom for the title
        size=(3430, 700),
        xlims=(fmin_global, fmax_global)) # Set x-axis limits
    annotate!(plt, [(0.5, -0.1, Plots.text("Extracted Spectrogram", :center, 10))]; annotation_clip=false) # Add title annotation below the plot

    #── store original axes limits AND ticks so the scatter won’t drop them
    orig_xlim, orig_ylim = xlims(plt), ylims(plt)
    # xticks(plt) returns a 1‑element Vector{Tuple{…}}, so grab [1]
    (orig_xticks, orig_xtick_labels) = xticks(plt)[1]
    (orig_yticks, orig_ytick_labels) = yticks(plt)[1]

    track_times = Float64[]
    track_f_bases = Float64[]

    push!(track_times, 0.5)
    push!(track_f_bases, 0.0)
    scatter!(plt, track_f_bases, track_times;
             markersize=2, markercolor=:blue, label="Hi", markerstrokewidth=0)

    # Restore the heatmap's freq–time limits and ticks AFTER scatter!
    xlims!(plt, orig_xlim)
    ylims!(plt, orig_ylim)
    xticks!(plt, orig_xticks, orig_xtick_labels)
    yticks!(plt, orig_yticks, orig_ytick_labels)

    # Display
    display_plot_with_imgcat(plt)
    println("Done2.")
end
