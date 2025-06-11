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
using Dates       # for timestamp

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
function imgcat(plot_obj)
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

# Returns the period (in number of samples/indices) and phase of the dominant frequency component for each window using FFT.
function sliding_window_peak_analysis(signal::Vector{<:Number}, window_size::Int, step_size::Int)
    n_signal = length(signal)
    # Calculate the number of full windows that fit
    n_windows = (n_signal >= window_size) ? floor(Int, (n_signal - window_size) / step_size) + 1 : 0

    if n_windows == 0
        return Float64[], Float64[] # Return empty arrays if no windows fit
    end

    periods = fill(NaN, n_windows)
    phases = fill(NaN, n_windows)
    fft_plan = plan_fft(zeros(ComplexF64, window_size)) # Plan FFT for efficiency
    win = DSP.hanning(window_size) # Hanning window

    for i in 1:n_windows
        # Calculate start and end indices for the current window
        start_idx = (i - 1) * step_size + 1
        end_idx = start_idx + window_size - 1

        window_data = view(signal, start_idx:end_idx) # Use view for efficiency

        # Apply window function and compute FFT
        windowed_data = window_data .* win
        fft_result = fft_plan * windowed_data # Use planned FFT

        # Find the peak magnitude in the positive frequency spectrum (excluding DC)
        # We only look up to N/2 (Nyquist)
        max_mag = -Inf
        peak_idx = -1
        # FFT indices are 1-based. Index 1 is DC. Indices 2 to floor(N/2)+1 are positive frequencies.
        for k in 2:floor(Int, window_size / 2) + 1
             mag = abs(fft_result[k])
             if mag > max_mag
                 max_mag = mag
                 peak_idx = k
             end
        end

        # Check if a peak was found (peak_idx will be > 1)
        if peak_idx > 1
            # Calculate frequency index (0-based for formula)
            freq_index = peak_idx - 1
            # Calculate period (samples per cycle)
            # Period = N / freq_index
            period = window_size / freq_index
            periods[i] = period

            # Calculate phase at the peak frequency
            # angle() returns phase in [-pi, pi], adjust to [0, 2pi) if desired
            phase = angle(fft_result[peak_idx])
            phases[i] = phase < 0 ? phase + 2*pi : phase # Adjust phase to [0, 2pi)
        end
        # If no peak found or only DC, periods[i] and phases[i] remain NaN
    end

    return periods, phases
end

# Returns a view of slice_db corresponding to the frequency range [min_freq, max_freq]
function extract_frequencies(slice_db::AbstractVector{<:Real}, fsh::AbstractVector{<:Real}, min_freq::Real, max_freq::Real)
    # Ensure frequencies are sorted (common for fftshift output)
    if !issorted(fsh)
        error("Frequency vector fsh must be sorted.")
    end
    if min_freq > max_freq
        error("min_freq cannot be greater than max_freq.")
    end

    # Find the start and end indices corresponding to the frequency range
    start_idx = findfirst(f -> f >= min_freq, fsh)
    end_idx = findlast(f -> f <= max_freq, fsh)

    # Handle cases where the range is outside the available frequencies
    if isnothing(start_idx) || isnothing(end_idx) || start_idx > end_idx
        # Return an empty view of the correct type
        return view(slice_db, 1:0)
    end

    # Return a view of the slice within the found indices
    # println("extract_frequencies: $min_freq:$max_freq => $start_idx:$end_idx total len: $(size(slice_db))")
    return view(slice_db, start_idx:end_idx)
end

function score_line_at_offset(first_slice_db, fsh, offs)
    arr1 = extract_frequencies(first_slice_db, fsh,offs, offs+2500)
    maxval = maximum(arr1)
    harmonic_values = Float64[]
    #println("Maximum value: $maxval")

    if false
        imgcat(plot(arr1))
    end


    # Compute the Real FFT of the extracted frequency slice
    rfft_result = FFTW.rfft(arr1)
    rfft_mag = abs.(rfft_result)

    # Create a frequency axis for the RFFT plot (cycles per original index)
    N = length(arr1)
    rfft_freqs = (0:(N ÷ 2)) ./ N # Frequencies from 0 to 0.5 cycles/index

    if false
        # Plot the magnitude of the RFFT
        plt_rfft = plot(rfft_freqs, rfft_mag;
                        xlabel="Frequency (cycles/index)", ylabel="Magnitude",
                        title="RFFT of Slice [-8000 Hz, -4000 Hz]",
                        label="", size=(800, 300))
        imgcat(plt_rfft)
    end


    # Find peak frequency > 0.03 cycles/index, calculate period and phase offset
    freq_threshold = 0.03
    min_idx = findfirst(f -> f >= freq_threshold, rfft_freqs)

    if isnothing(min_idx) || min_idx > length(rfft_mag)
        return NaN
    else
        # Find the peak magnitude and its index in the relevant frequency range
        # Use view for efficiency, handle empty range if min_idx is the last index
        view_range = min_idx:length(rfft_mag)
        if !isempty(view_range)
            peak_val, rel_idx = findmax(view(rfft_mag, view_range))
            peak_idx = min_idx + rel_idx - 1 # Adjust index back to the full rfft array

            peak_freq = rfft_freqs[peak_idx]
            peak_phase = angle(rfft_result[peak_idx]) # Phase in radians [-pi, pi]

            # Calculate period (in number of samples/indices in arr1)
            # Avoid division by zero if peak_freq is somehow zero (though unlikely for peak > 0.03)
            period = peak_freq ≈ 0.0 ? Inf : 1.0 / peak_freq

            # Calculate phase offset (in number of samples/indices in arr1)
            # Offset represents the index shift for the first peak relative to index 0
            # We use mod(..., period) to get the offset within the first cycle [0, period)
            offset_indices = mod((-peak_phase / (2 * pi)) * period, period)

            N_HARMONICS = 3

            # Compute the average value at the estimated peak locations (harmonics)
            if isfinite(period) && isfinite(offset_indices) && period > 0

                saved = arr1[1]
                push!(harmonic_values, -mean(arr1[1:Int(round(period/2))]));
                arr1[1] = maxval
            
                for k in 0:(N_HARMONICS - 1)
                    # Calculate the theoretical index for the k-th harmonic peak
                    idx_float = 0 + k * period
                    # Round to the nearest integer index (Julia is 1-based)
                    idx_int = round(Int, idx_float) + 1 # +1 because offset is 0-based relative to start

                    # Check if the index is within the bounds of arr1
                    if 1 <= idx_int <= length(arr1)
                        push!(harmonic_values, arr1[idx_int])
                    end
                end

                arr1[1] = saved
            end

            avg_harmonic_peak_value = if isempty(harmonic_values)
                return NaN
            else
                return mean(harmonic_values)
            end
        else
            return NaN
        end
    end
end




function find_dominant_harmonic_intervals(
    spectrum::Vector{Float64},
    epsilon::Int,
    min_interval::Int,
    max_interval::Int
)
    N = length(spectrum)
    if N == 0
        return Int[], Float64[]
    end
    if min_interval <= 0 || max_interval < min_interval || max_interval >= N
         error("Недопустимый диапазон интервалов: min=$min_interval, max=$max_interval, N=$N")
    end
     if epsilon <= 0
         error("Epsilon должен быть положительным: epsilon=$epsilon")
     end

    intervals = min_interval:max_interval
    num_intervals = length(intervals)

    # Массив для хранения "сырых" откликов для каждого интервала d
    # Размер: количество_интервалов x длина_спектра
    # raw_responses[k, i] будет хранить отклик для d = intervals[k] в точке i
    raw_responses = zeros(Float64, num_intervals, N)

    # 1. Вычисление Лаговых Произведений (Несглаженных Откликов)
    for (k, d) in enumerate(intervals) # k - индекс (1..num_intervals), d - значение интервала
        valid_len = N - d
        if valid_len > 0
            # Вычисляем S[i] * S[i+d] для i от 1 до N-d
            # Используем views для эффективности (избегаем копирования)
            # Используем max.(0.0, ...) на случай, если во входных данных могут быть отр. числа
            term1 = view(spectrum, 1:valid_len)
            term2 = view(spectrum, (d+1):N)
            response_slice = view(raw_responses, k, 1:valid_len)

            # Поэлементное умножение и запись в соответствующий срез
            response_slice .= max.(0.0, term1) .* max.(0.0, term2)
            # Оставшаяся часть raw_responses[k, (valid_len+1):N] уже заполнена нулями
        end
    end

    # 2. Use raw responses directly without smoothing
    smoothed_responses = raw_responses # No convolution, just use the raw responses

    # 3. Определение Доминирующего Интервала и Коэффициента Уверенности
    dominant_intervals = zeros(Int, N)
    confidence_scores = zeros(Float64, N)

    # Для каждой колонки i (позиции в спектре) находим строку k с максимальным значением
    # findmax возвращает кортеж: (максимальные_значения, индексы_максимумов)
    # Применяем вдоль 1-й размерности (интервалы)
    max_vals_and_indices = findmax(smoothed_responses, dims=1)

    # max_vals_and_indices[1] - матрица 1xN с максимальными значениями (confidence)
    # max_vals_and_indices[2] - матрица 1xN с CartesianIndex(k, i) максимальных элементов

    # Извлекаем индексы k (1-based)
    # getindex.(Tuple.(...)[1]) - способ извлечь первый элемент (k) из CartesianIndex
    dominant_k_indices = [getindex(Tuple(idx), 1) for idx in max_vals_and_indices[2]]

    # Преобразуем индексы k (1..num_intervals) обратно в реальные интервалы d
    for i in 1:N
        dominant_intervals[i] = intervals[dominant_k_indices[i]]
    end

    # Извлекаем максимальные значения как коэффициент уверенности/энергии
    values = dropdims(max_vals_and_indices[1], dims=1)
    for i in 1:N
        confidence_scores[i] = values[i]
    end

    return dominant_intervals, confidence_scores
end

function percentile_fast(arr, p)
    n = length(arr)
    k = ceil(Int, n * p)
    return partialsort(arr, k)
end

function normalize_magnitudes(magnitudes::Vector{Float64})
    WINDOW=300
    n = length(magnitudes)
    normalized = copy(magnitudes)
    
    # Compute moving average for each point
    for i in 1:n
        # Define window boundaries, ensuring they stay within array bounds
        start_idx = max(1, i - WINDOW÷2)
        end_idx = min(n, i + WINDOW÷2)
        
        # Compute the mean of the window
        window_mean = mean(magnitudes[start_idx:end_idx])
        
        # Subtract the local mean from the current value
        normalized[i] = magnitudes[i] - window_mean
    end
    
    return normalized
end

function logline(s:: String)
    # Log with millisecond precision timestamp
    timestamp = Dates.format(now(), "yyyy-mm-dd HH:MM:SS.sss")
    println("[$timestamp] $s")
end

function get_line_candidates(first_slice_db:: Vector{Float64}; charts:: Bool=false)
    #logline("begin get_line_candidates")
    retval = -20 * ones(length(first_slice_db)) 
    first_slice_db = normalize_magnitudes(first_slice_db)
    freq, score = find_dominant_harmonic_intervals(first_slice_db, 250, 8, 35)

    local plot_combined
    if charts
        plt_combined = plot(freq, size=(2000, 1200));
        plot!(score, xlims=view_lims);
        plot!(first_slice_db, xlims=view_lims);
    end

    #logline("begin scan")
    for p = 1:50:length(freq)-100
        vl1, vl2 = p, p+100

        domfreq = median(view(freq, vl1:vl2))
        subdata = view(first_slice_db, vl1:vl2)
    
        # Initialize array with zeros based on the dominant frequency
        offset_score = zeros(Int(round(domfreq)))
        for x in 1:length(subdata)
            offset_score[1 + (x - 1) % Int(round(domfreq))] += subdata[x]
        end
        phase = argmax(offset_score)-1
        inphase = Float64[]
        inphaseix = Float64[]
        for x in phase:domfreq:length(subdata)
            ix = Int(round(1+x))
            if ix <= length(subdata)
                push!(inphase, subdata[ix]);
                push!(inphaseix, vl1+x)
            end
        end
        maxi = argmax(inphase)
        for x in maxi-4:maxi-1
            if x >= 1 && x <= length(inphase)
                if charts
                    vline!(plt_combined,[inphaseix[x]], linestyle=:dash, color=:blue, linewidth=2, label=nothing);
                end
                ix = Int(round(inphaseix[x]))
                retval[ix] = -first_slice_db[ix]
            end
        end

    end

    if charts
        imgcat(plt_combined)
    end

    #logline("end scan")
    return retval

end

function try3()
    sub, subfs = extract_signal(sig, Float64(sr), 0.0, 25e4, 0.0, 6.0)
    mag_db, mag_lin, times, fsh = compute_spectrogram(sub, subfs)
    timerange = 1:100
    
    # Create a 2D array to store all line candidates
    freq_bins = size(mag_db, 1)
    time_bins = length(timerange)
    all_candidates = zeros(Float64, freq_bins, time_bins)
    
    # Process each time slice and add line candidates to the 2D array
    logline("Processing time slices for line candidates...")
    for (i, timeindex) in enumerate(timerange)
        first_slice_db = mag_db[:, timeindex]
        line_candidates = get_line_candidates(first_slice_db)
        
        # Store in our 2D array at the appropriate column
        all_candidates[:, i] = line_candidates
        
        # Log progress
        if i % 5 == 0 || i == time_bins
            logline(@sprintf("Processed %d/%d time slices", i, time_bins))
        end
    end

    W = 50
    chosen_candidates = zeros(Float64, freq_bins, time_bins)
    for i in 1:time_bins
        for f in 1:W:freq_bins-W
            fmin = max(1, f-W)
            fmax = min(freq_bins, f+W)
            tmin = max(1, i-10)
            tmax = min(time_bins, i+10)
            fscores = zeros(fmax-fmin+1)
            for fcheck in 1:length(fscores)
                for tcheck in tmin:tmax
                    if all_candidates[fcheck-1+fmin, tcheck] > 0
                        fscores[fcheck] += 1
                    end
                end
            end
            bestf = argmax(fscores)
            chosen_candidates[f, i] = fscores[bestf]
        end
    end

    sigs = mean(all_candidates, dims=2)
    
    # Apply simple moving average with centered kernel of size 3
    sigs_smoothed = similar(sigs)
    for i in 1:length(sigs)
        if i == 1
            # Edge case: first element
            sigs_smoothed[i] = (sigs[i] + sigs[i+1]) / 2
        elseif i == length(sigs)
            # Edge case: last element
            sigs_smoothed[i] = (sigs[i-1] + sigs[i]) / 2
        else
            # Standard case: centered kernel
            sigs_smoothed[i] = (sigs[i-1] + sigs[i] + sigs[i+1]) / 3
        end
    end
    sigs = sigs_smoothed

    # Visualize the results
    view_lims=(1, freq_bins)
    
    # Original spectrogram
    hm_original = heatmap(fsh, times[timerange], mag_db[:, timerange]',
        title="Original Spectrogram",
        xlabel="Frequency [Hz]",
        #ylabel="Time [s]",
        size=(3100, 200),
        legend = :none,
        yticks=false,
        xticks=50,
        xlims=(fsh[view_lims[1]], fsh[view_lims[2]]),
    )
    imgcat(hm_original)
    
    # Line candidates visualization
    hm_candidates = heatmap(fsh, times[timerange], all_candidates',
        title="Line Candidates",
        xlabel="Frequency [Hz]",
        #ylabel="Time [s]",
        size=(3100, 200),
        xticks=50,
        yticks=false,
        legend = :none,
        xlims=(fsh[view_lims[1]], fsh[view_lims[2]]),
        color=:viridis,
    )
    imgcat(hm_candidates)

    imgcat(plot(sigs, size=(3100, 300), xlims=view_lims, yticks=false))



    
    # Return the line candidates for further analysis
    return; #  all_candidates


    imgcat(hm)

    return;
end
