# How the library is stored: the study

Every book ships with the suite in one file, `assets/books/library.plb`
(`src/container.hpp`). The owner's budget for the whole library is about 128 MB. This is
the record of how the file came to be made the way it is, with the numbers that chose
each part, the losers included. Numbers are from the M4 unless marked; this Mac's
timings move by twice under load and were used only for sizes.

## The pipeline

1. **Normalized records** (`book.hpp`): what the importers write and the reader reads.
2. **Packing** (`text_pack.hpp`): tables learned once from the whole library turn the
   records into denser bytes. Each record becomes a control byte. A verse or chapter
   that counts on from the one before becomes one byte. Eight common characters outside
   ASCII get one byte each. 73,815 words become one-, two- or three-byte codes, each
   carrying the space after it. A capitalized or all-capital word is a flag byte plus its
   lowercase code. Exact for any records, including words the tables never saw.
3. **The base**: some books' packed texts end to end, coded as one stream. These are the
   Bible, the Tanakh, then a cross-section of the shelf (every so many books) to 16 MB.
4. **The coder** (`lz_coder.hpp`): LZ77 over the whole window with an optimal parse,
   coded bit by bit with adaptive probabilities by a range coder, in the manner of LZMA.
   Every book outside the base continues the base's trained model and may refer back
   into the base's text.
5. **Checks**: each book's text by its SHA-256, and the tables and the base by theirs.

Reading:
- The tables and the base are decoded once, on a GUI.Forms worker while the shelf is
  shown.
- A book inside the base is a slice of it.
- Any other book is decoded alone, reading back into the base where it lies, never
  copying it.

## The whole library

344 books, 224,794,551 bytes of normalized text. That is our own 8 and the 336 trial
imports of batch 1 (`.build/library/trial`).

| | bytes |
|---|---|
| packed | 92,961,498 (41.4%) |
| the base, packed | 15,999,082 |
| the base, coded | 8,565,161 |
| the other books, coded | 40,803,608 |
| **the file** | **49,656,183 (22.09% of the text)** |

Reading, M4:

| step | time or speed |
|---|---|
| prepared (once, on a worker) | 224 ms: base decoded 205, tables 11, base checked 7 |
| books inside the base | 340 MB/s of text (unpack and check) |
| other books | 116 MB/s of text (decode, unpack, check) |
| SHA-256 | 2.4 GB/s (Arm instructions; 138 MB/s portable, on this Mac) |
| a novel of 700 KB of text | about 6 ms |

Building it takes 44 s on the M4. A code cache keyed by the coder's format, the effort
and the base's digest re-codes only what changed. A change to any book changes the
learned tables, though, and so every packed text.

The shipped file, our own 8 books:
- v2 (each book coded alone): 4,243,176 bytes.
- v3: 3,685,235 bytes. All 8 books fit in the base, so after the one prepare each
  opens without decoding.

## The base's size

Measured over the whole library, with the base filled by stride before the second
filling pass:

| base, packed | file | prepared in |
|---|---|---|
| 3,374,202 (the Bibles only) | 52,141,067 | 46 ms |
| 6,493,948 | 50,475,895 | 98 ms |
| 15,902,658 | 49,471,183 | 236 ms |
| 21,281,289 | 49,249,278 | 310 ms |
| 31,995,901 | 49,148,463 | 444 ms |

16 MB is the knee. Beyond it each megabyte saves about 20 KB and costs about 13 ms at
every start.

## The coder, step by step

The sample: `.build/library/study/mini-p`, 11 books (5,512,700 packed bytes), coded
after an 8 MB base (`base-8m.bin`) at effort 48, using `tools/base_lab.cpp`. Decode
times are the best of five.

| change | base coded | books coded | base decoded | books decoded |
|---|---|---|---|---|
| version 3 as first written | 3,356,329 | 2,466,897 | 104 ms | 68 MB/s |
| raw distance bits sixteen at a time (one division); short matches copied by words | 3,356,338 | 2,466,908 | 88 ms | 81 MB/s |
| align bits 6 (LZMA has 4) | 3,357,432 | 2,467,445 | 95 ms | 76 MB/s |
| align bits 5 | 3,357,175 | 2,467,342 | 88 ms | 79 MB/s |
| align bits 3 | 3,356,958 | 2,467,669 | 86 ms | 82 MB/s |
| align bits 2 | 3,356,289 | 2,466,412 | 78–84 ms | 84–89 MB/s |
| align bits 1 | 3,355,148 | 2,465,482 | 78 ms | 91 MB/s |
| **align bits 0** | 3,354,756 | 2,465,567 | 79 ms | 91 MB/s |
| modelled distance slots to 4 | 3,353,555 | 2,465,224 | 72 ms | 96 MB/s |
| … to 6 | 3,354,379 | 2,464,823 | 73 ms | 95 MB/s |
| **… to 8** | 3,354,271 | 2,464,984 | 73 ms | 95 MB/s |
| … to 10 | 3,353,661 | 2,465,769 | 74 ms | 95 MB/s |
| … to 12 | 3,355,109 | 2,466,127 | 73 ms | 96 MB/s |
| **no matched literals** | **3,349,822** | **2,463,818** | **72 ms** | **97 MB/s** |
| literal context plus 1 bit of the byte before | 3,358,660 | 2,459,300 | 70 ms | 96 MB/s |
| … plus 2 bits | 3,367,293 | 2,466,909 | 68 ms | 98 MB/s |
| a counted literal loop | same | same | 72 ms | 96–97 MB/s |

In this packed text a far distance's low bits are as good as random, so modelling them
only costs. After a match, LZMA's matched literal bets that the next byte differs from
the byte the match would have given. With word codes that bet does not pay. The literal
context stays the whole previous byte; earlier, fewer bits of it were worse too.

Where the decoder's time goes, over the books outside the base:
- 291 million modelled bits;
- 101 million raw bits (11.2 a match, before they were taken sixteen at a time);
- 15.9 million literals and 9.0 million matches, averaging 6.8 bytes;
- 260 thousand repeats.

What is left is the serial chain of binary decisions.

Encoding, the same lab run (5 decodes included, about 0.65 s):

| change | time |
|---|---|
| version 3 as first written | 5.68 s |
| a tree node's two children in one cache line; the next walk's hash head prefetched | 5.30 s |
| matches compared eight bytes at a time | 5.10 s |
| both children of the node being compared prefetched | 4.74–4.94 s (A/B: 4.78–5.07 without) |
| a candidate's distance price computed once, not per length | no change measured |

The search effort (tree nodes per byte) hardly matters. Effort 24 is 0.2% larger than
48; efforts 96 and 192 are within 0.05% of it. The time is the same, because it goes
to cache misses, not steps. The default stays 48.

## Earlier steps (before this record was begun)

On the 11-book sample (10.75 MB of normalized text), each book coded alone:

| | coded |
|---|---|
| greedy coder, hash chains at depth 64 | 3,301,923 (decode 33 MB/s) |
| xz -9e | 3,003,880 |
| records packed | 3,271,909 |
| … and characters one byte each | 3,254,444 |
| … and words | 2,864,874 |
| … words with 32 two-byte and 1 three-byte prefix | 2,766,839 |
| … hash chains at depth 4096 | 2.675 M (xz -9e over the same packed text: 2.670 M) |
| … the binary tree at depth 32 | 2,640,126 (encode 2.5 s, against 15.7 s for chains at 1024) |
| … a space implied after every word code | 2,559,743 |
| … one-letter words, a word's worth `count × (2 × length − 1)` | 2,545,332 |
| … 8 one-byte characters, 32 two-byte prefixes, 1 three-byte | 2,538,928 |

Other settings measured then:
- Range coder probabilities moving a 16th or a 64th of the way: both worse than a 32nd.
- Parse spans of 512 to 8192: flat.
- A branchless bit decode: 31 → 36 MB/s.

Rejected:
- **A context-mixing coder** (`text_coder.hpp`, kept as a reference): 2.43 MB on the
  packed sample, about 9% under the LZ coder, but it decodes at 0.17 MB/s. A large book
  would take most of a minute to open.
- **Mixing two literal models inside the LZ coder**: 0.3% smaller, and decoding fell to
  4 MB/s.
- **A primer, for zstd**: 5.6% smaller. The base does the same work, better, in our own
  coder.

## Not done, and why

- **Literals as two 4-bit symbols, slots as one 64-way symbol** (multi-symbol adaptive
  coding, as LZNA does). This could make decoding perhaps 15–30% faster. It means a new
  coder and new pricing. Books already open in milliseconds, and the base is decoded off
  the main thread, so it waits until something needs it.
- **Reduced-offset LZ** (distances as an index among the earlier places sharing a
  context). This would shrink the raw distance bits, a third of the code, at the cost of
  a table update per decoded byte. The file is at 39% of its budget, so it is not
  needed now.
- **The tables learned anew with every library change**. Any change to any book changes
  every packed text, so a build re-codes everything (44 s on the M4). Fixed tables,
  learned once and extended, would let the cache serve unchanged books. Worth doing when
  books are added in batches.

## Reproducing

The shipped file is not kept in Git: every rebuild would stay in the history for good.
`tools/publish_library.py` publishes it to the repository's `library-data` release, named
by its content, and pins it in `pinned_assets.json`; builds fetch and check it
(`tools/pinned_assets.py`, run by the asset preparation) and ship it inside PlaySuite.
Commit the pin, never the file.

```sh
# the file, from normalized folders (the shipped one: only .build/library/normalized)
make_library --cache <dir> [--base-mb 16] [--effort 48] [--trial] <out.plb> <dir>...
# publish it and pin it (then commit pinned_assets.json)
python3 tools/publish_library.py assets/books/library.plb
# reading speed, per step
read_lab <file.plb> [repeats]
prepare_lab <file.plb>
# the coder alone, after a base
base_lab <folder of packed books> <base file> <effort>
# the packer alone
pack_lab <out folder> <folder>...
```

The tools build with the game's sources alone (`tools/*.cpp` plus `src/book.cpp`,
`container.cpp`, `lz_coder.cpp`, `text_pack.cpp` and `sha256.cpp`), with no GUI.Forms.
