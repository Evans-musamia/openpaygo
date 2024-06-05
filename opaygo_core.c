#include "opaygo_core.h"
#include "siphash.h"

uint32_t extractBits(uint32_t source, unsigned from, unsigned to) {
    uint32_t mask = ((1 << (to - from + 1)) - 1) << from;
    return (source & mask) >> from;
}

uint32_t ConvertHashToToken(uint64_t this_hash) {
    uint32_t hi_hash, lo_hash;
    hi_hash = this_hash >> 32;
    lo_hash = this_hash & 0xFFFFFFFF;
    hi_hash ^= lo_hash;
    uint32_t result = extractBits(hi_hash, 2, 32);
    if (result > 999999999) {
        result = result - 73741825;
    }
    return result;
}

uint32_t GenerateOPAYGOToken(uint32_t last_token, unsigned char secret_key[16]) {
    uint8_t a[8];
    a[0] = last_token >> 24;
    a[1] = last_token >> 16;
    a[2] = last_token >> 8;
    a[3] = last_token;
    a[4] = last_token >> 24;
    a[5] = last_token >> 16;
    a[6] = last_token >> 8;
    a[7] = last_token;
    uint64_t this_hash = siphash24(a, 8, secret_key);
    return ConvertHashToToken(this_hash);
}

int DecodeBase(uint16_t starting_code_base, uint16_t token_base) {
    if ((token_base - starting_code_base) < 0) {
        return token_base + 1000 - starting_code_base;
    } else {
        return token_base - starting_code_base;
    }
}

uint16_t GetTokenBase(uint32_t token) {
    return (token % 1000);
}

uint32_t PutBaseInToken(uint32_t token, uint16_t token_base) {
    return token - (token % 1000) + token_base;
}
