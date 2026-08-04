#pragma once

#include <filesystem>
#include <string>

#include "render/Vertex.h"

struct ObjLoadResult {
    MeshData mesh;
    std::string warnings;
};

class ObjLoader {
public:
    static ObjLoadResult load(const std::filesystem::path& path);
};
