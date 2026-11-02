/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_OPTIONAL_STORAGE_H
#define BB_OPTIONAL_STORAGE_H
/* Private composition recipe only. Known optional zero-initialized state is
 * collected inside the usual startup-cleared BSS range, without KEEP. Real
 * references retain it normally when a supported profile actually uses it.
 * Other recipes retain their original storage/layout behavior. Mach-O and
 * WASM host tests use ordinary globals; this is target linker placement. */
#if defined(BB_TRIM_INACTIVE_STATE) && BB_TRIM_INACTIVE_STATE && !defined(__APPLE__) && !defined(__wasm__)
#define BB_OPTIONAL_BSS(name) __attribute__((section(".bb_optional_bss." name)))
#else
#define BB_OPTIONAL_BSS(name)
#endif
#endif
