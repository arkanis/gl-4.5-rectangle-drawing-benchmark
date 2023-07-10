OpenGL 4.5 UI rectangle drawing benchmark
=========================================

This repository contains the stuff related to the rectangle rendering benchmark I spend a few weeks on.

- [Blog post](http://arkanis.de/weblog/2024-07-10-rectangle-rendering-benchmark) about it
- `binary` contains the original binaries and data used to gather the results
- `source` contains the source code
	- In that directory you can build the benchmark via `make 26-bench-rect-drawing`
	- On Windows I used `w64devkit-mini-1.19.0.zip` from [skeeto/w64devkit](https://github.com/skeeto/w64devkit) to build the binary
- `results` contains the raw CSV logs from various benchmark runs. Lots of unprocessed data in there. ;)
