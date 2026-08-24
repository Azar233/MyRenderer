#version 330 core

in float vViewDepth;

layout (location = 0) out float backfaceViewDepth;

void main() {
    backfaceViewDepth = max(vViewDepth, 0.0);
}
