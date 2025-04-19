using WAV
using DSP
import StatsBase # Change from 'using' to 'import'
using FFTW
using Plots
gr()  # Use GR backend
using Printf
using Base.Filesystem  # for rm, ispath
using Images
using ImageFiltering # Add this line
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

    # 2. Сглаживание Откликов (Внесение Локальности)
    # Используем Гауссово сглаживание вдоль оси N (вторая ось)
    # Ширина окна W связана с epsilon, W = 2*epsilon + 1
    # Стандартное отклонение sigma для Гауссова фильтра можно взять пропорциональным W или epsilon
    # Например, sigma = W / 5.0 или sigma = epsilon
    sigma_smooth = max(1.0, (2.0 * epsilon + 1.0) / 5.0) # Эмпирическая формула, >= 1.0

    # Создаем 1D Гауссово ядро
    # Радиус ядра должен быть достаточным (например, 3*sigma)
    kernel_radius = ceil(Int, 3 * sigma_smooth)
    # KernelFactors.gaussian(sigma, [длина_окна])
    gauss_kernel = KernelFactors.gaussian(sigma_smooth, 2 * kernel_radius + 1)

    # Применяем фильтр ко всем строкам (каждому интервалу d) независимо
    # Используем imfilter из ImageFiltering. Фильтруем вдоль 2-й размерности.
    # KernelFactors.Null() означает отсутствие фильтрации по 1-й размерности (интервалы).
    # "reflect" - стандартный способ обработки границ

    smoothed_responses = imfilter(raw_responses, (KernelFactors.reshaped(trivial_kernel, 1), gauss_kernel), "reflect")



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
    dominant_k_indices = getindex.(Tuple.(max_vals_and_indices[2]), 1)

    # Преобразуем индексы k (1..num_intervals) обратно в реальные интервалы d
    dominant_intervals .= intervals[dominant_k_indices]

    # Извлекаем максимальные значения как коэффициент уверенности/энергии
    # dropdims убирает лишнюю размерность 1, превращая матрицу 1xN в вектор N
    confidence_scores .= vec(max_vals_and_indices[1]) # Используем vec() для преобразования

    return dominant_intervals, confidence_scores
end

function try3(offs = -8100)
    sub, subfs = extract_signal(sig, Float64(sr), 0.0, 5e4, 0.0, 3.0)
    mag_db, mag_lin, times, fsh = compute_spectrogram(sub, subfs)
    first_slice_db = mag_db[:, 10]
    println("Running...", size(first_slice_db)[1])

    freq, score = find_dominant_harmonic_intervals(first_slice_db, 250, 5, 70)

    plt_combined = plot(freq);
    plot!(score);

    imgcat(plt_combined)


    return;

    local valz
    for z in 1:3
        timestamp_str = Dates.format(now(), "yyyy-mm-dd HH:MM:SS.s")
        println("[$timestamp_str] Iteration $z/10..")
        valz = Float64[]
        for i in fsh
            val = score_line_at_offset(first_slice_db, fsh, i)
            push!(valz, val)
        end
    end
    println("Done.")

    # Determine the full frequency range for consistent x-axes (needed if not already global)
    # Assuming fmin_global and fmax_global are accessible here or recalculate if needed
    fmin_global, fmax_global = minimum(fsh), maximum(fsh)

    # Create the base plot with the first series (Magnitude vs Frequency)
    plt_combined = plot(fsh, first_slice_db;
                        xlabel="Frequency [Hz]", ylabel="Magnitude [dB]",
                        xformatter = x -> @sprintf("%.0f", x), # Format x-ticks
                        xticks = 50, # Suggest more ticks
                        label="Magnitude", # Label for the first series
                        size=(3600, 600)) # Set x-axis limits

    # Add the second series (Score vs Frequency) using the right y-axis
    plot!(twinx(), fsh, valz;
          ylabel="Score",
          label="Score", # Label for the second series
          color=:red, # Choose a different color
          size=(3600, 600)
          ) # Position second legend entry if needed

    # Add a title below the plot
    plot!(plt_combined, bottom_margin=15Plots.Plots.mm) # Ensure bottom margin for title
    annotate!(plt_combined, [(0.5, -0.1, Plots.text("Time Slice Magnitude and Calculated Score vs. Frequency", :center, 10))]; annotation_clip=false)
    println("Sliding Window Peak Analysis Results (Period [indices], Phase [radians]): of array shape=%", size(first_slice_db))
    println("valz length=", length(valz))
    println(valz[1:10])

    # Display the combined plot
    imgcat(plt_combined)

    # return score_line_at_offset(first_slice_db, fsh, offs) # Keep this commented out or decide if needed
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

    periods, phases = sliding_window_peak_analysis(first_slice_db, 250, 1)
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


    imgcat(plt_slice)

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
    imgcat(plt)
    println("Done2.")
end
