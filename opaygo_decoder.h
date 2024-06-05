#ifndef OPAYGO_DECODER_H
#define OPAYGO_DECODER_H

#if (defined(__APPLE__) && defined(__MACH__)) /* MacOS X Framework build */
    #include <sys/types.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h> // Include stdbool.h for bool type

#include "opaygo_core.h"
#include "opaygo_value_utils.h"
#include "restricted_digit_set_mode.h"

#define MAX_TOKEN_JUMP 64 // This is a jump in count so up to twice as large as the number of tokens of the same type
#define MAX_TOKEN_JUMP_COUNTER_SYNC 100 // This is a jump in count so up to twice as large as the number of tokens
#define MAX_UNUSED_OLDER_TOKENS 16 // Maximum of 16 (8 tokens of the same type)
#define COUNTER_SYNC_VALUE 999
#define PAYG_DISABLE_VALUE 998

struct TokenDataStruct {
    int Value;
    uint16_t Count;
    uint32_t MaskedToken;
    uint32_t CurrentToken;
    uint16_t TokenBase;
    uint16_t StartingCodeBase;
    uint8_t TokenType;
};
typedef struct TokenDataStruct TokenData;

TokenData GetDataFromToken(uint64_t InputToken, uint16_t *MaxCount, uint16_t *UsedCounts, uint32_t StartingCode, unsigned char SECRET_KEY[16]);

bool IsInUsedCounts(int Count, uint16_t MaxCount, uint16_t UsedCounts);
bool IsCountValid(int Count, uint16_t MaxCount, int Value, uint16_t UsedCounts);
void MarkCountAsUsed(int Count, uint16_t *MaxCount, uint16_t *UsedCounts, int Value);

#endif // OPAYGO_DECODER_H
