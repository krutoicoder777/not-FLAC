# not-FLAC

A lossless audio compressor written in C. 

This is an **educational project** inspired by the core ideas of the FLAC format: audio prediction followed by entropy coding. However, it does **not** use FLAC's algorithms. Instead, it implements its own modular pipeline, allowing different algorithms to be plugged in.

## How it works

The current pipeline consists of two main stages:
1. **Prediction:** Currently uses **Delta Encoding** (difference between adjacent samples) + **ZigZag** transformation.
2. **Entropy Coding:** Currently uses an **Arithmetic Range Coder** with simple byte models.

The architecture is designed to be extensible. The `audio_encoder` and `audio_decoder` accept parameters for the prediction and entropy methods (though currently only `DELTA` and `RANGE_CODER` are implemented). Future plans include LPC and Rice coding.

### Differences from real FLAC
*   **Prediction:** Delta (order 1) instead of LPC (order 2-32).
*   **Entropy Coding:** Range Coder instead of Rice Coding (Golomb-Rice).
*   **Framing:** Stream-based instead of fixed-size blocks.

## Features
*   Pure C11, no external dependencies.
*   Streaming WAV parser (supports 16/24/32-bit PCM, mono/stereo).
*   Custom `.notflac` container format.
*   CLI for compression/decompression.
*   Streaming player (pipes raw PCM to stdout).
*   Automated round-trip tests.

## Build
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Usage

### CLI (Compress / Decompress)
```bash
# Compress WAV to notflac
./build/notflac encode input.wav output.notflac

# Decompress notflac to WAV
./build/notflac decode output.notflac restored.wav
```
*(Optional: You can pass predictor and entropy flags, e.g., `encode in.wav out.notflac 0 0`, but currently only `0 0` is supported).*

### Tests
The test suite automatically scans the `tests/data` directory, compresses/decompresses every valid WAV file, and verifies bit-exact restoration. It also prints the compression ratio.
```bash
./build/test_notflac_roundtrip
```
You can drop any `.wav` files into `tests/data/` to test them.

### Streaming Player
The `notflac_player` reads the compressed file **in real-time** and outputs raw PCM to `stdout`. This allows you to play the file without decompressing it to disk first. 

**Note:** The player outputs raw PCM, so you need to pipe it into a system audio player. It is platform-dependent (tested on Linux/WSL). 

```bash
# Example for 16-bit, 16kHz, mono:
./build/notflac_player output.notflac | aplay -f S16_LE -r 16000 -c 1

# Example for 24-bit, 96kHz, stereo (WSL/PulseAudio users might need `sox` to resample to 16-bit):
./build/notflac_player output.notflac | sox -t raw -r 96000 -c 2 -b 24 -e signed-integer - -t raw -r 44100 -c 2 -b 16 -e signed-integer - | paplay --raw --format=s16le --rate=44100 --channels=2
```

## Plans
* [ ] Add LPC (Linear Predictive Coding) for better prediction.
* [ ] Add Rice Coding for faster entropy coding.
* [ ] Mid/Side stereo decorrelation.
* [ ] Multi-file archiver (like a real `.flac` file).

## Notes
* The initial boilerplate for the CMake build system and tests were generated with the help of an LLM (DeepSeek-V3). All core algorithms (LZMA, Range Coder, WAV parser, Delta+ZigZag, audio encoder/decoder) were written by me.
