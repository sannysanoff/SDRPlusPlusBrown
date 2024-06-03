$build_dir=$args[0]
$root_dir=$args[1]

mkdir sdrpp_windows_x64

$RELDIR="RelWithDebInfo"

# Copy root
cp -Recurse $root_dir/* sdrpp_windows_x64/

cp C:/vcpkg/installed/x64-windows/bin/itpp.dll sdrpp_windows_x64/

# Copy core
cp $build_dir/$RELDIR/* sdrpp_windows_x64/
cp 'C:/Program Files/PothosSDR/bin/volk.dll' sdrpp_windows_x64/

# Copy source modules
cp $build_dir/source_modules/airspy_source/$RELDIR/airspy_source.dll sdrpp_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/airspy.dll' sdrpp_windows_x64/

cp $build_dir/source_modules/airspyhf_source/$RELDIR/airspyhf_source.dll sdrpp_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/airspyhf.dll' sdrpp_windows_x64/

cp $build_dir/source_modules/audio_source/$RELDIR/audio_source.dll sdrpp_windows_x64/modules/

cp $build_dir/source_modules/bladerf_source/$RELDIR/bladerf_source.dll sdrpp_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/bladeRF.dll' sdrpp_windows_x64/

cp $build_dir/source_modules/file_source/$RELDIR/file_source.dll sdrpp_windows_x64/modules/
cp $build_dir/source_modules/hl2_source/$RELDIR/hl2_source.dll sdrpp_windows_x64/modules/

cp $build_dir/source_modules/hackrf_source/$RELDIR/hackrf_source.dll sdrpp_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/hackrf.dll' sdrpp_windows_x64/

cp $build_dir/source_modules/hermes_source/$RELDIR/hermes_source.dll sdrpp_windows_x64/modules/

cp $build_dir/source_modules/limesdr_source/$RELDIR/limesdr_source.dll sdrpp_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/LimeSuite.dll' sdrpp_windows_x64/

cp $build_dir/source_modules/perseus_source/$RELDIR/perseus_source.dll sdrpp_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/perseus-sdr.dll' sdrpp_windows_x64/

cp $build_dir/source_modules/plutosdr_source/$RELDIR/plutosdr_source.dll sdrpp_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/libiio.dll' sdrpp_windows_x64/
cp 'C:/Program Files/PothosSDR/bin/libad9361.dll' sdrpp_windows_x64/

cp $build_dir/source_modules/rfspace_source/$RELDIR/rfspace_source.dll sdrpp_windows_x64/modules/

cp $build_dir/source_modules/rtl_sdr_source/$RELDIR/rtl_sdr_source.dll sdrpp_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/rtlsdr.dll' sdrpp_windows_x64/

cp $build_dir/source_modules/rtl_tcp_source/$RELDIR/rtl_tcp_source.dll sdrpp_windows_x64/modules/

cp $build_dir/source_modules/sdrplay_source/$RELDIR/sdrplay_source.dll sdrpp_windows_x64/modules/ -ErrorAction SilentlyContinue
cp 'C:/Program Files/SDRplay/API/x64/sdrplay_api.dll' sdrpp_windows_x64/ -ErrorAction SilentlyContinue

cp $build_dir/source_modules/sdrpp_server_source/$RELDIR/sdrpp_server_source.dll sdrpp_windows_x64/modules/

cp $build_dir/source_modules/soapy_source/$RELDIR/soapy_source.dll sdrpp_windows_x64/modules/

cp $build_dir/source_modules/spyserver_source/$RELDIR/spyserver_source.dll sdrpp_windows_x64/modules/

cp $build_dir/source_modules/kiwisdr_source/$RELDIR/kiwisdr_source.dll sdrpp_windows_x64/modules/


# Copy sink modules
cp $build_dir/sink_modules/audio_sink/$RELDIR/audio_sink.dll sdrpp_windows_x64/modules/
cp "C:/Program Files (x86)/RtAudio/bin/rtaudio.dll" sdrpp_windows_x64/

cp $build_dir/sink_modules/network_sink/$RELDIR/network_sink.dll sdrpp_windows_x64/modules/


# Copy decoder modules
cp $build_dir/decoder_modules/m17_decoder/$RELDIR/m17_decoder.dll sdrpp_windows_x64/modules/
cp $build_dir/decoder_modules/ft8_decoder/$RELDIR/ft8_decoder.dll sdrpp_windows_x64/modules/
cp $build_dir/decoder_modules/ch_extravhf_decoder/$RELDIR/ch_extravhf_decoder.dll sdrpp_windows_x64/modules/
#cp $build_dir/decoder_modules/ft8_decoder/$RELDIR/sdrpp_ft8_mshv.exe sdrpp_windows_x64/
cp "C:/Program Files/codec2/lib/libcodec2.dll" sdrpp_windows_x64/

cp $build_dir/decoder_modules/meteor_demodulator/$RELDIR/meteor_demodulator.dll sdrpp_windows_x64/modules/

cp $build_dir/decoder_modules/radio/$RELDIR/radio.dll sdrpp_windows_x64/modules/


# Copy misc modules
cp $build_dir/misc_modules/discord_integration/$RELDIR/discord_integration.dll sdrpp_windows_x64/modules/
cp $build_dir/misc_modules/noise_reduction_logmmse/$RELDIR/noise_reduction_logmmse.dll sdrpp_windows_x64/modules/

cp $build_dir/misc_modules/frequency_manager/$RELDIR/frequency_manager.dll sdrpp_windows_x64/modules/
cp $build_dir/misc_modules/websdr_view/$RELDIR/websdr_view.dll sdrpp_windows_x64/modules/
cp $build_dir/misc_modules/reports_monitor/$RELDIR/reports_monitor.dll sdrpp_windows_x64/modules/

cp $build_dir/misc_modules/iq_exporter/$RELDIR/iq_exporter.dll sdrpp_windows_x64/modules/

cp $build_dir/misc_modules/recorder/$RELDIR/recorder.dll sdrpp_windows_x64/modules/

cp $build_dir/misc_modules/rigctl_client/$RELDIR/rigctl_client.dll sdrpp_windows_x64/modules/

cp $build_dir/misc_modules/rigctl_server/$RELDIR/rigctl_server.dll sdrpp_windows_x64/modules/

cp $build_dir/misc_modules/scanner/$RELDIR/scanner.dll sdrpp_windows_x64/modules/


# Copy supporting libs
cp 'C:/Program Files/PothosSDR/bin/libusb-1.0.dll' sdrpp_windows_x64/
cp 'C:/Program Files/PothosSDR/bin/pthreadVC2.dll' sdrpp_windows_x64/

Compress-Archive -Path sdrpp_windows_x64/ -DestinationPath sdrpp_windows_x64.zip

rm -Force -Recurse sdrpp_windows_x64
