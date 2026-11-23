// Host tests only (test_me.c, test_region.c). run.sh gives test_region.c the Kconfig's origin; CONFIG_YK_REGIONS is left
// out (off) unless a build sets it, as ESP-IDF leaves out a bool that is off.
#pragma once
#ifndef CONFIG_YK_CLOUD_ORIGIN
#define CONFIG_YK_CLOUD_ORIGIN "https://device.test"
#endif
#ifndef CONFIG_YK_CLOUD_HTTP_BUFFER_MIN
#define CONFIG_YK_CLOUD_HTTP_BUFFER_MIN 0
#endif
