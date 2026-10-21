#pragma once

#include <wx/event.h>
#include <wx/string.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

wxDECLARE_EVENT(LITEMARK_EVT_OPEN_PATHS, wxThreadEvent);

namespace litemark {

bool SendOpenRequestToPrimary(const std::vector<wxString>& paths,
                              int timeoutMs = 2000);

class IpcServer {
public:
    explicit IpcServer(wxEvtHandler* target);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

private:
    void Run();
    void Wake();
    void Dispatch(const std::string& payload);

    wxEvtHandler* target_ = nullptr;
    std::atomic_bool stopping_ = false;
    std::thread thread_;
};

}  // namespace litemark
