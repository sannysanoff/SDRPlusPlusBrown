#pragma once

#define NEW_BUILTIN_MODE

#include <core.h>
#include <iostream>
#include <utils/wav.h>
#include <utils/riff.h>
#include "symbolic.h"
#include <utils/wstr.h>
#include <utils/strings.h>
#include <fcntl.h>

#ifdef __linux__
#include <unistd.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

#ifdef _WIN32
#include <io.h>
#endif

#ifdef NEW_BUILTIN_MODE
#include "ft8_etc/mshv_support.h"
#include "ft8_etc/mscore.h"
#include "ft8_etc/decoderms.h"
#include "symbolic.h"
#endif

namespace dsp {

    struct FT8Decoder {

    };

    namespace ft8 {

#ifdef _WIN32

        void PrintLastError(const std::string &ctx) {
            DWORD errorCode = GetLastError();
            if (errorCode != 0) {
                LPVOID lpMsgBuf;
                DWORD msgLength = FormatMessage(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    errorCode,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPTSTR)&lpMsgBuf,
                    0,
                    NULL);

                if (msgLength > 0) {
                    flog::error("{} - ERROR: {}", ctx, lpMsgBuf);
                    LocalFree(lpMsgBuf);
                }
                else {
                    flog::error("{} - ERROR {} - Failed to retrieve error message.", ctx, (int64_t)errorCode);
                }
            }
        }

        FILE* popen_gpt4(const char* command, const char* type) {
            // gpt4 was just released (today).
            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.lpSecurityDescriptor = NULL;
            sa.bInheritHandle = TRUE;

            HANDLE readPipe, writePipe;
            if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
                PrintLastError("popen - CreatePipe");
                return NULL;
            }

            PROCESS_INFORMATION pi;
            STARTUPINFO si;
            ZeroMemory(&si, sizeof(STARTUPINFO));
            si.cb = sizeof(STARTUPINFO);
            si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            si.wShowWindow = SW_HIDE;

            if (type[0] == 'r') {
                si.hStdOutput = writePipe;
                si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
                si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            }
            else {
                si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
                si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
                si.hStdInput = readPipe;
            }

            if (!CreateProcess(NULL, const_cast<char*>(command), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                PrintLastError("popen - CreateProcess");
                return NULL;
            }

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            if (type[0] == 'r') {
                CloseHandle(writePipe);
                return _fdopen(_open_osfhandle((intptr_t)readPipe, _O_RDONLY), type);
            }
            else {
                CloseHandle(readPipe);
                return _fdopen(_open_osfhandle((intptr_t)writePipe, _O_WRONLY), type);
            }
        }
#endif

        enum {
            DMS_FT8 = 11
        } DecoderMSMode;

#ifdef NEW_BUILTIN_MODE

        void invokeDecoder(int nthreads, const std::string &mode, const std::string &wavPath, const std::string &outPath,
                               const std::string &errPath, std::function<void(int mode, std::vector<std::string> result, std::atomic<const char *> &progress)> callback, std::atomic<const char *> &progress);

#else

        void invokeDecoder(int nthreads, const std::string &mode, const std::string &wavPath, const std::string &outPath,
                           const std::string &errPath, std::function<void(int mode, std::vector<std::string> result, std::atomic<const char *> &progress)> callback, std::atomic<const char *> &progress) {

#ifdef __ANDROID__
            Dl_info info;
            dladdr((void *) &invokeDecoder, &info);
            auto path = std::string(info.dli_fname);
            path = path.substr(0, path.rfind('/'));
            auto decoderPath = path + "/sdrpp_ft8_mshv.so";
#else
            core::configManager.acquire();
            std::string modules = core::configManager.conf["modulesDirectory"];
            core::configManager.release(false);
            auto decoderPath = modules+"/../../sdrpp_ft8_mshv";
#ifdef _WIN32
            decoderPath = modules + "/../sdrpp_ft8_mshv.exe";
            auto cmd = decoderPath + " --decode " + wavPath + " --mode " + mode+" --threads "+std::to_string(nthreads);
            std::replace(cmd.begin(), cmd.end(), '/', '\\');
            flog::info("FT8decoder: spawn: {}", cmd);
            FILE* data = popen_gpt4(cmd.c_str(), "r");
            int count = 0;
            if (data) {
                char line[4096];
                while (fgets(line, sizeof(line), data)) {
                    if (strstr(line, "DECODE_EOF") != 0 ) {
                        std::vector<std::string> selected;
                        selected.emplace_back("DECODE_EOF");
                        callback(DMS_FT8, selected, progress);
                    }
                    if (strncmp(line, "FT8_OUT", 7) != 0 || strncmp(line, "FT4_OUT", 7) != 0) {
                        std::vector<std::string> singleBroken;
                        splitStringV(std::string(line), "\t", singleBroken);
                        std::vector<std::string> selected;
                        // FT8_OUT	1675635874870	30	{0}	120000	{1}	-19	{2}	0.2	{3}	775	{4}	SQ9KWU DL1PP -14	{5}	? 0	{6}	0.1	{7}	1975
                        if (singleBroken.size() > 18) {
                            selected.emplace_back(singleBroken[4]);
                            selected.emplace_back(singleBroken[6]);
                            selected.emplace_back(singleBroken[8]);
                            selected.emplace_back(singleBroken[10]);
                            selected.emplace_back(singleBroken[12]);
                            selected.emplace_back(singleBroken[14]);
                            selected.emplace_back(singleBroken[16]);
                            selected.emplace_back(singleBroken[18]);
                            count++;
                            callback(DMS_FT8, selected, progress);
                        }
                    }
                }
                int status = _pclose(data);
                if (status == 0) {
                    flog::info("FT8 Decoder ({}): process ended. Count messages: {}", mode, count);
                } else {
                    printf("Command exited with status %d\n", status);
                }
            } else {
                printf("Failed to popen %s\n", cmd.c_str());
            }
            return;
#endif // _WIN32

#endif // __ANDROID__

            // non-windows code
#ifndef _WIN32

            auto mydta = std::make_shared<core::SpawnCommand>();
            strcpy(mydta->executable, decoderPath.c_str());
            strcpy(mydta->args[0], decoderPath.c_str());
            strcpy(mydta->args[1], outPath.c_str());
            strcpy(mydta->args[2], "--decode");
            strcpy(mydta->args[3], wavPath.c_str());
            strcpy(mydta->args[4], "--mode");
            strcpy(mydta->args[5], mode.c_str());
            strcpy(mydta->args[6], "--threads");
            snprintf(mydta->args[7], sizeof mydta->args[7], "%d", nthreads);
            mydta->nargs = 8;
            strcpy(mydta->errPath, errPath.c_str());
            strcpy(mydta->outPath, outPath.c_str());
            strcpy(mydta->info, mode.c_str());

            core::forkIt(mydta);
            int nsent = 0;
            int count = 0;
            int nwaiting = 0;
            int STEP_USEC = 100000;
            int MAXWAITING_STEPS = 20000000 / STEP_USEC;  // 20 second max decode
//            flog::info("Forked, waiting outside {} steps for {} for files", MAXWAITING_STEPS, mode);

            while (true) {
                progress = "usleeping";
                auto finished = mydta->completed.load();
                if (finished && mydta->completeStatus != 0) {
                    std::vector<std::string> selected;
                    selected.emplace_back("ERROR");
                    selected.emplace_back("decoder exec failed");
                    progress = "pre-err-callback";
                    callback(DMS_FT8, selected, progress);
                    progress = "post-err-callback";
                }
//                flog::info("Usleep {} begin for {}", nwaiting, mode);
                usleep(STEP_USEC);
                nwaiting++;
//                flog::info("Usleep {} finished for {}, outpath={}", nwaiting-1, mode, outPath.c_str());
                if (nwaiting > MAXWAITING_STEPS) {
                    progress = "break_maxwait";
                    flog::warn("MAXWAITING_STEPS elapsed for {} -> will abort",mode);
                    break;
                }
                try {
                    progress = "open...";
                    auto hdl = open(outPath.c_str(), O_RDONLY);
                    progress = "opened.";
                    char rdbuf[30000];
                    if (hdl > 0) {
                        int nrd = read(hdl, rdbuf, sizeof(rdbuf) - 1);
                        progress = "freaded";
                        if (nrd > 0) {
                            rdbuf[sizeof rdbuf - 1] = 0;
                            rdbuf[nrd] = 0;
                            std::vector<std::string> thisResult;
                            progress = "split...";
                            splitString(rdbuf, "\n", [&](const std::string& p) {
                                if (p.find("FT8_OUT") == 0 || p.find("FT4_OUT") == 0 || p.find("ERROR") == 0 || p.find("DECODE_EOF") == 0) {
                                    thisResult.emplace_back(p);
                                }
                            });
                            progress = "split ok";
//                            flog::info("ft8 read bytes: {} split {}", nrd, (int)thisResult.size());
                            for (int q = nsent; q < thisResult.size(); q++) {
                                std::vector<std::string> singleBroken;
                                progress = "split small";
                                splitStringV(thisResult[q], "\t", singleBroken);
                                progress = "split small ok";
                                flog::info("ft8 from decoder: {}", thisResult[q]);
                                std::vector<std::string> selected;
                                // FT8_OUT	1675635874870	30	{0}	120000	{1}	-19	{2}	0.2	{3}	775	{4}	SQ9KWU DL1PP -14	{5}	? 0	{6}	0.1	{7}	1975
                                if (singleBroken.size() > 18) {
                                    selected.emplace_back(singleBroken[4]);
                                    selected.emplace_back(singleBroken[6]);
                                    selected.emplace_back(singleBroken[8]);
                                    selected.emplace_back(singleBroken[10]);
                                    selected.emplace_back(singleBroken[12]);
                                    selected.emplace_back(singleBroken[14]);
                                    selected.emplace_back(singleBroken[16]);
                                    selected.emplace_back(singleBroken[18]);
                                    progress = "pre-callback";
                                    callback(DMS_FT8, selected, progress);
                                    progress = "post-callback";
                                }
                                if (singleBroken.size() > 1 && singleBroken[0] == "ERROR") {
                                    selected.emplace_back("ERROR");
                                    selected.emplace_back(singleBroken[1]);
                                    progress = "pre-err-callback";
                                    callback(DMS_FT8, selected, progress);
                                    progress = "post-err-callback";
                                }
                                if (singleBroken.size() >= 1 && singleBroken[0] == "DECODE_EOF") {
                                    flog::info("DECODE_EOF");
                                    finished = true;
                                    selected.emplace_back("DECODE_EOF");
                                    progress = "pre-eof-callback";
                                    callback(DMS_FT8, selected, progress);
                                    progress = "post-eof-callback";
                                }
                                count++;
                            }
                            nsent = thisResult.size();
                        }
                        progress = "pre-close";
                        close(hdl);
                        progress = "post-close";
                    }
                } catch (std::runtime_error &err) {
                    progress = "except";
                    flog::info("EXCEPTION for {}: {}", mode, err.what());
                    finished = true;
                }
                if (finished) {
                    progress = "finishing..";
                    flog::info("FT8 - finished");
//                    flog::info("Breaking the loop for {}", mode);
                    break;
                }
            }
            core::removeForkInProgress(mydta->seq);
            flog::info("FT8 Decoder ({}): process ended. Count messages: {}", mode, count);
            progress = "finished ok.";
#endif // !win32
        }

#endif // NEW_BUILTIN_MODE

        inline void decodeFT8(int nthreads, const std::string &mode, int sampleRate, dsp::stereo_t *samples, long long nsamples,
                              std::function<void(int mode, std::vector<std::string> result, std::atomic<const char *> &progress)> callback,
                              std::atomic<const char *> &progress, bool removeFiles
                              ) {

            static std::atomic_int _seq = 100;
            int seq = ++_seq;
            std::string seqS = std::to_string(seq);
            //
            //
            //

            std::string tempPath;
            core::configManager.acquire();
            if (core::configManager.conf.find("tempDir") != core::configManager.conf.end()) {
                tempPath = core::configManager.conf["tempDir"];
            }
            core::configManager.release(false);

            if (tempPath.empty()) {
                tempPath = (std::string) core::args["temp"];
            }

            if (tempPath.empty()) {
                std::error_code ec;
                auto p = std::filesystem::temp_directory_path(ec);
                if (!ec.value()) {
                    tempPath = std::string(wstr::wstr2str(p.c_str()));
                } else {
                    tempPath = "/tmp";
                }
            }


            std::vector<dsp::stereo_t> samplez;
            samplez.resize(nsamples);
            std::vector<dsp::stereo_t> resampledV;

            int max = 0;
            for (int i = 0; i < nsamples; i++) {
                max = std::max<float>(abs(samples[i].l), max);
                max = std::max<float>(abs(samples[i].r), max);
            }

//            flog::info("max: {}", max);
            if (max == 0) {
                max = 1;        // leave zeros. whatever.
            }
            for (int i = 0; i < nsamples; i++) {
                samplez[i].l = samples[i].l / max;
                samplez[i].r = samples[i].r / max;
            }
            samples = samplez.data();

            if (sampleRate != 12000) {
                long long int outSize = 3 * (nsamples * 12000) / sampleRate;
                resampledV.resize(outSize);
                dsp::multirate::RationalResampler<dsp::stereo_t> res;
                res.init(nullptr, sampleRate, 12000);
                nsamples = res.process(nsamples, samples, resampledV.data());
                samples = resampledV.data();
            }

            wav::Writer w;
            w.setChannels(2);
            w.setFormat(wav::FORMAT_WAV);
            w.setSampleType(wav::SAMP_TYPE_FLOAT32);
            w.setSamplerate(12000);
            auto wavPath = tempPath + "/sdrpp_ft8_mshv.wav." + seqS;
            w.open(wavPath);
            w.write((float *) samples, nsamples);
            w.close();


            auto outPath = tempPath + "/sdrpp_ft8_mshv.out." + seqS;
            auto errPath = tempPath + "/sdrpp_ft8_mshv.err." + seqS;

            invokeDecoder(nthreads, mode, wavPath, outPath, errPath, callback, progress);

            if (removeFiles) {
                std::filesystem::remove(wavPath);
                std::filesystem::remove(outPath);
                std::filesystem::remove(errPath);
            } else {
                flog::info("keeping WAV file: {}", wavPath);
            }

            return;
        }
    }

}

