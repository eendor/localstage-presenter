/* Network adapter discovery (external IPv4 interfaces only). */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>

#include "server.h"

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
