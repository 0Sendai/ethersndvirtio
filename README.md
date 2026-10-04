
## Description

This repository contains a simple [VirtIO Ethernet](https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html) adapter driver for the [Plan 9](https://9p.io/plan9/about.html) operating system. The driver implements only the essential functionality. Plan 9 was chosen because its codebase is simple and concise, while the operating system itself offers interesting concepts that were later adopted by more successful systems (such as namespaces and UTF-8).

This is primarily a hobby project aimed at studying networking, operating systems, and development tools for Plan 9. It should not be considered a production-ready solution, as it does not implement all of the functionality described in the VirtIO specifications.

## Project Structure

- [`ethersndvirtio.c`](ethersndvirtio.c) — source code for the entire driver
- [`mkfile`](mkfile) — Plan 9-style Makefile

## Building the Kernel with This Driver

Since all drivers are compiled directly into the kernel, the following steps are required:

1. Create a configuration file for our driver:

   ```sh
   cd /sys/src/9/pc64
   ```

   ```sh
   cp pc64 qemu
   ```

   In the configuration file, find the `link` section, comment out `ethervirtio` and `ethervirtio10`, and add the following line:

   ```text
   ethersndvirtio pci
   ```

2. In the project directory, run:

   ```sh
   mk bind
   ```

   ```sh
   mk build
   ```

3. Replace the kernel in the system's boot configuration.

   Mount the boot partition:

   ```sh
   9fs 9fat
   ```

   In `/n/9fat/plan9.ini` change  `bootfile` from `9pc64` to `9qemu`.

4. Reboot the system.

## Possible Improvements

- Add a control queue to support incoming packet filtering and other functionality.
- Replace the use of packed structures for accessing device registers with offset-based reads.
- Optimize memory allocation for incoming packets by moving memory allocation to a separate thread. Currently, memory is allocated directly inside the interrupt handler, which can potentially cause the system to hang.


