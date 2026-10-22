#include "core/policy/ResourcePolicy.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "resource_policy_tests: " << message << '\n';
        ++failures;
    }
}

void TestExternalSchemes() {
    using litemark::ResourceReferenceKind;
    Check(litemark::ClassifyResourceReference("https://example.com/a") ==
              ResourceReferenceKind::Https,
          "HTTPS URL should be recognized");
    Check(litemark::ClassifyResourceReference("HTTP://example.com/a") ==
              ResourceReferenceKind::Http,
          "HTTP scheme should be case insensitive");
    Check(litemark::ClassifyResourceReference("mailto:user@example.com") ==
              ResourceReferenceKind::Mailto,
          "mailto URL should be recognized");
}

void TestLocalReferences() {
    using litemark::ResourceReferenceKind;
    Check(litemark::ClassifyResourceReference("images/diagram.png") ==
              ResourceReferenceKind::LocalOrRelative,
          "relative image path should be local");
    Check(litemark::ClassifyResourceReference("C:\\images\\diagram.png") ==
              ResourceReferenceKind::LocalOrRelative,
          "Windows absolute path should be local");
    Check(litemark::ClassifyResourceReference("C:/images/diagram.png") ==
              ResourceReferenceKind::LocalOrRelative,
          "Windows forward-slash path should be local");
    Check(litemark::ClassifyResourceReference("#heading") ==
              ResourceReferenceKind::LocalOrRelative,
          "internal anchor should not be treated as an external scheme");
}

void TestUnsupportedSchemes() {
    using litemark::ResourceReferenceKind;
    const char* references[] = {
        "",
        "file:///C:/secret.txt",
        "ftp://example.com/file",
        "data:image/png;base64,AAAA",
        "javascript:alert(1)",
        "https:",
    };
    for (const char* reference : references) {
        Check(litemark::ClassifyResourceReference(reference) ==
                  ResourceReferenceKind::Unsupported,
              "unsupported or empty reference was accepted");
    }
}

void TestImageExtensions() {
    const char* supported[] = {"png", "jpg", "jpeg", "bmp", "gif",
                               "PNG", "JpEg"};
    for (const char* extension : supported) {
        Check(litemark::IsSupportedImageExtension(extension),
              "required image extension should be supported");
    }
    const char* unsupported[] = {"", "svg", "webp", "avif", "exe"};
    for (const char* extension : unsupported) {
        Check(!litemark::IsSupportedImageExtension(extension),
              "unsupported image extension was accepted");
    }
}

void TestRemotePredicate() {
    using litemark::ResourceReferenceKind;
    Check(litemark::IsRemoteResource(ResourceReferenceKind::Http),
          "HTTP should be remote");
    Check(litemark::IsRemoteResource(ResourceReferenceKind::Https),
          "HTTPS should be remote");
    Check(!litemark::IsRemoteResource(ResourceReferenceKind::LocalOrRelative),
          "local reference should not be remote");
    Check(!litemark::IsRemoteResource(ResourceReferenceKind::Mailto),
          "mailto should not be treated as an image URL");
}

}  // namespace

int main() {
    TestExternalSchemes();
    TestLocalReferences();
    TestUnsupportedSchemes();
    TestImageExtensions();
    TestRemotePredicate();

    if (failures != 0) {
        std::cerr << "resource_policy_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "resource_policy_tests: 5 cases passed\n";
    return EXIT_SUCCESS;
}
