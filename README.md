# OpenPAYGO Token System

## Overview

This project implements an OpenPAYGO token system in C. The system includes functionalities to generate and validate tokens for PAYGO (Pay-As-You-Go) systems. The code is designed to run on embedded devices such as Arduino.

## Repository Structure

- `opaygo_core.c` and `opaygo_core.h`: Core functions for generating PAYGO tokens.
- `opaygo_decoder.c` and `opaygo_decoder.h`: Functions for decoding and validating PAYGO tokens.
- `opaygo_value_utils.c` and `opaygo_value_utils.h`: Utility functions for manipulating token values.
- `restricted_digit_set_mode.c` and `restricted_digit_set_mode.h`: Functions for handling restricted digit sets (if applicable).
- `siphash.c` and `siphash.h`: Implementation of the SipHash cryptographic function.

## Installation

1. Clone the repository:
    ```bash
    git clone https://github.com/Evans-musamia/openpaygo.git
    ```

2. Navigate to the project directory:
    ```bash
    cd openpaygo
    ```

3. Compile the code:
    ```bash
    gcc -o opaygo_decoder opaygo_decoder.c opaygo_core.c opaygo_value_utils.c siphash.c
    ```

## Usage

### Generating a Token

To generate a token, you need to provide a secret key and token details such as count, value, and type. The Python script `server.py` can be used to generate tokens.

Example usage:
```python
secret_key = "8473aa4fe83a6317dea1b304f2d6f6e5"
count = 1
value = 20
token_type = TokenType.ADD_TIME

generated_count, generated_token = OpenPAYGOTokenEncoder.generate_token(secret_key, value, token_type)
print("Generated Count:", generated_count)
print("Generated Token:", generated_token)
