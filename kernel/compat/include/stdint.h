/* SPDX-License-Identifier: MIT */
#pragma once
typedef __INT8_TYPE__ int8_t;     typedef __UINT8_TYPE__ uint8_t;
typedef __INT16_TYPE__ int16_t;   typedef __UINT16_TYPE__ uint16_t;
typedef __INT32_TYPE__ int32_t;   typedef __UINT32_TYPE__ uint32_t;
typedef __INT64_TYPE__ int64_t;   typedef __UINT64_TYPE__ uint64_t;
typedef __INTPTR_TYPE__ intptr_t; typedef __UINTPTR_TYPE__ uintptr_t;
#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295U
#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL
