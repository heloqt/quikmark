#include "core/flow/Flowchart.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "flowchart_tests: " << message << '\n';
        ++failures;
    }
}

void TestSupportedDirections() {
    using litemark::FlowDirection;
    struct Case {
        const char* source;
        FlowDirection direction;
    };
    const Case cases[] = {
        {"flowchart TD\nA --> B\n", FlowDirection::TopDown},
        {"graph TB\nA --> B\n", FlowDirection::TopDown},
        {"flowchart BT\nA --> B\n", FlowDirection::BottomUp},
        {"flowchart LR\nA --> B\n", FlowDirection::LeftRight},
        {"graph RL\nA --> B\n", FlowDirection::RightLeft},
    };
    for (const auto& item : cases) {
        const auto result = litemark::ParseMermaidFlowchart(item.source);
        Check(result.ok, "supported direction should parse");
        Check(result.graph.direction == item.direction,
              "flowchart direction mapping mismatch");
    }
}

void TestSupportedNodeShapes() {
    using litemark::FlowNodeShape;
    const auto result = litemark::ParseMermaidFlowchart(
        "flowchart TD\n"
        "A[Rectangle]\n"
        "B(Rounded)\n"
        "C{Diamond}\n"
        "D((Circle))\n");
    Check(result.ok, "supported node shapes should parse");
    if (!result.ok || result.graph.nodes.size() != 4) {
        Check(false, "node shape fixture produced an unexpected node count");
        return;
    }
    Check(result.graph.nodes[0].shape == FlowNodeShape::Rectangle,
          "rectangle node shape mismatch");
    Check(result.graph.nodes[1].shape == FlowNodeShape::Rounded,
          "rounded node shape mismatch");
    Check(result.graph.nodes[2].shape == FlowNodeShape::Diamond,
          "diamond node shape mismatch");
    Check(result.graph.nodes[3].shape == FlowNodeShape::Circle,
          "circle node shape mismatch");
    Check(result.graph.nodes[0].label == "Rectangle",
          "node label mismatch");
}

void TestEdgesLabelsAndStyles() {
    using litemark::FlowEdgeStyle;
    const auto result = litemark::ParseMermaidFlowchart(
        "flowchart LR\n"
        "A -->|yes| B -.-> C ==> D --- E\n");
    Check(result.ok, "chained edges should parse");
    if (!result.ok || result.graph.edges.size() != 4) {
        Check(false, "chained edge fixture produced unexpected edge count");
        return;
    }
    Check(result.graph.edges[0].label == "yes", "edge label missing");
    Check(result.graph.edges[0].style == FlowEdgeStyle::Solid,
          "solid edge style mismatch");
    Check(result.graph.edges[1].style == FlowEdgeStyle::Dotted,
          "dotted edge style mismatch");
    Check(result.graph.edges[2].style == FlowEdgeStyle::Thick,
          "thick edge style mismatch");
    Check(result.graph.edges[3].style == FlowEdgeStyle::NoArrow,
          "no-arrow edge style mismatch");
    Check(result.graph.nodes.size() == 5,
          "chained edges should create five nodes");
}

void TestSingleLevelSubgraph() {
    const auto result = litemark::ParseMermaidFlowchart(
        "flowchart TD\n"
        "subgraph group [Work group]\n"
        "A[Start] --> B[Finish]\n"
        "end\n");
    Check(result.ok, "single-level subgraph should parse");
    if (result.ok) {
        Check(result.graph.clusters.size() == 1,
              "subgraph cluster missing");
        if (!result.graph.clusters.empty()) {
            Check(result.graph.clusters.front().id == "group",
                  "subgraph id mismatch");
            Check(result.graph.clusters.front().title == "Work group",
                  "subgraph title mismatch");
            Check(result.graph.clusters.front().nodeIds.size() == 2,
                  "subgraph should own both nodes");
        }
    }
}

void TestUnsupportedSyntaxFailsAsAWhole() {
    const char* sources[] = {
        "flowchart\nA --> B\n",
        "sequenceDiagram\nA --> B\n",
        "flowchart TD\nsubgraph outer\nsubgraph inner\nend\nend\n",
        "flowchart TD\nA --> A\n",
        "flowchart TD\nA -->|missing B\n",
        "flowchart TD\nA[\n",
        "flowchart TD\nstyle A fill:#fff\nA --> B\n",
        "flowchart TD\nclassDef hot fill:#fff\nA --> B\n",
        "flowchart TD\nclick A callback\n",
        "",
    };
    for (const char* source : sources) {
        const auto result = litemark::ParseMermaidFlowchart(source);
        Check(!result.ok,
              "unsupported Mermaid syntax should fail and use source fallback");
    }
}

void TestCommentsAndSemicolons() {
    const auto result = litemark::ParseMermaidFlowchart(
        "%% comment\n"
        "graph TD\n"
        "%% another comment\n"
        "A[Start] --> B[End];\n");
    Check(result.ok, "comments and trailing semicolon should be accepted");
    Check(result.graph.nodes.size() == 2,
          "comment fixture should produce two nodes");
    Check(result.graph.edges.size() == 1,
          "comment fixture should produce one edge");
}

}  // namespace

int main() {
    TestSupportedDirections();
    TestSupportedNodeShapes();
    TestEdgesLabelsAndStyles();
    TestSingleLevelSubgraph();
    TestUnsupportedSyntaxFailsAsAWhole();
    TestCommentsAndSemicolons();
    if (failures != 0) {
        std::cerr << "flowchart_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "flowchart_tests: 6 cases passed\n";
    return EXIT_SUCCESS;
}
