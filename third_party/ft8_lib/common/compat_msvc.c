/* SPDX-License-Identifier: MIT */
/* MSVC/Windows CRT 不提供 POSIX stpcpy；ft8_lib 的 message.c 用到它，
 * 这里补一个最小实现，避免改动上游源码。非 MSVC 平台本文件不产生符号。 */
#ifdef _MSC_VER
#include <stddef.h>
#include <string.h>

char* stpcpy(char* dst, const char* src) {
  const size_t n = strlen(src);
  memcpy(dst, src, n + 1);
  return dst + n;
}
#endif
