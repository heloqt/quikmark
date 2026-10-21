#pragma once

#include <string>
#include <vector>

namespace litemark {

enum class FlowDirection {
    TopDown,
    BottomUp,
    LeftRight,
    RightLeft
};

enum class FlowNodeShape {
    Rectangle,
    Rounded,
    Diamond,
    Circle
};

enum class FlowEdgeStyle {
    Solid,
    Dotted,
    Thick,
    NoArrow
};

struct FlowNode {
    std::string id;
    std::string label;
    FlowNodeShape shape = FlowNodeShape::Rectangle;
    int cluster = -1;
};

struct FlowEdge {
    std::string from;
    std::string to;
    std::string label;
    FlowEdgeStyle style = FlowEdgeStyle::Solid;
};

struct FlowCluster {
    std::string id;
    std::string title;
    std::vector<std::string> nodeIds;
};

struct FlowchartGraph {
    FlowDirection direction = FlowDirection::TopDown;
    std::vector<FlowNode> nodes;
    std::vector<FlowEdge> edges;
    std::vector<FlowCluster> clusters;
};

struct FlowchartParseResult {
    bool ok = false;
    std::string error;
    FlowchartGraph graph;
};

FlowchartParseResult ParseMermaidFlowchart(const std::string& source);

}  // namespace litemark
