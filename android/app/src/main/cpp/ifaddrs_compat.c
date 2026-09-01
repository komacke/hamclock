#if defined(__ANDROID__) && __ANDROID_API__ < 24

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ifaddrs.h>

int getifaddrs(struct ifaddrs **ifap) {
    if (!ifap) {
        errno = EINVAL;
        return -1;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    char buf[4096];
    struct ifconf ifc;
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;

    if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    struct ifaddrs *head = NULL;
    int count = ifc.ifc_len / (int)sizeof(struct ifreq);

    for (int i = 0; i < count; i++) {
        struct ifreq *cur_ifr = (struct ifreq *)(buf + i * sizeof(struct ifreq));

        struct ifaddrs *ifa = (struct ifaddrs *)calloc(1, sizeof(struct ifaddrs));
        if (!ifa) {
            continue;
        }

        ifa->ifa_name = strdup(cur_ifr->ifr_name);

        struct ifreq req;

        // Retrieve flags
        memset(&req, 0, sizeof(req));
        strncpy(req.ifr_name, cur_ifr->ifr_name, IFNAMSIZ - 1);
        if (ioctl(fd, SIOCGIFFLAGS, &req) == 0) {
            ifa->ifa_flags = (unsigned int)req.ifr_flags;
        }

        // Retrieve address
        memset(&req, 0, sizeof(req));
        strncpy(req.ifr_name, cur_ifr->ifr_name, IFNAMSIZ - 1);
        if (ioctl(fd, SIOCGIFADDR, &req) == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
            if (sa) {
                memcpy(sa, &req.ifr_addr, sizeof(struct sockaddr_in));
                ifa->ifa_addr = (struct sockaddr *)sa;
            }
        }

        // Retrieve netmask
        memset(&req, 0, sizeof(req));
        strncpy(req.ifr_name, cur_ifr->ifr_name, IFNAMSIZ - 1);
        if (ioctl(fd, SIOCGIFNETMASK, &req) == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
            if (sa) {
                memcpy(sa, &req.ifr_netmask, sizeof(struct sockaddr_in));
                ifa->ifa_netmask = (struct sockaddr *)sa;
            }
        }

        // Retrieve broadcast address if broadcast flag is set
        if (ifa->ifa_flags & IFF_BROADCAST) {
            memset(&req, 0, sizeof(req));
            strncpy(req.ifr_name, cur_ifr->ifr_name, IFNAMSIZ - 1);
            if (ioctl(fd, SIOCGIFBRDADDR, &req) == 0) {
                struct sockaddr_in *sa = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
                if (sa) {
                    memcpy(sa, &req.ifr_broadaddr, sizeof(struct sockaddr_in));
                    ifa->ifa_broadaddr = (struct sockaddr *)sa;
                }
            }
        }

        ifa->ifa_next = head;
        head = ifa;
    }

    close(fd);
    *ifap = head;
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa) {
    while (ifa) {
        struct ifaddrs *next = ifa->ifa_next;
        if (ifa->ifa_name) {
            free(ifa->ifa_name);
        }
        if (ifa->ifa_addr) {
            free(ifa->ifa_addr);
        }
        if (ifa->ifa_netmask) {
            free(ifa->ifa_netmask);
        }
        if (ifa->ifa_broadaddr) {
            free(ifa->ifa_broadaddr);
        }
        free(ifa);
        ifa = next;
    }
}

#endif // defined(__ANDROID__) && __ANDROID_API__ < 24
