// Adapted from https://github.com/Flix01/imgui/tree/c2dd0c9d58fdd6f6e6d3cad58d8e0e80ca9aebf0/addons/imguivariouscontrols addon library
#pragma once
// and https://github.com/ocornut/imgui/issues/632
// Stripped down to only PlotGraphlines and fixed up to work on ImGUI >v1.9x
#include <StdInc.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_API DLL_IMPORT
#include <imgui.h>

// These 2 have a completely different implementation:
// Posted by @JaapSuter and @maxint (please see: https://github.com/ocornut/imgui/issues/632)
void PlotMultiLines(const char* label,
int num_datas,
const char** names,
const ImColor* colors,
float (*getter)(const void* data, int idx),
const void* const* datas,
int values_count,
float scale_min,
float scale_max,
ImVec2 graph_size
);
