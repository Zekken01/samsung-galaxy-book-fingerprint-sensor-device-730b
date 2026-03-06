#!/bin/bash
# Samsung/CanvasBio Fingerprint Sensor USB Traffic Capture
# Usage: sudo ./usb_capture.sh [output_name]

set -e

CAPTURE_DIR="$(dirname "$0")/../captures"
mkdir -p "$CAPTURE_DIR"

OUTPUT_NAME="${1:-trace_$(date +%Y%m%d_%H%M%S)}"
OUTPUT_FILE="$CAPTURE_DIR/${OUTPUT_NAME}.pcapng"

# usbmon 모듈 로드
modprobe usbmon 2>/dev/null || true

# 장치 찾기
DEVICE_INFO=$(lsusb -d 04e8:730b 2>/dev/null)
if [ -z "$DEVICE_INFO" ]; then
    echo "[ERROR] Fingerprint sensor (04e8:730b) not found!"
    echo "  노트북 재부팅이 필요할 수 있습니다."
    exit 1
fi

BUS=$(echo "$DEVICE_INFO" | grep -oP 'Bus \K\d+')
ADDR=$(echo "$DEVICE_INFO" | grep -oP 'Device \K\d+')
BUS_NUM=$((10#$BUS))

echo "============================================"
echo "USB Traffic Capture"
echo "============================================"
echo "  Device: $DEVICE_INFO"
echo "  Bus: $BUS_NUM, Address: $ADDR"
echo "  Interface: usbmon${BUS_NUM}"
echo "  Output: $OUTPUT_FILE"
echo ""
echo "  Press Ctrl+C to stop capture"
echo "============================================"

# tshark로 캡처 (해당 버스의 해당 장치만 필터)
tshark -i "usbmon${BUS_NUM}" \
    -f "usb.addr == ${BUS_NUM}.${ADDR}" \
    -w "$OUTPUT_FILE" \
    2>&1

echo ""
echo "[OK] Capture saved: $OUTPUT_FILE"
echo "  분석: tshark -r $OUTPUT_FILE -V | less"
