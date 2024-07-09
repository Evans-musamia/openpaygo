#include "opaygo_core.h"
#include "opaygo_decoder.h"
#include "opaygo_value_utils.h"
#include "restricted_digit_set_mode.h"
#include "siphash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUFFER_SIZE 1024

// Macro definitions
#define CHECK_BIT(variable, position) ((variable) & (1 << (position)))
#define SET_BIT(variable, position, value) ((variable & ~(1 << position)) | (value << position))

void hex_to_bytes(const char* hex_str, unsigned char* bytes);
uint32_t GenerateStartingCode(unsigned char SECRET_KEY[16]);
bool IsInUsedCounts(int Count, uint16_t MaxCount, uint16_t UsedCounts);
bool IsCountValid(int Count, uint16_t MaxCount, int Value, uint16_t UsedCounts);
void MarkCountAsUsed(int Count, uint16_t *MaxCount, uint16_t *UsedCounts, int Value);
TokenData GetDataFromToken(uint64_t InputToken, uint16_t *MaxCount, uint16_t *UsedCounts, uint32_t StartingCode, unsigned char SECRET_KEY[16]);
void handle_command(char *command);

int main() {
    char command[BUFFER_SIZE];

    printf("Enter command: ");
    while (fgets(command, sizeof(command), stdin) != NULL) {
        handle_command(command);
        printf("\nEnter command: ");
    }

    return 0;
}

void handle_command(char *command) {
    char token_str[20] = {0};
    uint64_t token = 0;

    if (sscanf(command, "decode %llu", &token) == 1) {
        char hex_key[] = "236432F2318504F7236432F2318504F6";
        unsigned char key[16];
        hex_to_bytes(hex_key, key);

        uint32_t starting_code = 698400053;
        uint16_t maxCount = 0;
        uint16_t usedCounts = 0;

        TokenData result = GetDataFromToken(token, &maxCount, &usedCounts, starting_code, key);

        if (result.Value == -1) {
            printf("Invalid token!\n");
        } else if (result.Value == -2) {
            printf("Token already used!\n");
        } else {
            printf("Valid token.\nDecoded Value: %d\nDecoded Count: %d\nMasked Token: %u\n", 
                    result.Value, result.Count, result.MaskedToken);

            switch(result.TokenType) {
                case ADD_TIME:
                    printf("Token Type: ADD_TIME\n");
                    break;
                case SET_TIME:
                    printf("Token Type: SET_TIME\n");
                    break;
                case DISABLE_PAYG:
                    printf("Token Type: DISABLE_PAYG\n");
                    break;
                case COUNTER_SYNC:
                    printf("Token Type: COUNTER_SYNC\n");
                    break;
                default:
                    printf("Token Type: UNKNOWN\n");
                    break;
            }
        }
    } else {
        printf("Invalid command. Use 'decode <token>'.\n");
    }
}

void hex_to_bytes(const char* hex_str, unsigned char* bytes) {
    for (int i = 0; i < 16; i++) {
        sscanf(hex_str + 2 * i, "%2hhx", &bytes[i]);
    }
}

uint32_t GenerateStartingCode(unsigned char SECRET_KEY[16]) {
    uint64_t starting_hash = siphash24(SECRET_KEY, 16, SECRET_KEY);
    uint32_t hi_hash = (uint32_t)(starting_hash >> 32);
    uint32_t lo_hash = (uint32_t)(starting_hash & 0xFFFFFFFF);
    uint32_t result_hash = hi_hash ^ lo_hash;
    uint32_t starting_code = (result_hash & 0xFFFFFFFC) >> 2;
    if (starting_code > 999999999) {
        starting_code -= 73741825;
    }
    return starting_code;
}

bool IsInUsedCounts(int Count, uint16_t MaxCount, uint16_t UsedCounts) {
    uint16_t RelativeCount = MaxCount - Count - 1;
    if (Count % 2 && Count <= MaxCount) {
        return true;
    }
    if (CHECK_BIT(UsedCounts, RelativeCount) || Count == MaxCount) {
        return true;
    }
    return false;
}

bool IsCountValid(int Count, uint16_t MaxCount, int Value, uint16_t UsedCounts) {
    if (Value == COUNTER_SYNC_VALUE) {
        if (Count > MaxCount - MAX_TOKEN_JUMP) {
            return true;
        }
    } else if (Count > MaxCount) {
        return true;
    } else if (MAX_UNUSED_OLDER_TOKENS > 0 && Count != 0) {
        if (Count > MaxCount - MAX_UNUSED_OLDER_TOKENS) {
            if (Count % 2 == 0 && !IsInUsedCounts(Count, MaxCount, UsedCounts)) {
                return true;
            }
        }
    }
    return false;
}

void MarkCountAsUsed(int Count, uint16_t *MaxCount, uint16_t *UsedCounts, int Value) {
    uint16_t NewUsedCount = 0;
    if (Count % 2 || Value == COUNTER_SYNC_VALUE || Value == PAYG_DISABLE_VALUE) {
        for (int i = 0; i < MAX_UNUSED_OLDER_TOKENS; i++) {
            *UsedCounts = SET_BIT(*UsedCounts, i, 1);
        }
    } else {
        if (Count > *MaxCount) {
            uint16_t RelativeCount = Count - *MaxCount;
            if (RelativeCount > MAX_UNUSED_OLDER_TOKENS) {
                RelativeCount = MAX_UNUSED_OLDER_TOKENS;
            } else {
                NewUsedCount = SET_BIT(NewUsedCount, (RelativeCount - 1), 1);
            }
            for (int i = RelativeCount + 1; i < MAX_UNUSED_OLDER_TOKENS - RelativeCount; i++) {
                NewUsedCount = SET_BIT(NewUsedCount, i, CHECK_BIT(*UsedCounts, i - RelativeCount));
            }
            *UsedCounts = NewUsedCount;
        } else {
            *UsedCounts = SET_BIT(*UsedCounts, (*MaxCount - Count - 1), 1);
        }
    }
    if (Count > *MaxCount) {
        *MaxCount = Count;
    }
}

TokenData GetDataFromToken(uint64_t InputToken, uint16_t *MaxCount, uint16_t *UsedCounts, uint32_t StartingCode, unsigned char SECRET_KEY[16]) {
    uint16_t StartingCodeBase = GetTokenBase(StartingCode);
    uint16_t TokenBase = GetTokenBase((uint32_t)InputToken);
    uint32_t CurrentToken = PutBaseInToken(StartingCode, TokenBase);
    uint32_t MaskedToken;
    int MaxCountTry;
    int Value = DecodeBase(StartingCodeBase, TokenBase);
    TokenData output = { .Value = -1, .Count = 0, .MaskedToken = 0, .CurrentToken = 0, .TokenBase = TokenBase, .StartingCodeBase = StartingCodeBase };
    bool ValidOlderToken = false;

    if (Value == COUNTER_SYNC_VALUE) {
        MaxCountTry = *MaxCount + MAX_TOKEN_JUMP_COUNTER_SYNC;
    } else {
        MaxCountTry = *MaxCount + MAX_TOKEN_JUMP;
    }

    for (int Count = 0; Count <= MaxCountTry; Count++) {
        MaskedToken = PutBaseInToken(CurrentToken, TokenBase);
        output.MaskedToken = MaskedToken;
        output.CurrentToken = CurrentToken;

        if (MaskedToken == InputToken) {
            if (IsCountValid(Count, *MaxCount, Value, *UsedCounts)) {
                MarkCountAsUsed(Count, MaxCount, UsedCounts, Value);
                output.Value = Value;
                output.Count = Count;
                return output;
            } else {
                ValidOlderToken = true;
            }
        }
        CurrentToken = GenerateOPAYGOToken(CurrentToken, SECRET_KEY);
    }
    if (ValidOlderToken) {
        output.Value = -2;
    } else {
        output.Value = -1;
    }
    output.Count = *MaxCount;
    return output;
}
