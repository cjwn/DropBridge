#ifndef CC_H
#define CC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_RAND() ((u32_t)rand())

/* MSVC has no GCC-style packed attribute; use #pragma pack around lwIP wire
 * structs. Without this, init.c aborts with "Struct packing not implemented". */
#define PACK_STRUCT_BEGIN __pragma(pack(push, 1))
#define PACK_STRUCT_END __pragma(pack(pop))
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_FIELD(x) x

#endif /* CC_H */
