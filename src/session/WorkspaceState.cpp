#include "session/WorkspaceState.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <system_error>

namespace {

constexpr std::size_t kMaximumSavedTabs = 64;

std::string ReadValue(const std::map<std::string, std::string>& values,
                      const std::string& key,
                      const std::string& fallback = {}) {
    const auto found = values.find(key);
    return found == values.end() ? fallback : found->second;
}

int ReadInt(const std::map<std::string, std::string>& values,
            const std::string& key, int fallback = 0) {
    const std::string value = ReadValue(values, key);
    int parsed = 0;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc() &&
                   result.ptr == value.data() + value.size()
               ? parsed
               : fallback;
}

std::size_t ReadCount(const std::map<std::string, std::string>& values,
                      const std::string& key, std::size_t maximum) {
    const int value = ReadInt(values, key, 0);
    return value <= 0 ? 0 : std::min(maximum, static_cast<std::size_t>(value));
}

litemark::OutlineState ReadOutlineState(
    const std::map<std::string, std::string>& values) {
    const std::string value = ReadValue(values, "outline.state");
    if (value == "expanded") {
        return litemark::OutlineState::Expanded;
    }
    if (value == "collapsed") {
        return litemark::OutlineState::Collapsed;
    }
    if (value == "hidden") {
        return litemark::OutlineState::Hidden;
    }
    return ReadInt(values, "outline.visible", 0) != 0
               ? litemark::OutlineState::Expanded
               : litemark::OutlineState::Hidden;
}

void SetError(std::string* error, const std::string& value) {
    if (error != nullptr) {
        *error = value;
    }
}

}  // namespace

namespace litemark {

WorkspaceStore::WorkspaceStore(std::filesystem::path path)
    : path_(std::move(path)) {}

WorkspaceState WorkspaceStore::Load() const {
    WorkspaceState state;
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        return state;
    }

    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }

    if (ReadInt(values, "version", 0) != 1) {
        return state;
    }
    state.hasWindowPlacement = ReadInt(values, "window.valid", 0) != 0;
    state.windowX = ReadInt(values, "window.x", 0);
    state.windowY = ReadInt(values, "window.y", 0);
    state.windowWidth = std::max(0, ReadInt(values, "window.width", 0));
    state.windowHeight = std::max(0, ReadInt(values, "window.height", 0));
    state.maximized = ReadInt(values, "window.maximized", 0) != 0;
    state.outlineState = ReadOutlineState(values);

    const std::size_t tabCount =
        ReadCount(values, "tabs.count", kMaximumSavedTabs);
    for (std::size_t index = 0; index < tabCount; ++index) {
        const std::string prefix = "tab." + std::to_string(index) + ".";
        const std::string path = ReadValue(values, prefix + "path");
        if (path.empty()) {
            continue;
        }
        state.tabs.push_back(
            SavedTabState{path, std::max(0, ReadInt(values, prefix + "scroll", 0))});
    }
    state.selectedTab = state.tabs.empty()
                            ? 0
                            : std::min(
                                  state.tabs.size() - 1,
                                  static_cast<std::size_t>(
                                      std::max(0, ReadInt(values, "tabs.selected", 0))));

    const std::size_t recentCount =
        ReadCount(values, "recent.count", 5);
    for (std::size_t index = 0; index < recentCount; ++index) {
        const std::string path =
            ReadValue(values, "recent." + std::to_string(index));
        if (!path.empty()) {
            state.recentFiles.push_back(path);
        }
    }
    return state;
}

bool WorkspaceStore::Save(const WorkspaceState& state,
                          std::string* error) const {
    std::error_code filesystemError;
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path(),
                                            filesystemError);
    }
    if (filesystemError) {
        SetError(error, "无法创建状态目录");
        return false;
    }

    std::filesystem::path temporary = path_;
    temporary += L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        SetError(error, "无法写入状态文件");
        return false;
    }
    output << "version=1\n";
    output << "window.valid=" << (state.hasWindowPlacement ? 1 : 0) << '\n';
    output << "window.x=" << state.windowX << '\n';
    output << "window.y=" << state.windowY << '\n';
    output << "window.width=" << state.windowWidth << '\n';
    output << "window.height=" << state.windowHeight << '\n';
    output << "window.maximized=" << (state.maximized ? 1 : 0) << '\n';
    const char* outlineState =
        state.outlineState == OutlineState::Expanded
            ? "expanded"
            : state.outlineState == OutlineState::Collapsed
                  ? "collapsed"
                  : "hidden";
    output << "outline.state=" << outlineState << '\n';
    output << "outline.visible="
           << (state.outlineState == OutlineState::Hidden ? 0 : 1) << '\n';
    output << "tabs.selected=" << state.selectedTab << '\n';

    const std::size_t tabCount =
        std::min(state.tabs.size(), kMaximumSavedTabs);
    output << "tabs.count=" << tabCount << '\n';
    for (std::size_t index = 0; index < tabCount; ++index) {
        output << "tab." << index << ".path=" << state.tabs[index].path << '\n';
        output << "tab." << index << ".scroll="
               << std::max(0, state.tabs[index].scrollY) << '\n';
    }

    const std::size_t recentCount =
        std::min(state.recentFiles.size(), static_cast<std::size_t>(5));
    output << "recent.count=" << recentCount << '\n';
    for (std::size_t index = 0; index < recentCount; ++index) {
        output << "recent." << index << '=' << state.recentFiles[index] << '\n';
    }
    output.close();
    if (!output) {
        SetError(error, "写入状态文件失败");
        return false;
    }

    std::filesystem::rename(temporary, path_, filesystemError);
    if (filesystemError) {
        filesystemError.clear();
        std::filesystem::remove(path_, filesystemError);
        filesystemError.clear();
        std::filesystem::rename(temporary, path_, filesystemError);
    }
    if (filesystemError) {
        SetError(error, "无法替换状态文件");
        return false;
    }
    return true;
}

void WorkspaceStore::AddRecent(WorkspaceState* state,
                               const std::string& path,
                               std::size_t limit) {
    if (state == nullptr || path.empty() || limit == 0) {
        return;
    }
    state->recentFiles.erase(
        std::remove(state->recentFiles.begin(), state->recentFiles.end(),
                    path),
        state->recentFiles.end());
    state->recentFiles.insert(state->recentFiles.begin(), path);
    if (state->recentFiles.size() > limit) {
        state->recentFiles.resize(limit);
    }
}

}  // namespace litemark
