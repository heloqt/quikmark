#pragma once

#include "core/flow/Flowchart.h"
#include "ui/BlockLayout.h"
#include "ui/ThemeController.h"

#include <wx/image.h>
#include <wx/string.h>

#include <functional>
#include <memory>
#include <string>

class wxDC;
class wxWindow;

namespace litemark::detail {

struct ImageLoadedPayload {
    wxString key;
    std::shared_ptr<wxImage> image;
};

struct FlowchartLoadedPayload {
    std::string source;
    FlowchartParseResult result;
};

class DocumentMediaRenderer {
public:
    DocumentMediaRenderer(
        wxWindow* owner,
        int imageLoadedEventId,
        int flowchartLoadedEventId);
    ~DocumentMediaRenderer();

    DocumentMediaRenderer(const DocumentMediaRenderer&) = delete;
    DocumentMediaRenderer& operator=(const DocumentMediaRenderer&) = delete;

    void SetBasePath(const wxString& basePath);

    void BuildImageLayout(
        wxDC& dc,
        BlockLayout& item,
        int width,
        double scale);
    void BuildFlowchartLayout(
        wxDC& dc,
        BlockLayout& item,
        int width,
        double scale,
        double fontScale,
        FontMap* fonts);

    void DrawImage(
        wxDC& dc,
        const BlockLayout& item,
        const ThemePalette& palette,
        double fontScale);
    void DrawFlowchart(
        wxDC& dc,
        const BlockLayout& item,
        const ThemePalette& palette,
        double fontScale,
        const std::function<void(
            wxDC&, const TextLayout&, int, int)>& drawText);

    void HandleImageLoaded(const ImageLoadedPayload& payload);
    void HandleFlowchartLoaded(const FlowchartLoadedPayload& payload);

    std::size_t LoadedImageCount() const;
    std::size_t LoadedFlowchartCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace litemark::detail
