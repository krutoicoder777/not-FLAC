# not-FLAC

A lossless audio compressor written in C.

This is an **educational project** inspired by the core ideas of the FLAC format: audio prediction followed by entropy coding. It does **not** use FLAC's algorithms internally; instead it implements its own modular pipeline with selectable predictors and entropy coders.

## How it works

The current pipeline consists of two main stages:

1. **Prediction** (selectable):
   - **Delta Encoding**: each sample is replaced by its difference from the previous one.
   - **LPC (Linear Predictive Coding)**: each block of samples is predicted from a linear combination of the previous `order` samples. Coefficients are computed via autocorrelation + Levinson-Durbin, then quantized to integers. The LPC `order` is chosen based on the file's sample rate (`8` for ≤24 kHz, `12` for ≤48 kHz, `16` for >48 kHz) — the same heuristic FLAC uses.
   - In both cases the predictor output is ZigZag-encoded before entropy coding.

2. **Entropy Coding** (selectable):
   - **Arithmetic Range Coder** with adaptive models — better compression, slower.
   - **Golomb-Rice Coder** — 1.5–2× faster with a small size overhead. Each block is split into `2^p` partitions, with one Rice parameter `k[i]` per partition. Both `p` and the `k` array are chosen per block by exhaustive search over the estimated bit cost.

For **stereo** files, each block is additionally passed through one of four **stereo decorrelation modes** (independent / left-side / right-side / mid-side). The mode is picked per block by comparing the LPC residual cost of each candidate. This is the same family of modes FLAC uses; on the current test files it usually picks `independent` (wide stereo image), but the machinery is there.

The block size is chosen as `8192 × 2 × channels` (16384 samples for mono, 32768 for stereo), which empirically gives the best ratio/speed trade-off on the test files. The architecture is modular: `audio_encoder` and `audio_decoder` accept both predictor and entropy method as parameters.

## Features

- Pure C11, no external runtime dependencies.
- Streaming WAV parser (supports 16/24/32-bit PCM, mono/stereo).
- Custom `.notflac` container format with an on-disk **seek table**.
- CLI for compression/decompression with selectable predictor and entropy coder.
- Streaming player (pipes raw PCM to stdout) with **seek by seconds** (`start_sec`, `end_sec`).
- Automated round-trip benchmarks.

## Benchmarks

Comparison against reference lossless codecs. All times measured on WSL2 (single core), `-O3 -march=native`. Lower ratio = better.

**`wav.wav` — 16-bit / 16 kHz / mono (320 KB, music)**

| Codec | Original | Compressed | Ratio | Time |
|---|---:|---:|---:|---:|
| flac | 320078 | 120962 | 37.79% | 0.021 s |
| wavpack | 320078 | 139846 | 43.69% | 0.028 s |
| alac | 320078 | 168489 | 52.63% | 0.182 s |
| notflac LPC+Rice | 320078 | 184604 | 57.67% | 0.030 s |
| notflac LPC+Range | 320078 | 186760 | 58.34% | 0.038 s |
| notflac Delta+Rice | 320078 | 238408 | 74.48% | 0.030 s |
| notflac Delta+Range | 320078 | 240017 | 74.98% | 0.051 s |

**`16bit-44.1kHz-mono.wav` — 16-bit / 44.1 kHz / mono (158 MB, speech)**

| Codec | Original | Compressed | Ratio | Time |
|---|---:|---:|---:|---:|
| flac | 158598386 | 60440009 | 38.10% | 5.10 s |
| wavpack | 158598386 | 65872640 | 41.53% | 8.18 s |
| alac | 158598386 | 71386487 | 45.01% | 15.98 s |
| notflac LPC+Rice | 158598386 | 78639101 | 49.58% | 10.93 s |
| notflac LPC+Range | 158598386 | 82568961 | 52.06% | 13.54 s |
| notflac Delta+Rice | 158598386 | 99383196 | 62.66% | 10.35 s |
| notflac Delta+Range | 158598386 | 104627592 | 65.97% | 15.30 s |

**`24bit-96khz.wav` — 24-bit / 96 kHz / stereo (17 MB, music)**

| Codec | Original | Compressed | Ratio | Time |
|---|---:|---:|---:|---:|
| flac | 17280316 | 7750479 | 44.85% | 0.90 s |
| wavpack | 17280316 | 8059396 | 46.63% | 0.51 s |
| alac | 17280316 | 8400415 | 48.61% | 1.63 s |
| notflac LPC+Rice | 17280316 | 8205307 | 47.48% | 1.06 s |
| notflac LPC+Range | 17280316 | 8114331 | 46.95% | 1.49 s |
| notflac Delta+Rice | 17280316 | 10139600 | 58.67% | 1.05 s |
| notflac Delta+Range | 17280316 | 10111431 | 58.51% | 1.85 s |

**`stereo.wav` — 24-bit / 96 kHz / stereo (25 MB, music)**

| Codec | Original | Compressed | Ratio | Time |
|---|---:|---:|---:|---:|
| flac | 25534598 | 10442541 | 40.89% | 1.50 s |
| wavpack | 25534598 | 10963874 | 42.93% | 1.07 s |
| alac | 25534598 | 12115946 | 47.44% | 2.25 s |
| notflac LPC+Rice | 25534598 | 12295453 | 48.15% | 2.15 s |
| notflac LPC+Range | 25534598 | 12368346 | 48.43% | 2.32 s |
| notflac Delta+Rice | 25534598 | 15765927 | 61.74% | 2.06 s |
| notflac Delta+Range | 25534598 | 15921836 | 62.35% | 2.70 s |

**Observations:**
- **Best result:** `24bit-96khz.wav` at **47.48%** — within **2.6 pp of FLAC**, and ahead of ALAC (48.61%). Closest to the reference codecs.
- **Worst result:** `wav.wav` at **57.67%** — short file, header/seek-table overhead dominates, and no stereo/mid-side tricks apply.
- **Mid/side on stereo:** the 4-mode stereo selection was implemented, but on the current test files the LPC cost almost always picks `independent` — so the visible gain is small (~0.1–0.4 pp). The machinery is in place for files where mid/side actually wins.

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

# LPC + Range (slower, sometimes performs better compression)
./build/notflac compress input.wav output.notflac 1 0

# Delta + Rice (fastest, worst compression)
./build/notflac compress input.wav output.notflac 0 1

# Delta + Range (worst mode)
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
| 4      | 1    | `version`          | currently `7`                                     |
| 5      | 1    | `entropy_method`   | `0` = Range Coder, `1` = Rice Coder               |
| 6      | 1    | `predictor_method` | `0` = Delta, `1` = LPC                            |
| 7      | 1    | `reserved`         | `0`                                               |
| 8      | 4    | `sample_rate`      | Hz, e.g. 44100                                    |
| 12     | 2    | `channels`         | 1 = mono, 2 = stereo                              |
| 14     | 2    | `bits_per_sample`  | 16, 24 or 32                                      |
| 16     | 4    | `num_samples`      | total sample count (per all channels)             |
| 20     | 4    | `block_size`       | samples per block, e.g. 16384 or 32768            |
| 24     | 8    | `table_offset`     | absolute offset of the seek table                 |
| 32     | 4    | `order`            | LPC order (`8`/`12`/`16`, by sample rate) — **only if `predictor_method == 1`** |

`num_blocks = ceil(num_samples / block_size)` — the value is derived, not stored.

### Body

For each block, in file order, one **independent entropy stream** is emitted (the coder is created, flushed and destroyed per block). Blocks are self-contained, which is what makes O(1) seeking possible.

**Per-block stereo mode (stereo only):** the first byte of every block is a `stereo_mode` flag:

- `0` = independent (L/R)
- `1` = left-side (L, L−R)
- `2` = right-side (R, L−R)
- `3` = mid-side (M, S) with `M = (L+R)>>1`, `S = L−R`

After the flag, the block contains **two independent entropy streams** (primary and secondary), each self-contained. The decoder reads the flag, decodes both streams, then applies the inverse stereo transform to produce interleaved L/R. For mono files, the flag is always `0` and there is a single stream.

**Delta mode (`predictor_method == 0`):**
- With **Range** (`entropy_method == 0`): each residual `zigzag(sample - prev_sample)` is written through the per-byte adaptive models.
- With **Rice** (`entropy_method == 1`): each stream starts with a 1-byte `p` (partition order), followed by `1<<p` bytes of `k[i]`, then all residuals coded per partition as `unary(q) + k[i] bits(r)`, where `z = zigzag(sample - prev_sample)`, `q = z >> k[i]`, `r = z & ((1 << k[i]) - 1)`.
- `prev_sample` is reset to `0` at the start of every stream.

**LPC mode (`predictor_method == 1`):** each stream contains, in order:
1. `precision` — right-shift applied to quantized LPC coefficients.
2. `quotients[order]` — quantized coefficients.
3. For Rice only: `p` (1 byte) + `k[1<<p]` (1 byte each).
4. `residuals[n]` — per-sample residuals.

`precision` and `quotients` go through the Range models when `entropy_method == 0`, and are written raw (1 + `4 × order` bytes) when `entropy_method == 1`. The first `order` residuals of every stream are the **raw samples** (no history yet). History does not carry across stream or block boundaries; every block is decoded independently.

> **Note on LPC precision:** the encoder currently uses a fixed heuristic (`S = 14 − ceil(log2(max|a|))`, clamped to `[0, 15]`). A search loop over `S_base ± 1` was implemented and gave ~0.1–0.4 pp better ratio, but it was ~1.5–2× slower on LPC-heavy files, so it is disabled (`dS = 0`). The loop is left in the code behind a constant for future re-enabling.

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
- [X] **Adaptive LPC order** based on sample rate (8/12/16)
- [X] **Adaptive block size** based on channel count (`8192 × 2 × channels`)
- [X] **Mid/Side stereo** decorrelation (4 modes, chosen per block by LPC cost)
- [ ] **Verbatim mode** for noisy partitions (write raw when Rice is worse)
- [ ] **Adaptive LPC order per block** (AIC / residual energy)
- [ ] **Persistent entropy models**: stop resetting adaptive models at every block boundary (Range only, incompatible with O(1) seek)
- [ ] Optional: simple cross-platform GUI in Python (PySide6 or Tkinter) wrapping the CLI

## Notes

- The initial CMake boilerplate and the tests were generated with the help of an LLM (DeepSeek-V3). All core algorithms (Range Coder, Rice Coder, WAV parser, Delta+ZigZag, LPC with autocorrelation and Levinson-Durbin, stereo decorrelation, audio encoder/decoder) were written by me.
- The `.notflac` format is **little-endian**. Files are portable across x86-64 and ARM64 (both little-endian), but not to big-endian systems. The WAV parser uses `long` for file offsets, so WAV files larger than 2 GB may not open on 32-bit systems. For typical desktop and mobile platforms this is a non-issue.

## Updates

* **v0.2** — LPC predictor (autocorrelation, Levinson-Durbin, coefficient quantization, block-wise encoding) improves compression over Delta by 20–37% on test files. Also adds **seek support**: on-disk seek table, per-block entropy streams, and `notflac_player <file> [start_sec] [end_sec]` for O(1) random access.
* **v0.3** — **Rice coder** added as a second entropy method, selectable from the CLI (`0` = Range, `1` = Rice). Rice is 1.5–2× faster than Range at the cost of 1–3 pp in ratio. File format version bumped to `3`; the block header now starts with a 1-byte Rice parameter `k` when `entropy_method == 1`.
* **v0.4** — **Partitioned Rice** (each block is split into `2^p` partitions, one Rice parameter per partition, with `p` chosen by bit-cost search) closes the gap with the Range coder and even beats it on the large file. **Adaptive LPC order** (8 for ≤24 kHz, 12 for ≤48 kHz, 16 for >48 kHz) cuts encoding time by 14–29%. File format version bumped to `4`.
* **v0.5** — **Adaptive block size**: `block_size = 8192 × 2 × channels` (16384 for mono, 32768 for stereo), improving compression by 0.7–3.4 pp across all test files. File format version bumped to `5`.
* **v0.6** — **Stereo decorrelation** (independent / left-side / right-side / mid-side, chosen per block by LPC residual cost) and **`-O3 -march=native`** in the Release build. On the current test files mid/side only yields ~0.1–0.4 pp (the cost search usually picks `independent` because the stereo image is wide), but the machinery is in place for files where it matters. An experimental LPC precision search (`S_base ± 1`) gave a further ~0.1–0.4 pp but was ~1.5–2× slower on LPC-heavy files, so it is currently disabled (`dS = 0`) and left in the code for later. File format version bumped to `6`.
* **v0.7** added verbatim for better compression of noisy files and files with sound attacks.
