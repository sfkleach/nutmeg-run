# Low order tag bits

The bottom three bits have this interpretation:

| Bits | Meaning |
|------|---------|
| x00  | A 62-bit integer |
| x10  | a 62-bit floating point number |
| 001  | a 64-bit pointer, with offset of 1, aligned to 8-byte boundaries |
| 011  | Reserved |
| 101  | Reserved |
| 111  | Special literal    values |

