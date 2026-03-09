// config.h — Hand-written for CMake build of vendored libsidplayfp 2.16.1.
// Replaces the autoconf-generated config.h.
#ifndef SIDPLAYFP_CONFIG_H
#define SIDPLAYFP_CONFIG_H

// Package info
#define PACKAGE "libsidplayfp"
#define PACKAGE_NAME "libsidplayfp"
#define PACKAGE_VERSION "2.16.1"
#define PACKAGE_STRING "libsidplayfp 2.16.1"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL "https://github.com/libsidplayfp/libsidplayfp"
#define PACKAGE_TARNAME "libsidplayfp"
#define VERSION "2.16.1"

// C++ standard level (we require C++23)
#define HAVE_CXX11 1
#define HAVE_CXX14 1
#define HAVE_CXX17 1
#define HAVE_CXX20 1
#define HAVE_CXX23 1

// Standard headers (always present on modern platforms)
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1

// Platform-specific headers
#ifdef _WIN32
// No unistd.h on Windows
#else
#define HAVE_UNISTD_H 1
#define HAVE_DLFCN_H 1
#endif

// Type sizes
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4

// POSIX functions
#ifndef _WIN32
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#endif

// Endianness — little-endian on x86/x86_64/ARM (common targets).
// Big-endian platforms would need WORDS_BIGENDIAN defined.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WORDS_BIGENDIAN 1
#endif

// We use the internal MD5 implementation, not gcrypt.
// #undef HAVE_LIBGCRYPT

// No hardware SID builders.
// #undef HAVE_EXSID
// #undef HAVE_USBSID

// Shared library extension (not relevant for static build, but some code references it)
#ifdef __APPLE__
#define SHLIBEXT ".dylib"
#elif defined(_WIN32)
#define SHLIBEXT ".dll"
#else
#define SHLIBEXT ".so"
#endif

// Threading (not needed for our use case, but define if available)
#ifndef _WIN32
#define HAVE_PTHREAD 1
#define HAVE_PTHREAD_H 1
#endif

#endif // SIDPLAYFP_CONFIG_H
