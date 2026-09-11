#!/bin/bash
set -e
MNT="${1:-/run/media/$USER/archroot}"
[ -d "$MNT/etc/systemd/system" ] || { echo "not an Arch rootfs at $MNT"; exit 1; }

# 1. late report: proof Arch reached multi-user -> /var/log on the drive + pmsg/kmsg
cat > "$MNT/etc/systemd/system/linuxboot-report.service" <<'U'
[Unit]
Description=linux-boot proof and report
After=multi-user.target
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c '{ echo "==== linuxboot $(date -u) ===="; uname -a; echo "system: $(systemctl is-system-running)"; echo "root: $(findmnt -no SOURCE,FSTYPE /)"; echo "uptime:$(cat /proc/uptime)"; lsblk -o NAME,SIZE,LABEL,FSTYPE 2>/dev/null; ip -o addr 2>/dev/null; echo ====; } >> /var/log/linuxboot-boot.log 2>&1; echo "[arch] MULTIUSER $(uname -r)" > /dev/pmsg0 2>/dev/null; echo "[arch] MULTIUSER reached" > /dev/kmsg 2>/dev/null'
[Install]
WantedBy=multi-user.target
U

# 2. very early marker (pmsg/kmsg only; rootfs may still be ro)
cat > "$MNT/etc/systemd/system/linuxboot-early.service" <<'U'
[Unit]
Description=linux-boot early marker
DefaultDependencies=no
After=sysinit.target
Before=basic.target
[Service]
Type=oneshot
ExecStart=/bin/sh -c 'echo "[arch] EARLY $(uname -r)" > /dev/pmsg0 2>/dev/null; echo "[arch] EARLY reached" > /dev/kmsg 2>/dev/null'
[Install]
WantedBy=sysinit.target
U

mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants" "$MNT/etc/systemd/system/sysinit.target.wants"
ln -sf ../linuxboot-report.service "$MNT/etc/systemd/system/multi-user.target.wants/linuxboot-report.service"
ln -sf ../linuxboot-early.service  "$MNT/etc/systemd/system/sysinit.target.wants/linuxboot-early.service"

# 3. root autologin on tty1
mkdir -p "$MNT/etc/systemd/system/getty@tty1.service.d"
cat > "$MNT/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'U'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I linux
U

# 4. NEVER auto-suspend (fold hall-sensor + logind lid/power defaults sleep the phone)
mkdir -p "$MNT/etc/systemd/logind.conf.d"
cat > "$MNT/etc/systemd/logind.conf.d/nosuspend.conf" <<'U'
[Login]
HandleLidSwitch=ignore
HandleLidSwitchExternalPower=ignore
HandleLidSwitchDocked=ignore
HandlePowerKey=ignore
HandleSuspendKey=ignore
HandleHibernateKey=ignore
IdleAction=ignore
U
for t in sleep.target suspend.target hibernate.target hybrid-sleep.target suspend-then-hibernate.target; do
  ln -sf /dev/null "$MNT/etc/systemd/system/$t"
done

sync
echo "=== A + no-suspend applied to $MNT ==="
echo "logind.conf.d:"; ls -l "$MNT/etc/systemd/logind.conf.d/" 2>&1
echo "masked sleep targets:"; ls -l "$MNT/etc/systemd/system/"*.target 2>&1 | grep -E 'sleep|suspend|hibernate'

# 5. ssh key -> passwordless root login over SSH (ArchLinuxARM sshd is key-only for root)
PUBKEY_FILE="${2:-$HOME/.ssh/id_rsa.pub}"
[ -r "$PUBKEY_FILE" ] || { echo "no readable public key at $PUBKEY_FILE; pass one as arg 2"; exit 1; }
PUBKEY=$(cat "$PUBKEY_FILE")
install -d -m700 "$MNT/root/.ssh"
grep -qF "$PUBKEY" "$MNT/root/.ssh/authorized_keys" 2>/dev/null || echo "$PUBKEY" >> "$MNT/root/.ssh/authorized_keys"
chmod 600 "$MNT/root/.ssh/authorized_keys"
echo "authorized_keys now has $(wc -l < "$MNT/root/.ssh/authorized_keys") key(s)"
