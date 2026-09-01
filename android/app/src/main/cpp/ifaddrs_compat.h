#ifndef IFADDRS_COMPAT_H
#define IFADDRS_COMPAT_H

#if defined(__ANDROID__) && __ANDROID_API__ < 24

#include <ifaddrs.h>

#ifdef __cplusplus
extern "C" {
#endif

int getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#ifdef __cplusplus
}
#endif

#endif // defined(__ANDROID__) && __ANDROID_API__ < 24

#endif // IFADDRS_COMPAT_H
