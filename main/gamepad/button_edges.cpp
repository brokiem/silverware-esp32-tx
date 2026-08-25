#include "button_edges.h"

ButtonEdges detect_button_edges(ButtonEdgeState& previous, bool start, bool view, bool b) {
    const ButtonEdges edges = {start && !previous.start, view && !previous.view, b && !previous.b};
    previous = {start, view, b};
    return edges;
}
