#!/bin/bash
# Windows 10 VM 설치 및 USB Passthrough 설정
# Usage: sudo ./setup_vm.sh <windows10.iso>
#
# 사전 요구사항:
# - qemu-kvm, libvirt, ovmf 패키지 설치됨
# - Windows 10/11 ISO 파일
# - 지문 센서가 USB에 연결됨 (04e8:730b)

set -e

ISO_PATH="${1:?Usage: $0 <windows10.iso>}"
VM_NAME="win10-fp-capture"
VM_DISK="/var/lib/libvirt/images/${VM_NAME}.qcow2"
VM_RAM="4096"   # 4GB
VM_CPUS="2"
DISK_SIZE="40"  # 40GB

# 색상
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN} Windows 10 VM Setup for USB Capture${NC}"
echo -e "${GREEN}============================================${NC}"

# 1. ISO 확인
if [ ! -f "$ISO_PATH" ]; then
    echo -e "${RED}[ERROR] ISO file not found: $ISO_PATH${NC}"
    exit 1
fi
echo -e "${GREEN}[OK]${NC} ISO: $ISO_PATH"

# 2. 기존 VM 확인
if virsh list --all | grep -q "$VM_NAME"; then
    echo -e "${YELLOW}[WARN] VM '$VM_NAME' already exists.${NC}"
    read -p "Delete and recreate? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        virsh destroy "$VM_NAME" 2>/dev/null || true
        virsh undefine "$VM_NAME" --remove-all-storage 2>/dev/null || true
    else
        echo "Aborted."
        exit 0
    fi
fi

# 3. 디스크 이미지 생성
echo "[*] Creating disk image: $VM_DISK (${DISK_SIZE}GB)..."
qemu-img create -f qcow2 "$VM_DISK" "${DISK_SIZE}G"

# 4. VM 생성 (UEFI, VirtIO)
echo "[*] Creating VM..."
virt-install \
    --name "$VM_NAME" \
    --ram "$VM_RAM" \
    --vcpus "$VM_CPUS" \
    --os-variant win10 \
    --boot uefi \
    --disk path="$VM_DISK",size="$DISK_SIZE",bus=virtio,format=qcow2 \
    --cdrom "$ISO_PATH" \
    --network network=default,model=virtio \
    --graphics vnc,listen=0.0.0.0,port=5900 \
    --video qxl \
    --channel spicevmc \
    --noautoconsole \
    --wait -1

echo -e "${GREEN}[OK]${NC} VM created: $VM_NAME"
echo ""
echo "=== 다음 단계 ==="
echo "1. VNC로 연결: vncviewer localhost:5900"
echo "   또는 virt-manager GUI 사용"
echo ""
echo "2. Windows 설치 완료 후, USB passthrough 설정:"
echo "   virsh attach-device $VM_NAME --file usb_passthrough.xml --live"
echo ""
echo "3. USB 캡처 시작:"
echo "   sudo tshark -i usbmon3 -w captures/windows_init.pcapng"
