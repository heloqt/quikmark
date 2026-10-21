#include "core/parser/MarkdownParser.h"

#include <md4c.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using litemark::Block;
using litemark::BlockId;
using litemark::BlockKind;
using litemark::InlineSpan;
using litemark::MarkdownDocument;

struct SpanStyle {
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool code = false;
    bool image = false;
    std::string href;
};

struct ListLevel {
    bool ordered = false;
    int start = 1;
    int next = 1;
};

struct ListItem {
    bool ordered = false;
    int index = 1;
    bool task = false;
    bool checked = false;
    bool markerConsumed = false;
};

std::string AttributeText(const MD_ATTRIBUTE& attribute) {
    if (attribute.text == nullptr || attribute.size == 0) {
        return {};
    }
    return std::string(attribute.text, attribute.size);
}

std::string Utf8(char32_t codepoint) {
    std::string out;
    if (codepoint <= 0x7FU) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
        out.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
        out.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0x10FFFFU) {
        out.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
    return out;
}

std::string DecodeEntity(std::string entity) {
    static const struct {
        const char* name;
        const char* value;
    } kNamed[] = {
        {"&amp;", "&"},   {"&lt;", "<"},    {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"},  {"&nbsp;", " "},
        {"&mdash;", "—"}, {"&ndash;", "–"}, {"&hellip;", "…"},
    };

    for (const auto& item : kNamed) {
        if (entity == item.name) {
            return item.value;
        }
    }

    if (entity.size() < 4 || entity[0] != '&' || entity.back() != ';') {
        return entity;
    }

    const std::string body = entity.substr(1, entity.size() - 2);
    int base = 10;
    std::size_t start = 0;
    if (!body.empty() && (body[0] == 'x' || body[0] == 'X')) {
        base = 16;
        start = 1;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(body.c_str() + start, &end, base);
    if (end == nullptr || *end != '\0' || value == 0 || value > 0x10FFFFUL) {
        return entity;
    }
    return Utf8(static_cast<char32_t>(value));
}

class Parser {
public:
    Parser(MarkdownDocument& document, const std::atomic_bool* cancelled)
        : document_(document), cancelled_(cancelled) {}

    int EnterBlock(MD_BLOCKTYPE type, void* detail) {
        if (Cancelled()) {
            return 1;
        }
        switch (type) {
            case MD_BLOCK_QUOTE:
                ++quoteDepth_;
                break;
            case MD_BLOCK_UL:
                lists_.push_back({false, 1, 1});
                break;
            case MD_BLOCK_OL: {
                const auto* info = static_cast<const MD_BLOCK_OL_DETAIL*>(detail);
                const int start = info != nullptr ? static_cast<int>(info->start) : 1;
                lists_.push_back({true, start, start});
                break;
            }
            case MD_BLOCK_LI: {
                if (lists_.empty()) {
                    break;
                }
                const auto* info = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
                ListItem item;
                item.ordered = lists_.back().ordered;
                item.index = lists_.back().next++;
                if (info != nullptr && info->is_task != 0) {
                    item.task = true;
                    item.checked = info->task_mark == 'x' || info->task_mark == 'X';
                }
                items_.push_back(item);
                break;
            }
            case MD_BLOCK_H: {
                const auto* info = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
                BeginTextBlock(BlockKind::Heading,
                               info != nullptr ? static_cast<int>(info->level) : 1);
                break;
            }
            case MD_BLOCK_CODE: {
                const auto* info = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
                BeginRawBlock(BlockKind::Code,
                              info != nullptr ? AttributeText(info->lang) : std::string());
                break;
            }
            case MD_BLOCK_HTML:
                BeginRawBlock(BlockKind::Html, {});
                break;
            case MD_BLOCK_P:
                if (!inTable_) {
                    BeginTextBlock(BlockKind::Text, 0);
                }
                break;
            case MD_BLOCK_HR:
                FinalizeCurrent();
                current_ = {};
                current_.kind = BlockKind::Rule;
                CaptureListContext(&current_, false);
                EmitCurrent();
                break;
            case MD_BLOCK_TABLE:
                FinalizeCurrent();
                inTable_ = true;
                table_ = {};
                table_.kind = BlockKind::Table;
                break;
            case MD_BLOCK_TR:
                if (inTable_) {
                    table_.tableRows.emplace_back();
                }
                break;
            case MD_BLOCK_TH:
            case MD_BLOCK_TD:
                if (inTable_ && !table_.tableRows.empty()) {
                    table_.tableRows.back().emplace_back();
                }
                break;
            default:
                break;
        }
        return 0;
    }

    int LeaveBlock(MD_BLOCKTYPE type) {
        if (Cancelled()) {
            return 1;
        }
        switch (type) {
            case MD_BLOCK_QUOTE:
                quoteDepth_ = std::max(0, quoteDepth_ - 1);
                break;
            case MD_BLOCK_UL:
            case MD_BLOCK_OL:
                if (!lists_.empty()) {
                    lists_.pop_back();
                }
                break;
            case MD_BLOCK_LI:
                FinalizeCurrent();
                if (!items_.empty()) {
                    items_.pop_back();
                }
                break;
            case MD_BLOCK_H:
            case MD_BLOCK_P:
            case MD_BLOCK_CODE:
            case MD_BLOCK_HTML:
                FinalizeCurrent();
                break;
            case MD_BLOCK_TABLE:
                if (inTable_) {
                    table_.id = ++nextBlockId_;
                    document_.blocks.push_back(std::move(table_));
                    table_ = {};
                    inTable_ = false;
                }
                break;
            default:
                break;
        }
        return 0;
    }

    int EnterSpan(MD_SPANTYPE type, void* detail) {
        if (Cancelled()) {
            return 1;
        }
        SpanStyle style;
        if (!spanStack_.empty()) {
            style = spanStack_.back();
        }
        switch (type) {
            case MD_SPAN_EM:
                style.italic = true;
                break;
            case MD_SPAN_STRONG:
                style.bold = true;
                break;
            case MD_SPAN_DEL:
                style.strikethrough = true;
                break;
            case MD_SPAN_CODE:
                style.code = true;
                break;
            case MD_SPAN_A: {
                const auto* info = static_cast<const MD_SPAN_A_DETAIL*>(detail);
                if (info != nullptr) {
                    style.href = AttributeText(info->href);
                }
                break;
            }
            case MD_SPAN_IMG: {
                const auto* info = static_cast<const MD_SPAN_IMG_DETAIL*>(detail);
                style.image = true;
                if (info != nullptr) {
                    style.href = AttributeText(info->src);
                }
                break;
            }
            default:
                break;
        }
        spanStack_.push_back(std::move(style));
        return 0;
    }

    int LeaveSpan(MD_SPANTYPE) {
        if (Cancelled()) {
            return 1;
        }
        if (!spanStack_.empty()) {
            spanStack_.pop_back();
        }
        return 0;
    }

    int Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size) {
        if (Cancelled()) {
            return 1;
        }
        if (text == nullptr || size == 0) {
            return 0;
        }

        std::string value(text, size);
        if (type == MD_TEXT_ENTITY) {
            value = DecodeEntity(std::move(value));
        } else if (type == MD_TEXT_NULLCHAR) {
            value = Utf8(0xFFFDU);
        } else if (type == MD_TEXT_BR) {
            value = "\n";
        } else if (type == MD_TEXT_SOFTBR) {
            value = " ";
        }

        if (type == MD_TEXT_CODE && active_ && activeKind_ == BlockKind::Code) {
            current_.raw += value;
            return 0;
        }
        if (type == MD_TEXT_HTML && active_ && activeKind_ == BlockKind::Html) {
            current_.raw += value;
            return 0;
        }

        if (inTable_ && !table_.tableRows.empty() &&
            !table_.tableRows.back().empty()) {
            AppendSpan(table_.tableRows.back().back().spans, value);
            return 0;
        }

        if (!active_) {
            BeginTextBlock(BlockKind::Text, 0);
        }
        if (!active_) {
            return 0;
        }
        AppendSpan(current_.spans, value);
        return 0;
    }

private:
    bool Cancelled() const {
        return cancelled_ != nullptr && cancelled_->load(std::memory_order_relaxed);
    }

    void CaptureListContext(Block* block, bool consumeMarker) {
        block->quoteDepth = quoteDepth_;
        if (!items_.empty()) {
            ListItem& item = items_.back();
            block->listDepth = static_cast<int>(items_.size());
            block->listOrdered = item.ordered;
            block->listIndex = item.index;
            block->task = item.task;
            block->taskChecked = item.checked;
            block->listMarker = consumeMarker && !item.markerConsumed;
            if (block->listMarker) {
                item.markerConsumed = true;
            }
        }
    }

    void BeginTextBlock(BlockKind kind, int headingLevel) {
        FinalizeCurrent();
        current_ = {};
        current_.kind = kind;
        current_.headingLevel = headingLevel;
        CaptureListContext(&current_, true);
        active_ = true;
        activeKind_ = kind;
    }

    void BeginRawBlock(BlockKind kind, const std::string& language) {
        FinalizeCurrent();
        current_ = {};
        current_.kind = kind;
        current_.language = language;
        CaptureListContext(&current_, true);
        active_ = true;
        activeKind_ = kind;
    }

    void FinalizeCurrent() {
        if (!active_) {
            return;
        }
        EmitCurrent();
        active_ = false;
    }

    void EmitCurrent() {
        current_.id = ++nextBlockId_;
        document_.blocks.push_back(std::move(current_));
        current_ = {};
    }

    void AppendSpan(std::vector<InlineSpan>& target, const std::string& text) {
        SpanStyle style;
        if (!spanStack_.empty()) {
            style = spanStack_.back();
        }
        if (!target.empty()) {
            InlineSpan& last = target.back();
            if (last.bold == style.bold && last.italic == style.italic &&
                last.strikethrough == style.strikethrough &&
                last.code == style.code && last.image == style.image &&
                last.href == style.href) {
                last.text += text;
                return;
            }
        }

        InlineSpan span;
        span.text = text;
        span.bold = style.bold;
        span.italic = style.italic;
        span.strikethrough = style.strikethrough;
        span.code = style.code;
        span.image = style.image;
        span.href = style.href;
        target.push_back(std::move(span));
    }

    MarkdownDocument& document_;
    const std::atomic_bool* cancelled_ = nullptr;
    std::vector<Block> blocks_;
    std::vector<ListLevel> lists_;
    std::vector<ListItem> items_;
    std::vector<SpanStyle> spanStack_;
    Block current_;
    Block table_;
    bool active_ = false;
    BlockKind activeKind_ = BlockKind::Text;
    bool inTable_ = false;
    int quoteDepth_ = 0;
    BlockId nextBlockId_ = 0;
};

int EnterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
    return static_cast<Parser*>(userdata)->EnterBlock(type, detail);
}

int LeaveBlockCallback(MD_BLOCKTYPE type, void*, void* userdata) {
    return static_cast<Parser*>(userdata)->LeaveBlock(type);
}

int EnterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
    return static_cast<Parser*>(userdata)->EnterSpan(type, detail);
}

int LeaveSpanCallback(MD_SPANTYPE type, void*, void* userdata) {
    return static_cast<Parser*>(userdata)->LeaveSpan(type);
}

int TextCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    return static_cast<Parser*>(userdata)->Text(type, text, size);
}

void NormalizeImages(MarkdownDocument& document) {
    std::vector<Block> normalized;
    normalized.reserve(document.blocks.size());

    for (auto& block : document.blocks) {
        const bool hasImage =
            block.kind == BlockKind::Text &&
            std::any_of(block.spans.begin(), block.spans.end(),
                        [](const InlineSpan& span) { return span.image; });
        if (!hasImage) {
            normalized.push_back(std::move(block));
            continue;
        }

        Block base = block;
        std::vector<InlineSpan> textSpans;
        bool emitted = false;

        auto flushText = [&]() {
            if (textSpans.empty()) {
                return;
            }
            Block textBlock = base;
            textBlock.kind = BlockKind::Text;
            textBlock.spans = std::move(textSpans);
            textBlock.listMarker = base.listMarker && !emitted;
            normalized.push_back(std::move(textBlock));
            emitted = true;
            textSpans.clear();
        };

        for (auto& span : block.spans) {
            if (span.image) {
                flushText();
                Block imageBlock = base;
                imageBlock.kind = BlockKind::Image;
                imageBlock.spans.clear();
                imageBlock.imagePath = span.href;
                imageBlock.imageAlt = span.text;
                imageBlock.listMarker = base.listMarker && !emitted;
                normalized.push_back(std::move(imageBlock));
                emitted = true;
            } else {
                textSpans.push_back(std::move(span));
            }
        }
        flushText();
    }

    BlockId nextId = 1;
    for (auto& block : normalized) {
        block.id = nextId++;
    }
    document.blocks = std::move(normalized);
}

void NormalizeFlowcharts(MarkdownDocument& document) {
    for (auto& block : document.blocks) {
        if (block.kind != BlockKind::Code) {
            continue;
        }
        std::string language = block.language;
        std::transform(language.begin(), language.end(), language.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        if (language == "mermaid") {
            block.kind = BlockKind::Flowchart;
            block.flowSource = block.raw;
        }
    }
}

}  // namespace

namespace litemark {

std::shared_ptr<const MarkdownDocument> MarkdownParser::Parse(
    const std::string& utf8,
    std::string* error,
    const std::atomic_bool* cancelled) {
    auto document = std::make_shared<MarkdownDocument>();
    Parser parser(*document, cancelled);

    MD_PARSER mdParser{};
    mdParser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                     MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS;
    mdParser.enter_block = EnterBlockCallback;
    mdParser.leave_block = LeaveBlockCallback;
    mdParser.enter_span = EnterSpanCallback;
    mdParser.leave_span = LeaveSpanCallback;
    mdParser.text = TextCallback;

    const int result = md_parse(utf8.data(), static_cast<MD_SIZE>(utf8.size()),
                                &mdParser, &parser);
    if (result != 0) {
        if (error != nullptr) {
            *error = cancelled != nullptr && cancelled->load()
                         ? "Markdown parsing cancelled"
                         : "Markdown parsing failed";
        }
        return nullptr;
    }

    NormalizeImages(*document);
    NormalizeFlowcharts(*document);
    return document;
}

}  // namespace litemark
