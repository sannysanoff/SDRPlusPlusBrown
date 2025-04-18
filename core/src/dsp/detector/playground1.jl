using WAV
using DSP
using StatsBase # Add this if not already implicitly available via Plots/Statistics
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





function rev1()
    println("Rev1234")
end



function try2()
    sub, subfs = extract_signal(sig, Float64(sr), 0.0, 5e4, 0.0, 3.0)

    mag_db, mag_lin, times, fsh = compute_spectrogram(sub, subfs)

    first_slice_db = mag_db[:, 20]
    plt_slice = plot(fsh, first_slice_db;
                     xlabel="Frequency [Hz]", ylabel="Magnitude [dB]",
                     xformatter = x -> @sprintf("%.0f", x), # Format x-ticks as integers/fixed-point
                     # title="First Time Slice of Spectrogram", # Title removed from top
                     xticks = 50, # Suggest more ticks on the x-axis
                     bottom_margin=15Plots.Plots.mm, # Add margin at the bottom for the title
                     label="", size=(3600, 400))
    annotate!(plt_slice, [(0.5, -0.15, Plots.text("Time Slice at index 20 of Spectrogram", :center, 10))]; annotation_clip=false) # Add title annotation below the plot

    # Detect peaks in the slice
    peak_indices, peak_vals = StatsBase.findmaxima(first_slice_db) # Explicitly qualify findmaxima
    peak_freqs = fsh[peak_indices]
    # Add peaks to the plot
    scatter!(plt_slice, peak_freqs, peak_vals; markercolor=:red, markersize=3, label="Peaks")

    display_plot_with_imgcat(plt_slice)

    plt = heatmap(fsh, times, mag_db';
        xlabel="Freq [Hz]", ylabel="Time [s]",
        # title="Extracted Spectrogram", # Title removed from top
        xformatter = x -> @sprintf("%.0f", x), # Format x-ticks as integers/fixed-point
        xticks = 50, # Suggest more ticks on the x-axis
        bottom_margin=15Plots.Plots.mm, # Add margin at the bottom for the title
        size=(3600, 700))
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
