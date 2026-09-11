# Booting a generic Linux distro from USB on the Pixel 11 Pro Fold (yogi)

ArchLinuxARM aarch64 running off a USB drive on a Pixel 11 Pro Fold, RAM-booted with
`fastboot boot` so nothing is flashed and the installed Android is untouched.

## What is in this repo

| path | what |
|---|---|
| `initramfs/root/linuxboot_init` | PID1. Loads the vendor stack, pets the watchdog, starts aocd for the USB host role, waits for the drive by label, then switch_roots. |
| `initramfs/root/` | The initramfs staging tree, device binaries included. |
| `initramfs/mkcpio.py` | Builds the initramfs cpio from that tree. |
| `kernel/linux_boot_gki.fragment` | The config fragment: rdinit, squashfs, sysrq, USB-Ethernet built in. |
| `kernel/linux-boot-wiring.patch` | Wires the fragment into `BUILD.bazel` and drops the seven USB-net entries from `modules.bzl`. |
| `tools/modeset_test.c` | Minimal raw-ioctl DRM modeset, to prove the panel lights from userspace KMS. |
| `tools/setup-arch.sh` | Prepares the Arch rootfs on the drive: boot report, tty1 autologin, no-suspend, your SSH key. |

The kernel source is not here; this is a patch against ACK `android16-6.12`.

## Building the initramfs

The staging tree is committed, so this is one command:

```sh
python3 initramfs/mkcpio.py initramfs/root linux_boot_initramfs.cpio
```

Drop the result at the root of the kernel tree, which is where `CONFIG_INITRAMFS_SOURCE`
looks for it.

The tree carries device binaries, harvested off the phone in September 2026 and kept here so
this can be picked up again later without re-harvesting them. They are Google's, not ours:

| path | where it came from |
|---|---|
| `bin/busybox` | arm64, `lib/arm64-v8a/libbusybox.so` inside a Magisk release apk |
| `lib/modules/aoc_usb_driver.ko` | the device's vendor_dlkm |
| `vendor/firmware/aoc.bin` | the device's `/vendor/firmware` |
| `aoc/linker64`, `aoc/aocd`, `aoc/lib/*.so` | `/vendor/bin/aocd`, the bionic linker, and the libraries `/proc/<aocd-pid>/maps` lists for it |
| `linuxboot_init`, `modeset_test` | ours; `modeset_test` builds from `tools/modeset_test.c` |

Re-harvesting is only necessary if the device moves to a kernel generation these stop loading
against.

## Building the kernel

Against ACK `android16-6.12`, on top of the protected-export fragment (without it the stock
vendor modules this init loads are refused, see the blockers below):

```sh
cp kernel/linux_boot_gki.fragment arch/arm64/configs/
git apply kernel/linux-boot-wiring.patch
# build //common:kernel_aarch64_gki_artifacts, then magiskboot the Image into a stock boot.img
```

The result is RAM-booted with `fastboot boot`, never flashed.

## Status: CORE ACHIEVED

A generic ArchLinuxARM aarch64 rootfs on a USB drive boots and runs on yogi via
non-destructive `fastboot boot` (nothing is ever flashed; the daily kernel/system are
untouched). Proven end to end 2026-09-09:

- Arch boots from the USB drive (switch_root into its systemd).
- Interactive root shell over SSH (USB-Ethernet on the dock, DHCP + sshd).
- On-screen display works: the panel physically lights via a userspace KMS modeset on
  `/dev/dri/card0` through the stock `vs_drm` driver (cover panel lit green, confirmed).
- USB keyboard input works (USB HID).

Remaining for a standalone on-screen system: a KMS console/compositor on card0 (blocked only
on initializing the ArchLinuxARM pacman keyring). Everything hardware-level is proven.

## Goal and constraints

- RAM-boot a generic aarch64 Linux (settled on Arch Linux ARM) from a USB-C drive.
- Fully non-destructive: `fastboot boot` only, nothing written to the phone. The three
  brick-avoidance rules hold throughout (never write a bootloader partition, never
  --set-active, RAM-boot before any flash).
- The device's daily kernel (ksunext-susfs) and OrangeFox tree are untouched; this lives on
  its own `linux-boot` kernel branch, unpushed.

## Architecture / boot flow

Kernel: GKI 6.12.92, branch `linux-boot` @ `ea07ffd` (dirty, unpushed), off the daily
ksunext-susfs lockfix. Extra config in `arch/arm64/configs/linux_boot_gki.fragment`:
`VT`/`VT_CONSOLE`, `SQUASHFS`, `MAGIC_SYSRQ`, USB-Ethernet host drivers built-in
(`USB_NET_DRIVERS`+`MII`+`USB_USBNET`+`USB_NET_CDCETHER`+`USB_NET_CDC_NCM`+
`USB_NET_AX88179_178A`+`USB_RTL8152`), an embedded initramfs
(`CONFIG_INITRAMFS_SOURCE="linux_boot_initramfs.cpio"`), and
`CONFIG_CMDLINE="rdinit=/linuxboot_init log_buf_len=8M ignore_loglevel"` +
`CONFIG_CMDLINE_EXTEND=y`. NOTE: fbcon options (FB/FRAMEBUFFER_CONSOLE/DRM_FBDEV_EMULATION/
LOGO) were tried and REVERTED -- see blocker 8.

Image: `boot-linux-repack.img` -- the built `Image` magiskboot-repacked into a stock boot
container (mkbootimg-from-scratch bounces at the bootloader; magiskboot re-lz4s and rebuilds
the exact stock container + VBMETA footer).

Boot flow:
1. `fastboot boot boot-linux-repack.img` -- bootloader accepts, jumps to our kernel.
2. Kernel unpacks the embedded initramfs; `rdinit=/linuxboot_init` runs OUR init instead of
   Android's /init. The flashed vendor_kernel_boot ramdisk also populates `/lib/modules`
   (198 vendor .ko + modules.dep/modules.load, kernel-decompressed for free -- Arch cannot
   modprobe our-kernel modules itself, wrong version dir, so the init loads them before
   switch_root and they persist across the pivot).
3. init: mount pseudo-fs, load `google_wdt` + pet the watchdogs, load the vendor stack
   (retry-to-fixpoint, skipping aoc/gsa/modem/trusty for USB-host reasons), bring up the ACM
   gadget (USB serial shell), run `aocd` to get the USB host role, then wait for the Arch
   drive BY LABEL, mount it, load the display stack (card0), and `switch_root` into Arch.
4. Arch (ArchLinuxARM) runs from the USB drive as rootfs; systemd-networkd DHCPs the
   USB-Ethernet, sshd listens; log in with the laptop's SSH key.

## The blockers, and how each was solved

1. Bootloader bounce. A scratch mkbootimg container (raw uncompressed Image) bounced before
   the orange-state warning. Fix: magiskboot-repack the Image into a stock boot.img (lz4
   container + footer). Lesson applies to boot.img, not just vendor_boot.

2. APC hardware watchdog resets at 60s. The bootloader arms a 60s watchdog; on a normal boot
   the vendor `gs_watchdogd` pets `/dev/watchdog0/1`. Our minimal init pets nothing. Fix:
   load `google_wdt` (deps `google_gtc`, `google_timestamp_sync`) and pet the nodes. Shell-
   loop petters work pre-switch_root but die when switch_root deletes busybox; busybox
   `watchdog` DAEMONS (exec'd before the pivot, holding the fd) survive and keep petting.

3. Observability, one USB-C port. Hosting the drive puts the port in host mode, so the ACM
   device-mode shell is gone while the drive is attached. Channels: `/tmp/boot.log` (RAM,
   read live over ACM when on the laptop), `/dev/pmsg0` (pstore -- survives a watchdog/panic
   reset, NOT a power-hold), and once in Arch, SSH over the dock Ethernet. A boot report is
   also written to `/var/log/linuxboot-boot.log` ON THE DRIVE (survives any reset; read by
   moving the drive to the laptop).

4. USB host mode (to read the drive). Tensor USB role = google-usb-role-sw combining
   TCPCI_COMB + AOC + OFFLOAD votes. The AOC vote needs the AoC's `usb_control` service,
   which only starts when the `aocd` daemon runs. Fix: bake `/vendor/firmware/aoc.bin` into
   the initramfs (aoc_core boots the AoC) and run `aocd` as a standalone daemon (no binder):
   `LD_LIBRARY_PATH=/aoc/lib /aoc/linker64 /aoc/aocd` with the bionic linker + ~12 libs
   (pulled from /proc/<aocd>/maps). Then a USB drive (no laptop on the bus, charger-powered
   dock) enumerates and mounts.

5. switch_root into Arch. Mount the ext4 drive, `exec switch_root /newroot /sbin/init`. The
   drive IS the root disk -- it must stay attached; unplugging mid-run pulls the rootfs out.
   Host-mode enumerate + ext4 mount takes ~15-20s, so leave the drive attached.

6. USB-Ethernet for SSH. Android never hosts Ethernet, so the driver is not in the vendor
   module set, and Arch cannot load our-kernel modules. Fix: build the common USB-Ethernet
   drivers into the kernel (=y). Trap chain: GKI caps them at =m (config consistency check
   "actual m, expected y") until the parent `USB_NET_DRIVERS=y` is set; then =y hits the
   modules.bzl missing-.ko wall (the promote-to-builtin pattern) -- remove the 7 now-built-in
   entries from `modules.bzl` (mii, usbnet, cdc_ether, cdc_ncm, ax88179_178a, r8152,
   r8153_ecm). Dock NIC enumerates as `enu1u1`, DHCP + sshd come up on their own
   (ArchLinuxARM ships both enabled).

7. systemd auto-suspend. logind watches the fold's hall-effect sensor (`/dev/input/event1`)
   as a laptop lid and suspends the phone ~2s after multi-user (this also made early network
   scans find nothing -- it was asleep). Fix, on the Arch rootfs:
   `/etc/systemd/logind.conf.d/nosuspend.conf` with all `Handle*=ignore` + `IdleAction=ignore`,
   and mask `sleep/suspend/hibernate/hybrid-sleep/suspend-then-hibernate.target`.

8. On-screen display -- the CRC wall and the fix (Option 1). Kernel fbcon on a DRM panel
   REQUIRES `DRM_FBDEV_EMULATION`, and enabling it grows `struct drm_device`, changing the
   CRC of every DRM symbol taking `drm_device *` (e.g. `drm_helper_mode_fill_fb_struct`), so
   the stock `vs_drm` refuses to load ("disagrees about version"). So fbcon is fundamentally
   incompatible with a stock-CRC vs_drm. Fix: DROP fbcon; drive the panel from USERSPACE KMS
   on `/dev/dri/card0` (needs neither FB nor DRM_FBDEV_EMULATION -> no CRC change). Then:
   - vs_drm's remaining missing dep is `tzprot.ko` (exports `trusty_protect_ip`; NOT a
     trusty-* named module, so the init's `trusty*` skip missed it). With tzprot + the display
     components loaded, vs_drm binds and creates `/dev/dri/card0`:
     conn 500 = inner panel (2152x2076@120), conn 531 = cover (1080x2342@120), both connected;
     conn 534 = DP, disconnected.
   - The display stack needs a full module load (trusty/tzprot/vs_drm/panel/dpu). That load
     ALSO exposes the phone's INTERNAL UFS as /dev/sda-sde (sda1 = persist, f2fs). So drive
     detection MUST be by LABEL=archroot (blkid), never a blind `head -1` -- the old code
     grabbed internal sda1 and mounted a persist partition (no /sbin/init). Never mount
     internal partitions.
   - Proof: `modeset_test` (raw-ioctl DRM modeset, static aarch64, NDK r27; baked into the
     initramfs + scp'able) opens card0, SET_MASTER, CREATE_DUMB + ADDFB + SETCRTC, fills a
     color. It lit the cover panel green (confirmed on-screen). No vendor HWComposer needed
     for a basic modeset. Tool bug: it assigns both connectors the same CRTC (they share
     possible_crtcs) -- give distinct CRTCs to light both panels; a real compositor handles
     multi-display itself.

## Reproduction

Build (laptop drives main3):
- Kernel tree on `linux-boot`; `./remote-build.sh device` builds
  `//common:kernel_aarch64_gki_artifacts`, fetches boot.img + vmlinux to `remote-dist/`.
- Repack: magiskboot unpack remote-dist/boot.img -> Image; magiskboot unpack a stock
  boot.img, drop our Image over `kernel`, repack -> `boot-linux-repack.img`. (Scripted in
  the repack dir under the scratchpad.)

Arch drive (one-time, on the laptop):
- A USB drive, single ext4 partition, LABEL=archroot, ArchLinuxARM aarch64 rootfs extracted
  (bsdtar -xpf). Creds root/root.
- Run `setup-arch-A.sh` (sudo) against the mounted drive: installs a boot-report unit, an
  early marker, tty1 root autologin, the no-suspend config, and the laptop SSH pubkey in
  /root/.ssh/authorized_keys.

Boot + connect:
- On the laptop: `fastboot boot boot-linux-repack.img` (phone in bootloader).
- Move the phone to a charger-powered dock with the drive + USB-Ethernet (+ keyboard for
  on-screen). Init finds the drive by label, brings up card0, switch_roots into Arch.
- SSH: `ssh -i <laptop key> root@<dhcp-ip>` (find the IP with a LAN :22 scan; last lease was
  10.0.0.127).

## Reboot and observability techniques (no button)

- From the minimal-init ACM shell: `stty -F /dev/ttyACM0 115200 raw -echo` FIRST, then
  `printf 'echo b > /proc/sysrq-trigger\n' > /dev/ttyACM0` -> clean reboot to Android;
  `echo c` = panic (preserves pmsg). The stty raw step is REQUIRED or the write never reaches
  the device shell.
- From Arch: `systemctl reboot --reboot-argument=bootloader` -> STRAIGHT to the bootloader
  (honored by the vendor reboot handler); plain `systemctl reboot` -> normal Android.
- To get from a RAM-booted state to a new image: reboot -> Android/bootloader ->
  `adb reboot bootloader` (if in Android) -> `fastboot boot <img>`. fastboot needs the phone
  on the laptop; SSH/Ethernet needs it on the dock (one port -> move the cable, no button).
- pmsg readout after a watchdog/panic reset: boot to Android, `su -c 'cat
  /sys/fs/pstore/pmsg-ramoops-0'`. A power-hold (longkey/master_dc) WIPES pmsg.
- ACM applet-shadow quirk: the merged Android ramdisk shadows busybox applet symlinks; re-run
  `/bin/busybox --install -s /bin` per ACM session, or read files with a builtin
  `while read`/`case` loop.

## Remaining work

- KMS console/compositor on card0 for standalone on-screen use + the USB keyboard: init the
  ArchLinuxARM keyring (`pacman-key --init && pacman-key --populate archlinuxarm`), then
  install `cage`+`foot` or `weston` (self-contained terminal) or `kmscon` (AUR). Launch on
  /dev/dri/card0.
- Fix `modeset_test` CRTC assignment to light both fold panels (or rely on the compositor).
- Optional cleanup: tidy the init's module-load ordering; the display currently loads via a
  full fixpoint after the drive mount.

## Artifacts and file locations

- The RAM-boot image is built per the section above; it is not committed here.
- Arch drive: USB, ext4, `LABEL=archroot`, ArchLinuxARM aarch64. The init finds it by label,
  so any other disk on the bus is ignored.
- Toolchain for the modeset tester: NDK r27,
  `aarch64-linux-android35-clang -static` with the NDK's own drm headers.
