#include "session/TabManager.h"

#include "core/parser/MarkdownParser.h"

#include <wx/file.h>
#include <wx/filename.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

constexpr wxFileOffset kMaxFileSize = 64LL * 1024 * 1024;

bool IsUtf8Continuation(unsigned char value) {
    return (value & 0xC0U) == 0x80U;
}

bool IsValidUtf8(const std::string& bytes, std::size_t offset) {
    std::size_t index = offset;
    while (index < bytes.size()) {
        const auto first = static_cast<unsigned char>(bytes[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t required = 0;
        if (first >= 0xC2U && first <= 0xDFU) {
            required = 1;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            required = 2;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            required = 3;
        } else {
            return false;
        }

        if (index + required >= bytes.size()) {
            return false;
        }

        const auto second = static_cast<unsigned char>(bytes[index + 1]);
        if (required == 2) {
            if ((first == 0xE0U && second < 0xA0U) ||
                (first == 0xEDU && second > 0x9FU)) {
                return false;
            }
        } else if (required == 3) {
            if ((first == 0xF0U && second < 0x90U) ||
                (first == 0xF4U && second > 0x8FU)) {
                return false;
            }
        }

        for (std::size_t i = 1; i <= required; ++i) {
            if (!IsUtf8Continuation(static_cast<unsigned char>(bytes[index + i]))) {
                return false;
            }
        }
        index += required + 1;
    }
    return true;
}

void SetError(wxString* error, const char* message) {
    if (error != nullptr) {
        *error = wxString::FromUTF8(message);
    }
}

}  // namespace

namespace litemark {

TabOpenResult TabManager::OpenFile(const wxString& path) {
    const wxString normalizedPath = NormalizePath(path);
    const int existing = FindPath(normalizedPath);
    if (existing >= 0) {
        selectedIndex_ = existing;
        return TabOpenResult::Activated;
    }

    wxFileName file(normalizedPath);
    TabSession session;
    session.id = nextId_++;
    session.generation = 1;
    session.path = normalizedPath;
    session.title = file.GetFullName().empty() ? normalizedPath : file.GetFullName();
    session.loadState = DocumentLoadState::Loading;
    tabs_.push_back(std::move(session));
    selectedIndex_ = static_cast<int>(tabs_.size()) - 1;
    return TabOpenResult::Opened;
}

TabOpenResult TabManager::ReloadSelected(wxString* error) {
    TabSession* current = selectedMutable();
    if (current == nullptr) {
        SetError(error, "没有可重新加载的文档");
        return TabOpenResult::Failed;
    }

    ++current->generation;
    current->loadState = DocumentLoadState::Loading;
    current->parseError.clear();
    return TabOpenResult::Opened;
}

bool TabManager::CompleteParse(
    std::uint64_t tabId, std::uint64_t generation,
    const wxString& content,
    std::shared_ptr<const MarkdownDocument> document, const wxString& error) {
    TabSession* session = FindById(tabId, generation);
    if (session == nullptr) {
        return false;
    }

    if (document) {
        session->content = content;
        session->document = std::move(document);
        session->loadState = DocumentLoadState::Ready;
        session->parseError.clear();
    } else {
        session->parseError = error;
        session->loadState =
            session->document ? DocumentLoadState::Ready
                              : DocumentLoadState::Failed;
    }
    return true;
}

bool TabManager::LoadFileAndParse(
    const wxString& path, const std::atomic_bool* cancelled,
    wxString* content, std::shared_ptr<const MarkdownDocument>* document,
    wxString* error) {
    if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
        SetError(error, "加载已取消");
        return false;
    }
    if (!ReadUtf8File(path, content, error)) {
        return false;
    }
    if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
        SetError(error, "加载已取消");
        return false;
    }

    const wxScopedCharBuffer utf8 = content->ToUTF8();
    std::string parseError;
    auto parsed = MarkdownParser::Parse(
        std::string(utf8.data(), utf8.length()), &parseError, cancelled);
    if (!parsed) {
        if (error != nullptr) {
            *error = wxString::FromUTF8(parseError.c_str());
        }
        return false;
    }

    *document = std::move(parsed);
    return true;
}

void TabManager::Activate(std::size_t index) {
    if (index < tabs_.size()) {
        selectedIndex_ = static_cast<int>(index);
    }
}

void TabManager::ActivateNext() {
    if (tabs_.size() < 2) {
        return;
    }
    selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(tabs_.size());
}

void TabManager::CloseSelected() {
    if (selectedIndex_ < 0 || static_cast<std::size_t>(selectedIndex_) >= tabs_.size()) {
        return;
    }
    CloseAt(static_cast<std::size_t>(selectedIndex_));
}

void TabManager::CloseAt(std::size_t index) {
    if (index >= tabs_.size()) {
        return;
    }

    const int previousSelected = selectedIndex_;
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    if (tabs_.empty()) {
        selectedIndex_ = -1;
    } else if (previousSelected > static_cast<int>(index)) {
        selectedIndex_ = previousSelected - 1;
    } else if (previousSelected == static_cast<int>(index)) {
        selectedIndex_ =
            std::min(static_cast<int>(index), static_cast<int>(tabs_.size()) - 1);
    } else {
        selectedIndex_ = previousSelected;
    }
    if (selectedIndex_ >= static_cast<int>(tabs_.size())) {
        selectedIndex_ = static_cast<int>(tabs_.size()) - 1;
    }
}

const TabSession* TabManager::selected() const {
    if (selectedIndex_ < 0 || static_cast<std::size_t>(selectedIndex_) >= tabs_.size()) {
        return nullptr;
    }
    return &tabs_[static_cast<std::size_t>(selectedIndex_)];
}

TabSession* TabManager::selectedMutable() {
    return const_cast<TabSession*>(static_cast<const TabManager&>(*this).selected());
}

bool TabManager::ReadUtf8File(const wxString& path, wxString* text, wxString* error) {
    wxFileName fileInfo(path);
    if (!fileInfo.FileExists()) {
        SetError(error, "文件不存在");
        return false;
    }
    if (fileInfo.GetSize() > kMaxFileSize) {
        SetError(error, "文件超过 64 MiB 安全上限");
        return false;
    }

    wxFile file;
    if (!file.Open(path, wxFile::read)) {
        SetError(error, "无法打开文件");
        return false;
    }

    const wxFileOffset length = file.Length();
    if (length < 0 || length > kMaxFileSize) {
        SetError(error, "文件大小无效");
        return false;
    }

    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (length > 0 && file.Read(bytes.data(), static_cast<std::size_t>(length)) != length) {
        SetError(error, "读取文件失败");
        return false;
    }

    std::size_t offset = 0;
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEFU &&
        static_cast<unsigned char>(bytes[1]) == 0xBBU &&
        static_cast<unsigned char>(bytes[2]) == 0xBFU) {
        offset = 3;
    }

    if (!IsValidUtf8(bytes, offset)) {
        SetError(error, "文件编码不受支持或内容已损坏，仅支持 UTF-8");
        return false;
    }

    *text = wxString::FromUTF8(bytes.data() + offset, bytes.size() - offset);
    return true;
}

wxString TabManager::NormalizePath(const wxString& path) {
    wxFileName file(path);
    file.MakeAbsolute();
    file.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG);
    return file.GetFullPath();
}

int TabManager::FindPath(const wxString& normalizedPath) const {
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].path.CmpNoCase(normalizedPath) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

TabSession* TabManager::FindById(std::uint64_t tabId,
                                 std::uint64_t generation) {
    for (auto& tab : tabs_) {
        if (tab.id == tabId && tab.generation == generation) {
            return &tab;
        }
    }
    return nullptr;
}

}  // namespace litemark
