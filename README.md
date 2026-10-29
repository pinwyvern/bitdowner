### BUILD
```
cmake -S . -B build
cmake --build build -j
```

### USAGE
```
BitDowner - offline audio bit reduction effect

Usage:
  ./build/bitdowner INPUT OUTPUT [OPTIONS]

Options:
  --input-gain DB    Input gain, -40 to 40 (default: 0)
  --bit-down VALUE   BitDown control, 1 to 16 (default: 16)
  --downsampling N   Sample-hold interval, 1 to 200 (default: 1)
  --volume DB        Output gain, -90 to 12 (default: 0)
  -h, --help         Show this help

Example:
  ./build/bitdowner input.wav output.wav --input-gain 6 --bit-down 8 --downsampling 4 --volume -3
```
