#include "opaygo_decoder.h"
#include "siphash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK_BIT(variable, position) ((variable) & (1 << (position)))
#define SET_BIT(variable, position, value) ((variable & ~(1 << position)) | (value << position))

// EEPROM addresses
#define EEPROM_LAST_CODE_ADDRESS 0
#define EEPROM_MAX_COUNT_ADDRESS 2
#define EEPROM_CURRENT_TIME_ADDRESS 4

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

// Function to extract token details from the input token
void ExtractTokenDetails(uint64_t token, int* value, uint16_t* count, uint8_t* type) {
    *value = (token >> 48) & 0xFFFF; // Assuming value is in the upper 16 bits
    *count = (token >> 32) & 0xFFFF; // Assuming count is in the next 16 bits
    *type = (token >> 28) & 0xF;     // Assuming type is in the next 4 bits
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
    int token_value;
    uint16_t token_count;
    uint8_t token_type;

    // Extract token details
    ExtractTokenDetails(InputToken, &token_value, &token_count, &token_type);

    uint16_t StartingCodeBase = GetTokenBase(StartingCode);
    uint16_t TokenBase = GetTokenBase((uint32_t)InputToken);
    uint32_t CurrentToken = PutBaseInToken(StartingCode, TokenBase);
    uint32_t MaskedToken;
    int MaxCountTry;
    int Value = DecodeBase(StartingCodeBase, TokenBase);
    TokenData output = { .Value = token_value, .Count = token_count, .MaskedToken = 0, .CurrentToken = 0, .TokenBase = TokenBase, .StartingCodeBase = StartingCodeBase, .TokenType = token_type };
    bool ValidOlderToken = false;

    printf("StartingCodeBase: %u, TokenBase: %u\n", StartingCodeBase, TokenBase); // Debug print

    if (Value == COUNTER_SYNC_VALUE) {
        MaxCountTry = *MaxCount + MAX_TOKEN_JUMP_COUNTER_SYNC;
    } else {
        MaxCountTry = *MaxCount + MAX_TOKEN_JUMP;
    }

    for (int Count = 0; Count <= MaxCountTry; Count++) {
        MaskedToken = PutBaseInToken(CurrentToken, TokenBase);
        output.MaskedToken = MaskedToken;
        output.CurrentToken = CurrentToken;
        // printf("Count: %d, MaskedToken: %u, CurrentToken: %u\n", Count, MaskedToken, CurrentToken);

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

// EEPROM simulation functions
uint16_t readEEPROM16(int address) {
    return 0; // Replace with actual EEPROM read function
}

void writeEEPROM16(int address, uint16_t value) {
    // Replace with actual EEPROM write function
}

uint32_t readEEPROM32(int address) {
    return 0; // Replace with actual EEPROM read function
}

void writeEEPROM32(int address, uint32_t value) {
    // Replace with actual EEPROM write function
}

void updateCurrentTime(int added_time) {
    uint32_t current_time = readEEPROM32(EEPROM_CURRENT_TIME_ADDRESS);
    current_time += added_time;
    writeEEPROM32(EEPROM_CURRENT_TIME_ADDRESS, current_time);
}

int main() {
    uint64_t token;
    printf("Enter the token to decode: ");
    scanf("%llu", &token);

    char hex_key[] = "236432F2318504F7236432F2318504F7";
    unsigned char key[16];
    hex_to_bytes(hex_key, key); // Convert hex string to byte array

    uint32_t lastCode = readEEPROM32(EEPROM_LAST_CODE_ADDRESS);
    uint16_t maxCount = readEEPROM16(EEPROM_MAX_COUNT_ADDRESS);

    if (lastCode == 0) {
        lastCode = GenerateStartingCode(key);
        writeEEPROM32(EEPROM_LAST_CODE_ADDRESS, lastCode);
        maxCount = 0;
        writeEEPROM16(EEPROM_MAX_COUNT_ADDRESS, maxCount);
    }

    uint16_t usedCounts = 0;

    while (1) {
        TokenData result = GetDataFromToken(token, &maxCount, &usedCounts, lastCode, key);

        if (result.Value == -1) {
            printf("Invalid token!\n");
        } else if (result.Value == -2) {
            printf("Token already used!\n");
        } else {
            printf("Valid token.\n");

            // Update EEPROM with the new last code and max count
            lastCode = result.CurrentToken;
            writeEEPROM32(EEPROM_LAST_CODE_ADDRESS, lastCode);
            writeEEPROM16(EEPROM_MAX_COUNT_ADDRESS, maxCount);

            // Update current time if token type is ADD_TIME or SET_TIME
            if (result.TokenType == ADD_TIME) {
                updateCurrentTime(result.Value);
            } else if (result.TokenType == SET_TIME) {
                writeEEPROM32(EEPROM_CURRENT_TIME_ADDRESS, result.Value);
            }

            // Print the last valid code, count, and value
            printf("Last Valid Code: %u\n", lastCode);
            printf("Last Count: %d\n", result.Count);
            printf("Last Value: %d\n", result.Value);
        }

        // Print the last valid code, count, and value even if the token is invalid
        printf("Last Valid Code: %u\n", lastCode);
        printf("Last Count: %d\n", maxCount);
        printf("Last Value: %d\n", readEEPROM32(EEPROM_CURRENT_TIME_ADDRESS));

        // Break the loop if you want to exit, for example:
        // if (some_condition) break;

        // Prompt for the next token
        printf("Enter the next token to decode: ");
        scanf("%llu", &token);
    }

    return 0;
}
