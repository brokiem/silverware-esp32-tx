#pragma once

#include <stdbool.h>

struct ButtonEdgeState {
    bool start;
    bool view;
    bool b;
};

struct ButtonEdges {
    bool startClicked;
    bool viewClicked;
    bool bClicked;
};

ButtonEdges detect_button_edges(ButtonEdgeState& previous, bool start, bool view, bool b);
