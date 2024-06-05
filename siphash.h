#ifndef SIPHASH_H
#define SIPHASH_H

#include <stdint.h>
#include <stddef.h>

uint64_t siphash24(const void *src, unsigned long src_sz, unsigned char key[16]);

#endif // SIPHASH_H
