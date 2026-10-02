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

N.B. Integers (and floating point) use two-tags and incorporate bit 2, which is
nominally part of the tag, as their lowest bit. So even numbers use tag 000 and
odd numbers use tag 001. 

So although the payload is only 61-bits, the integer-values that are represented
are a full 62-bits because of this use of bit-2.
