using WAV
using DSP
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

# --- Main Processing ---
println("Loading WAV...")
y, sr = wavread(file_path)
yf = Float64.(y)
I, Q = yf[:,1], yf[:,2]
I ./= maximum(abs.(I)); Q ./= maximum(abs.(Q))
sig = I .+ im .* Q

# Extract sub-band 0–50kHz, 0–3s
sub, subfs = extract_signal(sig, Float64(sr), 0.0, 5e4, 0.0, 3.0)

# Compute spectrogram
mag_db, mag_lin, times, fsh = compute_spectrogram(sub, subfs)

# Plot spectrogram
# Plot rotated 90° CW: x=Freq, y=Time; transpose Z and boost vertical size
plt = heatmap(fsh, times, mag_db';
    xlabel="Freq [Hz]", ylabel="Time [s]",
    title="Extracted Spectrogram", colorbar_title="dB", cmap=:viridis,
    size=(3600, 500))

# --- Unified f₀ Detection ---
const MIN_H = 2 # Require fewer harmonics
const MIN_F0 = 80.0
const MAX_F0 = 400.0
const TOL = 25.0
# const MIN_STRONG_H = 3 # Minimum harmonics for a "strong" candidate - REMOVED
const MAX_HARMONIC_SPREAD = 5000.0 # Max Hz difference between f1 and subsequent harmonics
const THR_MULT       = 2.0    # lower threshold ⇒ more peaks
const F0_MAX_MULT    = 5.0    # allow higher f₀ energies before rejecting
const MIN_BIN_COUNT  = 3      # accept histogram bins with ≥3 votes

nfreq, ntime = size(mag_lin)
# First, collect all fundamental estimates per time slice
f0_cands = Vector{Vector{Tuple{Float64, Float64, Int}}}(undef, ntime) # Store (est, f1, cnt) pairs
for j in 1:ntime
    # --- DEBUG: Print info for first 5 time slices ---
    debug_print = j <= 5
    if debug_print; println("\n--- Time Slice j=$j (t=$(@sprintf("%.3f", times[j]))s) ---"); end
    slice = mag_lin[:, j]
    # noise floor & threshold
    nz = slice[slice .> 1e-12]
    nf = isempty(nz) ? 1e-12 : median(nz)
    thr = nf * THR_MULT
    # find peak indices
    peaks = [k for k in 2:nfreq-1 if slice[k]>thr && slice[k]>slice[k-1] && slice[k]>slice[k+1]]
    if debug_print
        peak_freqs = fsh[peaks]
        println("Found $(length(peaks)) peaks above threshold $(@sprintf("%.2e", thr)). Frequencies (Hz):")
        println(join([@sprintf("%.1f", f) for f in peak_freqs], ", "))
    end
    cands = Tuple{Float64, Float64, Int}[] # Initialize for tuples (est, f1, cnt)
    # check harmonic sets
    for p1 in 1:length(peaks)
        k1 = peaks[p1]; f1 = fsh[k1]
        for p2 in p1+1:length(peaks)
            k2 = peaks[p2]; f2 = fsh[k2]
            est = f2 - f1
            if est<MIN_F0 || est>MAX_F0
                continue
            end
            # --- Filter peaks to consider only those near f1 for harmonic check ---
            relevant_peak_indices = [p for p in 1:length(peaks) if abs(fsh[peaks[p]] - f1) <= MAX_HARMONIC_SPREAD]
            # Note: We only need the *frequencies* of relevant peaks for the check below.
            relevant_peak_freqs = fsh[relevant_peak_indices]
            # --- End Filter ---

            # count supporting harmonics
            cnt = 2
            for n in 2:10
                tgt = f1 + n*est
                # find any peak near tgt, using only relevant peaks
                found = any(abs.(relevant_peak_freqs .- tgt) .<= TOL) # Search only among relevant peaks' frequencies
                if found; cnt += 1 else break end
            end
            if debug_print && cnt >= MIN_H
                # suppress the noisy f1≈13240, f2≈13330 combination
                if !(round(f1, digits=1)==13240.0 && round(f2, digits=1)==13330.0)
                    println("  Harmonic set found: f1=$(round(f1,digits=1)), f2=$(round(f2,digits=1)) => est=$(round(est,digits=1)), num_harmonics=$cnt")
                end
            end
            if cnt >= MIN_H
                # reject any f₀ whose average energy over [f0‑est … f0+est/0.25] is too high
                f0 = f1 - est
                lo = f0 - est
                hi = f0 + est/0.25
                # collect bin‐indices in that band
                band_idxs = findall((fsh .>= lo) .& (fsh .<= hi))
                # if empty, fall back to nearest bin
                base_idx = argmin(abs.(fsh .- f0))
                e_avg = isempty(band_idxs) ? slice[base_idx] : mean(slice[band_idxs])
                if e_avg > nf * F0_MAX_MULT
                    if debug_print
                        println("    skip f0=$(round(f0, digits=1))Hz: avg_energy=$(round(e_avg, sigdigits=3)) over [$(round(lo)),$(round(hi))]")
                    end
                else
                    push!(cands, (est, f1, cnt))
                    if debug_print
                        f0 = f1 - est
                        println("    accept f0=$(round(f0,digits=1))Hz (est=$(round(est,digits=1)), cnt=$cnt)")
                    end
                end
            end
        end
    end
    if debug_print # Update debug print format
        println("  Final candidates for slice j=$j: $(isempty(cands) ? "None" : join([@sprintf("(%.1f, %.1f)", c[1], c[2]) for c in cands], ", "))")
    end
    f0_cands[j] = cands
end

# --- Find Stable Base Frequencies using Histogram ---

# 1. Collect all f_base values with their time and count
all_detections = []
for j in 1:ntime
    for (est, f1, cnt) in f0_cands[j]
        # Only consider candidates with at least MIN_H harmonics
        if cnt >= MIN_H
            f_base = f1 - est
            push!(all_detections, (time=times[j], f_base=f_base, count=cnt))
        end
    end
end

# 2. Create Histogram
using StatsBase # Add this if not already implicitly available via Plots/Statistics
f_bases_all = [d.f_base for d in all_detections]

# Flag to track if any series has been plotted yet (for legend purposes)
first_track_plotted1 = false
println("Assigned: ", first_track_plotted1)

if isempty(f_bases_all)
    println("No base frequency candidates found.")
else
    # Determine histogram range and bins
    min_fb, max_fb = minimum(f_bases_all), maximum(f_bases_all)
    bin_width = 25.0   # finer bins to split nearby carriers
    bins = range(floor(min_fb / bin_width) * bin_width, ceil(max_fb / bin_width) * bin_width, step=bin_width)
    hist = StatsBase.fit(Histogram, f_bases_all, bins)

    # 3. Identify Peaks (simple approach: bins with counts above a threshold)
    min_bin_count = MIN_BIN_COUNT
    peak_bin_indices = findall(hist.weights .>= min_bin_count)
    stable_f_base_candidates = [(bins[i] + bins[i+1]) / 2 for i in peak_bin_indices] # Use bin centers
    println("Stable f_base candidates (Hz): ", join([@sprintf("%.1f", f) for f in stable_f_base_candidates], ", "))

    # enforce minimum USB‐step spacing (~500 Hz) so carriers don’t cluster
    const MIN_USB_STEP = 500.0
    stable_f_base_candidates = sort(stable_f_base_candidates)
    filtered = Float64[]
    for fb in stable_f_base_candidates
        if all(abs(fb - g) >= MIN_USB_STEP for g in filtered)
            push!(filtered, fb)
        end
    end
    stable_f_base_candidates = filtered
    println("Filtered f_base (≥500 Hz apart): ", join([@sprintf("%.1f", f) for f in stable_f_base_candidates], ", "))

    # require each track to persist in at least X% of slices
    const MIN_TRACK_PERSISTENCE = 0.30       # 30% of active slices
    n_slices_with_candidates = count(!isempty, f0_cands)
    min_required_slices   = ceil(Int, MIN_TRACK_PERSISTENCE * n_slices_with_candidates)

    # 4. Track and Plot Closest Candidates
    const TOL_FB = 200.0  # allow ±200 Hz drift in track matching

    for (track_idx, f_base_stable) in enumerate(stable_f_base_candidates)
        track_times = Float64[]
        track_f_bases = Float64[]
        for j in 1:ntime
            best_match_fbase = NaN
            min_diff = TOL_FB
            best_cnt = -1
            # Find the best candidate in this slice matching the stable frequency
            for (est, f1, cnt) in f0_cands[j]
                 if cnt >= MIN_H # Ensure we only consider valid candidates from the start
                    f_base_cand = f1 - est
                    diff = abs(f_base_cand - f_base_stable)
                    if diff < min_diff # Prioritize closer match
                        min_diff = diff
                        best_match_fbase = f_base_cand
                        best_cnt = cnt
                    elseif diff == min_diff && cnt > best_cnt # If equally close, prefer higher harmonic count
                        best_match_fbase = f_base_cand
                        best_cnt = cnt
                    end
                end
            end
            # If a suitable candidate was found in this slice, add it to the track
            if !isnan(best_match_fbase)
                push!(track_times, times[j])
                push!(track_f_bases, best_match_fbase)
            end
        end

        # Plot this track
        if length(track_times) >= min_required_slices
            # Use blue for all points, add label only for the first track plotted
            current_label = ""
            if !first_track_plotted1
                current_label = "Detected Base Freqs"
                global first_track_plotted1 = true
            end
            scatter!(plt, track_f_bases, track_times; markersize=2, markercolor=:blue, label=current_label,
                     markerstrokewidth=0)
        end
    end
end

# Display
display_plot_with_imgcat(plt)
println("Done.")
