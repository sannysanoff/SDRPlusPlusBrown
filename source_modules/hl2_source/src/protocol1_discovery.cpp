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
#include <hl2_device.h>

#ifndef WIN32
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>
#else
#include <iphlpapi.h>
struct ifaddrs {
    struct ifaddrs *ifa_next;        /* Pointer to the next structure.  */

    char ifa_name[1000];                /* Name of this network interface.  */
    unsigned int ifa_flags;        /* Flags as from SIOCGIFFLAGS ioctl.  */

    struct sockaddr *ifa_addr;        /* Network address of this interface.  */
    struct sockaddr *ifa_netmask; /* Netmask of this interface.  */
    struct sockaddr _ifa_netmask; /* Netmask of this interface.  */
    union
    {
        /* At most one of the following two is valid.  If the IFF_BROADCAST
           bit is set in `ifa_flags', then `ifa_broadaddr' is valid.  If the
           IFF_POINTOPOINT bit is set, then `ifa_dstaddr' is valid.
           It is never the case that both these bits are set at once.  */
        struct sockaddr *ifu_broadaddr; /* Broadcast address of this interface. */
        struct sockaddr *ifu_dstaddr; /* Point-to-point destination address.  */
    } ifa_ifu;
    /* These very same macros are defined by <net/if.h> for `struct ifaddr'.
       So if they are defined already, the existing definitions will be fine.  */
# ifndef ifa_broadaddr
#  define ifa_broadaddr        ifa_ifu.ifu_broadaddr
# endif
# ifndef ifa_dstaddr
#  define ifa_dstaddr        ifa_ifu.ifu_dstaddr
# endif

    void *ifa_data;
    IP_ADAPTER_ADDRESSES *iaa = nullptr;
};
#endif
#include <string.h>
#include <errno.h>

#include "discovered.h"
#include "protocol1_discovery.h"

#include <thread>
#include <vector>
#include <mutex>
#include <utils/flog.h>

#include "utils/net.h"

static std::mutex discoveredLock;

std::string getLastSocketError() {
    std::string errtxt;
#ifdef WIN32
    wchar_t *s = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, WSAGetLastError(),
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPWSTR)&s, 0, NULL);
    char errbuf[300];
    wcstombs ( errbuf, s, sizeof(errbuf) );
    errtxt = errbuf;
    LocalFree(s);
#else
    errtxt = strerror(errno);
#endif
    return errtxt;
}

int getLastSocketErrorNo() {
    std::string errtxt;
#ifdef WIN32
    auto en = WSAGetLastError();
    if (en == WSAEINTR) {
        return EINTR;
    }
    if (en == WSAEWOULDBLOCK) {
        return EAGAIN;
    }
    return en;
#else
    return errno;
#endif
}

static void discover(struct ifaddrs* iface, const struct sockaddr_in *fixed, bool scanIP, bool fastScan, std::atomic<bool> *stopDiscovery) {
    int rc;
    struct sockaddr_in *sa;
    struct sockaddr_in *mask;

    char interface_name[64];
    struct sockaddr_in interface_addr = {0};
    struct sockaddr_in interface_netmask = {0};


    if (stopDiscovery->load()) {
        return;
    }

#define DISCOVERY_PORT 1024
    int discovery_socket;
    if (iface) {
        strcpy(interface_name, iface->ifa_name);
    } else {
        strcpy(interface_name, "fixed_addr");
    }
//    flog::info("discover: looking for HPSDR devices on {0}\n", interface_name);

    // send a broadcast to locate hpsdr boards on the network
    discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (discovery_socket < 0) {
        flog::error("discover: create socket failed for discovery_socket: for {0}", interface_name);
        return;
    }

    int optval = 1;
    setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR, (const char *) &optval, sizeof(optval));

    if (iface) {
        sa = (struct sockaddr_in *) iface->ifa_addr;
        mask = (struct sockaddr_in *) iface->ifa_netmask;
        interface_netmask.sin_addr.s_addr = mask->sin_addr.s_addr;

        // bind to this interface and the discovery port
        // interface_addr.sin_family = AF_INET;
        interface_addr.sin_family = iface->ifa_addr->sa_family;
        interface_addr.sin_addr.s_addr = sa->sin_addr.s_addr;
        // interface_addr.sin_port = htons(DISCOVERY_PORT*2);
        interface_addr.sin_port = htons(0); // system assigned port
        if (bind(discovery_socket, (struct sockaddr *) &interface_addr, sizeof(interface_addr)) < 0) {
            flog::error("discover: bind socket failed for discovery_socket, for {0}", interface_name);
            return;
        }
    }

    // allow broadcast on the socket
    if (!fixed) {
        int on = 1;
        rc = setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, (const char *) &on, sizeof(on));
        if (rc != 0) {
            flog::error("discover: cannot set SO_BROADCAST: rc={0}, for {1}", rc, interface_name);
            return;
        }
    }

    // setup to address
    struct sockaddr_in to_addr = {0};
    //to_addr.sin_family=AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);
    if (fixed) {
        to_addr.sin_family = AF_INET;
        to_addr.sin_addr = fixed->sin_addr;
        flog::info("HL2/HPSDR discover: fixed, sending to: ip={}:{}",
                   std::string(inet_ntoa(to_addr.sin_addr)).c_str(), DISCOVERY_PORT);
    } else {
        to_addr.sin_family = iface->ifa_addr->sa_family;
        to_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
#ifdef __linux__
        {
            int family;
            struct ifreq ifreq;
            char host[128];
            memset(&ifreq, 0, sizeof ifreq);
            strncpy(ifreq.ifr_name, interface_name, IFNAMSIZ);

            if(ioctl(discovery_socket, SIOCGIFBRDADDR, &ifreq) != 0)
            {
                fprintf(stderr, "Could not find interface named %s", interface_name);
            } else {
                getnameinfo(&ifreq.ifr_broadaddr, sizeof(ifreq.ifr_broadaddr), host, sizeof(host), 0, 0, NI_NUMERICHOST);
                auto z = ifreq.ifr_broadaddr;
                to_addr.sin_addr.s_addr = ((const sockaddr_in *)(&z))->sin_addr.s_addr;
                flog::info("Interface: %s  Broadcast: %s\n", interface_name, host);
            }
        }
#endif
        flog::info("HL2/HPSDR discover: bound to {} ip={}, sending to: ip={}:{}",
                   interface_name,
                   std::string(inet_ntoa(interface_addr.sin_addr)).c_str(),
                   std::string(inet_ntoa(to_addr.sin_addr)).c_str(), DISCOVERY_PORT);
    }


    // start a receive thread to collect discovery response packets
    std::thread receiver([&] {
        struct sockaddr_in addr;
        socklen_t len;
        unsigned char buffer[2048];
        int bytes_read;
        int i;
        int version;

//        printf("discover_receive_thread\n");

#ifdef WIN32
        DWORD msec = 2000;
        setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&msec,sizeof(msec));
#else
        //flog::info("thread started: recv on HPSDR device: {0}\n", interface_name);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 30000;
        version = 0;
        setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(struct timeval));
#endif
        int localDevicesFound = 0;
        len = sizeof(addr);
        int retryCount = 0;
        auto startTime = currentTimeMillis();
        while (1) {
            if (stopDiscovery->load()) {
                break;
            }
            bytes_read = recvfrom(discovery_socket, (char *) buffer, sizeof(buffer), 0, (struct sockaddr *) &addr,
                                  &len);
            if (bytes_read < 0) {
                auto en = getLastSocketErrorNo();
                if (en == EINTR || en == EAGAIN) {
                    if (currentTimeMillis() - startTime < 2000) {
                        usleep(10000);
                        retryCount++;
                        continue;
                    }
                }
                std::string errtxt = getLastSocketError();
                flog::error(
                        "discovery: recvfrom socket failed after {} retries for discover_receive_thread on {0}: {1}",
                        retryCount, interface_name, errtxt);
                break;
            }
            flog::info("HL2/HPSDR: discovered: received {0} bytes from {} on {} ",
                       bytes_read, std::string(inet_ntoa(addr.sin_addr)).c_str(), interface_name);
            if ((buffer[0] & 0xFF) == 0xEF && (buffer[1] & 0xFF) == 0xFE) {
                int status = buffer[2] & 0xFF;
                if (status == 2 || status == 3 || status == 28) { // 28 == hl_proxy, extended command set
                    bool isHL2Proxy = status == 28;
                    discoveredLock.lock();
                    if (devices < MAX_DEVICES) {
                        discovered[devices].protocol = PROTOCOL_1;
                        discovered[devices].hl2_protocol = false;
                        version = buffer[9] & 0xFF;
                        snprintf(discovered[devices].software_version, sizeof discovered[devices].software_version,
                                 "%d", version);
                        switch (buffer[10] & 0xFF) {
                            case OLD_DEVICE_METIS:
                                discovered[devices].device = DEVICE_METIS;
                                strcpy(discovered[devices].name, "Metis");
                                discovered[devices].supported_receivers = 5;
                                discovered[devices].supported_transmitters = 1;
                                discovered[devices].adcs = 1;
                                discovered[devices].frequency_min = 0.0;
                                discovered[devices].frequency_max = 61440000.0;
                                break;
                            case OLD_DEVICE_HERMES:
                                discovered[devices].device = DEVICE_HERMES;
                                strcpy(discovered[devices].name, "Hermes");
                                discovered[devices].supported_receivers = 5;
                                discovered[devices].supported_transmitters = 1;
                                discovered[devices].adcs = 1;
                                discovered[devices].frequency_min = 0.0;
                                discovered[devices].frequency_max = 61440000.0;
                                break;
                            case OLD_DEVICE_ANGELIA:
                                discovered[devices].device = DEVICE_ANGELIA;
                                strcpy(discovered[devices].name, "Angelia");
                                discovered[devices].supported_receivers = 7;
                                discovered[devices].supported_transmitters = 1;
                                discovered[devices].adcs = 2;
                                discovered[devices].frequency_min = 0.0;
                                discovered[devices].frequency_max = 61440000.0;
                                break;
                            case OLD_DEVICE_ORION:
                                discovered[devices].device = DEVICE_ORION;
                                strcpy(discovered[devices].name, "Orion");
                                discovered[devices].supported_receivers = 7;
                                discovered[devices].supported_transmitters = 1;
                                discovered[devices].adcs = 2;
                                discovered[devices].frequency_min = 0.0;
                                discovered[devices].frequency_max = 61440000.0;
                                break;
                            case OLD_DEVICE_HERMES_LITE:
                                discovered[devices].device = DEVICE_HERMES_LITE;
                                if (version < 42) {
                                    strcpy(discovered[devices].name, "Hermes Lite V1");
                                    discovered[devices].supported_receivers = 2;
                                } else {
                                    strcpy(discovered[devices].name, "Hermes Lite V2");
                                    discovered[devices].device = DEVICE_HERMES_LITE2;
                                    // HL2 send max supported receveirs in discovery response.
                                    discovered[devices].supported_receivers = buffer[0x13];
                                }
                                discovered[devices].supported_transmitters = 1;
                                discovered[devices].adcs = 1;
                                discovered[devices].frequency_min = 0.0;
                                discovered[devices].frequency_max = 30720000.0;
                                break;
                            case OLD_DEVICE_ORION2:
                                discovered[devices].device = DEVICE_ORION2;
                                strcpy(discovered[devices].name, "Orion 2");
                                discovered[devices].supported_receivers = 7;
                                discovered[devices].supported_transmitters = 1;
                                discovered[devices].adcs = 2;
                                discovered[devices].frequency_min = 0.0;
                                discovered[devices].frequency_max = 61440000.0;
                                break;
                            default:
                                discovered[devices].device = DEVICE_UNKNOWN;
                                strcpy(discovered[devices].name, "Unknown");
                                discovered[devices].supported_receivers = 7;
                                discovered[devices].supported_transmitters = 1;
                                discovered[devices].adcs = 1;
                                discovered[devices].frequency_min = 0.0;
                                discovered[devices].frequency_max = 61440000.0;
                                break;
                        }

                        for (i = 0; i < 6; i++) {
                            discovered[devices].info.network.mac_address[i] = buffer[i + 3];
                        }
                        discovered[devices].status = status;
                        memcpy((void *) &discovered[devices].info.network.address, (void *) &addr, sizeof(addr));
                        discovered[devices].info.network.address_length = sizeof(addr);
                        memcpy((void *) &discovered[devices].info.network.interface_address, (void *) &interface_addr,
                               sizeof(interface_addr));
                        memcpy((void *) &discovered[devices].info.network.interface_netmask,
                               (void *) &interface_netmask, sizeof(interface_netmask));
                        discovered[devices].info.network.interface_length = sizeof(interface_addr);
                        strcpy(discovered[devices].info.network.interface_name, interface_name);
                        char buf[10000];
                        if (isHL2Proxy) {
                            for (int q = 0; q < devices; q++) {
                                if (!memcmp(&discovered[q].info.network.address.sin_addr,
                                            &discovered[devices].info.network.address.sin_addr,
                                            sizeof(discovered[q].info.network.address.sin_addr))) {
                                    discovered[q].hl2_protocol = true;
                                }
                            }
                            // just update preceding device
                        } else {
                            bool found = false;
                            for (int q = 0; q < devices; q++) {
                                if (!memcmp(&discovered[q].info.network.address.sin_addr,
                                            &discovered[devices].info.network.address.sin_addr,
                                            sizeof(discovered[q].info.network.address.sin_addr))) {
                                    // already there; dropping duplicates
                                    found = true;
                                }
                            }
                            if (!found) {
                                // new ip:port
                                if (false) {
                                    // dont report here.
                                    snprintf(buf, sizeof buf,
                                             "HL2/HPSDR: discovery: found device=%d software_version=%s status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s",
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
                                    flog::info("{0}", buf);
                                }
                                devices++;
                                localDevicesFound++;
                            }
                        }
                    }
                    if (localDevicesFound > 0 && fastScan) {
                        //flog::info("Fast scan enabled, cancelling remaining discovery.");
                        stopDiscovery->store(true);
                    }
                    discoveredLock.unlock();
                }
            }

        }
//        flog::info("discovery: exiting discover_receive_thread, found {0} devices on {1}",localDevicesFound,interface_name);

    });


    if (!stopDiscovery->load()) {
        // send discovery packet
        unsigned char buffer[63];
        buffer[0] = 0xEF;
        buffer[1] = 0xFE;
        buffer[2] = 0x02;
        int i;
        for (i = 3; i < 63; i++) {
            buffer[i] = 0x00;
        }

        if (!stopDiscovery->load()) {
            if (sendto(discovery_socket, (char *) buffer, 63, 0, (struct sockaddr *) &to_addr, sizeof(to_addr)) < 0) {
                flog::info("WARNING discover: ({}) sendto (broadcast/direct) failed for discovery_socket", interface_name);
            }
            if (scanIP) {
                to_addr = *((sockaddr_in *) iface->ifa_addr);
                to_addr.sin_port = htons(1024);
                unsigned char *ipv4 = (unsigned char *) &to_addr.sin_addr.s_addr;
                //flog::info("Scanning interface {}:  {}.{}.{}.1 - .254..", interface_name, ipv4[0], ipv4[1], ipv4[2]);
                for (int q = 1; q <= 254; q++) {
                    if (stopDiscovery->load()) {
                        break;
                    }
                    ipv4[3] = q;
                    if (sendto(discovery_socket, (char *) buffer, 63, 0, (struct sockaddr *) &to_addr,
                               sizeof(to_addr)) <
                        0) {
                        flog::error("discover: sendto (scan) failed for discovery_socket: {}.{}.{}.{}: errno={}",
                                    ipv4[0],
                                    ipv4[1], ipv4[2], ipv4[3], errno);
                    }
                    usleep(1500);
                }
                //flog::info("Finished scanning {}", interface_name);
            }
        }
    }

    // wait for receive thread to complete
    receiver.join();

#ifdef WIN32
    ::closesocket(discovery_socket);
#else
    ::close(discovery_socket);
#endif

    ///flog::info("discover: exiting discover for {0}",interface_name);

}

#ifdef WIN32
void _cdecl freeifaddrs(struct ifaddrs *dest) {
    if (dest->iaa) {
        free(dest->iaa);
    }
    if (dest->ifa_next) {
        freeifaddrs(dest->ifa_next);
    }
    free(dest);
}

void __cdecl getifaddrs(struct ifaddrs **dest) {
    *dest = new ifaddrs();
    ULONG outBufLen = 100*sizeof(IP_ADAPTER_ADDRESSES);
    (*dest)->iaa = (PIP_ADAPTER_ADDRESSES)malloc(outBufLen);
    auto rv = GetAdaptersAddresses(AF_INET, 0, NULL, (*dest)->iaa, &outBufLen);
    if (rv == ERROR_BUFFER_OVERFLOW) {
        freeifaddrs(*dest);
        *dest = nullptr;
        return;
    }
    auto pCurrAddresses = (*dest)->iaa;
    auto target = *dest;
    while (pCurrAddresses) {
        target->ifa_flags = 0;
        if (pCurrAddresses->FirstUnicastAddress) {
            if (pCurrAddresses->OperStatus == IfOperStatusUp) {
                target->ifa_addr = pCurrAddresses->FirstUnicastAddress->Address.lpSockaddr;
                int plen = pCurrAddresses->FirstUnicastAddress->OnLinkPrefixLength;
                target->_ifa_netmask = *target->ifa_addr;
                target->ifa_netmask = &target->_ifa_netmask;
                sockaddr_in *si = (sockaddr_in *)target->ifa_netmask;
                si->sin_addr.S_un.S_addr = 0xFFFFFFFFFFFFFFFFLL << (32-plen);
                target->ifa_flags |= IFF_UP;
            }
        }
        wcstombs ( target->ifa_name, pCurrAddresses->Description, sizeof(target->ifa_name) );

        pCurrAddresses = pCurrAddresses->Next;
        if (pCurrAddresses) {
            target->ifa_next = new ifaddrs();
            target = target->ifa_next;
        }
    }
}
#endif

void protocol1_discovery(std::string staticIp, bool scanIp, bool fastScan) {
    struct ifaddrs *addrs,*ifa;

    flog::info("HL2/HPSDR: using protocol1_discovery..");
    getifaddrs(&addrs);
    ifa = addrs;
    std::vector<std::shared_ptr<std::thread>> interfaceThreads;
    std::atomic<bool> stopDiscovery(false);
    while (ifa) {
//        g_main_context_iteration(NULL, 0);
        if (ifa->ifa_addr && (ifa->ifa_addr->sa_family == AF_INET
                              #ifndef WIN32
        || ifa->ifa_addr->sa_family==AF_LOCAL
                              #endif
        )) {
            if((ifa->ifa_flags&IFF_UP)==IFF_UP
               #ifndef WIN32
                && (ifa->ifa_flags&IFF_RUNNING)==IFF_RUNNING
               #endif
                /*&& (ifa->ifa_flags&IFF_LOOPBACK)!=IFF_LOOPBACK*/) {
                auto thisif = ifa;
                if (true) {
                    interfaceThreads.emplace_back(std::make_shared<std::thread>([=, &stopDiscovery] {
                        discover(thisif, nullptr, scanIp, fastScan, &stopDiscovery);
                    }));
                }
            }
        }
        ifa = ifa->ifa_next;
    }
    if (!staticIp.empty()) {
        try {
            net::Address addr(staticIp, 1024);
            interfaceThreads.emplace_back(std::make_shared<std::thread>([&,a=addr]{
                discover(nullptr, &a.addr, false, fastScan, &stopDiscovery);
            }));
        } catch (std::exception &e) {
            flog::error("Invalid static IP address: {0}", e.what());
        }
    }

    for(auto t: interfaceThreads) {
        t->join();
    }

    freeifaddrs(addrs);

    flog::info( "HPSDR discovery found {} devices, fastScan={}",devices, (int)fastScan);

    auto q = discovered;

    int i;
    for(i=0;i<devices;i++) {
        char buf[1024];
        snprintf(buf, sizeof buf, "discovery: found device=%d software_version=%s status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s",
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
        flog::info("HL2/HPSDR: {0}", buf);
    }

}

