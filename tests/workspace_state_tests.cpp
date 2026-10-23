#include "session/WorkspaceState.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "workspace_state_tests: " << message << '\n';
        ++failures;
    }
}

struct TempDirectory {
    std::filesystem::path path;

    TempDirectory() {
        const auto stamp =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("litemark-workspace-tests-" + std::to_string(stamp));
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void TestRoundTrip() {
    TempDirectory temp;
    litemark::WorkspaceStore store(temp.path / "nested" / "workspace.ini");
    litemark::WorkspaceState state;
    state.hasWindowPlacement = true;
    state.windowX = -120;
    state.windowY = 48;
    state.windowWidth = 1280;
    state.windowHeight = 900;
    state.maximized = true;
    state.outlineState = litemark::OutlineState::Collapsed;
    state.selectedTab = 1;
    state.tabs = {
        {"C:\\文档\\alpha.md", 320},
        {"C:\\文档\\beta.md", 1450},
    };
    state.recentFiles = {"C:\\文档\\beta.md", "C:\\文档\\alpha.md"};

    std::string error;
    Check(store.Save(state, &error), "workspace state should save");
    Check(error.empty(), "successful save should not report an error");

    const auto loaded = store.Load();
    Check(loaded.hasWindowPlacement, "window placement flag should round-trip");
    Check(loaded.windowX == -120 && loaded.windowY == 48,
          "window position should round-trip");
    Check(loaded.windowWidth == 1280 && loaded.windowHeight == 900,
          "window size should round-trip");
    Check(loaded.maximized, "maximized state should round-trip");
    Check(loaded.outlineState == litemark::OutlineState::Collapsed,
          "collapsed outline state should round-trip");
    Check(loaded.selectedTab == 1, "selected tab should round-trip");
    Check(loaded.tabs.size() == 2, "saved tab count should round-trip");
    Check(loaded.tabs[0].scrollY == 320 &&
              loaded.tabs[1].scrollY == 1450,
          "tab scroll positions should round-trip");
    Check(loaded.recentFiles == state.recentFiles,
          "recent file order should round-trip");
}

void TestMissingFileUsesDefaults() {
    TempDirectory temp;
    litemark::WorkspaceStore store(temp.path / "missing.ini");
    const auto state = store.Load();
    Check(!state.hasWindowPlacement, "missing state should not restore geometry");
    Check(state.tabs.empty(), "missing state should not restore tabs");
    Check(state.recentFiles.empty(), "missing state should not have recents");
}

void TestCorruptFileUsesDefaults() {
    TempDirectory temp;
    std::filesystem::create_directories(temp.path);
    const auto path = temp.path / "corrupt.ini";
    std::ofstream(path) << "version=99\nwindow.x=garbage\ntabs.count=999\n";
    const auto state = litemark::WorkspaceStore(path).Load();
    Check(!state.hasWindowPlacement,
          "unsupported version should be ignored");
    Check(state.tabs.empty(), "corrupt state should not restore tabs");
}

void TestRecentDeduplicatesAndCapsAtFive() {
    litemark::WorkspaceState state;
    litemark::WorkspaceStore::AddRecent(&state, "a");
    litemark::WorkspaceStore::AddRecent(&state, "b");
    litemark::WorkspaceStore::AddRecent(&state, "c");
    litemark::WorkspaceStore::AddRecent(&state, "d");
    litemark::WorkspaceStore::AddRecent(&state, "e");
    litemark::WorkspaceStore::AddRecent(&state, "c");
    Check(state.recentFiles.size() == 5, "recent list should retain five items");
    Check(state.recentFiles.front() == "c",
          "reopened file should move to the front");
    Check(std::count(state.recentFiles.begin(), state.recentFiles.end(), "c") == 1,
          "recent list should not contain duplicates");

    litemark::WorkspaceStore::AddRecent(&state, "f");
    Check(state.recentFiles.size() == 5,
          "recent list should remain capped at five items");
    Check(state.recentFiles.back() == "b",
          "oldest recent item should be removed");
}

void TestSelectedTabIsClamped() {
    TempDirectory temp;
    const auto path = temp.path / "workspace.ini";
    std::filesystem::create_directories(temp.path);
    std::ofstream(path)
        << "version=1\n"
        << "tabs.selected=99\n"
        << "tabs.count=2\n"
        << "tab.0.path=a.md\n"
        << "tab.1.path=b.md\n";
    const auto state = litemark::WorkspaceStore(path).Load();
    Check(state.selectedTab == 1,
          "selected tab should clamp to the saved tab range");
}

void TestLegacyOutlineVisibilityMigrates() {
    TempDirectory temp;
    const auto path = temp.path / "workspace.ini";
    std::filesystem::create_directories(temp.path);
    std::ofstream(path)
        << "version=1\n"
        << "outline.visible=1\n";
    Check(litemark::WorkspaceStore(path).Load().outlineState ==
              litemark::OutlineState::Expanded,
          "legacy visible outline should migrate to expanded");

    std::ofstream(path, std::ios::trunc)
        << "version=1\n"
        << "outline.visible=0\n";
    Check(litemark::WorkspaceStore(path).Load().outlineState ==
              litemark::OutlineState::Hidden,
          "legacy hidden outline should migrate to hidden");
}

}  // namespace

int main() {
    TestRoundTrip();
    TestMissingFileUsesDefaults();
    TestCorruptFileUsesDefaults();
    TestRecentDeduplicatesAndCapsAtFive();
    TestSelectedTabIsClamped();
    TestLegacyOutlineVisibilityMigrates();
    if (failures != 0) {
        std::cerr << "workspace_state_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "workspace_state_tests: 6 cases passed\n";
    return EXIT_SUCCESS;
}
