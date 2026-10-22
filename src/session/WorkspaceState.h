#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace litemark {

struct SavedTabState {
    std::string path;
    int scrollY = 0;
};

struct WorkspaceState {
    bool hasWindowPlacement = false;
    int windowX = 0;
    int windowY = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    bool maximized = false;
    std::size_t selectedTab = 0;
    std::vector<SavedTabState> tabs;
    std::vector<std::string> recentFiles;
};

class WorkspaceStore {
public:
    explicit WorkspaceStore(std::filesystem::path path);

    WorkspaceState Load() const;
    bool Save(const WorkspaceState& state, std::string* error = nullptr) const;

    static void AddRecent(WorkspaceState* state, const std::string& path,
                          std::size_t limit = 5);

private:
    std::filesystem::path path_;
};

}  // namespace litemark
