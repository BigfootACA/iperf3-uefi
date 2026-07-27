# iperf3-uefi

`iperf3-uefi` is an iperf3 client for the UEFI Shell. It uses EDK II TCP4,
TCP6, UDP4, UDP6, and DNS protocols and interoperates with a standard iperf3
server on port 5201.

## Features

- TCP and UDP client tests
- IPv4, IPv6, and DNS server addresses
- Normal and reverse mode
- Time, byte-count, and block-count test limits
- Configurable report interval, bitrate, block size, and TCP window
- X64, IA32, ARM, and AARCH64 EDK II build targets

## Build

The build requires Git, GNU Make, GCC, Python 3, and the normal EDK II BaseTools
dependencies (`uuid-dev` and `nasm` on Debian/Ubuntu).

Clone the repository with submodules and build:

```bash
git clone --recursive https://github.com/BigfootACA/iperf3-uefi.git
cd iperf3-uefi
./build.sh
```

To use an existing EDK II checkout:

```bash
make EDK2_PATH=/path/to/edk2 ARCH=x86_64 -j"$(nproc)"
```

Supported `ARCH` values include `x86_64`, `i386`, `aarch64`, and `arm`. The
binary is written under
`build/Build/IPerf3Pkg/RELEASE_GCC5/<EDK2_ARCH>/IPerf3.efi`.

Build an Arch Linux package with:

```bash
makepkg -s
```

Build a Debian package with:

```bash
dpkg-buildpackage -b
```

Both package formats install `IPerf3.efi` and its debug file under
`/usr/share/iperf3-uefi/`.

## Run

Copy `IPerf3.efi` to a FAT volume, boot a UEFI Shell, configure networking if
the firmware has not already done so, and run:

```text
Shell> ifconfig -s eth0 dhcp
Shell> iperf3 -c 192.0.2.10
```

Start a server on another machine with `iperf3 -s`. Useful examples:

```text
Shell> iperf3 -c server.example.com -R
Shell> iperf3 -c 192.0.2.10 -u -b 100M -t 30
Shell> iperf3 -6 -c 2001:db8::10 -n 512M
```

Use `IPerf3.efi --help` for all options.

## Limitations

This is a UEFI client, not the upstream iperf3 POSIX application. Server mode,
parallel streams, authentication, JSON output, SCTP, and bidirectional mode are
not currently implemented. Network protocol availability depends on the UEFI
firmware and its network drivers.

## License

This project is licensed under the GNU General Public License version 2. See
`LICENSE` for the full license text.
