#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version=""
exe="${project_root}/build/linux-release/bin/quikmark"
wx_lib_dir="/opt/wxWidgets-3.3.0/lib"
tools_dir="${project_root}/build/appimage-tools"
output=""
skip_smoke_test=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            version="$2"
            shift 2
            ;;
        --exe)
            exe="$2"
            shift 2
            ;;
        --wx-lib-dir)
            wx_lib_dir="$2"
            shift 2
            ;;
        --tools-dir)
            tools_dir="$2"
            shift 2
            ;;
        --output)
            output="$2"
            shift 2
            ;;
        --skip-smoke-test)
            skip_smoke_test=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [[ -z "${version}" ]]; then
    version="$(sed -nE \
        's/.*project\(QuikMark VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' \
        "${project_root}/CMakeLists.txt" | head -n 1)"
fi
if [[ -z "${version}" ]]; then
    echo "Unable to read the QuikMark version." >&2
    exit 1
fi

for command_name in patchelf desktop-file-validate ldd; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Required command not found: ${command_name}" >&2
        exit 1
    fi
done
if ! command -v sha256sum >/dev/null 2>&1; then
    echo "Required command not found: sha256sum" >&2
    exit 1
fi

linuxdeploy="${tools_dir}/linuxdeploy-x86_64.AppImage"
appimagetool="${tools_dir}/appimagetool-x86_64.AppImage"
runtime="${tools_dir}/runtime-x86_64"
gtk_plugin="${tools_dir}/linuxdeploy-plugin-gtk.sh"
for tool in "${linuxdeploy}" "${appimagetool}" "${runtime}" "${gtk_plugin}"; do
    if [[ ! -x "${tool}" ]]; then
        echo "AppImage tool not found or not executable: ${tool}" >&2
        echo "Run scripts/setup-appimage-tools.sh first." >&2
        exit 1
    fi
done

if [[ ! -x "${exe}" ]]; then
    echo "Linux executable not found: ${exe}" >&2
    exit 1
fi
for library_name in \
    libwx_gtk3u_core-3.3.so.0.0.0 \
    libwx_baseu-3.3.so.0.0.0; do
    if [[ ! -f "${wx_lib_dir}/${library_name}" ]]; then
        echo "wxWidgets library not found: ${wx_lib_dir}/${library_name}" >&2
        exit 1
    fi
done

desktop_source="${project_root}/packaging/appimage/quikmark.desktop"
icon_source="${project_root}/packaging/appimage/quikmark.svg"
mime_source="${project_root}/packaging/debian/quikmark.xml"
license_source="${project_root}/LICENSE"
notice_source="${project_root}/THIRD_PARTY_NOTICES.md"
usage_source="${project_root}/doc/12-使用说明.md"
desktop-file-validate "${desktop_source}"

work_dir="${project_root}/build/appimage-package"
appdir="${work_dir}/QuikMark.AppDir"
case "${appdir}" in
    "${project_root}"/build/appimage-package/*) ;;
    *)
        echo "Refusing to remove unexpected AppDir: ${appdir}" >&2
        exit 1
        ;;
esac
rm -rf "${appdir}"
mkdir -p \
    "${appdir}/usr/share/applications" \
    "${appdir}/usr/share/icons/hicolor/scalable/apps" \
    "${appdir}/usr/share/quikmark" \
    "${appdir}/usr/share/mime/packages" \
    "${appdir}/usr/share/doc/quikmark"

export PATH="${tools_dir}:${PATH}"
export ARCH="x86_64"
export APPIMAGE_EXTRACT_AND_RUN=1
saved_library_path="${LD_LIBRARY_PATH-}"
export LD_LIBRARY_PATH="${wx_lib_dir}${saved_library_path:+:${saved_library_path}}"

"${linuxdeploy}" \
    --appdir "${appdir}" \
    --executable "${exe}" \
    --desktop-file "${desktop_source}" \
    --icon-file "${icon_source}" \
    --plugin gtk

if [[ -n "${saved_library_path}" ]]; then
    export LD_LIBRARY_PATH="${saved_library_path}"
else
    unset LD_LIBRARY_PATH
fi

install -D -m 0644 "${desktop_source}" \
    "${appdir}/usr/share/applications/quikmark.desktop"
install -D -m 0644 "${icon_source}" \
    "${appdir}/usr/share/icons/hicolor/scalable/apps/quikmark.svg"
install -D -m 0644 "${mime_source}" \
    "${appdir}/usr/share/mime/packages/quikmark.xml"
install -D -m 0644 "${license_source}" \
    "${appdir}/usr/share/doc/quikmark/LICENSE"
install -D -m 0644 "${notice_source}" \
    "${appdir}/usr/share/doc/quikmark/THIRD_PARTY_NOTICES.md"
install -D -m 0644 "${usage_source}" \
    "${appdir}/usr/share/doc/quikmark/使用说明.md"
install -D -m 0644 "${usage_source}" \
    "${appdir}/usr/share/quikmark/使用说明.md"
install -D -m 0644 "${notice_source}" \
    "${appdir}/usr/share/quikmark/THIRD_PARTY_NOTICES.md"

ln -sfn usr/share/applications/quikmark.desktop \
    "${appdir}/quikmark.desktop"
ln -sfn usr/share/icons/hicolor/scalable/apps/quikmark.svg \
    "${appdir}/quikmark.svg"
if [[ ! -e "${appdir}/AppRun" ]]; then
    echo "linuxdeploy did not create AppRun." >&2
    exit 1
fi

dependency_report="$(ldd "${appdir}/usr/bin/quikmark")"
if grep -q "not found" <<<"${dependency_report}"; then
    echo "${dependency_report}" >&2
    echo "AppDir contains unresolved runtime dependencies." >&2
    exit 1
fi

if [[ -z "${output}" ]]; then
    output="${project_root}/dist/QuikMark-${version}-x86_64.AppImage"
elif [[ "${output}" != /* ]]; then
    output="${project_root}/${output}"
fi
mkdir -p "$(dirname "${output}")"

"${appimagetool}" \
    --runtime-file "${runtime}" \
    --no-appstream \
    "${appdir}" \
    "${output}"
chmod 0755 "${output}"

package_hash="$(sha256sum "${output}" | awk '{print $1}')"
printf '%s  %s\n' "${package_hash}" "$(basename "${output}")" \
    > "${output}.sha256"

if [[ "${skip_smoke_test}" == false ]] &&
   command -v xvfb-run >/dev/null 2>&1 &&
   command -v timeout >/dev/null 2>&1; then
    smoke_config="${work_dir}/smoke-config"
    mkdir -p "${smoke_config}"
    LITEMARK_CONFIG_DIR="${smoke_config}" \
        timeout 30s xvfb-run -a "${output}" --smoke-test
    echo "AppImage Xvfb smoke test: passed"
elif [[ "${skip_smoke_test}" == false ]]; then
    echo "Warning: xvfb-run or timeout not found; smoke test skipped."
fi

echo "Packaged: ${output}"
echo "Size: $(stat -c '%s' "${output}") bytes"
echo "SHA256: ${package_hash}"
