#pragma once
#include "protocol1_discovery.h"
#include "discovered.h"
#include "plugin_main.h"
#include "utils/stream_tracker.h"
#include <dsp/types.h>
#include <signal_path/signal_path.h>
#include <ctm.h>
#include <core.h>

#ifdef __linux__
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#endif

#include <utils/usleep.h>

#define DATA_PORT 1024

struct ControlData {
    unsigned char C1; // upper bits [24..31]
    unsigned char C2; // middle bits [16..23]
    unsigned char C3; // lower middle bits [8..15]
    unsigned char C4; // lower bits [0..7]
};

struct HL2Device {

    DISCOVERED _discovered;
    DISCOVERED* discovered;

    enum {
        SYNC_0 = 0,
        SYNC_1,
        SYNC_2,
        CONTROL_0,
        CONTROL_1,
        CONTROL_2,
        CONTROL_3,
        CONTROL_4,
        LEFT_SAMPLE_HI,
        LEFT_SAMPLE_MID,
        LEFT_SAMPLE_LOW,
        RIGHT_SAMPLE_HI,
        RIGHT_SAMPLE_MID,
        RIGHT_SAMPLE_LOW,
        MIC_SAMPLE_HI,
        MIC_SAMPLE_LOW,
        SKIP
    };

    ControlData deviceControl[50] = { 0 }; // multiple hardware registers here.

    const int SYNC0 = 0;
    const int SYNC1 = 1;
    const int SYNC2 = 2;
    const int C0 = 3;
    const int C1 = 4;
    const int C2 = 5;
    const int C3 = 6;
    const int C4 = 7;

    const int SPEED_48K = 0x00;
    const int SPEED_96K = 0x01;
    const int SPEED_192K = 0x02;
    const int SPEED_384K = 0x03;


#define SYNC 0x7F

    SOCKET data_socket;
    struct sockaddr_in data_addr;
    int data_addr_length;
    bool running = false;
    //
    long long sendStartTime = 0;     // millis of first send packet
    long long sendStartSequence = 0; // send_sequence at a time of a start
    //
    int state = SYNC_0;
    unsigned char control_in[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };

    int receivers = 1; // limit of receivers here

    int nreceiver; // state machine
    int left_sample;
    int right_sample;
    short mic_sample;
    double left_sample_double;
    double right_sample_double;
    int nsamples;
    int iq_samples;

    bool adc_overload;
    bool overflow;
    bool underflow;
    double fill_level;
    long long fill_levelUpdate;
    bool IO1;
    bool IO2;
    bool IO3;
    int AIN3;
    int AIN4;
    int AIN6;
    int exciter_power;
    int alex_forward_power;
    int alex_reverse_power;
    float swr = 1;
    float fwd = 0, rev = 0; // real ones, calculated in getSWR()
    double temperature;

    unsigned char output_buffer[1024];
    int output_buffer_index = 8;
    int tx_output_buffer_index = 8;
    long send_sequence = -1;
    int metis_offset = 8;
    int current_rx = 0;
    unsigned char softwarePower = 255; // 0..255
    unsigned char hardwarePower = 255; // 0..255
    int command = 1;

#define LT2208_GAIN_ON    0x04
#define LT2208_DITHER_OFF 0x00
#define LT2208_DITHER_ON  0x08
#define LT2208_RANDOM_OFF 0x00
#define LT2208_RANDOM_ON  0x10


    int rx_sample_rate = 384000;

    const std::function<void(double, double)> handler;

    int pttHangTime = 6;
    int bufferLatency = 0x15; // as in linhpsdr

    HL2Device(DISCOVERED _discovered, const std::function<void(double, double)>& handler) : _discovered(_discovered), handler(handler), sendTracker("hl2 tx") {
        discovered = &this->_discovered;
        setADCGain(0);
        setFrequency(7000000);
        setHangLatency(pttHangTime, bufferLatency); // as in linhpsdr
        setDuplex(true);
        setSoftwarePower(255);
        bool debug = true;
        if (debug) {
            debugOut = fopen("/tmp/hl2_tx_stream.bin", "wb");
        }
    }

    bool isADCOverload() {
        return adc_overload;
    }

    void setADCGain(int gain) {
        gain += 12;
        deviceControl[0xA].C4 = gain | 0b1000000;
        gain |= 0x40; // ptt is off ??
        secondControlIndex = 0xA;
    }

    void setHangLatency(unsigned char pttHangTime, unsigned char bufferLatencyMS) {
        deviceControl[0x17].C1 = 0;
        deviceControl[0x17].C2 = 0;
        deviceControl[0x17].C3 = pttHangTime;
        deviceControl[0x17].C4 = bufferLatencyMS;
    }

    void setFrequency(long long frequency) { // RX freq
        deviceControl[0x02].C1 = frequency >> 24;
        deviceControl[0x02].C2 = frequency >> 16;
        deviceControl[0x02].C3 = frequency >> 8;
        deviceControl[0x02].C4 = frequency >> 0;
        if (txFrequency == 0) {
            txFrequency = frequency;
        }
        deviceControl[0x01].C1 = txFrequency >> 24;
        deviceControl[0x01].C2 = txFrequency >> 16;
        deviceControl[0x01].C3 = txFrequency >> 8;
        deviceControl[0x01].C4 = txFrequency >> 0;
    }

    void setSoftwarePower(unsigned char power) { // power = 0..255 (however only 4 upper bits (31..28) are used)
        // soft power is used. Hard is max.
        deviceControl[0x09].C1 = hardwarePower & 0xF0;
        this->softwarePower = power;
    }

    void setHardwarePower(unsigned char power) { // power = 0..255 (however only 4 upper bits (31..28) are used)
        // soft power is used. Hard is max.
        deviceControl[0x09].C1 = power & 0xF0;
        this->hardwarePower = power;
    }


    void setPAEnabled(bool enabled) { // bit 19
        deviceControl[0x09].C2 &= ~0x08;
        deviceControl[0x09].C2 |= enabled ? 0x08 : 0x00;
    }

    void setTxFrequency(long long txFrequency) {
        this->txFrequency = txFrequency;
        deviceControl[0x01].C1 = txFrequency >> 24;
        deviceControl[0x01].C2 = txFrequency >> 16;
        deviceControl[0x01].C3 = txFrequency >> 8;
        deviceControl[0x01].C4 = txFrequency >> 0;
    }

    float getSWR() {
        auto constant1 = 3.3;
        auto constant2 = 1.4;
        auto fwd_cal_offset = 6;
        auto fwd_power = alex_forward_power;
        auto rev_power = alex_reverse_power;


        if (rev_power > fwd_power) {
            fwd_power = alex_reverse_power;
            rev_power = alex_forward_power;
        }

        fwd_power = fwd_power - fwd_cal_offset;
        auto v1 = ((double)fwd_power / 4095.0) * constant1;
        fwd = (v1 * v1) / constant2;

        auto exciter = 0.0;

        rev = 0.0;
        if (fwd_power != 0) {
            v1 = ((double)rev_power / 4095.0) * constant1;
            rev = (v1 * v1) / constant2;
        }

        double this_swr = (1 + sqrt(rev / fwd)) / (1 - sqrt(rev / fwd));
        if (this_swr < 0.0) this_swr = 1.0;

        if (fwd < 0.05) {
            swr = 1;
        }
        else {
            if (isnan(swr) || isinf(swr)) {
                swr = 1; // fix previous value
            }
            // Exponential moving average filter
            double alpha = 0.7;
            swr = (alpha * this_swr) + (1 - alpha) * swr;
        }
        return swr;
    }

    // does not work; needs investigation; doing in software for now.
    void setTune(bool tune) {
        if (tune) {
            deviceControl[0x9].C2 |= 0x10; // bit 20
        }
        else {
            deviceControl[0x9].C2 &= ~0x10;
        }
    }

    bool transmitMode = false;

    void setPTT(bool ptt) {
        transmitMode = ptt;
    }

    void doTuneActive(bool tune) {
        setTune(tune);
        //        prepareRequest(1);
        //        sendToEndpoint(0x2, output_buffer);
    }

    void setDuplex(bool duplex) {
        deviceControl[0x0].C4 &= ~0b100;
        deviceControl[0x0].C4 |= duplex ? 0b100 : 0b000;
    }

    void setRxSampleRate(int rx_sample_rate) {
        deviceControl[0x0].C1 &= ~0x3;
        switch (rx_sample_rate) {
        case 48000:
            deviceControl[0x0].C1 |= SPEED_48K;
            break;
        case 96000:
            deviceControl[0x0].C1 |= SPEED_96K;
            break;
        case 192000:
            deviceControl[0x0].C1 |= SPEED_192K;
            break;
        case 384000:
            deviceControl[0x0].C1 |= SPEED_384K;
            break;
        }
    }

    void setSevenRelays(int sevenRelays) {
        deviceControl[0x0].C2 &= 1;
        deviceControl[0x0].C2 |= sevenRelays << 1;
    }

    //    void setDuplex(bool on) {
    //        deviceControl[0x0].C4 &= ~0x04;
    //        if (on) {
    //            deviceControl[0x0].C4 = 0x04 /* duplex on */;
    //        }
    //    }

    //

    FILE *debugOut = nullptr;

    float maxAmp = 0;

    // needs 63 samples as per protocol spec.
    void storeNextIQSamples(unsigned char* dest, const dsp::complex_t* samples) {
        float scale = softwarePower / 255.0;
        int clipped = 0;
        for (int i = 0; i < 63; i++) {
            auto comp = samples[i];
            // input: -1, 1, output range -32768..32767
            auto I = ((int32_t)((comp.re * scale) * 32767)) & 0xFFFF;
            auto Q = ((int32_t)((comp.im * scale) * 32767)) & 0xFFFF;
            float amp = comp.amplitude() * scale;
            if (amp > maxAmp) {
                maxAmp = amp;
            }
            if (amp > 1.0) {
                float nscale = scale * (1.0 / amp);
                I = ((int32_t)((comp.re * nscale) * 32767)) & 0xFFFF;
                Q = ((int32_t)((comp.im * nscale) * 32767)) & 0xFFFF;
                clipped++;
            }

            if (debugOut) {
                if (transmitMode) {
                    fwrite(&I, 2, 1, debugOut);
                    fwrite(&Q, 2, 1, debugOut);
                }
            }


            // add low pass filter HERE.

            // skip first 4 bytes

            dest[4] = I >> 8;
            dest[5] = I & 0xFF;
            dest[6] = Q >> 8;
            dest[7] = Q & 0xFF;

            // step is 8 bytes
            dest += 8;
        }
        if (clipped) {
            flog::info("TX I/Q has been clipped {} times", clipped);
        }
    }

    StreamTracker sendTracker;

    void prepareRequest(int sequence) {
        //        0  1  2  3    4     5  6  7  8  9  10
        static int sendRegisters[] = { 0, 1, 2, 9, 0xA, 0x17, 9, 1, 2, 9, 2 };
        if (sequence > 10 || sequence < 0) {
            sequence = 1;
        }
        int sendRegister = sendRegisters[sequence];

        memset(output_buffer, 0, sizeof(output_buffer));


        output_buffer[SYNC0] = SYNC;
        output_buffer[SYNC1] = SYNC;
        output_buffer[SYNC2] = SYNC;
        output_buffer[C0] = 0x00 | (transmitMode ? 1 : 0);
        output_buffer[C1] = deviceControl[0x00].C1;
        output_buffer[C2] = deviceControl[0x00].C2;
        output_buffer[C3] = deviceControl[0x00].C3;
        output_buffer[C4] = deviceControl[0x00].C4;

        maxAmp = 0;

        samplesToSend.lock.lock();
        if (samplesToSend.isDataReady(63)) {
            this->storeNextIQSamples(output_buffer + 8, samplesToSend.data.data());
            samplesToSend.consume_(nullptr, 63);
        }
        samplesToSend.lock.unlock();
//        sendTracker.add(63);
        // 512-8 = 504 bytes here, 63 elements (8 bytes per iq sample (of those 4 bytes are obsolete/unused), and 2 bytes per re, im)
        output_buffer[512 + SYNC0] = SYNC;
        output_buffer[512 + SYNC1] = SYNC;
        output_buffer[512 + SYNC2] = SYNC;
        output_buffer[512 + C0] = (sendRegister << 1) | (transmitMode ? 1 : 0);
        output_buffer[512 + C1] = deviceControl[sendRegister].C1;
        output_buffer[512 + C2] = deviceControl[sendRegister].C2;
        output_buffer[512 + C3] = deviceControl[sendRegister].C3;
        output_buffer[512 + C4] = deviceControl[sendRegister].C4;

        if (samplesToSend.isDataReady(63)) {
            samplesToSend.lock.lock();
            this->storeNextIQSamples(output_buffer + 8 + 512, samplesToSend.data.data());
            samplesToSend.consume_(nullptr, 63);
            samplesToSend.lock.unlock();
        }
//        sendTracker.add(63);

        // 512-8 = 504 bytes here, 63 elements (8 bytes per iq sample (of those 4 bytes are obsolete/unused), and 2 bytes per re, im)

        // total 1024 bytes
        sigpath::averageTxSignalLevel.emit(maxAmp);
    }


    void sendToEndpoint(int endpoint, unsigned char* buffer) {
        unsigned char metis_buffer[1024 + 8];
        memset(metis_buffer, 0, sizeof(metis_buffer));
        send_sequence++;
        metis_buffer[0] = 0xEF;
        metis_buffer[1] = 0xFE;
        metis_buffer[2] = 0x01;
        metis_buffer[3] = endpoint;
        metis_buffer[4] = (send_sequence >> 24) & 0xFF;
        metis_buffer[5] = (send_sequence >> 16) & 0xFF;
        metis_buffer[6] = (send_sequence >> 8) & 0xFF;
        metis_buffer[7] = (send_sequence)&0xFF;
        for (int i = 0; i < 1024; i++) {
            metis_buffer[i + 8] = buffer[i];
        }

        if (sendto(data_socket, (const char*)metis_buffer, 1024 + 8, 0, (struct sockaddr*)&data_addr, data_addr_length) != 1024 + 8) {
            perror("sendto socket failed for metis_send_data\n");
        }

        //        udpStream.fillFrom(metis_buffer, 1024+8);
    };

    // from receiver to PC
    void add_iq_samples(int receiverNo, double i_sample, double q_sample) {
        handler(i_sample, q_sample);
    }

    void process_control_bytes() {


        switch ((control_in[0] >> 3) & 0x1F) {
        case 0:
            adc_overload = (control_in[1] & 0x01) == 0x01;
            IO1 = (control_in[1] & 0x02) == 0x02;
            IO2 = (control_in[1] & 0x04) == 0x04;
            IO3 = (control_in[1] & 0x08) == 0x08;

            if (transmitMode) {
                int recovery = ((control_in[3] & 0xC0) >> 6);
                if (recovery == 3) {
                    fill_level = 10000.0; // overflow
                }
                else if (recovery == 2) {
                    fill_level = -1; // underflow
                }
                else {
                    int msb = control_in[3] & 0b00111111;
                    fill_level = (double)msb * 16 * 1.0 / 48; // 0..21
                    fill_levelUpdate = currentTimeMillis();
                }
            }
            break;
        case 1: {
            exciter_power = ((control_in[1] & 0xFF) << 8) | (control_in[2] & 0xFF); // from Penelope or Hermes

            int adc = ((control_in[1] & 0xFF) << 8) | (control_in[2] & 0xFF);

            double this_temperature = (3.26 * ((double)adc / 4096.0) - 0.5) / 0.01;
            // Exponential moving average filter
            double alpha = 0.7;
            temperature = (alpha * this_temperature) + (1 - alpha) * temperature;


            alex_forward_power = ((control_in[3] & 0xFF) << 8) | (control_in[4] & 0xFF); // from Alex or Apollo
            break;
        }
        case 2:
            alex_reverse_power = ((control_in[1] & 0xFF) << 8) | (control_in[2] & 0xFF); // from Alex or Apollo
            AIN3 = (control_in[3] << 8) + control_in[4];                                 // from Pennelope or Hermes
            break;
        case 3:
            AIN4 = (control_in[1] << 8) + control_in[2]; // from Pennelope or Hermes
            AIN6 = (control_in[3] << 8) + control_in[4]; // from Pennelope or Hermes
            break;
        }

        getSWR();
    }


    void process_ozy_input_buffer(unsigned char* buffer) {
        int i;
        for (i = 0; i < 512; i++) {
            process_ozy_byte(buffer[i] & 0xFF);
        }
    }

    // receive
    void process_ozy_byte(int b) {
        int i, j;
        switch (state) {
        case SYNC_0:
            if (b == SYNC) {
                state++;
            }
            break;
        case SYNC_1:
            if (b == SYNC) {
                state++;
            }
            break;
        case SYNC_2:
            if (b == SYNC) {
                state++;
            }
            break;
        case CONTROL_0:
            control_in[0] = b;
            state++;
            break;
        case CONTROL_1:
            control_in[1] = b;
            state++;
            break;
        case CONTROL_2:
            control_in[2] = b;
            state++;
            break;
        case CONTROL_3:
            control_in[3] = b;
            state++;
            break;
        case CONTROL_4:
            control_in[4] = b;
            process_control_bytes();
            nreceiver = 0;
            iq_samples = (512 - 8) / ((receivers * 6) + 2);
            nsamples = 0;
            state++;
            break;
        case LEFT_SAMPLE_HI:
            left_sample = (int)((signed char)b << 16);
            state++;
            break;
        case LEFT_SAMPLE_MID:
            left_sample |= (int)((((unsigned char)b) << 8) & 0xFF00);
            state++;
            break;
        case LEFT_SAMPLE_LOW:
            left_sample |= (int)((unsigned char)b & 0xFF);
            left_sample_double = (double)left_sample / 8388607.0; // 24 bit sample 2^23-1
            state++;
            break;
        case RIGHT_SAMPLE_HI:
            right_sample = (int)((signed char)b << 16);
            state++;
            break;
        case RIGHT_SAMPLE_MID:
            right_sample |= (int)((((unsigned char)b) << 8) & 0xFF00);
            state++;
            break;
        case RIGHT_SAMPLE_LOW: {
            right_sample |= (int)((unsigned char)b & 0xFF);
            right_sample_double = (double)right_sample / 8388607.0; // 24 bit sample 2^23-1
            add_iq_samples(nreceiver, left_sample_double, right_sample_double);
            nreceiver++;
            if (nreceiver == receivers) {
                state++;
            }
            else {
                state = LEFT_SAMPLE_HI;
            }
            break;
        }
        case MIC_SAMPLE_HI:
            mic_sample = (short)(b << 8);
            state++;
            break;
        case MIC_SAMPLE_LOW:
            mic_sample |= (short)(b & 0xFF);
            //                if(!radio->local_microphone) {
            //                    mic_samples++;
            //                    if(mic_samples>=mic_sample_divisor) { // reduce to 48000
            //                        add_mic_sample(radio->transmitter,(float)mic_sample/32768.0);
            //                        mic_samples=0;
            //                    }
            //                }
            nsamples++;
            if (nsamples == iq_samples) {
                state = SYNC_0;
            }
            else {
                nreceiver = 0;
                state = LEFT_SAMPLE_HI;
            }
            break;
        }
    }


    void start() {
        if (!running) {
            try {
                start_protocol1_thread();
                metis_restart();
            }
            catch (std::exception& ex) {
                flog::error(ex.what());
            }
            running = true;
        }
    }

    void stop() {
        if (running) {
            running = false;
            if (receiveThread) {
                receiveThread->join();
            }
            if (sendThread) {
                sendThread->join();
            }
//            if (sendThread2) {
//                sendThread2->join();
//            }
            metis_start_stop(0);
#ifdef WIN32
            closesocket(data_socket);
#else
            close(data_socket);
#endif
            fill_level = 0;
        }
    }

    int secondControlIndex = 1;

    void metis_restart() {
        prepareRequest(2);
        sendToEndpoint(0x2, output_buffer);
        prepareRequest(0x14);
        sendToEndpoint(0x2, output_buffer);
        usleep(50000);

        // start the data flowing
        metis_start_stop(1); // IQ data (wideband data disabled)
    }


    void metis_start_stop(int command) {
        int i;
        unsigned char buffer[64];

        state = SYNC_0;

#ifdef USBOZY
        if (radio->discovered->device != DEVICE_OZY) {
#endif

            buffer[0] = 0xEF;
            buffer[1] = 0xFE;
            buffer[2] = 0x04;    // start/stop command
            buffer[3] = command; // send EP6 and EP4 data (0x00=stop)

            for (i = 0; i < 60; i++) {
                buffer[i + 4] = 0x00;
            }

            metis_send_buffer(buffer, sizeof(buffer));
#ifdef USBOZY
        }
#endif
    }

    void metis_send_buffer(unsigned char* buffer, int length) {
        if (sendto(data_socket, (const char*)buffer, length, 0, (struct sockaddr*)&data_addr, data_addr_length) != length) {
            perror("sendto socket failed for metis_send_data\n");
        }
    }


    dsp::queue<dsp::complex_t> samplesToSend;

    long long psnd;
    long long lastSendTime = 0;
    long long lastReceiveTime = 0;

    FILE* logg = nullptr;


    void maybeSendNextPacket() {

        // this gets called once a millisecond.
        // normal sends occur around 1/3 of total calls.

        static int sent = 0;
        static int returned = 0;
        static int entries = 0;
        const int firstBurst = 8;
        auto ctm = currentTimeMillis();
        char logbuf[1024];

        entries++;

        if (!transmitMode) {
            // when recv, just send something
            if (ctm - lastSendTime < 3) {
                returned++;
                return;
            }
        }
        else {
//            if (!logg) {
//                logg = fopen("/tmp/trx.log.txt", "w");
//            }
//            sprintf(logbuf, "%lld %lld %f %d", ctm, ctm - fill_levelUpdate, fill_level, (int)samplesToSend.data.size());
            // when transmit, check the buffer size.
            if (entries %2 == 1) {
//                fprintf(logg, "%s - even ......\n", logbuf);
                returned++;
                return; // too early
            }
            if (fill_level < 1) {
                // fall through
            } else {
                if (ctm - fill_levelUpdate > 5) { // stopped updating
                    if (ctm - lastSendTime < 3) { // to prevent tx iq buffer underflow, averaging by time.
//                        fprintf(logg, "%s - STUCK (sent ago %d), skipping send ....... \n", logbuf, (int)(lastSendTime - ctm));
                        returned++;
                        return; // too early
                    } else {
//                        sprintf(logbuf + strlen(logbuf)," - STUCK, sending anyway")
                    }
                }
                if (fill_level > 15) {
//                    fprintf(logg, "%s - full .......\n", logbuf);
                    returned++;
                    return; // too early
                }
            }
        }
        sent++;
        prepareRequest(secondControlIndex++);
        if (secondControlIndex > 10) {
            secondControlIndex = 1;
        }

        lastSendTime = currentTimeMillis();

        sendToEndpoint(0x2, output_buffer);
        auto afterSendTime = currentTimeMillis();
        if (transmitMode) {
//            fprintf(logg, "%s - SENT: %d,%d\n", logbuf, (int)(lastSendTime - ctm), (int)(afterSendTime - lastSendTime));
        }
        //        if (sent % 1000 == 0) {
        //            flog::info("Sent {} packets, of total {} calls", sent, returned+sent);
        //        }
    }


    void start_protocol1_thread() {

        data_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (data_socket < 0) {
            throw std::runtime_error("protocol1: create socket failed for data_socket\n");
        }

        int optval = 1;
        if (setsockopt(data_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) < 0) {
            throw std::runtime_error("data_socket: SO_REUSEADDR");
        }

        int send_buf_size = (1024 + 8) * 10 + 512;
        if (setsockopt(data_socket, SOL_SOCKET, SO_SNDBUF, (const char*)&send_buf_size, sizeof(send_buf_size)) < 0) {
            throw std::runtime_error("data_socket: SO_SNDBUF");
        }
#ifndef WIN32
        if (setsockopt(data_socket, SOL_SOCKET, SO_REUSEPORT, (const char*)&optval, sizeof(optval)) < 0) {
            throw std::runtime_error("data_socket: SO_REUSEPORT");
        }
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        setsockopt(data_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(struct timeval));
#else
        DWORD msec = 100;
        setsockopt(data_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&msec, sizeof(msec));
#endif

        auto ua = (unsigned char*)&discovered->info.network.interface_address.sin_addr;
        flog::info("HL2: Binding socket to interface {}: {}.{}.{}.{}", discovered->info.network.interface_name, ua[0], ua[1], ua[2], ua[3]);
        // bind to the interface
        if (bind(data_socket, (struct sockaddr*)&discovered->info.network.interface_address, discovered->info.network.interface_length) < 0) {
            auto what = std::string("protocol1: bind socket failed for data_socket: errno=") + std::to_string(errno);
            throw std::runtime_error(what.c_str());
            exit(-1);
        }

        memcpy(&data_addr, &discovered->info.network.address, discovered->info.network.address_length);
        data_addr_length = discovered->info.network.address_length;
        data_addr.sin_port = htons(DATA_PORT);
        sendStartTime = 0;
        send_sequence = 0;
        ua = (unsigned char*)&data_addr.sin_addr;
        flog::info("HL2: Target address: {}.{}.{}.{}", ua[0], ua[1], ua[2], ua[3]);

        running = true;
        sendThread = std::make_shared<std::thread>([&] {
            SetThreadName("hl2_send_planner");
            while (running) {
                // we assume the receiving happens with same or higher frequency than the sending
                // sending happens at 48000 samples per second.
                this->maybeSendNextPacket();
                usleep(1000);
            }
        });
//        sendThread2 = std::make_shared<std::thread>([&] {
//            SetThreadName("hl2_send_udp");
//            static int _cnt = 0;
//            while (running) {
//                if (udpStream.isDataReady(1024 + 8)) {
//                    unsigned char buf[1024 * 8];
//                    udpStream.consume(buf, 1024 * 8);
//                    auto b1 = currentTimeMillis();
//                    _cnt++;
//                    if (sendto(data_socket, (const char*)buf, 1024 + 8, 0, (struct sockaddr*)&data_addr, data_addr_length) != 1024 + 8) {
//                        perror("sendto socket failed for metis_send_data\n");
//                    }
//                    auto b2 = currentTimeMillis();
//                    if (b2 - b1 > 3) {
//                        flog::warn("({}) sendto took {} ms", _cnt, (int64_t)(b2 - b1));
//                    }
//                    continue;
//                }
//                usleep(1000);
//            }
//        });
        receiveThread = std::make_shared<std::thread>([&] {
            struct sockaddr_in addr;
            socklen_t length;
            unsigned char buffer[2048];
            int bytes_read;
            int ep;
            long sequence;

            SetThreadName("hl2_receive_thread");

            fprintf(stderr, "hl2: protocol1: receive_thread started\n");

            length = sizeof(addr);
            auto ctm = currentTimeNanos();
            std::string logg;
            while (running) {
                auto pctm = currentTimeNanos();
                if ((pctm - ctm) / 1000 > 500) {
                    logg += "?" + std::to_string((pctm - ctm) / 1000) + " ";
                }
                bytes_read = recvfrom(data_socket, (char*)buffer, sizeof(buffer), 0, (struct sockaddr*)&addr, &length);
                auto rcv = currentTimeNanos();
                lastReceiveTime = currentTimeMillis();
                logg += std::to_string((rcv - pctm) / 1000) + " ";
                ctm = rcv;
                if (logg.size() > 100) {
                    //                    flog::info("hl2 recv timings: {0}", logg);
                    logg = "";
                }
                if (bytes_read < 0) {
                    bool timeout = false;
#ifdef WIN32
                    DWORD err = WSAGetLastError();
                    timeout = err == WSATRY_AGAIN || err == WSAETIMEDOUT;
#else
                    timeout = errno == EAGAIN;
#endif
                    if (timeout) {
                        //                                printf("protocol1: receiver_thread: recvfrom socket failed: %s\n", "Radio not sending data\n");
                    }
                    else {
                        if (errno != EINTR) {
                            flog::info("protocol1: receiver_thread: recvfrom socket failed: {0}\n", getLastSocketError());
                        }
                    }
                    // running=FALSE;
                    continue;
                }


                if (buffer[0] == 0xEF && buffer[1] == 0xFE) {
                    switch (buffer[2]) {
                    case 1:
                        // get the end point
                        ep = buffer[3] & 0xFF;

                        // get the sequence number
                        //                        sequence = ((buffer[4] & 0xFF) << 24) + ((buffer[5] & 0xFF) << 16) + ((buffer[6] & 0xFF) << 8) + (buffer[7] & 0xFF);

                        switch (ep) {
                        case 6: // EP6
                            // process the data
                            process_ozy_input_buffer(&buffer[8]);
                            process_ozy_input_buffer(&buffer[520]);
                            //                                            full_tx_buffer(radio->transmitter);
                            break;
                        default:
                            fprintf(stderr, "unexpected EP %d length=%d\n", ep, bytes_read);
                            break;
                        }
                        break;
                    case 2: // response to a discovery packet
                        fprintf(stderr, "unexepected discovery response when not in discovery mode\n");
                        break;
                    default:
                        fprintf(stderr, "unexpected packet type: 0x%02X\n", buffer[2]);
                        break;
                    }
                }
                else {
                    fprintf(stderr, "received bad header bytes on data port %02X,%02X\n", buffer[0], buffer[1]);
                }
            }

            fprintf(stderr, "hl2: protocol1: receive_thread exited\n");
            return 0;
        });
    }


    std::shared_ptr<std::thread> receiveThread;
    std::shared_ptr<std::thread> sendThread;
//    std::shared_ptr<std::thread> sendThread2;
    dsp::queue<unsigned char> udpStream;
    long long int txFrequency = 0;
};
