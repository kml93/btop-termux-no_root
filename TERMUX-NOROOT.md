# btop — Termux (non-root) fork

`btop` that **actually starts on non-rooted Android/Termux**.

The official Termux package lives in `root-packages` and refuses to launch
without root (`"btop can't do anything useful on Termux without root"`). This
fork removes that hard requirement so btop runs unprivileged, showing what an
app can still read on modern Android.

## What works / what doesn't

On non-rooted Android, SELinux denies apps read access to most of `/proc` and
`/sys`. That is a **kernel-level** restriction — no patch, no API, nothing
non-root can bypass it. This fork makes btop **degrade gracefully** instead of
crashing, and shows whatever is still readable:

| Works (real data)               | Blocked by kernel (empty/zero)              |
|---------------------------------|----------------------------------------------|
| Memory (RAM / swap / cache)     | CPU % (global + per-core + per-process)      |
| Your own processes (pid/cmd/user/mem) | Network throughput                     |
| CPU temperature (if a sensor)   | Disk I/O                                     |
| CPU name / core count           | Load average, uptime                         |
|                                 | Battery (`/sys/class/power_supply` blocked)  |

> The per-process **CPU% column** and the **cpu lazy / cpu direct** sort modes
> have been removed (they sort on a value that is always 0 here). Processes are
> sorted by memory by default.

## How this is done (3 commits on top of upstream `v1.4.7`)

1. **`termux: Android build compatibility`** — the 6 Android patches from
   `termux-packages/root-packages/btop` (CMake linux-collector detection,
   bionic pthread fix, `getloadavg`, no `/etc/fstab`, disk dedup, sensor
   guessing). The `exit-early-when-unprivileged` patch is **intentionally
   excluded**.
2. **`termux: tolerate blocked /proc and /sys without root`** — converts the
   fatal `throw`s in `Shared::init()` (on `/proc/stat`, `/proc/uptime`,
   `/proc/filesystems`, `/sys/...`) into graceful warnings, and guards
   `std::filesystem::directory_iterator` calls with `access(R_OK)`.
3. **`termux: hide unusable CPU% column and cpu lazy/direct sort modes`**.

These changes are **adaptive**: on a less-locked-down device where some of these
files *are* readable, btop will simply show more (e.g. real CPU%).

## Build (on Termux)

```sh
pkg install clang cmake lowdown make git
git clone <this-repo> && cd btop
make PREFIX="$PREFIX" STATIC=false GPU=false    # or: cmake/make directly
cp bin/btop "$PREFIX/bin/btop"                  # or: make install
```

## Install from a Release (prebuilt, no compile)

See the **Releases** page. Download the `.deb` and:

```sh
pkg install ./btop-termux_<version>_aarch64.deb
```

Then run `btop`. Recommended `~/.config/btop/btop.conf` for non-root:

```conf
shown_boxes = "mem proc"
use_fstab = false
show_battery = false
proc_sorting = "memory"
proc_cpu_graphs = false
```

## Credits & license

btop is © aristocratos, licensed **Apache-2.0** (see `LICENSE`).
The Android compatibility patches originate from the
[termux-packages](https://github.com/termux/termux-packages) project
(`root-packages/btop`) — credit to their maintainers.
The non-root tolerance patches in this fork are derivative work under the same
Apache-2.0 license.
