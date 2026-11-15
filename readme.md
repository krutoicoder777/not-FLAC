# not-FLAC

> A lossless audio compressor written in C. FLAC-grade bitrate, faster encoding, custom container.

`not-FLAC` implements the same core idea as FLAC — **block-wise LPC prediction followed by Rice entropy coding**, with per-block stereo decorrelation and an on-disk seek table for O(1) random access. It is not a FLAC-compatible format and does not reuse FLAC code; the pipeline, the container, and the tooling are original.

The goal is **FLAC-grade compression at higher encoding speed**, at the cost of a slower decoder and no support for 32-bit audio, metadata, or non-WAV containers.

## How it works

Each block of PCM is encoded **independently** — no history carried across blocks, no persistent entropy state. That is what makes seeking O(1): the player computes the block index, seeks to the offset stored in the on-disk seek table, and decodes a single block.

The pipeline per block:

1. **Stereo decorrelation** (stereo only). One of four modes is chosen per block: independent, left-side, right-side, mid-side. The winner is picked by comparing residual cost under a cheap second-order fixed predictor.
2. **LPC prediction.** Each stream is modeled as a linear combination of the previous `order` samples. Autocorrelation is computed on Tukey-windowed input, then Levinson–Durbin produces coefficients. The actual order is chosen by AIC; coefficients are quantized with error feedback, and the precision is picked by a small Rice-cost search.
3. **Fixed-predictor fallback.** On noisy or very smooth blocks LPC can overfit, so the encoder also computes the second-order fixed residual and picks the cheaper of the two per stream.
4. **ZigZag.** Residuals are mapped to non-negative integers.
5. **Partitioned Rice coding.** Each stream is split into `2^p` partitions with a separate `k` parameter per partition. `p` and `k[]` are chosen by bit-cost search with early exit. A partition falls back to **verbatim** (raw samples) when Rice costs more than raw.

Block size and maximum LPC order depend on sample rate, channel count and bit depth.

## Features

- Pure C11, no runtime dependencies.
- Streaming WAV parser (8 / 16 / 24-bit PCM, mono / stereo). **32-bit WAV rejected at the header** — the residual pipeline uses `int32_t` throughout.
- Custom `.notflac` container with an on-disk seek table.
- CLI for compression / decompression.
- Streaming player with raw PCM output and sub-second seeking.
- Bit-exact round trip verified on 100 real music files and 13 synthetic test files.
- Release build: `-O3 -march=native -fopenmp-simd -funroll-loops -ftree-vectorize`.

## Benchmark summary

100 real music files (mixed genres) plus synthetic content. Reference codecs at their highest preset (`flac -8`, `wavpack -hh`, ALAC via `ffmpeg`), single core, `-O3 -march=native`.

### vs FLAC

| Metric | notflac | FLAC | Wins |
|---|---:|---:|---:|
| bitrate | 852 kbps | **827 kbps** | 21 / 100 |
| compress | **21.0 MB/s** | 20.2 MB/s | **61 / 100** |
| decompress | 45.7 MB/s | **73.7 MB/s** | 13 / 100 |

### vs WavPack

| Metric | notflac | WavPack | Wins |
|---|---:|---:|---:|
| bitrate | 852 kbps | **821 kbps** | 12 / 100 |
| compress | 21.0 MB/s | **32.8 MB/s** | 6 / 100 |
| decompress | **45.7 MB/s** | 34.6 MB/s | **89 / 100** |

### vs ALAC

| Metric | notflac | ALAC | Wins |
|---|---:|---:|---:|
| bitrate | **852 kbps** | 862 kbps | **88 / 100** |
| compress | **21.0 MB/s** | 11.6 MB/s | **98 / 100** |
| decompress | **45.7 MB/s** | 43.6 MB/s | **67 / 100** |

**Summary:**

- **vs FLAC** — ~3% behind on bitrate, roughly tied on compression, ~38% slower on decompression.
- **vs WavPack** — ~3.5% behind on bitrate, slower at compression, **~1.3× faster** at decompression.
- **vs ALAC** — **smaller files**, **1.8× faster** at compression, slightly faster at decompression. ALAC loses on every axis.

Per-file breakdowns for all 113 test files are attached at the bottom of this README.

## Version 1.0

Version 1.0 is the largest change the encoder has had: a major modeling improvement, a full optimization pass over the hot path, and a large reduction in code — half the point of 1.0 is what it no longer contains.

### Modeling

The single biggest improvement is **Tukey-windowed autocorrelation** in the LPC front-end. Previously the autocorrelation was computed on raw samples; now it uses the same Tukey window (α = 0.5) that FLAC uses, which substantially reduces spectral leakage at block boundaries. The effect on real music is large — v0.9 and v1.0 should be treated as different codecs.

Two related improvements ship with it:

- **Coefficient precision search** over `S_base − 1, S_base, S_base + 1`, chosen by actual Rice cost instead of a fixed heuristic.
- **Higher LPC ceilings**: up to 28 for 24-bit, 24 for 16-bit. AIC still picks the actual order per block.

### Format — version 10

- **VERBATIM safety margin** — verbatim partitions write raw residuals at `bits_per_sample + 5` bits (clamped to 32) instead of `+1`. The old `+1` silently truncated on **24-bit side channels** and broke bit-exactness on those files; `+5` covers the worst case with one bit of headroom.
- **8-bit WAV support** — verified lossless.
- **32-bit rejected at the header.** Honest limit, not a silent lossy path.
- **Rice partition header is bit-packed** — `p` and `k[]` are written through the bit queue rather than a byte per field.
- **Warm-up samples written raw** at the start of every LPC stream, outside the Rice coder, unless a Rice-coded delta is strictly cheaper.

### Removed in 1.0

- **Range coder — removed entirely.** The adaptive arithmetic coder was ~400 lines and delivered 1–3 pp better bitrate than Rice at 1.5–2× slower compression, and its persistent entropy state is incompatible with O(1) seeking. The extra couple of percentage points did not justify the cost.
- **Delta predictor — removed.** Mathematically identical to `fixed-2`, which remains as the LPC fallback.
- Various small abstractions that only supported the removed Range and Delta paths (`dz_processor`, `sp_cost_*`, `entropy_encode_byte`, `entropy_decode_byte`, `model_t`, `predictor_encoder_method`, `predictor_decoder_method`).

### Optimization pass

- **Rice**: local bit buffers, `getc_unlocked` / `putc_unlocked`, `k0 ± 1` search instead of `k0 ± 3`, early exit over partition order, 64-bit batched writes.
- **LPC**: 16-way unrolled predictor loops with dual accumulators; ring-buffer history in the decoder (no per-sample shift); early exit in Levinson–Durbin when residual energy plateaus.
- **I/O**: block-level `fread` / `fwrite` instead of per-sample; decoder fast path that skips `fseek` when the next block is already buffered.

### Final `.notflac` format

All multi-byte integers are little-endian.

**Header** — 32 bytes, +4 when the predictor is LPC:

| Offset | Size | Field | Notes |
|---|---:|---|---|
| 0 | 4 | `magic` | `"NFLA"` |
| 4 | 1 | `version` | `10` |
| 5 | 1 | `entropy_method` | always `1` (Rice) |
| 6 | 1 | `predictor_method` | always `1` (LPC) |
| 7 | 1 | `reserved` | `0` |
| 8 | 4 | `sample_rate` | Hz |
| 12 | 2 | `channels` | 1 = mono, 2 = stereo |
| 14 | 2 | `bits_per_sample` | 8, 16 or 24 |
| 16 | 4 | `num_samples` | total, all channels |
| 20 | 4 | `block_size` | samples per block |
| 24 | 8 | `table_offset` | absolute file offset of the seek table |
| 32 | 4 | `order` | max LPC order (LPC only) |

**Body** — one independent bit stream per block, in file order:

- (stereo only) 1-byte `stereo_mode` (0–3)
- stream A, stream B (stereo only)

Each stream starts with a 1-byte `predictor_type`: `0` = LPC, `3` = fixed-2 fallback. In v1.0 no other values are emitted.

- LPC streams: 1-byte `precision`, then `order × 2-byte` signed quotients.
- Warm-up (LPC only): 1-byte `warmup_mode` (`0` = raw, `1` = Rice-coded delta), followed by the warm-up samples.
- Rice stream: 5-bit partition order `p`, `2^p` × 5-bit `k[]` values (bit-packed), then residuals per partition as `unary(q) + k bits(r)`. Verbatim partitions write `zz` raw at `bits_per_sample + 5` bits.

**Seek table** — one 12-byte entry per block: 8-byte absolute `block_offset` and 4-byte `block_size`. Because each block is self-contained, the player can jump to any block without decoding earlier ones.

## Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

Requires GCC or Clang with C11. Uses `<pthread.h>` and `<dirent.h>`, so it builds under WSL2, MSYS2 (MinGW), or any POSIX system.

## Usage

### CLI

```bash
# compress
./build/notflac compress input.wav output.notflac

# decompress
./build/notflac decompress output.notflac restored.wav
```

### Tests

```bash
./build/test_notflac_roundtrip
```

Scans `tests/data/real/` and `tests/data/synth/`, compresses and decompresses every `.wav`, and verifies a **bit-exact** round trip sample by sample.

### Streaming player

```
./build/notflac_player <file.notflac> [start_sec] [end_sec]
```

Seeks directly to `start_sec` via the on-disk seek table, then writes raw PCM to stdout. Sample rate, channel count and bit depth are printed on stderr so you can pick the right flags for your backend:

```bash
# 16-bit mono 44.1 kHz
./build/notflac_player out.notflac | paplay --raw --format=s16le --rate=44100 --channels=1

# 24-bit stereo 96 kHz, seconds 12 through 18
./build/notflac_player out.notflac 12 18 | paplay --raw --format=s24_3le --rate=96000 --channels=2
```

## Plans

- [X] LPC predictor with autocorrelation + Levinson–Durbin
- [X] On-disk seek table and O(1) seeking player
- [X] Rice coding with partitioned streams and verbatim fallback
- [X] Per-block stereo decorrelation
- [X] Per-block fixed-predictor fallback
- [X] Precision search for LPC coefficients
- [X] Tukey-windowed autocorrelation
- [X] 8-bit WAV support
- [ ] **Multithreaded encoding.** Blocks are already independent — encoding them across cores is a format-neutral speedup on typical hardware.
- [ ] **Non-WAV containers.** AIFF, CAF, and raw PCM with explicit parameters.
- [ ] **Metadata support.** Title, artist, album, cover art, ReplayGain — stored in a `METADATA` block between the header and the body, ignored by the decoder.
- [ ] **GUI client** for compression / decompression.
- [ ] **VLC plugin** for playback of `.notflac` files.

## Notes

### Compatibility

- **Little-endian only.** Portable across x86-64 and ARM64, not to big-endian systems.
- **POSIX required.** Builds on Linux, macOS, WSL2 and MSYS2 (MinGW). Native MSVC is not supported — the test harness depends on `<dirent.h>` and the encoder uses `<pthread.h>`.
- **Containers.** Input is WAV only (8 / 16 / 24-bit PCM, mono or stereo). 32-bit WAV is rejected at the header — the residual pipeline uses `int32_t` throughout and cannot represent 32-bit samples losslessly.
- **File size.** File offsets use `long`. On 64-bit systems this is a non-issue; on 32-bit systems WAV files larger than 2 GB may fail to open.

### Test corpus

The 100 real music files are real recordings from open sources, covering a wide range of genres. The 13 synthetic WAVs in `tests/data/synth/` are small pathological files (silence, DC, noise, sines, clipped audio, 8-bit sine) written to exercise parser edge cases.

### Provenance

All core algorithms — Rice coder, WAV parser, LPC (autocorrelation + Levinson–Durbin), coefficient quantization, fixed predictors, stereo decorrelation, encoder / decoder, `.notflac` format and seek table — are hand-written, with no external compression libraries. The CMake boilerplate, the tests source files and some micro-optimizations were generated with LLM assistance, then reviewed and validated against the round-trip test.

## Full benchmark results

Per-file bitrate, compression speed and decompression speed for all 100 real music files and 13 synthetic test files, compared against `flac -8`, `wavpack -hh` and ALAC.

→ **See `benchmark_full.txt`** (attached separately).

## Updates

* **v0.2** — LPC predictor, seek support, on-disk seek table, streaming player.
* **v0.3** — Rice coder added alongside Range; CLI switch.
* **v0.4** — Partitioned Rice, adaptive LPC order.
* **v0.5** — Adaptive block size.
* **v0.6** — Stereo decorrelation (4 modes, chosen per block), `-O3 -march=native`.
* **v0.7** — Verbatim partitions.
* **v0.8** — ZigZag bug fix on high-delta signals.
* **v0.9** — Fixed predictors per block.
* **v1.0** — Tukey-windowed autocorrelation (major compression improvement, closes most of the gap to FLAC); coefficient precision search re-enabled; 8-bit WAV support; 32-bit rejected at the header; VERBATIM safety margin fixed; Rice partition header bit-packed; **Range coder and Delta predictor removed**; extensive micro-optimizations across Rice, LPC and I/O. Format version bumped to `10`.

## Full benchmark results

All measurements on WSL2, single core, `-O3 -march=native`. Reference codecs at their highest preset (`flac -8`, `wavpack -hh`, ALAC via `ffmpeg`).

### REAL MUSIC (100 files)

| File | Codec | Compressed | Ratio | Bitrate | Cmp MB/s | Dec MB/s |
|---|---|---:|---:|---:|---:|---:|
| 0.wav | flac | 1318442 | 34.33% | 527 k | 22.79 | 86.62 |
| 0.wav | wavpack | 1302868 | 33.92% | 521 k | 26.39 | 37.12 |
| 0.wav | alac | 1387869 | 36.14% | 555 k | 9.27 | 26.64 |
| 0.wav | **notflac** | **1315585** | **34.25%** | **526 k** | **25.19** | **56.77** |
| 1.wav | flac | 1880371 | 48.96% | 752 k | 24.88 | 96.32 |
| 1.wav | wavpack | 1828786 | 47.62% | 731 k | 28.13 | 36.99 |
| 1.wav | alac | 1949150 | 50.75% | 779 k | 10.65 | 25.57 |
| 1.wav | **notflac** | **1885683** | **49.10%** | **754 k** | **24.40** | **46.98** |
| 10.wav | flac | 118270842 | 43.39% | 612 k | 22.24 | 92.64 |
| 10.wav | wavpack | 117386922 | 43.06% | 607 k | 41.82 | 37.78 |
| 10.wav | alac | 131899789 | 48.39% | 682 k | 13.34 | 105.62 |
| 10.wav | **notflac** | **116629830** | **42.79%** | **603 k** | **19.92** | **43.83** |
| 100.wav | flac | 75991723 | 69.07% | 1591 k | 18.70 | 107.77 |
| 100.wav | wavpack | 75413456 | 68.55% | 1579 k | 248.77 | 47.46 |
| 100.wav | alac | 78033578 | 70.93% | 1634 k | 14.23 | 81.18 |
| 100.wav | **notflac** | **81123196** | **73.74%** | **1698 k** | **28.52** | **53.14** |
| 101.wav | flac | 80333303 | 70.85% | 1632 k | 20.58 | 100.08 |
| 101.wav | wavpack | 79785040 | 70.36% | 1621 k | 45.26 | 52.04 |
| 101.wav | alac | 82288037 | 72.57% | 1672 k | 21.40 | 86.45 |
| 101.wav | **notflac** | **89955196** | **79.33%** | **1827 k** | **33.18** | **62.65** |
| 102.wav | flac | 42191676 | 68.66% | 1582 k | 21.88 | 106.21 |
| 102.wav | wavpack | 41853728 | 68.11% | 1569 k | 45.83 | 55.68 |
| 102.wav | alac | 43361768 | 70.57% | 1625 k | 15.73 | 78.85 |
| 102.wav | **notflac** | **43942176** | **71.51%** | **1647 k** | **34.73** | **63.89** |
| 103.wav | flac | 82902213 | 67.45% | 1554 k | 44.39 | 106.57 |
| 103.wav | wavpack | 82132324 | 66.83% | 1539 k | 45.20 | 54.40 |
| 103.wav | alac | 84646816 | 68.87% | 1586 k | 16.06 | 82.70 |
| 103.wav | **notflac** | **86757756** | **70.59%** | **1626 k** | **35.35** | **66.34** |
| 104.wav | flac | 133979244 | 62.34% | 1436 k | 26.91 | 98.33 |
| 104.wav | wavpack | 133034798 | 61.90% | 1426 k | 75.05 | 54.41 |
| 104.wav | alac | 136284027 | 63.41% | 1461 k | 16.33 | 90.37 |
| 104.wav | **notflac** | **137682345** | **64.06%** | **1476 k** | **36.21** | **141.79** |
| 11.wav | flac | 22897122 | 44.48% | 627 k | 22.73 | 97.35 |
| 11.wav | wavpack | 22783886 | 44.26% | 624 k | 32.08 | 41.22 |
| 11.wav | alac | 25582501 | 49.70% | 701 k | 14.42 | 63.82 |
| 11.wav | **notflac** | **22504721** | **43.72%** | **617 k** | **23.20** | **57.68** |
| 12.wav | flac | 68572476 | 66.81% | 942 k | 22.63 | 90.33 |
| 12.wav | wavpack | 66806716 | 65.08% | 918 k | 31.01 | 38.11 |
| 12.wav | alac | 71718721 | 69.87% | 986 k | 19.06 | 58.96 |
| 12.wav | **notflac** | **70072757** | **68.27%** | **963 k** | **23.50** | **51.23** |
| 13.wav | flac | 72137030 | 66.72% | 941 k | 22.16 | 88.10 |
| 13.wav | wavpack | 70276984 | 65.00% | 917 k | 31.06 | 37.16 |
| 13.wav | alac | 75070519 | 69.44% | 979 k | 14.58 | — |
| 13.wav | **notflac** | **73330507** | **67.83%** | **957 k** | **23.96** | **53.29** |
| 14.wav | flac | 23388678 | 69.63% | 982 k | 22.26 | 95.02 |
| 14.wav | wavpack | 22693030 | 67.56% | 953 k | 30.81 | 40.59 |
| 14.wav | alac | 24300394 | 72.34% | 1020 k | 13.78 | 46.85 |
| 14.wav | **notflac** | **23727854** | **70.64%** | **996 k** | **21.79** | **50.27** |
| 15.wav | flac | 6748736 | 47.82% | 734 k | 21.62 | 115.38 |
| 15.wav | wavpack | 6713656 | 47.57% | 730 k | 28.60 | 39.26 |
| 15.wav | alac | 7208631 | 51.08% | 784 k | 13.37 | 40.72 |
| 15.wav | **notflac** | **6729476** | **47.68%** | **732 k** | **24.88** | **56.90** |
| 150.wav | flac | 1696923 | 28.29% | 399 k | 25.27 | 108.62 |
| 150.wav | wavpack | 1680656 | 28.02% | 395 k | 31.98 | 41.38 |
| 150.wav | alac | 1781986 | 29.71% | 419 k | 11.72 | 30.11 |
| 150.wav | **notflac** | **1728556** | **28.82%** | **406 k** | **28.05** | **59.99** |
| 151.wav | flac | 33594136 | 49.08% | 692 k | 23.23 | 68.30 |
| 151.wav | wavpack | 32958178 | 48.15% | 679 k | 32.48 | 37.15 |
| 151.wav | alac | 34559782 | 50.49% | 712 k | 24.07 | 54.44 |
| 151.wav | **notflac** | **33753423** | **49.31%** | **695 k** | **25.49** | **54.36** |
| 152.wav | flac | 39792708 | 49.68% | 701 k | 24.14 | 84.60 |
| 152.wav | wavpack | 39124766 | 48.85% | 689 k | 30.29 | 36.23 |
| 152.wav | alac | 40797030 | 50.94% | 718 k | 14.37 | 58.00 |
| 152.wav | **notflac** | **39990161** | **49.93%** | **704 k** | **25.28** | **51.26** |
| 153.wav | flac | 70536536 | 48.63% | 686 k | 33.33 | 82.22 |
| 153.wav | wavpack | 69614580 | 47.99% | 677 k | 31.57 | 36.84 |
| 153.wav | alac | 74660834 | 51.47% | 726 k | 14.30 | 60.30 |
| 153.wav | **notflac** | **71331909** | **49.18%** | **694 k** | **37.23** | **54.51** |
| 154.wav | flac | 64305534 | 45.85% | 647 k | 23.89 | 94.45 |
| 154.wav | wavpack | 63761310 | 45.46% | 641 k | 30.36 | 37.78 |
| 154.wav | alac | 68406600 | 48.78% | 688 k | 14.38 | 333.68 |
| 154.wav | **notflac** | **64946192** | **46.31%** | **653 k** | **25.77** | **56.56** |
| 155.wav | flac | 35217520 | 47.86% | 675 k | 23.71 | 102.88 |
| 155.wav | wavpack | 35000538 | 47.56% | 671 k | 31.70 | 38.92 |
| 155.wav | alac | 37433825 | 50.87% | 717 k | 14.10 | 57.56 |
| 155.wav | **notflac** | **35492283** | **48.23%** | **680 k** | **23.30** | **53.46** |
| 156.wav | flac | 5397136 | 37.04% | 522 k | 24.59 | 116.79 |
| 156.wav | wavpack | 5377926 | 36.90% | 520 k | 32.66 | 40.31 |
| 156.wav | alac | 5656927 | 38.82% | 547 k | 12.88 | 44.38 |
| 156.wav | **notflac** | **5415863** | **37.16%** | **524 k** | — | **60.11** |
| 157.wav | flac | 66189730 | 56.44% | 796 k | 23.07 | 97.51 |
| 157.wav | wavpack | 65600268 | 55.94% | 789 k | 31.54 | 33.08 |
| 157.wav | alac | 68837938 | 58.70% | 828 k | 14.09 | 53.23 |
| 157.wav | **notflac** | **66600637** | **56.79%** | **801 k** | **24.91** | **29.95** |
| 158.wav | flac | 30640207 | 52.75% | 744 k | 17.06 | 87.60 |
| 158.wav | wavpack | 30257044 | 52.09% | 735 k | 25.74 | 23.47 |
| 158.wav | alac | 31912800 | 54.94% | 775 k | 8.98 | 30.20 |
| 158.wav | **notflac** | **30851845** | **53.12%** | **749 k** | **16.44** | **30.24** |
| 159.wav | flac | 61381690 | 62.06% | 875 k | 23.09 | 52.04 |
| 159.wav | wavpack | 60721836 | 61.39% | 866 k | 23.31 | 26.31 |
| 159.wav | alac | 63955103 | 64.66% | 912 k | 8.75 | 31.78 |
| 159.wav | **notflac** | **61425815** | **62.11%** | **876 k** | **13.92** | **28.53** |
| 16.wav | flac | 28843100 | 56.79% | 872 k | 12.33 | 62.03 |
| 16.wav | wavpack | 28676272 | 56.46% | 867 k | 19.22 | 21.00 |
| 16.wav | alac | 30937716 | 60.91% | 935 k | 9.25 | 29.73 |
| 16.wav | **notflac** | **28819250** | **56.74%** | **871 k** | **12.57** | **30.73** |
| 160.wav | flac | 38082351 | 59.23% | 835 k | 13.92 | 62.32 |
| 160.wav | wavpack | 37638710 | 58.54% | 826 k | 21.16 | 33.70 |
| 160.wav | alac | 39815064 | 61.93% | 873 k | 12.48 | 46.20 |
| 160.wav | **notflac** | **38141373** | **59.32%** | **837 k** | **21.45** | **50.87** |
| 16bit-44.1kHz-mono.wav | flac | 60440009 | 38.10% | 268 k | 39.09 | 92.90 |
| 16bit-44.1kHz-mono.wav | wavpack | 65872640 | 41.53% | 293 k | 30.83 | 28.21 |
| 16bit-44.1kHz-mono.wav | alac | 71386487 | 45.01% | 317 k | 13.32 | 43.64 |
| 16bit-44.1kHz-mono.wav | **notflac** | **58174129** | **36.68%** | **258 k** | **20.62** | **43.79** |
| 17.wav | flac | 23301826 | 61.69% | 947 k | 17.22 | 72.70 |
| 17.wav | wavpack | 23205846 | 61.44% | 943 k | 24.91 | 31.72 |
| 17.wav | alac | 24869107 | 65.84% | 1011 k | 11.92 | 38.70 |
| 17.wav | **notflac** | **23303910** | **61.70%** | **947 k** | **18.82** | **43.56** |
| 2.wav | flac | 1062436 | 27.66% | 424 k | 21.73 | 91.60 |
| 2.wav | wavpack | 1065346 | 27.74% | 426 k | 28.91 | 25.98 |
| 2.wav | alac | 1065292 | 27.74% | 426 k | 9.67 | 20.44 |
| 2.wav | **notflac** | **1080726** | **28.14%** | **432 k** | **21.71** | **46.18** |
| 200.wav | flac | 8982121 | 53.53% | 377 k | 19.74 | 83.55 |
| 200.wav | wavpack | 8440366 | 50.30% | 354 k | 23.44 | 35.71 |
| 200.wav | alac | 9128202 | 54.40% | 383 k | 11.24 | 29.23 |
| 200.wav | **notflac** | **9033640** | **53.84%** | **379 k** | **18.18** | **40.24** |
| 201.wav | flac | 19926683 | 66.58% | 469 k | 18.88 | 86.99 |
| 201.wav | wavpack | 18104812 | 60.49% | 426 k | 27.25 | 31.13 |
| 201.wav | alac | 20221600 | 67.56% | 476 k | 12.97 | 38.63 |
| 201.wav | **notflac** | **20121229** | **67.23%** | **474 k** | **20.70** | **42.74** |
| 202.wav | flac | 14330895 | 25.96% | 366 k | 20.46 | 85.34 |
| 202.wav | wavpack | 15170264 | 27.48% | 387 k | 277.80 | 32.31 |
| 202.wav | alac | 17797610 | 32.24% | 455 k | 14.00 | 49.11 |
| 202.wav | **notflac** | **13877575** | **25.14%** | **354 k** | **22.49** | **49.18** |
| 203.wav | flac | 10568625 | 29.97% | 422 k | 20.86 | 89.42 |
| 203.wav | wavpack | 11318542 | 32.09% | 452 k | 28.78 | 36.44 |
| 203.wav | alac | 13650509 | 38.70% | 546 k | 13.45 | 39.01 |
| 203.wav | **notflac** | **10117901** | **28.69%** | **404 k** | **20.72** | **38.28** |
| 204.wav | flac | 14888527 | 31.42% | 443 k | 18.32 | 86.56 |
| 204.wav | wavpack | 15986300 | 33.74% | 476 k | 28.64 | 36.19 |
| 204.wav | alac | 19447728 | 41.05% | 579 k | 13.32 | 39.80 |
| 204.wav | **notflac** | **14223768** | **30.02%** | **423 k** | **16.61** | **40.23** |
| 205.wav | flac | 45805826 | 68.50% | 966 k | 16.70 | 53.90 |
| 205.wav | wavpack | 45341690 | 67.81% | 957 k | 20.02 | 25.53 |
| 205.wav | alac | 46212175 | 69.11% | 975 k | 9.59 | 46.96 |
| 205.wav | **notflac** | **46060002** | **68.88%** | **972 k** | **22.07** | **35.38** |
| 206.wav | flac | 59952405 | 68.09% | 960 k | 21.90 | 68.39 |
| 206.wav | wavpack | 59496482 | 67.57% | 953 k | 23.57 | 19.00 |
| 206.wav | alac | 60446278 | 68.65% | 968 k | 8.82 | 43.90 |
| 206.wav | **notflac** | **60284022** | **68.46%** | **966 k** | **20.12** | **85.99** |
| 207.wav | flac | 86771463 | 65.60% | 925 k | 19.37 | 46.08 |
| 207.wav | wavpack | 85754292 | 64.83% | 915 k | 15.41 | 23.77 |
| 207.wav | alac | 87544946 | 66.19% | 934 k | 10.75 | 26.10 |
| 207.wav | **notflac** | **87582919** | **66.22%** | **934 k** | **10.51** | **23.32** |
| 208.wav | flac | 25520564 | 53.73% | 758 k | 11.87 | 30.99 |
| 208.wav | wavpack | 25240418 | 53.14% | 750 k | 17.07 | 23.88 |
| 208.wav | alac | 26536280 | 55.87% | 788 k | 9.96 | 17.31 |
| 208.wav | **notflac** | **25557111** | **53.81%** | **759 k** | **15.82** | **35.52** |
| 209.wav | flac | 48608542 | 55.63% | 785 k | 18.73 | 75.92 |
| 209.wav | wavpack | 48131286 | 55.09% | 777 k | 41.71 | 31.46 |
| 209.wav | alac | 50529914 | 57.83% | 816 k | 11.45 | 35.97 |
| 209.wav | **notflac** | **48699360** | **55.74%** | **786 k** | **16.19** | **36.72** |
| 210.wav | flac | 50426143 | 55.74% | 786 k | 16.64 | 64.53 |
| 210.wav | wavpack | 49870358 | 55.12% | 777 k | 26.14 | 28.01 |
| 210.wav | alac | 52271489 | 57.78% | 815 k | 9.12 | 22.70 |
| 210.wav | **notflac** | **50517613** | **55.84%** | **788 k** | **12.12** | **53.72** |
| 211.wav | flac | 3768365 | 57.77% | 815 k | 16.04 | 51.28 |
| 211.wav | wavpack | 3733324 | 57.24% | 807 k | 23.20 | 26.37 |
| 211.wav | alac | 3859250 | 59.17% | 835 k | 6.55 | 20.53 |
| 211.wav | **notflac** | **3763370** | **57.70%** | **814 k** | **20.85** | **42.25** |
| 24bit-96khz.wav | flac | 7750479 | 44.85% | 2066 k | 19.06 | 57.41 |
| 24bit-96khz.wav | wavpack | 8059396 | 46.63% | 2149 k | 40.38 | 37.95 |
| 24bit-96khz.wav | alac | 8400415 | 48.61% | 2240 k | 13.24 | 43.39 |
| 24bit-96khz.wav | **notflac** | **7702461** | **44.57%** | **2053 k** | **23.31** | **58.15** |
| 3.wav | flac | 35661855 | 82.18% | 1159 k | 19.51 | 87.33 |
| 3.wav | wavpack | 35521032 | 81.85% | 1155 k | 24.82 | 30.92 |
| 3.wav | alac | 35968315 | 82.88% | 1169 k | 12.37 | 39.15 |
| 3.wav | **notflac** | **35777264** | **82.44%** | **1163 k** | **21.46** | **41.72** |
| 300.wav | flac | 27962171 | 51.16% | 722 k | 21.01 | 81.66 |
| 300.wav | wavpack | 27676726 | 50.64% | 714 k | 26.46 | 30.38 |
| 300.wav | alac | 28506562 | 52.16% | 736 k | 12.38 | 37.13 |
| 300.wav | **notflac** | **28096171** | **51.41%** | **725 k** | **18.75** | **39.20** |
| 301.wav | flac | 20449677 | 51.33% | 724 k | 22.02 | 85.64 |
| 301.wav | wavpack | 20221832 | 50.75% | 716 k | 27.56 | 32.38 |
| 301.wav | alac | 20866655 | 52.37% | 739 k | 12.56 | 44.12 |
| 301.wav | **notflac** | **20536671** | **51.54%** | **727 k** | **20.35** | **38.54** |
| 302.wav | flac | 9574521 | 46.04% | 649 k | 18.94 | 67.67 |
| 302.wav | wavpack | 9491796 | 45.64% | 644 k | 27.03 | 31.08 |
| 302.wav | alac | 9826583 | 47.25% | 666 k | 12.21 | 41.06 |
| 302.wav | **notflac** | **9562358** | **45.98%** | **648 k** | **22.45** | **44.29** |
| 303.wav | flac | 17902965 | 38.44% | 542 k | 49.36 | 47.77 |
| 303.wav | wavpack | 17665668 | 37.93% | 535 k | 23.72 | 27.74 |
| 303.wav | alac | 18712651 | 40.18% | 567 k | 10.69 | 39.71 |
| 303.wav | **notflac** | **17846917** | **38.32%** | **540 k** | **17.30** | **38.21** |
| 304.wav | flac | 24503241 | 38.80% | 547 k | 18.24 | 68.74 |
| 304.wav | wavpack | 24140634 | 38.22% | 539 k | 26.62 | 31.30 |
| 304.wav | alac | 25543048 | 40.44% | 570 k | 10.91 | 41.26 |
| 304.wav | **notflac** | **24452135** | **38.71%** | **546 k** | **24.14** | **30.79** |
| 305.wav | flac | 23067117 | 36.93% | 521 k | 16.59 | 78.64 |
| 305.wav | wavpack | 22709444 | 36.36% | 513 k | 21.98 | 29.88 |
| 305.wav | alac | 24059660 | 38.52% | 543 k | 10.55 | 33.89 |
| 305.wav | **notflac** | **22968287** | **36.78%** | **519 k** | **14.93** | **35.76** |
| 306.wav | flac | 22795693 | 47.44% | 669 k | 16.52 | 48.52 |
| 306.wav | wavpack | 22669582 | 47.18% | 665 k | 20.96 | 27.38 |
| 306.wav | alac | 23826091 | 49.59% | 699 k | 9.94 | 24.32 |
| 306.wav | **notflac** | **22954320** | **47.77%** | **674 k** | **15.51** | **19.31** |
| 307.wav | flac | 21598835 | 48.43% | 683 k | 14.90 | 67.08 |
| 307.wav | wavpack | 21497670 | 48.21% | 680 k | 12.46 | 119.73 |
| 307.wav | alac | 22730471 | 50.97% | 719 k | 7.37 | 18.50 |
| 307.wav | **notflac** | **21725927** | **48.72%** | **687 k** | **13.18** | **29.35** |
| 308.wav | flac | 15693079 | 46.29% | 653 k | 17.32 | 70.59 |
| 308.wav | wavpack | 15628486 | 46.10% | 650 k | 22.68 | 29.98 |
| 308.wav | alac | 16434576 | 48.48% | 684 k | 8.26 | 21.25 |
| 308.wav | **notflac** | **15819138** | **46.67%** | **658 k** | **16.31** | **32.10** |
| 309.wav | flac | 26908130 | 45.31% | 639 k | 18.12 | 30.54 |
| 309.wav | wavpack | 26552196 | 44.71% | 631 k | 15.33 | 22.25 |
| 309.wav | alac | 27913523 | 47.00% | 663 k | 8.25 | 17.36 |
| 309.wav | **notflac** | **27059546** | **45.56%** | **643 k** | **12.89** | **65.53** |
| 310.wav | flac | 21495831 | 49.43% | 697 k | 12.11 | 31.54 |
| 310.wav | wavpack | 21257978 | 48.88% | 689 k | 15.12 | 23.92 |
| 310.wav | alac | 22480865 | 51.69% | 729 k | 8.46 | 21.16 |
| 310.wav | **notflac** | **21595751** | **49.66%** | **700 k** | **12.54** | **5.15** |
| 311.wav | flac | 26456122 | 44.73% | 631 k | 10.17 | 51.90 |
| 311.wav | wavpack | 26281254 | 44.43% | 627 k | 16.10 | 26.10 |
| 311.wav | alac | 27958584 | 47.27% | 667 k | 7.69 | 33.75 |
| 311.wav | **notflac** | **26493379** | **44.79%** | **632 k** | **19.43** | **39.07** |
| 312.wav | flac | 61992184 | 62.86% | 1448 k | 18.33 | 57.15 |
| 312.wav | wavpack | 61754102 | 62.61% | 1442 k | 31.88 | 34.80 |
| 312.wav | alac | 63786081 | 64.67% | 1490 k | 10.36 | 50.48 |
| 312.wav | **notflac** | **62603726** | **63.48%** | **1462 k** | **28.77** | **51.10** |
| 350.wav | flac | 26177229 | 56.19% | 793 k | 18.29 | 56.51 |
| 350.wav | wavpack | 26418116 | 56.71% | 800 k | 25.47 | 31.27 |
| 350.wav | alac | 27463665 | 58.96% | 832 k | 4.47 | 25.82 |
| 350.wav | **notflac** | **26079957** | **55.99%** | **790 k** | **16.26** | **36.87** |
| 351.wav | flac | 26485561 | 61.15% | 863 k | 16.57 | 65.59 |
| 351.wav | wavpack | 26648176 | 61.53% | 868 k | 27.62 | 30.94 |
| 351.wav | alac | 27813066 | 64.22% | 906 k | 9.84 | 26.98 |
| 351.wav | **notflac** | **26415683** | **60.99%** | **860 k** | **17.33** | **41.89** |
| 352.wav | flac | 37673158 | 59.58% | 840 k | 20.11 | 69.91 |
| 352.wav | wavpack | 37902640 | 59.94% | 845 k | 24.32 | 37.96 |
| 352.wav | alac | 39417395 | 62.33% | 879 k | 10.18 | 33.46 |
| 352.wav | **notflac** | **37572994** | **59.42%** | **838 k** | **18.37** | **39.20** |
| 353.wav | flac | 6270777 | 46.88% | 661 k | 18.72 | 111.38 |
| 353.wav | wavpack | 6182448 | 46.22% | 652 k | 22.91 | 40.41 |
| 353.wav | alac | 6450742 | 48.22% | 680 k | 9.56 | 40.49 |
| 353.wav | **notflac** | **6273520** | **46.90%** | **661 k** | **17.70** | **35.49** |
| 354.wav | flac | 19750156 | 53.37% | 753 k | 16.24 | 69.13 |
| 354.wav | wavpack | 19547860 | 52.83% | 745 k | 29.06 | 33.82 |
| 354.wav | alac | 20580755 | 55.62% | 784 k | 10.08 | 36.22 |
| 354.wav | **notflac** | **19775076** | **53.44%** | **754 k** | **18.60** | **40.17** |
| 355.wav | flac | 17456280 | 51.59% | 728 k | 18.95 | 61.40 |
| 355.wav | wavpack | 17272274 | 51.04% | 720 k | 28.46 | 33.60 |
| 355.wav | alac | 18214431 | 53.83% | 759 k | 10.96 | 33.28 |
| 355.wav | **notflac** | **17479509** | **51.65%** | **729 k** | **18.43** | **40.42** |
| 356.wav | flac | 33618831 | 51.86% | 731 k | 18.33 | 73.41 |
| 356.wav | wavpack | 33190918 | 51.20% | 722 k | 26.49 | 28.94 |
| 356.wav | alac | 34515456 | 53.25% | 751 k | 10.67 | 38.65 |
| 356.wav | **notflac** | **33973444** | **52.41%** | **739 k** | **17.81** | **47.44** |
| 357.wav | flac | 51063879 | 61.63% | 869 k | 20.89 | 67.95 |
| 357.wav | wavpack | 50617332 | 61.09% | 862 k | 26.84 | 33.60 |
| 357.wav | alac | 51709756 | 62.41% | 880 k | 11.14 | 40.32 |
| 357.wav | **notflac** | **51222707** | **61.82%** | **872 k** | **19.55** | **44.34** |
| 358.wav | flac | 30157228 | 46.85% | 661 k | 20.39 | 77.01 |
| 358.wav | wavpack | 29789144 | 46.28% | 653 k | 25.92 | 29.33 |
| 358.wav | alac | 31048832 | 48.24% | 680 k | 11.55 | 37.12 |
| 358.wav | **notflac** | **30355949** | **47.16%** | **665 k** | **21.60** | **48.86** |
| 359.wav | flac | 8214794 | 52.18% | 736 k | 19.98 | 81.04 |
| 359.wav | wavpack | 8185818 | 51.99% | 733 k | 28.52 | 26.19 |
| 359.wav | alac | 8800476 | 55.90% | 788 k | 11.66 | 27.72 |
| 359.wav | **notflac** | **8243195** | **52.36%** | **738 k** | **20.18** | **54.34** |
| 360.wav | flac | 44810404 | 58.96% | 832 k | 19.52 | 79.86 |
| 360.wav | wavpack | 44267664 | 58.25% | 822 k | 26.56 | 35.15 |
| 360.wav | alac | 46799585 | 61.58% | 869 k | 12.13 | 41.70 |
| 360.wav | **notflac** | **45149390** | **59.41%** | **838 k** | **20.79** | **41.34** |
| 361.wav | flac | 23730185 | 60.88% | 859 k | 19.86 | 63.83 |
| 361.wav | wavpack | 23478820 | 60.23% | 850 k | 29.90 | 36.63 |
| 361.wav | alac | 24590536 | 63.08% | 890 k | 10.79 | 33.34 |
| 361.wav | **notflac** | **23843152** | **61.17%** | **863 k** | **18.83** | **41.87** |
| 4.wav | flac | 51807953 | 82.03% | 1157 k | 21.69 | 78.94 |
| 4.wav | wavpack | 51597870 | 81.70% | 1153 k | 27.00 | 32.83 |
| 4.wav | alac | 52295061 | 82.80% | 1168 k | 11.58 | 40.26 |
| 4.wav | **notflac** | **51994428** | **82.33%** | **1161 k** | **18.93** | **34.14** |
| 450.wav | flac | 34092272 | 43.58% | 615 k | 17.10 | 74.35 |
| 450.wav | wavpack | 33935748 | 43.38% | 612 k | 24.73 | 28.45 |
| 450.wav | alac | 36292919 | 46.39% | 654 k | 12.00 | 37.46 |
| 450.wav | **notflac** | **34193668** | **43.71%** | **616 k** | **23.10** | **47.58** |
| 451.wav | flac | 35210096 | 47.99% | 677 k | 18.57 | 82.90 |
| 451.wav | wavpack | 35036848 | 47.75% | 673 k | 30.72 | 33.11 |
| 451.wav | alac | 37371492 | 50.93% | 718 k | 11.31 | 37.37 |
| 451.wav | **notflac** | **35332955** | **48.15%** | **679 k** | **19.52** | **44.99** |
| 452.wav | flac | 23648790 | 48.36% | 682 k | 20.20 | 72.87 |
| 452.wav | wavpack | 23480920 | 48.02% | 677 k | 25.73 | 30.59 |
| 452.wav | alac | 25061145 | 51.25% | 723 k | 10.71 | 38.10 |
| 452.wav | **notflac** | **23732759** | **48.53%** | **684 k** | **20.54** | **44.95** |
| 453.wav | flac | 20668151 | 45.41% | 640 k | 21.13 | 73.31 |
| 453.wav | wavpack | 20489876 | 45.01% | 635 k | 25.56 | 35.05 |
| 453.wav | alac | 21701378 | 47.68% | 672 k | 11.13 | 39.42 |
| 453.wav | **notflac** | **20801780** | **45.70%** | **644 k** | **20.50** | **49.20** |
| 454.wav | flac | 13884484 | 52.36% | 738 k | 22.04 | 89.86 |
| 454.wav | wavpack | 13688930 | 51.62% | 728 k | 26.91 | 33.67 |
| 454.wav | alac | 14428307 | 54.41% | 767 k | 10.52 | 38.62 |
| 454.wav | **notflac** | **14012300** | **52.84%** | **745 k** | **22.36** | **50.45** |
| 455.wav | flac | 24476232 | 53.19% | 750 k | 19.74 | 80.96 |
| 455.wav | wavpack | 24122656 | 52.42% | 739 k | 28.41 | 33.71 |
| 455.wav | alac | 25485731 | 55.39% | 781 k | 11.24 | 43.39 |
| 455.wav | **notflac** | **24653641** | **53.58%** | **756 k** | **22.36** | **41.78** |
| 456.wav | flac | 29414951 | 58.84% | 830 k | 21.02 | 81.46 |
| 456.wav | wavpack | 29135696 | 58.28% | 822 k | 30.21 | 32.90 |
| 456.wav | alac | 30158813 | 60.33% | 851 k | 11.76 | 43.51 |
| 456.wav | **notflac** | **29533602** | **59.08%** | **833 k** | **20.57** | **46.01** |
| 457.wav | flac | 22207115 | 49.00% | 691 k | 20.68 | 65.23 |
| 457.wav | wavpack | 22015904 | 48.58% | 685 k | 25.35 | 34.92 |
| 457.wav | alac | 23268295 | 51.34% | 724 k | 10.30 | 25.38 |
| 457.wav | **notflac** | **22266568** | **49.13%** | **693 k** | **16.77** | **23.84** |
| 458.wav | flac | 31099879 | 55.43% | 782 k | 15.56 | 68.19 |
| 458.wav | wavpack | 30707564 | 54.73% | 772 k | 21.16 | 29.55 |
| 458.wav | alac | 32193012 | 57.38% | 809 k | 11.04 | 29.60 |
| 458.wav | **notflac** | **31256418** | **55.71%** | **786 k** | **21.07** | **42.44** |
| 459.wav | flac | 53339858 | 71.80% | 1654 k | 22.72 | 80.72 |
| 459.wav | wavpack | 53005584 | 71.35% | 1644 k | 40.04 | 41.84 |
| 459.wav | alac | 53937969 | 72.61% | 1673 k | 11.34 | 58.14 |
| 459.wav | **notflac** | **65927910** | **88.75%** | **2044 k** | **29.34** | **55.88** |
| 460.wav | flac | 86340277 | 72.07% | 1660 k | 21.66 | 65.04 |
| 460.wav | wavpack | 85645458 | 71.49% | 1647 k | 38.62 | 41.62 |
| 460.wav | alac | 87093170 | 72.70% | 1675 k | 13.14 | 58.94 |
| 460.wav | **notflac** | **103539275** | **86.42%** | **1991 k** | **28.62** | **44.07** |
| 461.wav | flac | 65825440 | 73.00% | 1681 k | 17.39 | 59.66 |
| 461.wav | wavpack | 65454176 | 72.58% | 1672 k | 37.46 | 39.11 |
| 461.wav | alac | 66442667 | 73.68% | 1697 k | 11.40 | 50.10 |
| 461.wav | **notflac** | **82527770** | **91.52%** | **2108 k** | **24.65** | **37.19** |
| 5.wav | flac | 63251790 | 76.61% | 1081 k | 20.07 | 23.25 |
| 5.wav | wavpack | 62980956 | 76.28% | 1076 k | 19.31 | 27.57 |
| 5.wav | alac | 64396237 | 78.00% | 1100 k | 6.78 | 23.56 |
| 5.wav | **notflac** | **63555660** | **76.98%** | **1086 k** | **9.66** | **17.50** |
| 500.wav | flac | 7833278 | 37.27% | 526 k | 7.92 | 40.47 |
| 500.wav | wavpack | 7738324 | 36.82% | 519 k | 19.63 | 24.52 |
| 500.wav | alac | 8169910 | 38.88% | 548 k | 7.58 | 22.91 |
| 500.wav | **notflac** | **7826632** | **37.24%** | **525 k** | **15.80** | **40.68** |
| 501.wav | flac | 24455463 | 51.74% | 730 k | 9.52 | 41.97 |
| 501.wav | wavpack | 24067768 | 50.92% | 718 k | 22.48 | 30.24 |
| 501.wav | alac | 25250084 | 53.42% | 753 k | 9.25 | 34.78 |
| 501.wav | **notflac** | **24579129** | **52.00%** | **733 k** | **19.07** | **49.51** |
| 502.wav | flac | 70288941 | 52.39% | 739 k | 13.66 | 26.01 |
| 502.wav | wavpack | 69281836 | 51.64% | 728 k | 23.29 | 27.95 |
| 502.wav | alac | 72333774 | 53.91% | 760 k | 10.22 | 31.85 |
| 502.wav | **notflac** | **70803793** | **52.77%** | **744 k** | **22.07** | **44.94** |
| 503.wav | flac | 28962256 | 57.32% | 808 k | 19.94 | 81.42 |
| 503.wav | wavpack | 28636254 | 56.67% | 799 k | 25.87 | 33.91 |
| 503.wav | alac | 29761863 | 58.90% | 831 k | 11.92 | 41.36 |
| 503.wav | **notflac** | **29105580** | **57.60%** | **812 k** | **22.43** | **51.04** |
| 504.wav | flac | 20163240 | 59.40% | 838 k | 19.94 | 83.42 |
| 504.wav | wavpack | 19893342 | 58.61% | 827 k | 26.71 | 37.91 |
| 504.wav | alac | 20728016 | 61.07% | 861 k | 11.94 | 33.74 |
| 504.wav | **notflac** | **20296633** | **59.80%** | **843 k** | **20.85** | **53.49** |
| 505.wav | flac | 54847098 | 55.14% | 778 k | 16.20 | 23.63 |
| 505.wav | wavpack | 54322956 | 54.61% | 770 k | 25.78 | 33.79 |
| 505.wav | alac | 56774086 | 57.07% | 805 k | 11.41 | 36.80 |
| 505.wav | **notflac** | **55055129** | **55.35%** | **781 k** | **20.97** | **42.06** |
| 506.wav | flac | 23727972 | 57.81% | 815 k | 19.90 | 100.07 |
| 506.wav | wavpack | 23453914 | 57.15% | 806 k | 26.24 | 34.95 |
| 506.wav | alac | 24574799 | 59.88% | 845 k | 11.41 | 43.33 |
| 506.wav | **notflac** | **23848978** | **58.11%** | **820 k** | **20.58** | **44.92** |
| 507.wav | flac | 27921910 | 56.66% | 799 k | 19.05 | 93.27 |
| 507.wav | wavpack | 27689424 | 56.18% | 792 k | 28.34 | 30.76 |
| 507.wav | alac | 29027285 | 58.90% | 831 k | 12.54 | 41.04 |
| 507.wav | **notflac** | **28017392** | **56.85%** | **802 k** | **21.29** | **42.71** |
| 550.wav | flac | 51106177 | 41.69% | 588 k | 15.58 | 21.92 |
| 550.wav | wavpack | 50784132 | 41.43% | 584 k | 26.17 | 33.53 |
| 550.wav | alac | 53269677 | 43.45% | 613 k | 12.15 | 41.31 |
| 550.wav | **notflac** | **51458395** | **41.98%** | **592 k** | **22.62** | **49.57** |
| 551.wav | flac | 37135964 | 46.61% | 657 k | 20.08 | 72.84 |
| 551.wav | wavpack | 36736190 | 46.11% | 650 k | 29.11 | 33.31 |
| 551.wav | alac | 38209190 | 47.95% | 676 k | 11.55 | 38.86 |
| 551.wav | **notflac** | **37349132** | **46.88%** | **661 k** | **23.03** | **46.76** |
| 552.wav | flac | 26554530 | 47.93% | 676 k | 22.71 | 78.12 |
| 552.wav | wavpack | 26190912 | 47.27% | 667 k | 21.18 | 31.07 |
| 552.wav | alac | 27166582 | 49.04% | 692 k | 10.46 | 32.24 |
| 552.wav | **notflac** | **26736002** | **48.26%** | **681 k** | **22.61** | **46.65** |
| 553.wav | flac | 138735522 | 73.29% | 1688 k | 12.45 | 23.13 |
| 553.wav | wavpack | 137413700 | 72.59% | 1672 k | 35.84 | 45.00 |
| 553.wav | alac | 140709528 | 74.33% | 1712 k | 13.01 | 61.16 |
| 553.wav | **notflac** | **169860619** | **89.73%** | **2067 k** | **28.70** | **44.77** |
| 554.wav | flac | 85354895 | 73.00% | 1681 k | 22.49 | 77.49 |
| 554.wav | wavpack | 84705814 | 72.44% | 1669 k | 38.02 | 41.74 |
| 554.wav | alac | 86802375 | 74.24% | 1710 k | 12.17 | 63.66 |
| 554.wav | **notflac** | **102093966** | **87.32%** | **2011 k** | **28.17** | **54.63** |
| 6.wav | flac | 31670386 | 42.88% | 658 k | 19.79 | 74.55 |
| 6.wav | wavpack | 31436868 | 42.57% | 653 k | 25.92 | 34.10 |
| 6.wav | alac | 33440522 | 45.28% | 695 k | 10.86 | 43.14 |
| 6.wav | **notflac** | **31790805** | **43.05%** | **661 k** | **20.87** | **45.82** |
| 7.wav | flac | 39676017 | 48.33% | 742 k | 19.76 | 36.63 |
| 7.wav | wavpack | 39155294 | 47.70% | 732 k | 26.04 | 34.00 |
| 7.wav | alac | 41187825 | 50.18% | 770 k | 10.15 | 33.14 |
| 7.wav | **notflac** | **40032453** | **48.77%** | **749 k** | **20.54** | **40.10** |
| 8.wav | flac | 36243705 | 42.40% | 651 k | 19.19 | 28.92 |
| 8.wav | wavpack | 35756298 | 41.83% | 642 k | 27.44 | 30.23 |
| 8.wav | alac | 37684124 | 44.09% | 677 k | 10.75 | 37.22 |
| 8.wav | **notflac** | **36572421** | **42.79%** | **657 k** | **20.92** | **46.58** |
| 9.wav | flac | 16311294 | 42.03% | 593 k | 18.72 | 79.48 |
| 9.wav | wavpack | 16194240 | 41.72% | 588 k | 28.54 | 36.47 |
| 9.wav | alac | 18149503 | 46.76% | 659 k | 11.83 | 41.62 |
| 9.wav | **notflac** | **16142140** | **41.59%** | **586 k** | **19.54** | **52.07** |
| orchestra.wav | flac | 4528472 | 55.80% | 787 k | 19.77 | 117.46 |
| orchestra.wav | wavpack | 4512502 | 55.61% | 784 k | 29.03 | 26.59 |
| orchestra.wav | alac | 4808774 | 59.26% | 836 k | 8.13 | 32.19 |
| orchestra.wav | **notflac** | **4527206** | **55.79%** | **787 k** | **20.64** | **36.26** |
| stereo.wav | flac | 10442541 | 40.89% | 628 k | 19.98 | 78.07 |
| stereo.wav | wavpack | 10963874 | 42.93% | 659 k | 27.75 | 36.71 |
| stereo.wav | alac | 12115946 | 47.44% | 728 k | 11.11 | 36.73 |
| stereo.wav | **notflac** | **10272113** | **40.22%** | **617 k** | **18.38** | **54.66** |

### Summary

| Comparison | Bitrate wins | Compress wins | Decode wins |
|---|---:|---:|---:|
| notflac vs flac | 21 / 100 | 61 / 100 | 13 / 100 |
| notflac vs wavpack | 12 / 100 | 6 / 100 | 89 / 100 |
| notflac vs alac | 88 / 100 | 98 / 100 | 67 / 100 |
