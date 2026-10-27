#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version=""
exe="${project_root}/build/linux-release/bin/quikmark"
wx_lib_dir="/opt/wxWidgets-3.3.0/lib"
output=""

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
        --output)
            output="$2"
            shift 2
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

for command_name in dpkg-deb dpkg-shlibdeps patchelf desktop-file-validate; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Required command not found: ${command_name}" >&2
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

work_dir="${project_root}/build/deb-package"
package_name="quikmark_${version}_amd64"
stage="${work_dir}/${package_name}"
desktop_source="${project_root}/packaging/debian/quikmark.desktop"
mime_source="${project_root}/packaging/debian/quikmark.xml"
license_source="${project_root}/LICENSE"
notice_source="${project_root}/THIRD_PARTY_NOTICES.md"
usage_source="${project_root}/doc/12-使用说明.md"
usage_en_source="${project_root}/doc/12-Usage.md"
locale_source="${project_root}/assets/locale/zh_CN/LC_MESSAGES/quikmark.mo"
wx_locale_source="${project_root}/assets/locale/zh_CN/LC_MESSAGES/wxstd-3.3.mo"

rm -rf "${stage}"
mkdir -p \
    "${stage}/DEBIAN" \
    "${stage}/usr/bin" \
    "${stage}/usr/lib/quikmark" \
    "${stage}/usr/share/applications" \
    "${stage}/usr/share/doc/quikmark" \
    "${stage}/usr/share/quikmark" \
    "${stage}/usr/share/quikmark/locale/zh_CN/LC_MESSAGES" \
    "${stage}/usr/share/mime/packages"

install -m 0755 "${exe}" "${stage}/usr/bin/quikmark"
for library_name in \
    libwx_gtk3u_core-3.3.so.0.0.0 \
    libwx_baseu-3.3.so.0.0.0; do
    cp -a "${wx_lib_dir}/${library_name}" \
        "${stage}/usr/lib/quikmark/${library_name}"
    ln -s "${library_name}" \
        "${stage}/usr/lib/quikmark/${library_name%.0.0.0}.0"
done

patchelf --set-rpath '$ORIGIN/../lib/quikmark' \
    "${stage}/usr/bin/quikmark"
patchelf --set-rpath '$ORIGIN' \
    "${stage}/usr/lib/quikmark/libwx_gtk3u_core-3.3.so.0.0.0"
patchelf --set-rpath '$ORIGIN' \
    "${stage}/usr/lib/quikmark/libwx_baseu-3.3.so.0.0.0"

install -m 0644 "${desktop_source}" \
    "${stage}/usr/share/applications/quikmark.desktop"
install -m 0644 "${mime_source}" \
    "${stage}/usr/share/mime/packages/quikmark.xml"
install -m 0644 "${license_source}" \
    "${stage}/usr/share/doc/quikmark/copyright"
install -m 0644 "${notice_source}" \
    "${stage}/usr/share/doc/quikmark/THIRD_PARTY_NOTICES.md"
install -m 0644 "${usage_source}" \
    "${stage}/usr/share/doc/quikmark/使用说明.md"
install -m 0644 "${usage_en_source}" \
    "${stage}/usr/share/doc/quikmark/Usage.md"
install -m 0644 "${usage_source}" \
    "${stage}/usr/share/quikmark/使用说明.md"
install -m 0644 "${usage_en_source}" \
    "${stage}/usr/share/quikmark/Usage.md"
install -m 0644 "${locale_source}" \
    "${stage}/usr/share/quikmark/locale/zh_CN/LC_MESSAGES/quikmark.mo"
install -m 0644 "${wx_locale_source}" \
    "${stage}/usr/share/quikmark/locale/zh_CN/LC_MESSAGES/wxstd-3.3.mo"
install -m 0644 "${notice_source}" \
    "${stage}/usr/share/quikmark/THIRD_PARTY_NOTICES.md"
desktop-file-validate \
    "${stage}/usr/share/applications/quikmark.desktop"

shlib_source="${work_dir}/debian"
mkdir -p "${shlib_source}"
cat > "${shlib_source}/control" <<EOF
Source: quikmark
Section: utils
Priority: optional
Maintainer: QuikMark contributors <noreply@localhost>
Standards-Version: 4.6.2

Package: quikmark
Architecture: any
Depends: \${shlibs:Depends}, \${misc:Depends}
Description: Lightweight native Markdown reader
 QuikMark is a native, lightweight Markdown reader built with wxWidgets.
EOF

dependencies="$(
    cd "${work_dir}"
    dpkg-shlibdeps -O \
        -l"${stage}/usr/lib/quikmark" \
        -e"${stage}/usr/bin/quikmark" \
        -e"${stage}/usr/lib/quikmark/libwx_gtk3u_core-3.3.so.0.0.0" \
        -e"${stage}/usr/lib/quikmark/libwx_baseu-3.3.so.0.0.0" \
        2>/dev/null |
        sed -n 's/^shlibs:Depends=//p'
)"
if [[ -z "${dependencies}" ]]; then
    echo "dpkg-shlibdeps did not produce dependencies." >&2
    exit 1
fi
dependencies="${dependencies}, xdg-utils, desktop-file-utils, shared-mime-info"

installed_size="$(du -sk "${stage}" | awk '{print $1}')"
cat > "${stage}/DEBIAN/control" <<EOF
Package: quikmark
Version: ${version}
Section: utils
Priority: optional
Architecture: amd64
Installed-Size: ${installed_size}
Maintainer: QuikMark contributors <noreply@localhost>
Depends: ${dependencies}
Description: Lightweight native Markdown reader
 QuikMark is a native, lightweight Markdown reader built with wxWidgets.
EOF

if [[ -z "${output}" ]]; then
    output="${project_root}/dist/quikmark_${version}_amd64.deb"
elif [[ "${output}" != /* ]]; then
    output="${project_root}/${output}"
fi
mkdir -p "$(dirname "${output}")"
dpkg-deb --root-owner-group --build "${stage}" "${output}"
package_hash="$(sha256sum "${output}" | awk '{print $1}')"
printf '%s  %s\n' "${package_hash}" "$(basename "${output}")" \
    > "${output}.sha256"

echo "Packaged: ${output}"
dpkg-deb --field "${output}" Package Version Architecture Depends
