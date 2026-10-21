#include "core/flow/Flowchart.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string Trim(std::string value) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    return value;
}

std::string StripQuotes(std::string value) {
    value = Trim(std::move(value));
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

bool IsIdChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

struct ParsedNode {
    std::string id;
    std::string label;
    litemark::FlowNodeShape shape = litemark::FlowNodeShape::Rectangle;
    std::size_t next = 0;
};

bool ParseNode(const std::string& line, std::size_t start, ParsedNode* node) {
    std::size_t position = start;
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position]))) {
        ++position;
    }

    const std::size_t idStart = position;
    while (position < line.size() && IsIdChar(line[position])) {
        ++position;
    }
    if (position == idStart) {
        return false;
    }
    node->id = line.substr(idStart, position - idStart);
    node->label = node->id;

    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position]))) {
        ++position;
    }
    if (position >= line.size()) {
        node->next = position;
        return true;
    }

    const std::string rest = line.substr(position);
    auto parseDelimited = [&](std::string_view open, std::string_view close,
                              litemark::FlowNodeShape shape) {
        if (rest.rfind(open, 0) != 0) {
            return false;
        }
        const std::size_t contentStart = position + open.size();
        const std::size_t end = line.find(close, contentStart);
        if (end == std::string::npos) {
            return false;
        }
        node->label = StripQuotes(
            line.substr(contentStart, end - contentStart));
        node->shape = shape;
        node->next = end + close.size();
        return true;
    };

    if (parseDelimited("((", "))", litemark::FlowNodeShape::Circle) ||
        parseDelimited("[", "]", litemark::FlowNodeShape::Rectangle) ||
        parseDelimited("(", ")", litemark::FlowNodeShape::Rounded) ||
        parseDelimited("{", "}", litemark::FlowNodeShape::Diamond)) {
        return true;
    }

    node->next = position;
    return true;
}

struct EdgeToken {
    litemark::FlowEdgeStyle style = litemark::FlowEdgeStyle::Solid;
    std::size_t next = 0;
};

bool ParseEdge(const std::string& line, std::size_t start, EdgeToken* edge) {
    std::size_t position = start;
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position]))) {
        ++position;
    }

    const struct {
        const char* token;
        litemark::FlowEdgeStyle style;
    } operators[] = {
        {"-.->", litemark::FlowEdgeStyle::Dotted},
        {"==>", litemark::FlowEdgeStyle::Thick},
        {"---", litemark::FlowEdgeStyle::NoArrow},
        {"-->", litemark::FlowEdgeStyle::Solid},
    };

    for (const auto& candidate : operators) {
        const std::string_view token(candidate.token);
        if (line.compare(position, token.size(), token) == 0) {
            edge->style = candidate.style;
            edge->next = position + token.size();
            return true;
        }
    }
    return false;
}

}  // namespace

namespace litemark {

FlowchartParseResult ParseMermaidFlowchart(const std::string& source) {
    FlowchartParseResult result;
    std::istringstream input(source);
    std::string line;
    bool headerSeen = false;
    int activeCluster = -1;
    std::map<std::string, int> nodeIndex;

    auto addNode = [&](const ParsedNode& parsed) -> int {
        const auto found = nodeIndex.find(parsed.id);
        if (found != nodeIndex.end()) {
            FlowNode& node = result.graph.nodes[found->second];
            if (parsed.label != parsed.id) {
                node.label = parsed.label;
                node.shape = parsed.shape;
            }
            if (node.cluster < 0 && activeCluster >= 0) {
                node.cluster = activeCluster;
                result.graph.clusters[activeCluster].nodeIds.push_back(node.id);
            }
            return found->second;
        }

        FlowNode node;
        node.id = parsed.id;
        node.label = parsed.label;
        node.shape = parsed.shape;
        node.cluster = activeCluster;
        const int index = static_cast<int>(result.graph.nodes.size());
        result.graph.nodes.push_back(node);
        nodeIndex[node.id] = index;
        if (activeCluster >= 0) {
            result.graph.clusters[activeCluster].nodeIds.push_back(node.id);
        }
        return index;
    };

    while (std::getline(input, line)) {
        line = Trim(std::move(line));
        if (line.empty() || line.rfind("%%", 0) == 0) {
            continue;
        }

        if (!headerSeen) {
            std::smatch match;
            const std::regex header(
                R"(^\s*(?:flowchart|graph)\s+(TD|TB|BT|LR|RL)\s*$)",
                std::regex::icase);
            if (!std::regex_match(line, match, header)) {
                result.error = "Mermaid 缺少 flowchart/graph 方向声明";
                return result;
            }
            std::string direction = match[1].str();
            std::transform(direction.begin(), direction.end(),
                           direction.begin(), [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            if (direction == "td" || direction == "tb") {
                result.graph.direction = FlowDirection::TopDown;
            } else if (direction == "bt") {
                result.graph.direction = FlowDirection::BottomUp;
            } else if (direction == "lr") {
                result.graph.direction = FlowDirection::LeftRight;
            } else {
                result.graph.direction = FlowDirection::RightLeft;
            }
            headerSeen = true;
            continue;
        }

        if (line.rfind("subgraph ", 0) == 0) {
            if (activeCluster >= 0) {
                result.error = "暂不支持嵌套 subgraph";
                return result;
            }
            std::string descriptor = Trim(line.substr(9));
            FlowCluster cluster;
            const std::size_t open = descriptor.find('[');
            if (open != std::string::npos && descriptor.back() == ']') {
                cluster.id = Trim(descriptor.substr(0, open));
                cluster.title = StripQuotes(
                    descriptor.substr(open + 1,
                                      descriptor.size() - open - 2));
            } else {
                cluster.id = descriptor;
                cluster.title = descriptor;
            }
            if (cluster.id.empty()) {
                result.error = "subgraph 缺少标识";
                return result;
            }
            activeCluster = static_cast<int>(result.graph.clusters.size());
            result.graph.clusters.push_back(std::move(cluster));
            continue;
        }
        if (line == "end") {
            if (activeCluster < 0) {
                result.error = "多余的 end";
                return result;
            }
            activeCluster = -1;
            continue;
        }
        if (line.rfind("style ", 0) == 0 ||
            line.rfind("classDef ", 0) == 0 ||
            line.rfind("class ", 0) == 0 ||
            line.rfind("linkStyle ", 0) == 0 ||
            line.rfind("click ", 0) == 0) {
            result.error = "Mermaid 样式或交互指令不在支持范围内";
            return result;
        }

        if (!line.empty() && line.back() == ';') {
            line.pop_back();
        }

        std::size_t position = 0;
        ParsedNode current;
        if (!ParseNode(line, position, &current)) {
            result.error = "无法解析 flowchart 节点";
            return result;
        }
        addNode(current);
        position = current.next;

        while (position < line.size()) {
            EdgeToken token;
            if (!ParseEdge(line, position, &token)) {
                result.error = "无法解析 flowchart 连线";
                return result;
            }
            position = token.next;
            while (position < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[position]))) {
                ++position;
            }

            std::string edgeLabel;
            if (position < line.size() && line[position] == '|') {
                const std::size_t labelEnd = line.find('|', position + 1);
                if (labelEnd == std::string::npos) {
                    result.error = "连线标签缺少结束符";
                    return result;
                }
                edgeLabel =
                    StripQuotes(line.substr(position + 1,
                                            labelEnd - position - 1));
                position = labelEnd + 1;
            }

            ParsedNode next;
            if (!ParseNode(line, position, &next)) {
                result.error = "无法解析连线目标节点";
                return result;
            }
            addNode(next);
            position = next.next;
            if (current.id == next.id) {
                result.error = "暂不支持 flowchart 自环";
                return result;
            }
            result.graph.edges.push_back(
                FlowEdge{current.id, next.id, edgeLabel, token.style});
            current = next;
        }
    }

    if (!headerSeen) {
        result.error = "flowchart 内容为空";
        return result;
    }
    if (activeCluster >= 0) {
        result.error = "subgraph 缺少 end";
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace litemark
