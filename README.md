# fglibc

`fglibc` is a Linux command-line runner that provides a per-user glibc sysroot for running glibc-linked programs on musl-based distributions. It can also install Arch Linux packages into that sysroot and run Windows programs through an available Wine or Steam Proton installation.

The project is experimental. Compatibility depends on the host kernel, enabled user namespaces, the target application's runtime requirements, and available host display/audio services. It does not emulate a complete Debian or Arch system.

## Build and install

Requirements: Linux x86_64, a C++17 compiler, GNU Make, and standard system utilities used by the setup scripts.

```sh
make -j"$(nproc)"
./scripts/install.sh
```

The install script puts `fglibc` in `~/.local/bin` for regular users. Ensure that directory is on `PATH`.

## Initialize and run

```sh
fglibc init
fglibc ./my-glibc-program
fglibc run /path/to/program --program-arg
fglibc shell
fglibc shell sh -c 'printf "shell works\\n"'
fglibc sh -c 'find /usr/lib -name "libqxcb.so"'
```

`fglibc shell` opens bash when available in the sysroot, otherwise `sh`. Arguments following `shell` or `sh` are passed through to that shell, so `-c` commands and script paths work. The `direct` engine requires its chosen shell to exist in the sysroot; the default namespace engine can also see host shell tools.

The default prefix is `~/.fglibc`. Set `FGLIBC_PREFIX` to use another location. `FGLIBC_ENGINE` can be `namespace` (default), `bwrap`, or `direct`; the latter engines have different isolation and host-visibility characteristics. `FGLIBC_DEBUG=1` enables debug logging.

## Packages

Packages are installed into the user-owned sysroot; no host package database is changed.

```sh
fglibc install fastfetch
fglibc install electron
fglibc install gaming
fglibc uninstall fastfetch
fglibc list
fglibc clean
```

Package names are Arch Linux repository/AUR names and project-defined bundles. `fglibc install` requires package names; the former interactive TUI has been removed. See `fglibc help` for the command list.

Local Arch package archives can be extracted into the sysroot with:

```sh
fglibc install ./package.pkg.tar.zst
```

This is archive extraction, not a general package-manager transaction. Debian `.deb` installation via `dpkg -i` is not supported as a rootless package install. You can inspect or extract one with host `dpkg-deb -I` and `dpkg-deb -x`, then run its binary through `fglibc`; dependencies, desktop integration, and package maintainer scripts may still need separate handling.

## Setup and desktop integration

Setup actions are explicit; bare `fglibc setup` does not launch a menu or install a default stack.

```sh
fglibc setup mirrors
fglibc setup apt
fglibc setup devel
fglibc setup status
fglibc sync-desktop
```

`fglibc setup all` runs the project's broad Arch/AUR and desktop setup sequence. Review its actions before using it.

## Windows applications

Windows executables need Wine or Steam Proton installed on the host. `win-setup` and `winetricks` invoke the bundled scripts:

```sh
fglibc win-setup status
fglibc winetricks list
```

## Commands

```text
fglibc <program> [args...]       Run a program in the configured environment
fglibc run <program> [args...]   Explicit program launch
fglibc shell [shell args...]     Open/pass arguments to a sysroot shell
fglibc init [--gaming]           Initialize the prefix
fglibc install <pkg...>          Install packages or extract a local Arch archive
fglibc uninstall <pkg...>        Remove files tracked by fglibc manifests
fglibc list                      List fglibc-managed packages
fglibc clean                     Clear downloaded package cache
fglibc setup <component...>      Explicit package-manager/desktop setup
fglibc sync-desktop              Export desktop entries
fglibc mirrors                   Rank configured Arch mirrors
fglibc check <binary>            Inspect ELF or PE binary
fglibc status                    Show prefix/runtime information
fglibc win-setup <component>     Configure Windows runtime helpers
fglibc winetricks [verb...]      Run Winetricks
fglibc test                      Run the project's stress-test script
fglibc help | version            Show help/version
```

## Development

```sh
make
make clean
make examples
make test
```

The project is licensed under MIT; see [LICENSE](LICENSE).
