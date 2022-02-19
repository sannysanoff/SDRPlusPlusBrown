#pragma once

#include "protocol1_discovery.h"
#include "discovered.h"
#include "plugin_main.h"

#define DATA_PORT 1024

struct ControlData {
    unsigned char C1;
    unsigned char C2;
    unsigned char C3;
    unsigned char C4;
};

#ifdef __linux__
#include <sys/prctl.h>
#endif
static void SetThreadName( const char* threadName)
{
#ifdef __linux__
    prctl(PR_SET_NAME,threadName,0,0,0);
#endif
}
static std::string GetThreadName( ) {
#ifdef __linux__
    char thread_name_buffer[100] = { 0 };
    prctl(PR_GET_NAME,thread_name_buffer,0,0,0);
    return std::string(thread_name_buffer);
#endif
    return "??";
}



struct HL2Device {

    DISCOVERED _discovered;
    DISCOVERED *discovered;

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

    ControlData deviceControl[50] = {0};

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
    bool running;
    int state = SYNC_0;
    unsigned char control_in[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

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
    bool IO1;
    bool IO2;
    bool IO3;
    int AIN3;
    int AIN4;
    int AIN6;
    int exciter_power;
    int alex_forward_power;
    int alex_reverse_power;
    double temperature;

    unsigned char output_buffer[1024];
    int output_buffer_index = 8;
    int tx_output_buffer_index = 8;
    long send_sequence = -1;
    int metis_offset = 8;
    int current_rx = 0;
    int command = 1;

#define LT2208_GAIN_ON            0x04
#define LT2208_DITHER_OFF         0x00
#define LT2208_DITHER_ON          0x08
#define LT2208_RANDOM_OFF         0x00
#define LT2208_RANDOM_ON          0x10


    int rx_sample_rate = 384000;

    const std::function<void(double, double)> handler;

    HL2Device(DISCOVERED _discovered, const std::function<void(double, double)> &handler) : _discovered(_discovered), handler(handler) {
        discovered = &this->_discovered;
        setADCGain(0);
        setFrequency(7000000);
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

    void setFrequency(long long frequency) {
        deviceControl[0x01].C1 = frequency >> 24;
        deviceControl[0x01].C2 = frequency >> 16;
        deviceControl[0x01].C3 = frequency >> 8;
        deviceControl[0x01].C4 = frequency >> 0;
    }

    void setTxFrequency(long long frequency) {
        deviceControl[0x01].C1 = frequency >> 24;
        deviceControl[0x01].C2 = frequency >> 16;
        deviceControl[0x01].C3 = frequency >> 8;
        deviceControl[0x01].C4 = frequency >> 0;
    }

    void setTune(bool tune) {
        if (tune) {
            deviceControl[0x0].C2 |= 0x10;
        } else {
            deviceControl[0x0].C2 &= ~0x10;
        }
    }

    bool transmitMode = false;

    void doTuneActive(bool tune) {
        setTune(tune);
        transmitMode = tune;
        prepareRequest(1);
        sendToEndpoint(0x2, output_buffer);
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

    void setDuplex(bool on) {
        deviceControl[0x0].C4 &= ~0x04;
        if (on) {
            deviceControl[0x0].C4 = 0x04 /* duplex on */;
        }
    }

    void prepareRequest(int secondControlIndex) {
        switch(secondControlIndex) {
            case 0xA:       // rx gain / LNA
            case 1:         // rx freq
                break;
            default:
                secondControlIndex = 2; // something safe ;)
        }
        memset(output_buffer, 0, sizeof(output_buffer));
        output_buffer[SYNC0] = SYNC;
        output_buffer[SYNC1] = SYNC;
        output_buffer[SYNC2] = SYNC;
        output_buffer[C0] = 0x00 | (transmitMode ? 1: 0);
        output_buffer[C1] = deviceControl[0].C1;
        output_buffer[C2] = deviceControl[0].C2;
        output_buffer[C3] = deviceControl[0].C3;
        output_buffer[C4] = deviceControl[0].C4;
        output_buffer[512+SYNC0] = SYNC;
        output_buffer[512+SYNC1] = SYNC;
        output_buffer[512+SYNC2] = SYNC;
        output_buffer[512+C0] = (secondControlIndex << 1)| (transmitMode ? 1: 0);
        output_buffer[512+C1] = deviceControl[secondControlIndex].C1;
        output_buffer[512+C2] = deviceControl[secondControlIndex].C2;
        output_buffer[512+C3] = deviceControl[secondControlIndex].C3;
        output_buffer[512+C4] = deviceControl[secondControlIndex].C4;
    }

    void sendToEndpoint(int endpoint, unsigned char *buffer) {
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
        metis_buffer[7] = (send_sequence) & 0xFF;
        for (int i = 0; i < 1024; i++) {
            metis_buffer[i + 8] = buffer[i];
        }

        if (sendto(data_socket, (const char *)metis_buffer, 1024+8, 0, (struct sockaddr *) &data_addr, data_addr_length) != 1024+8) {
            perror("sendto socket failed for metis_send_data\n");
        }

//        char pbuf[50000];
//        int ix = 0;
//        for (int i = 0; i < 1024 + 8; ++i) {
//            if (!(i % 128) && i) {
//                sprintf(pbuf + ix, "\n");
//                ix += 1;
//            }
//            sprintf(pbuf + ix, "%02x ", metis_buffer[i]);
//            ix += 3;
//        }
//        static int cnt = 0;
//        cnt++;
//        printf("---------- begin %d ----------------\n%s\n--------------------end--------\n", cnt, pbuf);

    };

    void add_iq_samples(int receiverNo, double i_sample, double q_sample) {
        handler(i_sample, q_sample);
//        rx->iq_input_buffer[rx->samples*2]=i_sample;
//        rx->iq_input_buffer[(rx->samples*2)+1]=q_sample;
//        rx->samples=rx->samples+1;
//        if(rx->samples>=rx->buffer_size) {
//            full_rx_buffer(rx);
//            rx->samples=0;
//        }
//
//        if(rx->bpsk_enable && rx->bpsk!=NULL) {
//            bpsk_add_iq_samples(rx->bpsk,i_sample,q_sample);
//        }
    }

    void process_control_bytes() {
//        gboolean previous_ptt;
//        // Unused - commented in case used in future
//        //gboolean previous_dot;
//        //gboolean previous_dash;
//
//        gint tx_mode=USB;
//
//        RECEIVER *tx_receiver=radio->transmitter->rx;
//        if(tx_receiver!=NULL) {
//#ifdef USE_VFO_B_MODE_AND_FILTER
//            if(radio->transmitter->rx->split) {
//      tx_mode=tx_receiver->mode_b;
//    } else {
//#endif
//            tx_mode=tx_receiver->mode_a;
//#ifdef USE_VFO_B_MODE_AND_FILTER
//            }
//#endif
//        }
//
//        previous_ptt=radio->local_ptt;
//        //previous_dot=radio->dot;
//        //previous_dash=radio->dash;
//        radio->ptt=(control_in[0]&0x01)==0x01;
//        //radio->dash=(control_in[0]&0x02)==0x02;
//        //radio->dot=(control_in[0]&0x04)==0x04;
//
//        radio->local_ptt=radio->ptt;
//        if(tx_mode==CWL || tx_mode==CWU) {
//            radio->local_ptt=radio->ptt|radio->dot|radio->dash;
//        }
//        if(previous_ptt!=radio->local_ptt) {
//            g_print("process_control_bytes: ppt=%d dot=%d dash=%d\n",radio->ptt,radio->dot,radio->dash);
//            g_idle_add(ext_ptt_changed,(gpointer)radio);
//        }



        switch ((control_in[0] >> 3) & 0x1F) {
            case 0:
                adc_overload = (control_in[1] & 0x01) == 0x01;
                IO1 = (control_in[1] & 0x02) == 0x02;
                IO2 = (control_in[1] & 0x04) == 0x04;
                IO3 = (control_in[1] & 0x08) == 0x08;


//                //HL2 Buffer over/underflow
//#ifdef CWDAEMON
//                if ((radio->ptt) || keytx) {
//          int recov = (control_in[3]&0x40) == 0x40;
//          int msb = (control_in[3]&0x80) == 0x80;
//	  /*
//          if (msb == 1) {
//            g_print("Buffer recovery %d %d\n", recov, msb);
//          }
//	  */
//      }
//#endif
//                //}
//
//                if(radio->mercury_software_version!=control_in[2]) {
//                    radio->mercury_software_version=control_in[2];
//                    fprintf(stderr,"  Mercury Software version: %d (0x%0X)\n",radio->mercury_software_version,radio->mercury_software_version);
//                }
//                if(radio->penelope_software_version!=control_in[3]) {
//                    radio->penelope_software_version=control_in[3];
//
//
//                    if(radio->discovered->device!=DEVICE_HERMES_LITE2) {
//                        fprintf(stderr,"  Penelope Software version: %d (0x%0X)\n",radio->penelope_software_version,radio->penelope_software_version);
//                    }
//                }
//                if(radio->ozy_software_version!=control_in[4]) {
//                    radio->ozy_software_version=control_in[4];
//                    fprintf(stderr,"FPGA firmware version: %d.%d\n",radio->ozy_software_version/10,radio->ozy_software_version%10);
//                }
                break;
            case 1: {
                exciter_power = ((control_in[1] & 0xFF) << 8) | (control_in[2] & 0xFF); // from Penelope or Hermes

                int adc = ((control_in[1] & 0xFF) << 8) | (control_in[2] & 0xFF);

                double this_temperature = (3.26 * ((double) adc / 4096.0) - 0.5) / 0.01;
                // Exponential moving average filter
                double alpha = 0.7;
                temperature = (alpha * this_temperature) + (1 - alpha) * temperature;


                alex_forward_power = ((control_in[3] & 0xFF) << 8) | (control_in[4] & 0xFF); // from Alex or Apollo
                break;
            }
            case 2:
                alex_reverse_power = ((control_in[1] & 0xFF) << 8) | (control_in[2] & 0xFF); // from Alex or Apollo
                AIN3 = (control_in[3] << 8) + control_in[4]; // from Pennelope or Hermes
                break;
            case 3:
                AIN4 = (control_in[1] << 8) + control_in[2]; // from Pennelope or Hermes
                AIN6 = (control_in[3] << 8) + control_in[4]; // from Pennelope or Hermes
                break;
        }
    }


    void process_ozy_input_buffer(unsigned char *buffer) {
        int i;
        for (i = 0; i < 512; i++) {
            process_ozy_byte(buffer[i] & 0xFF);
        }
    }

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
                left_sample = (int) ((signed char) b << 16);
                state++;
                break;
            case LEFT_SAMPLE_MID:
                left_sample |= (int) ((((unsigned char) b) << 8) & 0xFF00);
                state++;
                break;
            case LEFT_SAMPLE_LOW:
                left_sample |= (int) ((unsigned char) b & 0xFF);
                left_sample_double = (double) left_sample / 8388607.0; // 24 bit sample 2^23-1
                state++;
                break;
            case RIGHT_SAMPLE_HI:
                right_sample = (int) ((signed char) b << 16);
                state++;
                break;
            case RIGHT_SAMPLE_MID:
                right_sample |= (int) ((((unsigned char) b) << 8) & 0xFF00);
                state++;
                break;
            case RIGHT_SAMPLE_LOW: {
                right_sample |= (int) ((unsigned char) b & 0xFF);
                right_sample_double = (double) right_sample / 8388607.0; // 24 bit sample 2^23-1
                //find receiver
//                i=-1;
//                for(j=0;j<discovered->supported_receivers;j++) {
//                    if(radio->receiver[j]!=NULL) {
//                        i++;
//                        if(i==nreceiver) break;
//                    }
//                }
//                if(radio->receiver[j]!=NULL) {
                add_iq_samples(nreceiver, left_sample_double, right_sample_double);
                static int q = 0;
                q++;
                if (q % 10000 == 0) {
                    prepareRequest(secondControlIndex++);
                    if (secondControlIndex == 32) {
                        secondControlIndex = 1;
                    }
                    sendToEndpoint(0x2, output_buffer);
                }
//                }
                nreceiver++;
                if (nreceiver == receivers) {
                    state++;
                } else {
                    state = LEFT_SAMPLE_HI;
                }
                break;
            }
            case MIC_SAMPLE_HI:
                mic_sample = (short) (b << 8);
                state++;
                break;
            case MIC_SAMPLE_LOW:
                mic_sample |= (short) (b & 0xFF);
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
                } else {
                    nreceiver = 0;
                    state = LEFT_SAMPLE_HI;
                }
                break;
        }
    }


    void start() {
        try {
            start_protocol1_thread();
            metis_restart();
        } catch (std::exception &ex) {
            spdlog::error(ex.what());
        }

    }

    void stop() {
        running = false;
        receiveThread->join();
        metis_start_stop(0);
#ifdef WIN32
        closesocket(data_socket);
#else
        close(data_socket);
#endif
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
        if(radio->discovered->device!=DEVICE_OZY) {
#endif

        buffer[0] = 0xEF;
        buffer[1] = 0xFE;
        buffer[2] = 0x04;    // start/stop command
        buffer[3] = command;    // send EP6 and EP4 data (0x00=stop)

        for (i = 0; i < 60; i++) {
            buffer[i + 4] = 0x00;
        }

        metis_send_buffer(buffer, sizeof(buffer));
#ifdef USBOZY
        }
#endif
    }

    void metis_send_buffer(unsigned char *buffer, int length) {
        if (sendto(data_socket, (const char *)buffer, length, 0, (struct sockaddr *) &data_addr, data_addr_length) != length) {
            perror("sendto socket failed for metis_send_data\n");
        }
    }


    void start_protocol1_thread() {

        switch (discovered->device) {
            default:
                data_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (data_socket < 0) {
                    throw std::runtime_error("protocol1: create socket failed for data_socket\n");
                }

                int optval = 1;
                if (setsockopt(data_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval)) < 0) {
                    throw std::runtime_error("data_socket: SO_REUSEADDR");
                }
#ifndef WIN32
                if (setsockopt(data_socket, SOL_SOCKET, SO_REUSEPORT, (const char *)&optval, sizeof(optval)) < 0) {
                    throw std::runtime_error("data_socket: SO_REUSEPORT");
                }
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100000;
                setsockopt(data_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
#else
                DWORD msec = 100;
                setsockopt(data_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&msec,sizeof(msec));
#endif

                // bind to the interface
                if (bind(data_socket, (struct sockaddr *) &discovered->info.network.interface_address, discovered->info.network.interface_length) < 0) {
                    auto what = std::string("protocol1: bind socket failed for data_socket: errno=") + std::to_string(errno);
                    throw std::runtime_error(what.c_str());
                    exit(-1);
                }

                memcpy(&data_addr, &discovered->info.network.address, discovered->info.network.address_length);
                data_addr_length = discovered->info.network.address_length;
                data_addr.sin_port = htons(DATA_PORT);
                break;
        }

        receiveThread = std::make_shared<std::thread>([&] {
            struct sockaddr_in addr;
            socklen_t length;
            unsigned char buffer[2048];
            int bytes_read;
            int ep;
            long sequence;

            SetThreadName("hl2_receive_thread");

            fprintf(stderr, "hl2: protocol1: receive_thread started\n");
            running = true;

            length = sizeof(addr);
            while (running) {

                switch (discovered->device) {

                    default:
                        bytes_read = recvfrom(data_socket, (char *)buffer, sizeof(buffer), 0, (struct sockaddr *) &addr, &length);
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
                            } else {
                                spdlog::info("protocol1: receiver_thread: recvfrom socket failed: {0}\n", getLastSocketError());
                            }
                            //running=FALSE;
                            continue;
                        }

                        if (buffer[0] == 0xEF && buffer[1] == 0xFE) {
                            switch (buffer[2]) {
                                case 1:
                                    // get the end point
                                    ep = buffer[3] & 0xFF;

                                    // get the sequence number
                                    sequence = ((buffer[4] & 0xFF) << 24) + ((buffer[5] & 0xFF) << 16) + ((buffer[6] & 0xFF) << 8) + (buffer[7] & 0xFF);

                                    switch (ep) {
                                        case 6: // EP6
                                            // process the data
                                            process_ozy_input_buffer(&buffer[8]);
                                            process_ozy_input_buffer(&buffer[520]);
//                                            full_tx_buffer(radio->transmitter);
                                            break;
                                        case 4: // EP4
//                                            ep4_sequence++;
//                                            if(sequence!=ep4_sequence) {
//                                                ep4_sequence=sequence;
//                                            } else {
                                            //int seq=(int)(sequence%32L);
//                                                if((sequence%32L)==0L) {
//                                                    reset_wideband_buffer_index(radio->wideband);
//                                                }
//                                                process_wideband_buffer(&buffer[8]);
//                                                process_wideband_buffer(&buffer[520]);
//                                            }
                                            break;
                                        default:
                                            fprintf(stderr, "unexpected EP %d length=%d\n", ep, bytes_read);
                                            break;
                                    }
                                    break;
                                case 2:  // response to a discovery packet
                                    fprintf(stderr, "unexepected discovery response when not in discovery mode\n");
                                    break;
                                default:
                                    fprintf(stderr, "unexpected packet type: 0x%02X\n", buffer[2]);
                                    break;
                            }
                        } else {
                            fprintf(stderr, "received bad header bytes on data port %02X,%02X\n", buffer[0], buffer[1]);
                        }
                        break;
                }

            }

            fprintf(stderr, "hl2: protocol1: receive_thread exited\n");
            return 0;
        });

    }


    std::shared_ptr<std::thread> receiveThread;
};



