# cheviot-kernel

This repository contains the sources of the CheviotOS microkernel.

This repository is brought in to the cheviot-project base repository as a
git submodule.

# Introduction

Cheviot is a multi-server microkernel operating system for the Raspberry Pi 4.
The kernel's API is over 80 system calls and growing. These implement a POSIX-like
API.  This is different to many microkernels that tend to reduce the microkernel
to a minimal set of functionality. The kernel implements process management,
memory management and a virtual file system (VFS).

Within the kernel the VFS implements common POSIX-like system calls such as open, close,
read and write. These file system operations then send messages to processes that
handle the specifics of a file system format which in turn send messages to block device
drivers to read and write blocks from the physical media.

As message passing and context switching between processes has overheads the VFS
implements a file level cache and directory lookup cache within the kernel. If a file
or portion of a file had been read previously and it's contents are still in the cache this will
eliminate a number of messages to file system handler and device driver processes.

## Pi 4 and Pi 1

Development is currently done on a Raspberry Pi 4. The Pi 1 build is currently in a non-working state.

# Licensing and Copyrights

The majority of sources is under the Apache license, see source code for details. The safe-string
copying functions, Strlcat and Strlcpy are the copyright of Todd C. Miller, again see the headers
in utility/string.h


