#include "opaygo_decoder.h"
#include "siphash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK_BIT(variable, position) ((variable) & (1 << (position)))
#define SET_BIT(variable, position, value) ((variable & ~(1 << position)) | (value << position))

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
#ifdef RESTRICTED_DIGIT_SET_MODE
    InputToken = ConvertFromFourDigitToken(InputToken);
#endif
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

// Function to save data to a CSV file
void save_to_csv(const char* filename, int value, int count, uint32_t masked_token) {
    FILE* file = fopen(filename, "a");
    if (file == NULL) {
        printf("Error opening file for writing.\n");
        return;
    }
    fprintf(file, "%d,%d,%u\n", value, count, masked_token);
    fclose(file);
}

// Function to fetch the latest starting code from the CSV file
uint32_t fetch_latest_starting_code(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("CSV file not found. Creating a new one.\n");
        return 0;
    }

    char line[256];
    uint32_t latest_starting_code = 0;

    while (fgets(line, sizeof(line), file)) {
        char* token = strtok(line, ",");
        int value = atoi(token);
        token = strtok(NULL, ",");
        int count = atoi(token);
        token = strtok(NULL, ",");
        uint32_t masked_token = (uint32_t)strtoul(token, NULL, 10);

        latest_starting_code = masked_token;
    }

    fclose(file);
    return latest_starting_code;
}

int main() {
    uint64_t token;
    printf("Enter the token to decode: ");
    scanf("%llu", &token);

    char hex_key[] = "236432F2318504F7236432F2318504F6";
    unsigned char key[16];
    hex_to_bytes(hex_key, key); // Convert hex string to byte array

    uint32_t startingCode = fetch_latest_starting_code("token_data.csv");
    if (startingCode == 0) {
        startingCode = GenerateStartingCode(key);
        printf("Starting Code (from generation): %u\n", startingCode);
        save_to_csv("token_data.csv", 0, 0, startingCode); // Save the first code to CSV
    } else {
        printf("Starting Code (from CSV): %u\n", startingCode);
    }

    uint16_t maxCount = 0;
    uint16_t usedCounts = 0;

    TokenData result = GetDataFromToken(token, &maxCount, &usedCounts, startingCode, key);

    if (result.Value == -1) {
        printf("Invalid token!\n");
    } else if (result.Value == -2) {
        printf("Token already used!\n");
    } else {
        printf("Valid token.\n");
        printf("Decoded Value: %d\n", result.Value);
        printf("Decoded Count: %d\n", result.Count);
        printf("Masked Token: %u\n", result.MaskedToken);

        // Display the type of token based on the TokenType
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

        // Save data to CSV
        save_to_csv("token_data.csv", result.Value, result.Count, result.MaskedToken);
    }

    return 0;
}


// gcc -o opaygo_decoder opaygo_decoder.c siphash.c  opaygo_core.c