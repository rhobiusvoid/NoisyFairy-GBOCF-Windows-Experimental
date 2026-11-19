/* SPDX-License-Identifier: GPL-2.0-or-later
 * Crypto++ calls these two helpers when built with clang on Windows (they are normally supplied by
 * its MASM file x64dll.asm). GNU-style replacements for the MinGW-w64 build. */
#include <cpuid.h>

unsigned long long XGETBV64(unsigned int index) {
    unsigned int lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(index));
    return ((unsigned long long)hi << 32) | lo;
}

unsigned long long CPUID64(unsigned int function, unsigned int subfunction, unsigned int *output) {
    __cpuid_count(function, subfunction, output[0], output[1], output[2], output[3]);
    return 0;
}
