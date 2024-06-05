#ifndef OPAYGO_VALUE_UTILS_H
#define OPAYGO_VALUE_UTILS_H

#include <stdio.h>
#include <stdint.h>

#if (defined(__APPLE__) && defined(__MACH__)) /* MacOS X Framework build */
    #include <sys/types.h>
#endif

int DecodeBase(uint16_t starting_code_base, uint16_t token_base);
uint16_t GetTokenBase(uint32_t token);
uint32_t PutBaseInToken(uint32_t token, uint16_t token_base);

#endif // OPAYGO_VALUE_UTILS_H
