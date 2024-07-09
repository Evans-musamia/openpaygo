#include "opaygo_decoder.h"
#include "siphash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK_BIT(variable, position) ((variable) & (1 << (position)))
#define SET_BIT(variable, position, value) ((variable & ~(1 << position)) | (value << position))

// Global linked list to keep track of token states
TokenState* tokenStates = NULL;

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
        if (Count > MaxCount - MAX_TOKEN_JUMP_COUNTER_SYNC) {
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

enum TokenType GetTokenType(int Value, int Count) {
    if (Count % 2 == 0) {
        return ADD_TIME;
    } else {
        if (Value == COUNTER_SYNC_VALUE) {
            return COUNTER_SYNC;
        } else if (Value == PAYG_DISABLE_VALUE) {
            return DISABLE_PAYG;
        } else {
            return SET_TIME;
        }
    }
}

TokenState* get_token_state(uint32_t startingCode) {
    TokenState* current = tokenStates;
    while (current != NULL) {
        if (current->startingCode == startingCode) {
            return current;
        }
        current = current->next;
    }
    // If not found, create a new state
    TokenState* newState = (TokenState*)malloc(sizeof(TokenState));
    newState->startingCode = startingCode;
    newState->maxCount = 0;
    newState->usedCounts = 0;
    newState->storedValue = 0;
    newState->next = tokenStates;
    tokenStates = newState;
    return newState;
}

void update_token_state(uint32_t startingCode, uint16_t maxCount, uint16_t usedCounts, int storedValue) {
    TokenState* state = get_token_state(startingCode);
    state->maxCount = maxCount;
    state->usedCounts = usedCounts;
    state->storedValue = storedValue;
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
    TokenData output = { .Value = -1, .Count = 0, .MaskedToken = 0, .CurrentToken = 0, .TokenBase = TokenBase, .StartingCodeBase = StartingCodeBase, .TokenType = INVALID };
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
                output.TokenType = GetTokenType(Value, Count); // Ensure the TokenType is set
                return output;
            } else {
                ValidOlderToken = true;
            }
        }
        CurrentToken = GenerateOPAYGOToken(CurrentToken, SECRET_KEY);
    }
    if (ValidOlderToken) {
        output.Value = ALREADY_USED;
    } else {
        output.Value = INVALID;
    }
    output.Count = *MaxCount;
    return output;
}

int main() {
    uint64_t token;
    char hex_key[] = "236432F2318504F7236432F2318504F6";
    unsigned char key[16];
    hex_to_bytes(hex_key, key); // Convert hex string to byte array

    uint32_t startingCode = 698400053;
    printf("Starting Code: %u\n", startingCode);

    TokenState* state = get_token_state(startingCode);

    while (true) {
        printf("Enter the token to decode (or 'exit' to quit): ");
        char input[20];
        scanf("%s", input);

        if (strcmp(input, "exit") == 0) {
            break;
        }

        token = strtoull(input, NULL, 10);

        TokenData result = GetDataFromToken(token, &state->maxCount, &state->usedCounts, startingCode, key);

        if (result.Value == INVALID) {
            printf("Invalid token!\n");
        } else if (result.Value == ALREADY_USED) {
            printf("Token already used!\n");
        } else {
            printf("Valid token.\n");
            printf("Decoded Value: %d\n", result.Value);
            printf("Decoded Count: %d\n", result.Count);
            printf("Masked Token: %u\n", result.MaskedToken);

            // Update and display the stored value based on the token type
            if (result.TokenType == ADD_TIME) {
                state->storedValue += result.Value;
            } else if (result.TokenType == SET_TIME) {
                state->storedValue = result.Value;
            }

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
                    printf("Invalid Token Type!\n");
                    break;
            }

            // Print the updated parameters
            printf("Stored Value: %d\n", state->storedValue);
            printf("Max Count: %d\n", state->maxCount);
            printf("Used Counts: %d\n", state->usedCounts);
        }

        update_token_state(startingCode, state->maxCount, state->usedCounts, state->storedValue);
    }

    return 0;
}

// gcc -o opaygo_decoder opaygo_decoder.c siphash.c opaygo_core.c
