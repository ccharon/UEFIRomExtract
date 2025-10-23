# UEFIRomExtract (cleaned & CMake-enabled)
UEFI Video Card ROM Extractor
This tool can automatically extract the .efi part from a video card ROM file.
Created by Andy Vandijck.

https://github.com/andyvand/UEFIRomExtract/

I only tidied the code and added a simple CMake build so it compiles easily on Linux.

I tested it roughly with my videocard ROM, the Linux and the 64Bit Windows Version extract the same efi part having the same checksum.

## Changes in this fork
- Code formatting and small cleanups for readability.
- Added `CMakeLists.txt` and build instructions for Linux compatibility.
- No functional changes to core algorithms.

## Build (Linux)
```bash
mkdir -p build
cd build
cmake ..
make
```

## Usage
> UEFI option ROM extractor and decompressor V1.0 <br>
> This program extracts and decompresses UEFI .rom files in their .efi files <br>
> Usage: ./UEFIRomExtract <In_File> <Out_File>
