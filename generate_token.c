#include "siphash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Function to convert a 32-character hex string to a 16-byte array
void hex_to_bytes(const char* hex_str, unsigned char* bytes) {
    for (int i = 0; i < 16; i++) {
        sscanf(hex_str + 2 * i, "%2hhx", &bytes[i]);
    }
}

// Function to generate the starting code from the secret key
uint32_t GenerateStartingCode(unsigned char SECRET_KEY[16]) {
    uint64_t starting_hash = siphash24(SECRET_KEY, 16, SECRET_KEY);
    uint32_t hi_hash = (uint32_t)(starting_hash >> 32);
    uint32_t lo_hash = (uint32_t)(starting_hash & 0xFFFFFFFF);
    uint32_t result_hash = hi_hash ^ lo_hash;
    uint32_t starting_code = (result_hash & 0xFFFFFFFC) >> 2; // Reduce to 30 bits
    if (starting_code > 999999999) {
        starting_code -= 73741825;
    }
    printf("Generated Starting Code: %u\n", starting_code); // Debug print
    return starting_code;
}

int main() {
    char hex_key[] = "0123456789abcdef0123456789abcdef";
    unsigned char key[16];
    hex_to_bytes(hex_key, key); // Convert hex string to byte array

    uint32_t startingCode = GenerateStartingCode(key); // Generate starting code from secret key
    printf("First Token: %u\n", startingCode);

    return 0;
}
