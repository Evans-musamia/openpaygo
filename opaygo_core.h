#ifndef OPAYGO_CORE_H
#define OPAYGO_CORE_H

#if (defined(__APPLE__) && defined(__MACH__)) /* MacOS X Framework build */
    #include <sys/types.h>
#endif

#include <stdint.h>
#include <stdio.h>

#define MAX_ACTIVATION_VALUE 995
#define PAYG_DISABLE_VALUE 998
#define COUNTER_SYNC_VALUE 999

uint32_t GenerateOPAYGOToken(uint32_t last_token, unsigned char secret_key[16]);
uint16_t GetTokenBase(uint32_t token);
int DecodeBase(uint16_t starting_code_base, uint16_t token_base);
uint32_t PutBaseInToken(uint32_t token, uint16_t token_base);

#endif // OPAYGO_CORE_H
