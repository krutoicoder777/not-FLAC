# not-FLAC

A lossless audio compressor written in C.

This is an **educational project** inspired by the core ideas of the FLAC format: audio prediction followed by entropy coding. It does **not** use FLAC's algorithms internally; instead it implements its own modular pipeline with selectable predictors and entropy coders.

## How it works

The current pipeline consists of two main stages:

1. **Prediction** (selectable):
   - **Delta Encoding**: each sample is replaced by its difference from the previous one.
   - **LPC (Linear Predictive Coding)**: each block of samples is predicted from a linear combination of the previous `ORDER` samples. Coefficients are computed via autocorrelation + Levinson-Durbin, then quantized to integers.
   - In both cases the predictor output is ZigZag-encoded before entropy coding.

2. **Entropy Coding** (selectable):
   - **Arithmetic Range Coder** with adaptive models — better compression, slower.
   - **Golomb-Rice Coder** — 1.5–2× faster with a small size overhead. The Rice parameters `p` and `k[1<<p]` are chosen per block by exhaustive search over the bit cost.

The architecture is modular: `audio_encoder` and `audio_decoder` accept both predictor and entropy method as parameters, so any combination of `{Delta, LPC} × {Range, Rice}` can be selected from the CLI.

## Features

- Pure C11, no external runtime dependencies.
- Streaming WAV parser (supports 16/24/32-bit PCM, mono/stereo).
- Custom `.notflac` container format with an on-disk **seek table**.
- CLI for compression/decompression with selectable predictor and entropy coder.
- Streaming player (pipes raw PCM to stdout) with **seek by seconds** (`start_sec`, `end_sec`).
- Automated round-trip benchmarks.

## Benchmarks

Comparison against reference lossless codecs. All times measured on WSL2 (single core). Lower ratio = better.

**`wav.wav` — 16-bit / 16 kHz / mono (320 KB)**

| Codec | Original | Compressed | Ratio | Time |
|---|---:|---:|---:|---:|
| flac | 320078 | 120962 | 37.79% | 0.033 s |
| wavpack | 320078 | 139846 | 43.69% | 0.026 s |
| alac | 320078 | 168489 | 52.63% | 0.173 s |
| notflac LPC+Rice | 320078 | 187918 | 58.71% | 0.038 s |
| notflac LPC+Range | 320078 | 186800 | 58.36% | 0.041 s |
| notflac Delta+Rice | 320078 | 238722 | 74.58% | 0.032 s |
| notflac Delta+Range | 320078 | 240241 | 75.05% | 0.051 s |

**`16bit-44.1kHz-mono.wav` — 16-bit / 44.1 kHz / mono (158 MB)**

| Codec | Original | Compressed | Ratio | Time |
|---|---:|---:|---:|---:|
| flac | 158598386 | 60440009 | 38.10% | 5.51 s |
| wavpack | 158598386 | 65872640 | 41.53% | 6.18 s |
| alac | 158598386 | 71386487 | 45.01% | 16.41 s |
| notflac LPC+Rice | 158598386 | 81562129 | 51.42% | 14.92 s |
| notflac LPC+Range | 158598386 | 83375613 | 52.57% | 15.97 s |
| notflac Delta+Rice | 158598386 | 98486778 | 62.09% | 11.59 s |
| notflac Delta+Range | 158598386 | 102579338 | 64.67% | 15.72 s |

**`24bit-96khz.wav` — 24-bit / 96 kHz / stereo (17 MB)**

| Codec | Original | Compressed | Ratio | Time |
|---|---:|---:|---:|---:|
| flac | 17280316 | 7750479 | 44.85% | 0.93 s |
| wavpack | 17280316 | 8059396 | 46.63% | 0.55 s |
| alac | 17280316 | 8400415 | 48.61% | 1.65 s |
| notflac LPC+Rice | 17280316 | 8844112 | 51.18% | 1.21 s |
| notflac LPC+Range | 17280316 | 8731002 | 50.52% | 1.49 s |
| notflac Delta+Rice | 17280316 | 13593476 | 78.66% | 1.01 s |
| notflac Delta+Range | 17280316 | 13630419 | 78.87% | 2.11 s |

**Observations:**
- **Rice is 1.5–2× faster than Range** on every file, at the cost of 1–3 pp in compression ratio. That is the same trade-off FLAC makes, and it is the main reason Rice is the default entropy coder.
- **Partitioned Rice** closed most of the gap with the Range coder: on the large 16-bit file, LPC+Rice is now **1.2 pp better** than LPC+Range (51.42% vs 52.57%), while still being faster.
- On `24bit-96khz.wav`, LPC+Rice trails FLAC by **6.3 pp** in ratio (51.18% vs 44.85%) and is somewhat slower (1.21 s vs 0.93 s).
- On `16bit-44.1kHz-mono.wav`, LPC+Rice trails FLAC by **13.3 pp** (51.42% vs 38.10%) and is ~2.7× slower. The gap is mostly due to **fixed LPC order** — FLAC adapts the order per block. Adaptive order is on the roadmap and expected to close a large part of this gap.
- **Delta is consistently worse than LPC** on all files; LPC is the default predictor.

### LPC vs Delta (internal comparison, LPC+Rice)

| File | Delta+Rice ratio | LPC+Rice ratio | LPC gain |
|------|-----------------:|---------------:|---------:|
| `wav.wav` | 74.94% | **60.14%** | **14.8% smaller** |
| `16bit-44.1kHz-mono.wav` | 66.07% | **54.90%** | **11.2% smaller** |
| `24bit-96khz.wav` | 79.37% | **53.15%** | **26.2% smaller** |

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
# Compress WAV to notflac (default: LPC + Rice)
./build/notflac compress input.wav output.notflac

# Explicit LPC + Rice (same as default)
./build/notflac compress input.wav output.notflac 1 1

# LPC + Range (slower, slightly better compression)
./build/notflac compress input.wav output.notflac 1 0

# Delta + Rice (fastest, worst compression)
./build/notflac compress input.wav output.notflac 0 1

# Delta + Range
./build/notflac compress input.wav output.notflac 0 0

# Decompress notflac to WAV
./build/notflac decompress output.notflac restored.wav
```

Arguments after the output path:
- `predictor`: `0` = Delta, `1` = LPC (**default: 1**).
- `entropy`: `0` = Range Coder, `1` = Rice Coder (**default: 1**).

### Tests

```bash
./build/test_notflac_roundtrip
```

The benchmark scans `tests/data/`, compresses/decompresses every `.wav` for all four `{predictor} × {entropy}` combinations, prints size and time, and verifies a bit-exact round-trip. Drop your own `.wav` files into `tests/data/` to include them.

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

Because the output is **raw PCM**, the downstream tool (e.g. `aplay`, `paplay`, `sox`) must be told the exact format (sample rate, channel count, bit depth). `notflac_player` prints these on startup to **stderr**, so you can read them and pick the right flags.

Player launch examples:

```bash
# 16-bit mono 16 kHz
./build/notflac_player out.notflac | paplay --raw --format=s16le --rate=16000 --channels=1

# 24-bit stereo 96 kHz
./build/notflac_player out.notflac | paplay --raw --format=s24_3le --rate=96000 --channels=2
```

## File format: `.notflac`

All multi-byte integers are **little-endian**.

### Header (32 bytes, +4 when LPC)

| Offset | Size | Field              | Notes                                             |
|--------|------|--------------------|---------------------------------------------------|
| 0      | 4    | `magic`            | `0x4E464C41` (`"NFLA"`)                           |
| 4      | 1    | `version`          | currently `3`                                     |
| 5      | 1    | `entropy_method`   | `0` = Range Coder, `1` = Rice Coder               |
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

For each block, in file order, one **independent entropy stream** is emitted (the coder is created, flushed and destroyed per block). Blocks are self-contained, which is what makes O(1) seeking possible.

**Delta mode (`predictor_method == 0`):**
- With **Range** (`entropy_method == 0`): each residual `zigzag(sample - prev_sample)` is written through the per-byte adaptive models.
- With **Rice** (`entropy_method == 1`): the block starts with a 1-byte Rice parameter `p`, followed by `1<<p` `k` parameters and all residuals coded as `unary(q) + k bits(r)`, where `z = zigzag(sample - prev_sample)`, `q = z >> k`, `r = z & ((1 << k) - 1)`.
- `prev_sample` is reset to `0` at the start of every block.

**LPC mode (`predictor_method == 1`):** each block contains, in order:
1. `precision` — right-shift applied to quantized LPC coefficients.
2. `quotients[order]` — quantized coefficients.
3. For Rice only: `k` — 1-byte Rice parameter.
4. `residuals[block_size]` — per-sample residuals.

`precision` and `quotients` go through the Range models when `entropy_method == 0`, and are written raw (1 + `4 × order` bytes) when `entropy_method == 1`. The first `order` residuals of every block are the **raw samples** (no history yet). History does not carry across block boundaries; every block is decoded independently.

### Seek table

Located at `table_offset`, immediately after the body. One entry per block, each **12 bytes**:

| Size | Field          | Notes                                                    |
|------|----------------|----------------------------------------------------------|
| 8    | `block_offset` | absolute file offset of the block's entropy stream        |
| 4    | `block_size`   | samples in this block (last block may be < header's `block_size`) |

With the seek table, `notflac_player` can jump straight to any block: `fseek(block_offset[i])`, create a fresh decoder, decode. No need to decode earlier blocks.

## Plans

- [X] Add **LPC**
- [X] **Seek support**: on-disk seek table + `notflac_player <file> [start_sec] [end_sec]`
- [X] **Architecture refactoring**: unify predictors under a single `predictor_t` interface and entropy coders under a single `entropy_coder_t` interface
- [X] Add **Rice Coding** for faster entropy coding
- [X] **Partitioned Rice**: split each block into `2^p` partitions, one `k` per partition
- [ ] Enhance **LPC** with adaptive order
- [ ] **Mid/Side stereo** decorrelation
- [ ] **Persistent entropy models**: stop resetting adaptive models at every block boundary (Range only, incompatible with O(1) seek)
- [ ] Optional: simple cross-platform GUI in Python (PySide6 or Tkinter) wrapping the CLI

## Notes

- The initial CMake boilerplate and the tests were generated with the help of an LLM (DeepSeek-V3). All core algorithms (Range Coder, Rice Coder, WAV parser, Delta+ZigZag, LPC with autocorrelation and Levinson-Durbin, audio encoder/decoder) were written by me.
- The `.notflac` format is **little-endian**. Files are portable across x86-64 and ARM64 (both little-endian), but not to big-endian systems. The WAV parser uses `long` for file offsets, so WAV files larger than 2 GB may not open on 32-bit systems. For typical desktop and mobile platforms this is a non-issue.

## Updates

* **v0.2** — LPC predictor (autocorrelation, Levinson-Durbin, coefficient quantization, block-wise encoding) improves compression over Delta by 20–37% on test files. Also adds **seek support**: on-disk seek table, per-block entropy streams, and `notflac_player <file> [start_sec] [end_sec]` for O(1) random access.
* **v0.3** — **Rice coder** added as a second entropy method, selectable from the CLI (`0` = Range, `1` = Rice). Rice is 1.5–2× faster than Range at the cost of 1–3 pp in ratio. File format version bumped to `3`; the block header now starts with a 1-byte Rice parameter `k` when `entropy_method == 1`.
