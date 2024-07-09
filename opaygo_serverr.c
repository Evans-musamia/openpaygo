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

typedef struct {
    uint32_t starting_code;
    int value;
    int count;
} Record;

typedef struct {
    Record *records;
    int count;
} RecordList;

typedef struct {
    char oem_id[20];
    uint64_t token;
    double decoded_value;
    char token_type[20];
    int decoded_count;
    uint32_t starting_code;
    unsigned char secret_key[16];
    uint16_t used_counts;
} CSVRecord;

void hex_to_bytes(const char* hex_str, unsigned char* bytes);
uint32_t GenerateStartingCode(unsigned char SECRET_KEY[16]);
bool IsInUsedCounts(int Count, uint16_t MaxCount, uint16_t UsedCounts);
bool IsCountValid(int Count, uint16_t MaxCount, int Value, uint16_t UsedCounts);
void MarkCountAsUsed(int Count, uint16_t *MaxCount, uint16_t *UsedCounts, int Value);
TokenData GetDataFromToken(uint64_t InputToken, uint16_t *MaxCount, uint16_t *UsedCounts, uint32_t StartingCode, unsigned char SECRET_KEY[16]);
void save_to_csv(const char* filename, int value, int count, uint32_t masked_token);
RecordList fetch_records_for_oem_id(const char* filename, const char* oem_id);
CSVRecord* read_csv(const char* filename, int* record_count);
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
    char oem_id[20] = {0};
    char token_str[20] = {0};
    uint64_t token = 0;

    if (sscanf(command, "fetch %19s", oem_id) == 1) {
        RecordList recordList = fetch_records_for_oem_id("token_data.csv", oem_id);
        if (recordList.count == 0) {
            printf("No records found for OEM ID %s.\n", oem_id);
        } else {
            for (int i = 0; i < recordList.count; i++) {
                printf("Record %d: starting_code=%u, value=%d, count=%d\n", i, recordList.records[i].starting_code, recordList.records[i].value, recordList.records[i].count);
            }
        }
        free(recordList.records);
    } else if (sscanf(command, "decode %19s %llu", oem_id, &token) == 2) {
        char hex_key[] = "236432F2318504F7236432F2318504F6";
        unsigned char key[16];
        hex_to_bytes(hex_key, key);

        RecordList recordList = fetch_records_for_oem_id("token_data.csv", oem_id);
        if (recordList.count == 0) {
            printf("No records found for OEM ID %s.\n", oem_id);
            return;
        }

        Record latestRecord = recordList.records[0];
        uint16_t maxCount = 0;
        uint16_t usedCounts = 0;

        TokenData result = GetDataFromToken(token, &maxCount, &usedCounts, latestRecord.starting_code, key);

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

            save_to_csv("token_data.csv", result.Value, result.Count, result.MaskedToken);
        }

        free(recordList.records);
    } else {
        printf("Invalid command. Use 'fetch <oem_id>' or 'decode <oem_id> <token>'.\n");
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

void save_to_csv(const char* filename, int value, int count, uint32_t masked_token) {
    FILE* file = fopen(filename, "a");
    if (file == NULL) {
        printf("Error opening file for writing.\n");
        return;
    }
    fprintf(file, "%d,%d,%u\n", value, count, masked_token);
    fclose(file);
}

RecordList fetch_records_for_oem_id(const char* filename, const char* oem_id) {
    int record_count = 0;
    CSVRecord* records = read_csv(filename, &record_count);
    RecordList recordList;
    recordList.records = malloc(record_count * sizeof(Record));
    recordList.count = 0;

    if (record_count == 0) {
        printf("CSV file not found or empty. Creating a new one.\n");
        free(records);
        return recordList;
    }

    for (int i = 0; i < record_count; i++) {
        if (strcmp(records[i].oem_id, oem_id) == 0) {
            recordList.records[recordList.count].starting_code = records[i].starting_code;
            recordList.records[recordList.count].value = (int)records[i].decoded_value;
            recordList.records[recordList.count].count = records[i].decoded_count;
            recordList.count++;
        }
    }

    free(records);
    return recordList;
}

CSVRecord* read_csv(const char* filename, int* record_count) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error opening file: %s\n", filename);
        *record_count = 0;
        return NULL;
    }

    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), file)) {
        count++;
    }
    rewind(file);

    CSVRecord* records = (CSVRecord*)malloc(sizeof(CSVRecord) * count);
    if (records == NULL) {
        printf("Memory allocation failed\n");
        fclose(file);
        *record_count = 0;
        return NULL;
    }

    int index = 0;
    while (fgets(line, sizeof(line), file)) {
        if (index == 0) {
            index++;
            continue; // Skip header line
        }
        sscanf(line, "%19[^,],%llu,%lf,%19[^,],%d,%u,%32s,%hx",
            records[index-1].oem_id,
            &records[index-1].token,
            &records[index-1].decoded_value,
            records[index-1].token_type,
            &records[index-1].decoded_count,
            &records[index-1].starting_code,
            (char *)records[index-1].secret_key,
            &records[index-1].used_counts);

        hex_to_bytes((char *)records[index-1].secret_key, records[index-1].secret_key);
        index++;
    }

    fclose(file);
    *record_count = count - 1; // Exclude header line
    return records;
}