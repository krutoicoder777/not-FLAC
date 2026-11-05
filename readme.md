# not-FLAC

A lossless audio compressor written in C.

This is an **educational project** inspired by the core ideas of the FLAC format: audio prediction followed by entropy coding. It does **not** use FLAC's algorithms internally; instead it implements its own modular pipeline with selectable predictors and entropy coders.

## How it works

The current pipeline consists of two main stages:

1. **Prediction** (selectable):
   - **Delta Encoding**: each sample is replaced by its difference from the previous one.
   - **LPC (Linear Predictive Coding)**: each block of samples is predicted from a linear combination of the previous `ORDER` samples. Coefficients are computed via autocorrelation + Levinson-Durbin, then quantized to integers.
   - In both cases the predictor output is ZigZag-encoded before entropy coding.

2. **Entropy Coding** (currently only one option):
   - **Arithmetic Range Coder** with adaptive byte models (for Delta) or slot+bits models (for LPC coefficients and residuals).

The architecture is designed to be extensible: `audio_encoder` and `audio_decoder` accept parameters for predictor and entropy method. Future plans include a Rice coder and a unified coder interface.

## Features

- Pure C11, no external runtime dependencies.
- Streaming WAV parser (supports 16/24/32-bit PCM, mono/stereo).
- Custom `.notflac` container format with an on-disk **seek table**.
- CLI for compression/decompression.
- Streaming player (pipes raw PCM to stdout) with **seek by seconds** (`start_sec`, `end_sec`).
- Automated round-trip benchmarks.

## Benchmarks

Comparison against reference lossless codecs on two test files: `wav.wav` (16-bit / 16 kHz / mono) and `24bit-96khz.wav` (24-bit / 96 kHz / stereo). All times measured on WSL2 (single core). Lower ratio = better.

```
24bit-96khz.wav                  flac       17280316    7750479   44.85% .601007536s
24bit-96khz.wav                  wavpack    17280316    8059396   46.63% .342791494s
24bit-96khz.wav                  alac       17280316    8400415   48.61% 1.471391168s
24bit-96khz.wav                  notflac    17280316    8731002   50.52% .951504023s

wav.wav                          flac         320078     120962   37.79% .019811397s
wav.wav                          wavpack      320078     139846   43.69% .017895038s
wav.wav                          alac         320078     168489   52.63% .132017954s
wav.wav                          notflac      320078     186800   58.36% .032965135s
```

**Observations:**
- On the 24-bit stereo file, not-FLAC is within ~6% of FLAC but trails ALAC by ~2%. WavPack and ALAC remain competitive.
- On the small 16-bit mono file, not-FLAC trails FLAC significantly — mostly due to fixed LPC order and range coder which is worse than Rice Coding. Both are on the roadmap.
- WavPack consistently outperforms not-FLAC on both files. FLAC remains the reference.

### LPC vs Delta (internal comparison)

| File | Delta ratio | LPC ratio | LPC gain |
|------|------------:|----------:|---------:|
| `wav.wav` | 75.1% | **58.4%** | **22.2% smaller** |
| `24bit-96khz.wav` | 78.9% | **50.5%** | **35.9% smaller** |

Switching from Delta to LPC cuts the output size by 22–36% on the same data.

## Build

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

> **Note:** tests use `<dirent.h>` (POSIX), so on Windows either build under WSL2 / MSYS2 (MinGW), or skip the test targets.

## Usage

### CLI

```bash
# Compress WAV to notflac (default: LPC predictor)
./build/notflac compress input.wav output.notflac

# Explicit LPC (predictor=1, same as default)
./build/notflac compress input.wav output.notflac 1

# Delta predictor (predictor=0)
./build/notflac compress input.wav output.notflac 0

# Decompress notflac to WAV
./build/notflac decompress output.notflac restored.wav
```

Arguments after the output path:
- `predictor`: `0` = Delta, `1` = LPC (**default: 1**).
- `entropy`: `0` = Range Coder (**default: 0**, only supported value).

### Tests

```bash
./build/test_notflac_roundtrip
```

The benchmark scans `tests/data/`, compresses/decompresses every `.wav`, prints size and time for both predictors, and verifies a bit-exact LPC round-trip. Drop your own `.wav` files into `tests/data/` to include them.

### Streaming Player

```
notflac_player <file.notflac> [start_sec] [end_sec]
```

Reads the compressed file, **seeks directly to `start_sec`** using the on-disk seek table (O(1) — no need to decode from the start), and writes **raw PCM** to stdout. `start_sec` and `end_sec` are optional floats in seconds; omit `end_sec` to play to the end.

Examples:

```bash
# whole file
./build/notflac_player out.notflac | <player>

# from 3s to the end
./build/notflac_player out.notflac 3 | <player>

# 3s to 5s
./build/notflac_player out.notflac 3 5 | <player>
```

Because the output is **raw PCM**, the downstream tool (e.g. aplay, paplay, etc.) must be told the exact format (sample rate, channel count, bit depth). `notflac_player` prints these on startup to **stderr**, so you can read them and pick the right flags.

player launch example:
```bash
#16 bit mono 16 kHz
./build/notflac_player out.notflac | paplay --raw --format=s16le --rate=16000 --channels=1

#24 bit stereo 96 kHz
./build/notflac_player out.notflac | paplay --raw --format=s24_3le --rate=96000 --channels=2
```

## File format: `.notflac`

All multi-byte integers are **little-endian**.

### Header (32 bytes, +4 when LPC)

| Offset | Size | Field              | Notes                                             |
|--------|------|--------------------|---------------------------------------------------|
| 0      | 4    | `magic`            | `0x4E464C41` (`"NFLA"`)                           |
| 4      | 1    | `version`          | currently `2`                                     |
| 5      | 1    | `entropy_method`   | `0` = Range Coder                                 |
| 6      | 1    | `predictor_method` | `0` = Delta, `1` = LPC                            |
| 7      | 1    | `reserved`         | `0`                                               |
| 8      | 4    | `sample_rate`      | Hz, e.g. 44100                                    |
| 12     | 2    | `channels`         | 1 = mono, 2 = stereo                              |
| 14     | 2    | `bits_per_sample`  | 16, 24 or 32                                      |
| 16     | 4    | `num_samples`      | total sample count (per all channels)             |
| 20     | 4    | `block_size`       | samples per block, e.g. 4096                      |
| 24     | 8    | `table_offset`     | absolute offset of the seek table                 |
| 32     | 4    | `order`            | LPC order — **only if `predictor_method == 1`**   |

`num_blocks = ceil(num_samples / block_size)` — the value is derived, not stored.

### Body

For each block, in file order, one **independent Range Coder stream** is emitted (the coder is created, flushed and destroyed per block). Blocks are self-contained, which is what makes O(1) seeking possible.

**Delta mode (`predictor_method == 0`):** for each of the block's samples, the encoder writes `zigzag(sample - prev_sample)` using the per-byte adaptive models. `prev_sample` is reset to `0` at the start of every block.

**LPC mode (`predictor_method == 1`):** each block's stream contains, in order:
1. `precision` — right-shift applied to quantized LPC coefficients.
2. `quotients[order]` — quantized coefficients.
3. `residuals[block_size]` — per-sample residuals.

The first `order` residuals of every block are the **raw samples** (no history yet). History does not carry across block boundaries; every block is decoded independently.

### Seek table

Located at `table_offset`, immediately after the body. One entry per block, each **12 bytes**:

| Size | Field          | Notes                                                    |
|------|----------------|----------------------------------------------------------|
| 8    | `block_offset` | absolute file offset of the block's Range Coder stream    |
| 4    | `block_size`   | samples in this block (last block may be < header's `block_size`) |

With the seek table, `notflac_player` can jump straight to any block: `fseek(block_offset[i])`, create a fresh decoder, decode. No need to decode earlier blocks.

## Plans

- [X] Add **LPC**
- [X] **Seek support**: on-disk seek table + `notflac_player <file> [start_sec] [end_sec]`
- [X] **Architecture refactoring**: unify predictors under a single `predictor_t` interface (currently `encode_audio` branches on `predictor_method`), and unify entropy coders under a single `entropy_coder_t` interface so Range Coder and future Rice Coder are interchangeable.
- [ ] **Persistent entropy models**: stop resetting adaptive models at every block boundary; add a reset interval so models warm up once and stay warm (biggest single compression win, especially on short files). Requires decoding from the last reset point on seek.
- [ ] Add **Rice Coding** for faster entropy coding (comparable to FLAC's speed advantage).
- [ ] Enhance **LPC** with adaptive order.
- [ ] **Mid/Side stereo** decorrelation (5–10% win on stereo).
- [ ] Optional: simple cross-platform GUI in Python (PySide6 or Tkinter) wrapping the CLI — planned as a thin frontend, not part of the C codebase.

## Notes

- The initial CMake boilerplate and the tests were generated with the help of an LLM (DeepSeek-V3). All core algorithms (Range Coder, WAV parser, Delta+ZigZag, LPC with autocorrelation and Levinson-Durbin, audio encoder/decoder) were written by me.

## Updates

* **v0.2** — LPC predictor (autocorrelation, Levinson-Durbin, coefficient quantization, block-wise encoding) improves compression over Delta by **20–37%** on test files. Also adds **seek support**: on-disk seek table, per-block Range Coder streams, and `notflac_player <file> [start_sec] [end_sec]` for O(1) random access.
