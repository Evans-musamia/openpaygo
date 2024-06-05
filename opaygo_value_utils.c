#include "opaygo_value_utils.h"

int DecodeBase(uint16_t starting_code_base, uint16_t token_base) {
    if((token_base - starting_code_base) < 0) {
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
