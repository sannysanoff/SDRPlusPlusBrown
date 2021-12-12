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
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <string.h>
#include <errno.h>

#include "discovered.h"
#include "protocol1_discovery.h"

#include <thread>
#include <vector>
#include <mutex>
#include <unistd.h>

static std::mutex discoveredLock;

static void discover(struct ifaddrs* iface) {
    int rc;
    struct sockaddr_in *sa;
    struct sockaddr_in *mask;

    char interface_name[64];
    struct sockaddr_in interface_addr={0};
    struct sockaddr_in interface_netmask={0};

#define DISCOVERY_PORT 1024
    int discovery_socket;

    strcpy(interface_name,iface->ifa_name);
    fprintf(stdout, "discover: looking for HPSDR devices on %s\n", interface_name);

    // send a broadcast to locate hpsdr boards on the network
    discovery_socket=socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(discovery_socket<0) {
        perror("discover: create socket failed for discovery_socket\n");
        exit(-1);
    }

    int optval = 1;
    setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    sa = (struct sockaddr_in *) iface->ifa_addr;
    mask = (struct sockaddr_in *) iface->ifa_netmask;
    interface_netmask.sin_addr.s_addr = mask->sin_addr.s_addr;

    // bind to this interface and the discovery port
    //interface_addr.sin_family = AF_INET;
    interface_addr.sin_family = iface->ifa_addr->sa_family;
    interface_addr.sin_addr.s_addr = sa->sin_addr.s_addr;
    //interface_addr.sin_port = htons(DISCOVERY_PORT*2);
    interface_addr.sin_port = htons(0); // system assigned port
    if(bind(discovery_socket,(struct sockaddr*)&interface_addr,sizeof(interface_addr))<0) {
        perror("discover: bind socket failed for discovery_socket\n");
        exit(-1);
    }

    printf("discover: bound to %s\n",interface_name);

    // allow broadcast on the socket
    int on=1;
    rc=setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    if(rc != 0) {
        printf("discover: cannot set SO_BROADCAST: rc=%d\n", rc);
        exit(-1);
    }

    // setup to address
    struct sockaddr_in to_addr={0};
    //to_addr.sin_family=AF_INET;
    to_addr.sin_family = iface->ifa_addr->sa_family;
    to_addr.sin_port=htons(DISCOVERY_PORT);
    to_addr.sin_addr.s_addr=htonl(INADDR_BROADCAST);

    // start a receive thread to collect discovery response packets
    std::thread receiver([&] {
        struct sockaddr_in addr;
        socklen_t len;
        unsigned char buffer[2048];
        int bytes_read;
        struct timeval tv;
        int i;
        int version;

        printf("discover_receive_thread\n");

        tv.tv_sec = 2;
        tv.tv_usec = 0;
        version=0;

        setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

        len=sizeof(addr);
        while(1) {
            bytes_read=recvfrom(discovery_socket,buffer,sizeof(buffer),0,(struct sockaddr*)&addr,&len);
            if(bytes_read<0) {
                printf("discovery: bytes read %d\n", bytes_read);
                perror("discovery: recvfrom socket failed for discover_receive_thread");
                break;
            }
            printf("discovered: received %d bytes\n",bytes_read);
            if ((buffer[0] & 0xFF) == 0xEF && (buffer[1] & 0xFF) == 0xFE) {
                int status = buffer[2] & 0xFF;
                if (status == 2 || status == 3) {
                    discoveredLock.lock();
                    if(devices<MAX_DEVICES) {
                        discovered[devices].protocol=PROTOCOL_1;
                        version=buffer[9]&0xFF;
                        sprintf(discovered[devices].software_version,"%d",version);
                        switch(buffer[10]&0xFF) {
                            case OLD_DEVICE_METIS:
                                discovered[devices].device=DEVICE_METIS;
                                strcpy(discovered[devices].name,"Metis");
                                discovered[devices].supported_receivers=5;
                                discovered[devices].supported_transmitters=1;
                                discovered[devices].adcs=1;
                                discovered[devices].frequency_min=0.0;
                                discovered[devices].frequency_max=61440000.0;
                                break;
                            case OLD_DEVICE_HERMES:
                                discovered[devices].device=DEVICE_HERMES;
                                strcpy(discovered[devices].name,"Hermes");
                                discovered[devices].supported_receivers=5;
                                discovered[devices].supported_transmitters=1;
                                discovered[devices].adcs=1;
                                discovered[devices].frequency_min=0.0;
                                discovered[devices].frequency_max=61440000.0;
                                break;
                            case OLD_DEVICE_ANGELIA:
                                discovered[devices].device=DEVICE_ANGELIA;
                                strcpy(discovered[devices].name,"Angelia");
                                discovered[devices].supported_receivers=7;
                                discovered[devices].supported_transmitters=1;
                                discovered[devices].adcs=2;
                                discovered[devices].frequency_min=0.0;
                                discovered[devices].frequency_max=61440000.0;
                                break;
                            case OLD_DEVICE_ORION:
                                discovered[devices].device=DEVICE_ORION;
                                strcpy(discovered[devices].name,"Orion");
                                discovered[devices].supported_receivers=7;
                                discovered[devices].supported_transmitters=1;
                                discovered[devices].adcs=2;
                                discovered[devices].frequency_min=0.0;
                                discovered[devices].frequency_max=61440000.0;
                                break;
                            case OLD_DEVICE_HERMES_LITE:
                                discovered[devices].device=DEVICE_HERMES_LITE;
                                if (version < 42) {
                                    strcpy(discovered[devices].name,"Hermes Lite V1");
                                    discovered[devices].supported_receivers = 2;
                                } else {
                                    strcpy(discovered[devices].name,"Hermes Lite V2");
                                    discovered[devices].device = DEVICE_HERMES_LITE2;
                                    // HL2 send max supported receveirs in discovery response.
                                    discovered[devices].supported_receivers=buffer[0x13];
                                }
                                discovered[devices].supported_transmitters=1;
                                discovered[devices].adcs=1;
                                discovered[devices].frequency_min=0.0;
                                discovered[devices].frequency_max=30720000.0;
                                break;
                            case OLD_DEVICE_ORION2:
                                discovered[devices].device=DEVICE_ORION2;
                                strcpy(discovered[devices].name,"Orion 2");
                                discovered[devices].supported_receivers=7;
                                discovered[devices].supported_transmitters=1;
                                discovered[devices].adcs=2;
                                discovered[devices].frequency_min=0.0;
                                discovered[devices].frequency_max=61440000.0;
                                break;
                            default:
                                discovered[devices].device=DEVICE_UNKNOWN;
                                strcpy(discovered[devices].name,"Unknown");
                                discovered[devices].supported_receivers=7;
                                discovered[devices].supported_transmitters=1;
                                discovered[devices].adcs=1;
                                discovered[devices].frequency_min=0.0;
                                discovered[devices].frequency_max=61440000.0;
                                break;
                        }

                        for(i=0;i<6;i++) {
                            discovered[devices].info.network.mac_address[i]=buffer[i+3];
                        }
                        discovered[devices].status=status;
                        memcpy((void*)&discovered[devices].info.network.address,(void*)&addr,sizeof(addr));
                        discovered[devices].info.network.address_length=sizeof(addr);
                        memcpy((void*)&discovered[devices].info.network.interface_address,(void*)&interface_addr,sizeof(interface_addr));
                        memcpy((void*)&discovered[devices].info.network.interface_netmask,(void*)&interface_netmask,sizeof(interface_netmask));
                        discovered[devices].info.network.interface_length=sizeof(interface_addr);
                        strcpy(discovered[devices].info.network.interface_name,interface_name);
                        printf("discovery: found device=%d software_version=%s status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s\n",
                                discovered[devices].device,
                                discovered[devices].software_version,
                                discovered[devices].status,
                                inet_ntoa(discovered[devices].info.network.address.sin_addr),
                                discovered[devices].info.network.mac_address[0],
                                discovered[devices].info.network.mac_address[1],
                                discovered[devices].info.network.mac_address[2],
                                discovered[devices].info.network.mac_address[3],
                                discovered[devices].info.network.mac_address[4],
                                discovered[devices].info.network.mac_address[5],
                                discovered[devices].info.network.interface_name);
                        devices++;
                    }
                    discoveredLock.unlock();
                }
            }

        }
        printf("discovery: exiting discover_receive_thread\n");
        //g_thread_exit(NULL);
//        return NULL;

    });



    // send discovery packet
    unsigned char buffer[63];
    buffer[0]=0xEF;
    buffer[1]=0xFE;
    buffer[2]=0x02;
    int i;
    for(i=3;i<63;i++) {
        buffer[i]=0x00;
    }

    if(sendto(discovery_socket,buffer,63,0,(struct sockaddr*)&to_addr,sizeof(to_addr))<0) {
        perror("discover: sendto socket failed for discovery_socket\n");
        if(errno!=EHOSTUNREACH && errno!=EADDRNOTAVAIL) {
            exit(-1);
        }
    }

    // wait for receive thread to complete
    receiver.join();

    ::close(discovery_socket);

    printf("discover: exiting discover for %s\n",iface->ifa_name);

}

void protocol1_discovery() {
    struct ifaddrs *addrs,*ifa;

    printf("protocol1_discovery\n");
    getifaddrs(&addrs);
    ifa = addrs;
    std::vector<std::shared_ptr<std::thread>> interfaceThreads;
    while (ifa) {
//        g_main_context_iteration(NULL, 0);
        if (ifa->ifa_addr && (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family==AF_LOCAL)) {
            if((ifa->ifa_flags&IFF_UP)==IFF_UP
                && (ifa->ifa_flags&IFF_RUNNING)==IFF_RUNNING
                /*&& (ifa->ifa_flags&IFF_LOOPBACK)!=IFF_LOOPBACK*/) {
                auto thisif = ifa;
                interfaceThreads.emplace_back(std::make_shared<std::thread>([=]{
                    discover(thisif);
                }));
            }
        }
        ifa = ifa->ifa_next;
    }

    for(auto t: interfaceThreads) {
        t->join();
    }

    freeifaddrs(addrs);

    printf( "discovery found %d devices\n",devices);

    auto q = discovered;

    int i;
    for(i=0;i<devices;i++) {
                    printf("discovery: found device=%d software_version=%s status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s\n",
                            discovered[i].device,
                            discovered[i].software_version,
                            discovered[i].status,
                            inet_ntoa(discovered[i].info.network.address.sin_addr),
                            discovered[i].info.network.mac_address[0],
                            discovered[i].info.network.mac_address[1],
                            discovered[i].info.network.mac_address[2],
                            discovered[i].info.network.mac_address[3],
                            discovered[i].info.network.mac_address[4],
                            discovered[i].info.network.mac_address[5],
                            discovered[i].info.network.interface_name);
    }

}

