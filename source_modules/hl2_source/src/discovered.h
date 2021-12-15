/* Copyright (C)
* 2018 - John Melton, G0ORX/N6LYT
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

#ifndef DISCOVERED_H
#define DISCOVERED_H

#ifndef WIN32
#include <netinet/in.h>
#include <arpa/inet.h>
#define SOCKET int
#else


#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
typedef int socklen_t;
inline void usleep(int micros) {
    Sleep(micros / 1000);
}

#endif

#ifdef SOAPYSDR
#include <SoapySDR/Device.h>
#endif

#define MAX_DEVICES 16

#define OLD_DEVICE_METIS 0
#define OLD_DEVICE_HERMES 1
#define OLD_DEVICE_GRIFFIN 2
#define OLD_DEVICE_ANGELIA 4
#define OLD_DEVICE_ORION 5
#define OLD_DEVICE_HERMES_LITE 6
#define OLD_DEVICE_ORION2 10 

#define NEW_DEVICE_ATLAS 0
#define NEW_DEVICE_HERMES 1
#define NEW_DEVICE_HERMES2 2
#define NEW_DEVICE_ANGELIA 3
#define NEW_DEVICE_ORION 4
#define NEW_DEVICE_ORION2 5
#define NEW_DEVICE_HERMES_LITE 6


enum {
  DEVICE_UNKNOWN=-1
  ,DEVICE_METIS=0
  ,DEVICE_HERMES
  ,DEVICE_HERMES2
  ,DEVICE_ANGELIA
  ,DEVICE_ORION
  ,DEVICE_ORION2
  ,DEVICE_HERMES_LITE
  ,DEVICE_HERMES_LITE2
#ifdef SOAPYSDR
  ,DEVICE_SOAPYSDR
#endif
};

#define STATE_AVAILABLE 2
#define STATE_SENDING 3

#define PROTOCOL_1 0
#define PROTOCOL_2 1
#ifdef SOAPYSDR
#define PROTOCOL_SOAPYSDR 2
#endif

struct _DISCOVERED {
    int protocol;
    int device;
    char name[64];
    char software_version[128];
    int status;
    int supported_receivers;
    int supported_transmitters;
    int adcs;
    double frequency_min;
    double frequency_max;
    union {
      struct network {
        unsigned char mac_address[6];
        int address_length;
        struct sockaddr_in address;
        int interface_length;
        struct sockaddr_in interface_address;
        struct sockaddr_in interface_netmask;
        char interface_name[64];
      } network;
#ifdef SOAPYSDR
      struct soapy {
        int rtlsdr_count;
        int sdrplay_count;
        int sample_rate;
        size_t rx_channels;
        size_t rx_gains;
        char **rx_gain;
        SoapySDRRange *rx_range;
        gboolean rx_has_automatic_gain;
        gboolean rx_has_automatic_dc_offset_correction;
        size_t rx_antennas;
        char **rx_antenna;
        size_t tx_channels;
        size_t tx_gains;
        char **tx_gain;
        SoapySDRRange *tx_range;
        size_t tx_antennas;
        char **tx_antenna;
        size_t sensors;
        char **sensor;
        gboolean has_temp;
        char address[32];
      } soapy;
#endif
    } info;
};

typedef struct _DISCOVERED DISCOVERED;

extern int devices;
extern DISCOVERED discovered[MAX_DEVICES];

#endif
