#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tool_dir="${project_root}/build/appimage-tools"
if [[ $# -gt 0 ]]; then
    tool_dir="$1"
fi

linuxdeploy_version="1-alpha-20251107-1"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
appimagetool_sha256="95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6"
appimage_runtime_sha256="617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46"
gtk_plugin_commit="7a3fbc31a9e5075073ff8790f26effbac5f84453"
gtk_plugin_sha256="b0f4cbc684a0103a9651f0955b635eaea0096b3a66c0f5a2c2aa337960375171"

if command -v curl >/dev/null 2>&1; then
    download() {
        curl -fL --retry 3 --connect-timeout 15 "$1" -o "$2"
    }
elif command -v wget >/dev/null 2>&1; then
    download() {
        wget -O "$2" "$1"
    }
else
    echo "curl or wget is required to download AppImage tools." >&2
    exit 1
fi

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

install_verified() {
    local url="$1"
    local destination="$2"
    local expected="$3"
    local temporary="${destination}.download"

    if [[ -f "${destination}" ]] &&
       [[ "$(sha256_of "${destination}")" == "${expected}" ]]; then
        echo "Using existing: ${destination}"
        return
    fi

    rm -f "${temporary}"
    echo "Downloading: ${url}"
    download "${url}" "${temporary}"
    local actual
    actual="$(sha256_of "${temporary}")"
    if [[ "${actual}" != "${expected}" ]]; then
        rm -f "${temporary}"
        echo "SHA256 mismatch for ${url}" >&2
        echo "Expected: ${expected}" >&2
        echo "Actual:   ${actual}" >&2
        exit 1
    fi
    mv -f "${temporary}" "${destination}"
    chmod 0755 "${destination}"
}

mkdir -p "${tool_dir}"
tool_dir="$(cd "${tool_dir}" && pwd)"

install_verified \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/${linuxdeploy_version}/linuxdeploy-x86_64.AppImage" \
    "${tool_dir}/linuxdeploy-x86_64.AppImage" \
    "${linuxdeploy_sha256}"
install_verified \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
    "${tool_dir}/appimagetool-x86_64.AppImage" \
    "${appimagetool_sha256}"
install_verified \
    "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64" \
    "${tool_dir}/runtime-x86_64" \
    "${appimage_runtime_sha256}"
install_verified \
    "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/${gtk_plugin_commit}/linuxdeploy-plugin-gtk.sh" \
    "${tool_dir}/linuxdeploy-plugin-gtk.sh" \
    "${gtk_plugin_sha256}"

echo
echo "AppImage tools installed in: ${tool_dir}"
