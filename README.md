# sparsebundle-fuse
[![CodeFactor](https://www.codefactor.io/repository/github/gyf304/sparsebundle-fuse/badge)](https://www.codefactor.io/repository/github/gyf304/sparsebundle-fuse)

> macOS sparsebundle compatible network block device (NBD) / FUSE filesystem

## Prerequisites

* libfuse
* nbdkit

## Building

Linux

(The fuse implementation is also macOS compatible,
but you should use the system built-in Disk Utility instead of this.)

```sh
git clone --recursive https://github.com/gyf304/sparsebundle-fuse.git
cd sparsebundle-fuse
# build everything
make
# build nbdkit plugin only
make sparse-nbd.so
# build fuse only
make sparse-fuse
```

## Usage

### NBD

1. Install nbdkit (see https://github.com/libguestfs/nbdkit, your distro may have this already)
2. Build nbdkit plugin `make sparse-nbd.so`
3. Run `nbdkit ./sparse-nbd.so path=SPARSEBUNDLE` (refer to `man nbdkit`)
4. Connect to NBD (refer to `man nbd-client`)

### FUSE

1. Install fuse (see https://github.com/libfuse/libfuse, your distro may have this already)
2. Build fuse program `make sparse-fuse`
3. Run `./sparse-fuse SPARSEBUNDLE MOUNTPOINT`
4. There should be a `sparsebundle.dmg` file under `MOUNTPOINT`

### mkfs.sparsebundle

This is a script for creating a sparsebundle.

```
mkfs.sparsebundle [-s SIZE] [-b BLOCKSIZE] SPARSEBUNDLE

mkfs.sparsebundle -s 1G ./test.sparsebundle
```
