#include "ui/TableLayout.h"

#include "ui/TextLayout.h"

#include <wx/string.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

int MeasureCellWidth(wxDC& dc,
                     const std::vector<litemark::InlineSpan>& spans,
                     bool header,
                     double fontScale,
                     litemark::detail::FontMap* fonts) {
    int widest = 0;
    for (const auto& sourceSpan : spans) {
        litemark::detail::TextStyle style;
        style.pointSize = 10;
        style.bold = header || sourceSpan.bold;
        style.italic = sourceSpan.italic;
        style.mono = sourceSpan.code;
        style.code = sourceSpan.code;
        wxString text = wxString::FromUTF8(sourceSpan.text);
        if (sourceSpan.image) {
            text = text.empty()
                       ? wxString::FromUTF8("[图片]")
                       : wxString::FromUTF8("[图片: ") + text +
                             wxString::FromUTF8("]");
        }
        style.pointSize = litemark::detail::ScaledPointSize(
            style.pointSize, fontScale);
        auto found = fonts != nullptr
                         ? fonts->find(style.key())
                         : litemark::detail::FontMap::iterator();
        if (fonts != nullptr && found != fonts->end()) {
            dc.SetFont(found->second);
        } else {
            const wxFont font = litemark::detail::BuildFont(style);
            dc.SetFont(font);
            if (fonts != nullptr) {
                (*fonts)[style.key()] = font;
            }
        }
        const wxArrayString lines = wxSplit(text, '\n', '\0');
        for (const auto& line : lines) {
            widest = std::max(widest, dc.GetTextExtent(line).x);
        }
    }
    return widest;
}

}  // namespace

namespace litemark::detail {

void BuildTableLayout(
    wxDC& dc,
    const Block& block,
    BlockLayout& item,
    int width,
    double scale,
    double fontScale,
    FontMap* fonts) {
    const std::size_t rowCount = block.tableRows.size();
    std::size_t columnCount = 0;
    for (const auto& row : block.tableRows) {
        columnCount = std::max(columnCount, row.size());
    }
    if (rowCount == 0 || columnCount == 0) {
        item.height = 20;
        return;
    }

    const int cellPadding =
        static_cast<int>(std::lround(9 * scale));
    const int minColumnWidth =
        static_cast<int>(std::lround(64 * scale));
    const int maxMinColumnWidth =
        static_cast<int>(std::lround(150 * scale));
    const int desiredColumnWidth =
        static_cast<int>(std::lround(380 * scale));

    std::vector<int> minimums(columnCount, minColumnWidth);
    std::vector<int> desired(columnCount, minColumnWidth);
    for (std::size_t column = 0; column < columnCount; ++column) {
        int contentWidth = 0;
        const std::size_t measuredRows =
            block.tableColumnWidthHints.empty()
                ? rowCount
                : std::min<std::size_t>(rowCount, 32);
        for (std::size_t row = 0; row < measuredRows; ++row) {
            if (column < block.tableRows[row].size()) {
                contentWidth = std::max(
                    contentWidth,
                    MeasureCellWidth(
                        dc, block.tableRows[row][column].spans,
                        row == 0, fontScale, fonts));
            }
        }
        if (column < block.tableColumnWidthHints.size()) {
            contentWidth = std::max(
                contentWidth,
                static_cast<int>(std::lround(
                    block.tableColumnWidthHints[column] * 9 * scale *
                    fontScale)));
        }
        desired[column] = std::clamp(
            contentWidth + cellPadding * 2,
            minColumnWidth, desiredColumnWidth);
        minimums[column] = std::clamp(
            desired[column], minColumnWidth, maxMinColumnWidth);
    }

    const int totalMinimum =
        std::accumulate(minimums.begin(), minimums.end(), 0);
    const int totalDesired =
        std::accumulate(desired.begin(), desired.end(), 0);
    std::vector<int> columnWidths(columnCount, minColumnWidth);

    if (totalDesired <= width) {
        columnWidths = desired;
    } else if (totalMinimum <= width) {
        int flexible = 0;
        for (std::size_t column = 0; column < columnCount; ++column) {
            flexible += desired[column] - minimums[column];
        }
        const int extra = width - totalMinimum;
        int assigned = 0;
        for (std::size_t column = 0; column < columnCount; ++column) {
            const int weight = desired[column] - minimums[column];
            const int share =
                flexible > 0 ? extra * weight / flexible : 0;
            columnWidths[column] = minimums[column] + share;
            assigned += columnWidths[column];
        }
        if (!columnWidths.empty() && assigned < width) {
            columnWidths.back() += width - assigned;
        }
    } else {
        columnWidths = minimums;
    }

    item.tableContentWidth =
        std::accumulate(columnWidths.begin(), columnWidths.end(), 0);
    item.tableScrollX =
        std::clamp(item.tableScrollX, 0,
                   std::max(0, item.tableContentWidth - width));
    item.tableScrollbarHeight =
        item.tableContentWidth > width
            ? static_cast<int>(std::lround(9 * scale))
            : 0;

    int y = 0;
    std::vector<int> columnX(columnCount, 0);
    for (std::size_t column = 1; column < columnCount; ++column) {
        columnX[column] =
            columnX[column - 1] + columnWidths[column - 1];
    }

    for (std::size_t row = 0; row < rowCount; ++row) {
        RowLayout rowLayout;
        rowLayout.y = y;
        for (std::size_t column = 0; column < columnCount; ++column) {
            CellLayout cell;
            cell.x = columnX[column];
            cell.width = columnWidths[column];
            TextStyle style;
            style.pointSize = 10;
            style.bold = block.tableFirstRowIsHeader && row == 0;
            if (column < block.tableRows[row].size()) {
                BuildTextLayout(
                    dc, cell.text,
                    columnWidths[column] - cellPadding * 2,
                    block.tableRows[row][column].spans,
                    style, fontScale, fonts);
            } else {
                cell.text.height = 0;
            }
            rowLayout.cells.push_back(std::move(cell));
        }

        int rowHeight = static_cast<int>(std::lround(20 * scale));
        for (const auto& cell : rowLayout.cells) {
            rowHeight =
                std::max(rowHeight, cell.text.height + cellPadding * 2);
        }
        rowLayout.height = rowHeight;
        y += rowHeight;
        item.tableRows.push_back(std::move(rowLayout));
    }
    item.tableRowsHeight = y;
    item.height = y + item.tableScrollbarHeight;
}

}  // namespace litemark::detail
