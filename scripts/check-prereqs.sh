#!/usr/bin/env bash
# Verify the tools needed to build, test and run the lab.
#   (no option) everything for development
#   --e2e       only what `make e2e` itself needs (the suite crafts packets with scapy)
#   --kvm       what the optional KVM lab mode needs, plus KVM itself (contracts/lab-kvm.md):
#               exit 2 missing tools, 3 no KVM on this host, 4 KVM present but not usable by
#               the developer ($SUDO_UID; checked only when it is set and not 0)
set -u
export PATH="$PATH:/usr/sbin:/sbin" # arping, sysctl, ... live here on Debian
mode="${1:-}"
need="gcc clang cmake gdb ip arping tcpdump iperf3 socat jq python3"
install="  sudo apt install build-essential cmake clang gdb iproute2 iputils-ping arping \\
                   tcpdump iperf3 socat jq python3-venv"
case "$mode" in
    --e2e) need="gcc cmake ip socat jq python3" ;;
    --kvm)
        need="qemu-system-x86_64 qemu-img qemu-io genisoimage debugfs e2fsck ssh ssh-keygen jq ip python3 setpriv curl"
        install="  sudo apt install qemu-system-x86 qemu-utils genisoimage e2fsprogs openssh-client"
        ;;
    "") ;;
    *) echo "usage: $0 [--e2e | --kvm]" >&2; exit 2 ;;
esac

missing=""
for t in $need; do
    command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
done
if [ -n "$missing" ]; then
    echo "missing tools:$missing" >&2
    echo "install with:" >&2
    echo "$install" >&2
    exit 2
fi

if [ "$mode" = "--kvm" ]; then
    dev="${MINIVR_KVM_DEVICE:-/dev/kvm}"
    if [ ! -e "$dev" ]; then
        echo "KVM unavailable: $dev missing (enable VT-x/AMD-V; in VMware: \"Virtualize Intel VT-x/EPT\")" >&2
        if [ "$(grep -cE 'vmx|svm' /proc/cpuinfo)" -eq 0 ]; then
            echo "  the CPU flags show no vmx/svm: hardware virtualization is off for this machine" >&2
        fi
        exit 3
    fi
    uid="${SUDO_UID:-}"
    if [ -n "$uid" ] && [ "$uid" != 0 ]; then
        user="${SUDO_USER:-uid $uid}"
        if ! setpriv --reuid "$uid" --regid "${SUDO_GID:-$uid}" --init-groups \
                test -r "$dev" -a -w "$dev" 2>/dev/null; then
            echo "KVM present but $dev not accessible to $user (add to group kvm)" >&2
            exit 4
        fi
    fi
fi
echo "prerequisites OK"
