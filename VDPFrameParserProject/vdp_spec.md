# VDP (Vehicle Diagnostic Protocol) Specification v1.0

## Overview
VDP is a simplified diagnostic protocol for vehicle communication. All communication is sequential - only one ECU can be addressed at a time.

## Frame Structure

Each VDP frame consists of:

```
[START_BYTE][LENGTH][ECU_ID][COMMAND][DATA...][CHECKSUM][END_BYTE]
```

### Field Definitions:

- **START_BYTE** (1 byte): Always `0x7E`
- **LENGTH** (1 byte): Total frame length including START and END bytes (min: 7, max: 255)
- **ECU_ID** (1 byte): Target ECU identifier (0x01-0x7F)
- **COMMAND** (1 byte): Command type
- **DATA** (0-247 bytes): Command-specific data
- **CHECKSUM** (1 byte): XOR of all bytes between START and CHECKSUM (exclusive)
- **END_BYTE** (1 byte): Always `0x7F`

### Command Types:

- `0x10`: READ_DATA - Read diagnostic data
- `0x20`: WRITE_DATA - Write configuration
- `0x30`: CLEAR_CODES - Clear error codes
- `0x40`: ECU_RESET - Reset ECU
- `0x50`: KEEP_ALIVE - Maintain connection

### Response Frame:

Response frames follow the same structure with these modifications:
- ECU_ID is OR'd with `0x80` to indicate response (e.g., 0x01 becomes 0x81)
- First DATA byte indicates status:
  - `0x00`: SUCCESS
  - `0x01`: INVALID_COMMAND
  - `0x02`: INVALID_DATA
  - `0x03`: ECU_BUSY
  - `0xFF`: GENERAL_ERROR

### Data Encoding:
- String data is null-terminated within the DATA field
- Multi-byte integer values are encoded consistently across all frames

## Example Frames:

### Request: Read data from ECU 0x01
```
7E 08 01 10 00 01 1A 7F
```
- START: 0x7E
- LENGTH: 0x08 (8 bytes total)
- ECU_ID: 0x01
- COMMAND: 0x10 (READ_DATA)
- DATA: 0x00 0x01 (read parameter 0x0001)
- CHECKSUM: 0x1A (0x08 XOR 0x01 XOR 0x10 XOR 0x00 XOR 0x01)
- END: 0x7F

### Response: Success with data
```
7E 0A 81 10 00 12 34 56 A9 7F
```
- ECU_ID: 0x81 (response from ECU 0x01)
- Status: 0x00 (SUCCESS)
- DATA: 0x12 0x34 0x56 (returned values)

## Timing Requirements:
- Response timeout: 100ms maximum
- Inter-frame delay: Minimum 5ms recommended
- Retry policy: Implementation defined