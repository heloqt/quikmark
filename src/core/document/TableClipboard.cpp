#include "core/document/TableClipboard.h"

#include "core/document/DocumentText.h"

#include <algorithm>
#include <string_view>

namespace {

std::size_t ColumnCount(const litemark::TableBlockRange& range,
                        const litemark::MarkdownDocument& document) {
    std::size_t count = 0;
    for (std::size_t index = range.begin; index < range.end; ++index) {
        const auto& rows = document.blocks[index].tableRows;
        for (const auto& row : rows) {
            count = std::max(count, row.size());
        }
    }
    return count;
}

void AppendEscapedHtml(std::string* output, std::string_view text,
                       bool convertNewlines) {
    for (char ch : text) {
        switch (ch) {
            case '&':
                output->append("&amp;");
                break;
            case '<':
                output->append("&lt;");
                break;
            case '>':
                output->append("&gt;");
                break;
            case '"':
                output->append("&quot;");
                break;
            case '\'':
                output->append("&#39;");
                break;
            case '\n':
                if (convertNewlines) {
                    output->append("<br>");
                } else {
                    output->push_back(ch);
                }
                break;
            case '\r':
                break;
            default:
                output->push_back(ch);
                break;
        }
    }
}

std::string CellHtml(const litemark::TableCell& cell) {
    std::string html;
    for (const auto& span : cell.spans) {
        std::string content;
        AppendEscapedHtml(&content, span.text, true);
        if (span.code) {
            content = "<code>" + content + "</code>";
        }
        if (span.strikethrough) {
            content = "<s>" + content + "</s>";
        }
        if (span.bold) {
            content = "<strong>" + content + "</strong>";
        }
        if (span.italic) {
            content = "<em>" + content + "</em>";
        }
        if (!span.image && !span.href.empty()) {
            std::string href;
            AppendEscapedHtml(&href, span.href, false);
            content =
                "<a href=\"" + href + "\">" + content + "</a>";
        }
        html += content;
    }
    return html;
}

}  // namespace

namespace litemark {

std::optional<TableBlockRange> TableRangeForBlock(
    const MarkdownDocument& document, std::size_t blockIndex) {
    if (blockIndex >= document.blocks.size() ||
        document.blocks[blockIndex].kind != BlockKind::Table) {
        return std::nullopt;
    }

    std::size_t begin = blockIndex;
    while (begin > 0 &&
           document.blocks[begin].continuesPreviousBlock &&
           document.blocks[begin - 1].kind == BlockKind::Table) {
        --begin;
    }

    std::size_t end = blockIndex + 1;
    while (end < document.blocks.size() &&
           document.blocks[end].kind == BlockKind::Table &&
           document.blocks[end].continuesPreviousBlock) {
        ++end;
    }
    return TableBlockRange{begin, end};
}

std::optional<TableClipboardData> BuildTableClipboardData(
    const MarkdownDocument& document, std::size_t blockIndex) {
    const auto range = TableRangeForBlock(document, blockIndex);
    if (!range) {
        return std::nullopt;
    }

    const std::size_t columns = ColumnCount(*range, document);
    if (columns == 0) {
        return std::nullopt;
    }

    TableClipboardData data;
    data.plainText.reserve(1024);
    data.html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "</head><body><table border=\"1\" cellspacing=\"0\" "
        "cellpadding=\"4\" style=\"border-collapse:collapse\">";

    bool firstRow = true;
    for (std::size_t blockOffset = range->begin;
         blockOffset < range->end; ++blockOffset) {
        const Block& block = document.blocks[blockOffset];
        for (const auto& row : block.tableRows) {
            if (!firstRow) {
                data.plainText.push_back('\n');
            }

            const bool header =
                firstRow && range->begin < document.blocks.size() &&
                document.blocks[range->begin].tableFirstRowIsHeader;
            data.html.append(header ? "<tr><th>" : "<tr><td>");

            for (std::size_t column = 0; column < columns; ++column) {
                if (column > 0) {
                    data.plainText.push_back('\t');
                    data.html.append(
                        header ? "</th><th>" : "</td><td>");
                }
                if (column >= row.size()) {
                    continue;
                }
                data.plainText += TableCellPlainText(row[column]);
                data.html += CellHtml(row[column]);
            }
            data.html.append(header ? "</th></tr>" : "</td></tr>");
            firstRow = false;
        }
    }
    data.html.append("</table></body></html>");
    return data;
}

}  // namespace litemark
