# Layout of Heap Objects

## General Principles

- All Nutmeg pointers are aligned to 64-bit word boundaries.
- In a cell, pointers are tagged with 001, which means they must be detagged before use.
- Heap objects are contiguous strips of memory that are aligned
  on 64-bit word boundaries.
- Heap objects have a single field dedicated to storing the 
  concrete runtime type of the object, called the `datakey`.
- The untagged pointer always points to a `datakey`, which is
  typically near the front (lowest address) of the heap-object.

The following types of heap objects are supported:

- Keys, which are values representing the type of an object.
- Records, which are a fixed set of 64-bit fields, some of
  which are tagged cells and others are raw. The raw fields may be
  carved up into sub-fields.
- Vectors, which are arrays of tagged-cells.
- Binarrays, which are arrays of raw 64-bit fields.
- Procedures, which are a complex structure containing abstract machine
  instructions.

## Key-objects

Key-objects are record-like datastructures that are laid out as follows:

| Position | Description | Tagged? |
|--------|-------------|---------|
| 0 | Datakey | Tagged |
| 1 | Flavour | Raw, 8 bits |
| 2 | NumWords | Raw, 16 bits |
| 3 | NumCells | Raw, 16 bits |
| 4 | BitWidth | Raw, 8 bits |

N.B. The BitWidth is used to describe the width of each array-entry when using
Binary arrays.

The `datakey` of any key object is the unique `keykey` object, which is a 
unique key with the layout:

| Position | Description | Value |
|--------|-------------|---------|
| 0 | Datakey (self-pointer) | Tagged |
| 1 | Flavour | 0 |
| 2 | NumWords | 1 |
| 3 | NumCells | 0 |

## Record-objects

Each record object of a given type has a datakey followed by W raw words and C
tagged cells.

| Position | Description | Tagged? |
|--------|-------------|---------|
| 0 | Datakey | Tagged |
| 1 | Raw #1 | Raw, 64-bits |
| .. | ... | Raw, 64-bits |
| W | Raw #W | Raw, 64-bits |
| W+1 | Cell #1 | Tagged |
| .. | ... | Tagged |
| W+C | Cell #C | Tagged |


## Vector-objects

Vector objects are like record-objects but have a leading length field L and
following the fixed fields a variable number L of tagged cells.

| Position | Description | Tagged? |
|--------|-------------|---------|
| -1 | Length | Tagged, guaranteed x00 tag |
| 0 | Datakey | Tagged |
| 1 | Raw #1 | Raw, 64-bits |
| .. | ... | Raw, 64-bits |
| W | Raw #W | Raw, 64-bits |
| W+1 | Cell #1 | Tagged |
| .. | ... | Tagged |
| W+C | Cell #C | Tagged |
| W+C+1 | Position 0 | Tagged |
| .. | ... | Tagged |
| W+C+L | Position L-1 | Tagged |


## Binarray-objects

Binary array objects are like record-objects but have a leading length field L and
following the fixed fields a variable number L of raw words.

| Position | Description | Tagged? |
|--------|-------------|---------|
| -1 | Length | Tagged, guaranteed x00 tag |
| 0 | Datakey | Tagged |
| 1 | Raw #1 | Raw, 64-bits |
| .. | ... | Raw, 64-bits |
| W | Raw #W | Raw, 64-bits |
| W+1 | Cell #1 | Tagged |
| .. | ... | Tagged |
| W+C | Cell #C | Tagged |
| W+C+1 | Position 0 | Raw, BitWidth |
| .. | ... | Raw, BitWidth |
| W+C+L | Position L-1 | Raw, BitWidth |

N.B. This is the object-type used to represented UTF-8 strings with a
bit-width of 8.


## Procedure-objects

Procedure objects are relatively complex, custom object layouts. The
instructions begin immediately after the datakey and interleave instructions and
data, including tagged cells. The location of tagged-pointer cells is recorded
in packed array of 32 bit unsigned values at the end of the instructions. This
is the T-block (T for "tagged").

There are two length pointers: one is number of instruction words and the other
is the length of the T-block in 32-bit words.

Following the datakey are two numerical values: the number of locals and the
number of arguments accepted by the function.


| Position | Description | Tagged? |
|--------|-------------|---------|
| -2  | Length N, number of instruction words | Tagged, guaranteed x00 tag |
| -1  | Length L, of the T-block | Tagged, guaranteed x00 tag |
| 0 | Datakey | Tagged |
| 1 | Number of locals | Raw, 32 bits |
| 1.5 | Number of input arguments | Raw, 32 bits |
| 2  | Instruction word #1 | Mixed |
| ... | ... | Mixed |
| 2+N-1 | Last instruction #N | Mixed |
| 2+N | Start of T-block | Raw, 32 bits |
| ... | ... | Raw, 32 bits |
| 2+N+L-1 | Last T-block value | Raw, 32-bits |


