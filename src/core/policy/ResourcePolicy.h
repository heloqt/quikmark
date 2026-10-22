#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace litemark {

enum class ResourceReferenceKind {
    Unsupported,
    LocalOrRelative,
    Http,
    Https,
    Mailto
};

inline ResourceReferenceKind ClassifyResourceReference(
    std::string_view reference) {
    if (reference.empty()) {
        return ResourceReferenceKind::Unsupported;
    }

    const std::size_t colon = reference.find(':');
    if (colon == std::string_view::npos) {
        return ResourceReferenceKind::LocalOrRelative;
    }

    // A Windows drive path such as C:\pictures\a.png is local, not a URI.
    const bool windowsDrive =
        colon == 1 && std::isalpha(
                          static_cast<unsigned char>(reference.front())) &&
        reference.size() > 2 &&
        (reference[2] == '/' || reference[2] == '\\');
    if (windowsDrive) {
        return ResourceReferenceKind::LocalOrRelative;
    }

    std::string scheme(reference.substr(0, colon));
    if (scheme.empty() ||
        !std::isalpha(static_cast<unsigned char>(scheme.front())) ||
        std::any_of(scheme.begin() + 1, scheme.end(), [](unsigned char ch) {
            return !std::isalnum(ch) && ch != '+' && ch != '-' && ch != '.';
        })) {
        return ResourceReferenceKind::LocalOrRelative;
    }
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    if (colon + 1 >= reference.size()) {
        return ResourceReferenceKind::Unsupported;
    }
    if (scheme == "http") {
        return ResourceReferenceKind::Http;
    }
    if (scheme == "https") {
        return ResourceReferenceKind::Https;
    }
    if (scheme == "mailto") {
        return ResourceReferenceKind::Mailto;
    }
    return ResourceReferenceKind::Unsupported;
}

inline bool IsRemoteResource(ResourceReferenceKind kind) {
    return kind == ResourceReferenceKind::Http ||
           kind == ResourceReferenceKind::Https;
}

inline bool IsSupportedImageExtension(std::string_view extension) {
    std::string value(extension);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value == "png" || value == "jpg" || value == "jpeg" ||
           value == "bmp" || value == "gif";
}

}  // namespace litemark
