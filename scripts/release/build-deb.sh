#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TOD_DIR="${REPO_ROOT}/libfprint-tod-canvasbio"
DIST_DIR="${REPO_ROOT}/dist"

VERSION=""

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--version <version>]

Builds a Debian package for Ubuntu deployment.

Options:
  --version <version>   Package version (default: latest git tag or 0.2.0)
  -h, --help            Show this help
USAGE
}

log() { printf '[*] %s\n' "$*"; }
ok() { printf '[OK] %s\n' "$*"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="${2:-}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
  shift
done

if [[ -z "$VERSION" ]]; then
  if git -C "$REPO_ROOT" describe --tags --abbrev=0 >/tmp/canvasbio_latest_tag 2>/dev/null; then
    VERSION="$(cat /tmp/canvasbio_latest_tag | sed 's/^v//')"
  else
    VERSION="0.2.0"
  fi
fi

ARCH="$(dpkg --print-architecture)"
PKG_NAME="canvasbio-fp"
PKG_DIR="$(mktemp -d)"
PKG_ROOT="${PKG_DIR}/${PKG_NAME}_${VERSION}_${ARCH}"
OUT_DEB="${DIST_DIR}/${PKG_NAME}_${VERSION}_${ARCH}.deb"
TOD_TARGET_DIR="/usr/lib/x86_64-linux-gnu/libfprint-2/tod-1"

trap 'rm -rf "$PKG_DIR" /tmp/canvasbio_latest_tag' EXIT

log "Building release binaries"
(
  cd "$REPO_ROOT"
  cargo build --release
)

log "Building TOD shared object"
(
  cd "$TOD_DIR"
  meson setup build --reconfigure
  ninja -C build
)

TOD_SO_PATH="$(find "$TOD_DIR/build" -maxdepth 2 -type f -name 'liblibfprint-tod-canvasbio.so' | head -n 1)"
if [[ -z "$TOD_SO_PATH" ]]; then
  echo "TOD shared object not found in build output" >&2
  exit 1
fi

log "Preparing Debian package layout"
mkdir -p \
  "$PKG_ROOT/DEBIAN" \
  "$PKG_ROOT/usr/bin" \
  "$PKG_ROOT/usr/lib/udev/rules.d" \
  "$PKG_ROOT${TOD_TARGET_DIR}" \
  "$PKG_ROOT/usr/share/doc/${PKG_NAME}"

cp "$REPO_ROOT/target/release/canvasbio-fp" "$PKG_ROOT/usr/bin/"
cp "$REPO_ROOT/target/release/analyze-capture" "$PKG_ROOT/usr/bin/"
cp "$REPO_ROOT/target/release/extract-image" "$PKG_ROOT/usr/bin/"
cp "$TOD_SO_PATH" "$PKG_ROOT${TOD_TARGET_DIR}/"
cp "$TOD_DIR/60-libfprint-canvasbio.rules" "$PKG_ROOT/usr/lib/udev/rules.d/"
cp "$REPO_ROOT/README.md" "$PKG_ROOT/usr/share/doc/${PKG_NAME}/README.md"

cat > "$PKG_ROOT/DEBIAN/control" <<CONTROL
Package: ${PKG_NAME}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: Choi Won <help@myso.kr>
Depends: libusb-1.0-0, libfprint-2-tod1, fprintd
Description: CanvasBio fingerprint driver and TOD plugin for Samsung Galaxy Book
 Includes CLI tools and libfprint TOD plugin artifacts for USB 04e8:730b.
CONTROL

cat > "$PKG_ROOT/DEBIAN/postinst" <<'POSTINST'
#!/usr/bin/env bash
set -e
udevadm control --reload-rules || true
udevadm trigger || true
systemctl restart fprintd || pkill -9 fprintd || true
cat <<MSG
[canvasbio-fp] Installation completed.
Next steps:
  1) fprintd-enroll -f right-index-finger
  2) fprintd-verify -f right-index-finger
  3) Optional: run scripts/ops/install-ubuntu-login.sh --configure-pam-force from source repo
MSG
POSTINST
chmod 755 "$PKG_ROOT/DEBIAN/postinst"

mkdir -p "$DIST_DIR"
log "Building .deb package"
dpkg-deb --build "$PKG_ROOT" "$OUT_DEB" >/dev/null

ok "Debian package created: $OUT_DEB"
