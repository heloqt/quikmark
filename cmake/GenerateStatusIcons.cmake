if(NOT DEFINED STATUS_ICON_MANIFEST OR
   NOT DEFINED STATUS_ICON_SOURCE_DIR OR
   NOT DEFINED STATUS_ICON_HEADER OR
   NOT DEFINED STATUS_ICON_SOURCE)
    message(FATAL_ERROR "Status icon generation paths are incomplete")
endif()

file(STRINGS "${STATUS_ICON_MANIFEST}" entries)
if(NOT entries)
    message(FATAL_ERROR "Status icon manifest is empty")
endif()

get_filename_component(header_dir "${STATUS_ICON_HEADER}" DIRECTORY)
get_filename_component(source_dir "${STATUS_ICON_SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${header_dir}" "${source_dir}")

set(header_content
"#pragma once

#include <cstddef>
#include <cstdint>

namespace litemark::detail {

enum class StatusIconId : std::uint8_t {
")
set(source_content
"#include \"litemark/StatusIconsGenerated.h\"

namespace litemark::detail {
")
set(code_point_cases "")
set(asset_cases "")

foreach(entry IN LISTS entries)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 id)
    list(GET fields 1 code_point)
    list(GET fields 2 asset_name)
    set(png_path "${STATUS_ICON_SOURCE_DIR}/${asset_name}.png")
    if(NOT EXISTS "${png_path}")
        message(FATAL_ERROR "Missing status icon: ${png_path}")
    endif()

    file(READ "${png_path}" png_hex HEX)
    string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1," bytes
        "${png_hex}")
    string(APPEND header_content "    ${id},\n")
    string(APPEND source_content
        "static const unsigned char k${id}[] = {${bytes}};\n\n")
    string(APPEND code_point_cases
        "        case 0x${code_point}U:\n"
        "            *icon = StatusIconId::${id};\n"
        "            return true;\n")
    string(APPEND asset_cases
        "        case StatusIconId::${id}:\n"
        "            bytes = k${id};\n"
        "            size = sizeof(k${id});\n"
        "            break;\n")
endforeach()

string(APPEND header_content
"    Count
};

struct StatusIconAsset {
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
};

bool TryStatusIconForCodePoint(std::uint32_t codePoint, StatusIconId* icon);
bool GetStatusIconAsset(StatusIconId id, StatusIconAsset* asset);

}  // namespace litemark::detail
")

string(APPEND source_content
"bool TryStatusIconForCodePoint(std::uint32_t codePoint, StatusIconId* icon) {
    if (icon == nullptr) {
        return false;
    }
    switch (codePoint) {
${code_point_cases}        default:
            return false;
    }
}

bool GetStatusIconAsset(StatusIconId id, StatusIconAsset* asset) {
    if (asset == nullptr) {
        return false;
    }
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    switch (id) {
${asset_cases}        default:
            return false;
    }
    asset->bytes = bytes;
    asset->size = size;
    return true;
}

}  // namespace litemark::detail
")

file(WRITE "${STATUS_ICON_HEADER}" "${header_content}")
file(WRITE "${STATUS_ICON_SOURCE}" "${source_content}")
