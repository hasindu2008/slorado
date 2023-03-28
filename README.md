# slorado

A simplified version of Dorado.

## Compilation and running

```
sudo apt-get install zlib1g-dev   #install zlib development libraries
git clone --recursive https://github.com/BonsonW/slorado
cd slorado
```

The commands to install zlib development libraries on some popular distributions:

```
On Debian/Ubuntu : sudo apt-get install zlib1g-dev
On Fedora/CentOS : sudo dnf/yum install zlib-devel
On OS X : brew install zlib
```

### Make

CPU version:

```
scripts/install-torch12.sh
make -j
./slorado basecaller -x cpu models/dna_r10.4.1_e8.2_400bps_fast@v4.0.0 test/oneread_r10.blow5
```

CUDA GPU version:
```
scripts/install-torch12.sh
make cuda=1 koi=1 -j
./slorado basecaller models/dna_r10.4.1_e8.2_400bps_fast@v4.0.0 test/oneread_r10.blow5
```

CUDA GPU version (requires cuda 11.3):
```
scripts/install-torch12.sh
make cuda=1 koi=1 -j
./slorado basecaller models/dna_r10.4.1_e8.2_400bps_fast@v4.0.0 test/oneread_r10.blow5
```

CUDA GPU version without koi library (uses CPU decoder):
```
scripts/install-torch12.sh
make cuda=1 -j
./slorado basecaller models/dna_r10.4.1_e8.2_400bps_fast@v4.0.0 test/oneread_r10.blow5
```

advanced options:
```
make cuda=1 LIBTORCH_DIR=/path/to/torachlib CUDA_ROOT=/path/to/cuda/library/
```

## Calculate basecalling accuracy
```
set variables MINIMAP2, REFERENCE_GENOME, FASTQ_FILE in the script and run
scripts/calculate_basecalling_accuarcy.sh
```

## Acknowledgement
Code snippets have been taken from [Minimap2](https://github.com/lh3/minimap2).

## Walkthrough

The repository contains the following:
1. slow5lib as a git submodule
1. A [Makefile](Makefile)
2. C programme[source files](src/)
3. [shell script](test/test.sh) for testing
4. [GitHub actions workflow](.github/workflows/c-cpp.yml)
5. Miscellaneous files such as license, .gitignore, etc


## Links

SLOW5 specification: https://hasindu2008.github.io/slow5specs<br/>
slow5lib: https://hasindu2008.github.io/slow5lib/<br/>
slow5tools: https://hasindu2008.github.io/slow5tools<br/>
Pre-print: https://www.biorxiv.org/content/10.1101/2021.06.29.450255v1<br/>
Publication: https://www.nature.com/articles/s41587-021-01147-4<br/>




