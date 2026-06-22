# True-Peak Limiter - LADSPA Plugin

LADSPA stereo brickwall true-peak limiter with inter-sample peak (ISP) detection.

## Project Home

https://github.com/simdott/True-Peak_Limiter

## Compilation and Installation

1. Open a terminal in the plugin's folder
2. Run `make`
3. Run `sudo make install`

## Uninstall

1. Open a terminal in the plugin's folder
2. Run `sudo make uninstall`

## Features

- Stereo processing with linked gain reduction
- Fixed look-ahead delay (1.46 ms at 48 kHz, scales with sample rate)
- ISP (inter-sample peak) detection via 4x cubic interpolation
- Adjustable threshold (-12 dB to 0 dB, default 0 dB)
- Adjustable release (10 ms to 200 ms, default 100 ms)
- Automatic latency compensation in compatible hosts (Ardour, Renoise, etc.)

## Controls

- **Input Gain**: 0 dB to 18 dB
- **Threshold**: -12 dB to 0 dB (default -1.2 dB)
- **Release**: 10 ms to 200 ms (default 100 ms)

## Version

- **v1.0.0** - Initial release (2026-06-22)

## License

GPL-2.0-or-later

## Author

Simon Delaruotte (simdott)
