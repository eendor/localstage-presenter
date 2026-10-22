/* Network adapter discovery (external IPv4 interfaces only). */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

#ifdef _WIN32
#include <iphlpapi.h>

int net_list(Iface *out, int max) {
    ULONG size = 16384;
    IP_ADAPTER_ADDRESSES *list = NULL;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; tries++) {
        free(list);
        list = malloc(size);
        if (!list) return 0;
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, list, &size);
    }
    int n = 0;
    if (rc == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES *a = list; a && n < max; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u && n < max; u = u->Next) {
                if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
                struct sockaddr_in *sin = (struct sockaddr_in *)u->Address.lpSockaddr;
                inet_ntop(AF_INET, &sin->sin_addr, out[n].address, sizeof out[n].address);
                if (!strncmp(out[n].address, "169.254.", 8)) continue; /* no DHCP lease */
                if (!WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, out[n].name, sizeof out[n].name, NULL, NULL))
                    snprintf(out[n].name, sizeof out[n].name, "%s", a->AdapterName);
                n++;
            }
        }
    }
    free(list);
    return n;
}
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

int net_list(Iface *out, int max) {
    struct ifaddrs *list, *it;
    int n = 0;
    if (getifaddrs(&list) != 0) return 0;
    for (it = list; it && n < max; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        if ((it->ifa_flags & IFF_LOOPBACK) || !(it->ifa_flags & IFF_UP)) continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)it->ifa_addr;
        snprintf(out[n].name, sizeof out[n].name, "%s", it->ifa_name);
        inet_ntop(AF_INET, &sin->sin_addr, out[n].address, sizeof out[n].address);
        n++;
    }
    freeifaddrs(list);
    return n;
}

#endif

static int wireless(const char *name) {
    char low[64];
    size_t i;
    for (i = 0; name[i] && i < sizeof low - 1; i++) low[i] = (char)tolower((unsigned char)name[i]);
    low[i] = 0;
    /* Linux names Wi-Fi adapters wlan0 / wlp2s0; Windows and macOS use descriptive names. */
    return strncmp(low, "wl", 2) == 0 || strstr(low, "wifi") || strstr(low, "wi-fi") || strstr(low, "wireless");
}

int net_default(char *out, size_t size) {
    Iface list[32];
    int n = net_list(list, 32);
    if (n == 0) { out[0] = 0; return 0; }
    for (int i = 0; i < n; i++)
        if (wireless(list[i].name)) { snprintf(out, size, "%s", list[i].address); return 1; }
    snprintf(out, size, "%s", list[0].address);
    return 1;
}

int net_has(const char *address) {
    Iface list[32];
    int n = net_list(list, 32);
    for (int i = 0; i < n; i++) if (strcmp(list[i].address, address) == 0) return 1;
    return 0;
}
