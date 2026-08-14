#pragma once

// Minimal Wavefront .OBJ loader (our own, no Assimp).
//
// It reads the common parts of an .obj file:
//   v  x y z     -> a vertex position
//   vt u v       -> a texture coordinate
//   vn x y z     -> a vertex normal
//   f  a b c ...  -> a face, where each corner "a" is written as "v", "v/vt", "v//vn" or "v/vt/vn"
//                    (the indices are 1-based in the file). Faces with more than 3 corners
//                    (quads, n-gons) are triangulated with a simple fan.
//
// It is intentionally simple: enough to load static decorative props (columns, statues, torches).
// It does not handle materials (.mtl), smoothing groups, or negative/relative indices.
//
// Because our Vertex bundles position + normal + uv together, but the .obj stores those in three
// separate lists, for every face-corner we build one Vertex that combines the three referenced
// values. We do NOT remove duplicate vertices (one Vertex per face-corner): simpler, and fine for
// static props. De-duplicating would only reduce memory, not the triangle count.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "engine/mesh.h"

// Parse one face-corner token ("3", "3/1", "3//2", "3/1/2") into 0-based indices.
// An index that is not present becomes -1.
inline void parseObjCorner(const std::string& token, int& posIdx, int& uvIdx, int& normIdx) {
    posIdx = uvIdx = normIdx = -1;

    size_t firstSlash = token.find('/');
    if (firstSlash == std::string::npos) {
        // just "v"
        posIdx = std::stoi(token);
    }
    else {
        posIdx = std::stoi(token.substr(0, firstSlash));
        size_t secondSlash = token.find('/', firstSlash + 1);

        if (secondSlash == std::string::npos) {
            // "v/vt"
            std::string vt = token.substr(firstSlash + 1);
            if (!vt.empty()) uvIdx = std::stoi(vt);
        }
        else {
            // "v/vt/vn" or "v//vn"
            std::string vt = token.substr(firstSlash + 1, secondSlash - firstSlash - 1);
            if (!vt.empty()) uvIdx = std::stoi(vt);
            std::string vn = token.substr(secondSlash + 1);
            if (!vn.empty()) normIdx = std::stoi(vn);
        }
    }

    // .obj indices are 1-based -> make them 0-based
    if (posIdx  > 0) posIdx  -= 1;
    if (uvIdx   > 0) uvIdx   -= 1;
    if (normIdx > 0) normIdx -= 1;
}

inline Mesh loadOBJ(const char* path) {
    // the three separate lists as stored in the file
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    // the combined vertices/indices we will hand to the Mesh
    std::vector<Vertex> outVertices;
    std::vector<GLuint> outIndices;
    bool fileHasNormals = false;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "ERROR: OBJ file not found: " << path << std::endl;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::string tag;
        stream >> tag;

        if (tag == "v") {
            glm::vec3 p;
            stream >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (tag == "vt") {
            glm::vec2 t;
            stream >> t.x >> t.y;
            uvs.push_back(t);
        }
        else if (tag == "vn") {
            glm::vec3 n;
            stream >> n.x >> n.y >> n.z;
            normals.push_back(n);
            fileHasNormals = true;
        }
        else if (tag == "f") {
            // read all the corner tokens of this face
            std::vector<std::string> corners;
            std::string tok;
            while (stream >> tok) corners.push_back(tok);

            // triangulate as a fan: (0,1,2), (0,2,3), (0,3,4), ...
            for (size_t i = 1; i + 1 < corners.size(); i++) {
                std::string triangle[3] = { corners[0], corners[i], corners[i + 1] };
                for (int k = 0; k < 3; k++) {
                    int pi, ti, ni;
                    parseObjCorner(triangle[k], pi, ti, ni);

                    Vertex v;
                    v.Position  = (pi >= 0 && pi < (int)positions.size()) ? positions[pi] : glm::vec3(0.0f);
                    v.TexCoords = (ti >= 0 && ti < (int)uvs.size())       ? uvs[ti]       : glm::vec2(0.0f);
                    v.Normal    = (ni >= 0 && ni < (int)normals.size())   ? normals[ni]   : glm::vec3(0.0f);
                    v.Tangent   = glm::vec3(0.0f);   // not needed until normal mapping
                    v.Bitangent = glm::vec3(0.0f);

                    outVertices.push_back(v);
                    outIndices.push_back((GLuint)outVertices.size() - 1);
                }
            }
        }
        // any other tag (mtllib, usemtl, o, g, s, comments...) is ignored
    }

    // if the file had no normals, compute a flat normal per triangle so lighting still works
    if (!fileHasNormals) {
        for (size_t i = 0; i + 2 < outVertices.size(); i += 3) {
            glm::vec3 a = outVertices[i].Position;
            glm::vec3 b = outVertices[i + 1].Position;
            glm::vec3 c = outVertices[i + 2].Position;
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            outVertices[i].Normal = n;
            outVertices[i + 1].Normal = n;
            outVertices[i + 2].Normal = n;
        }
    }

    // safety: never build a Mesh from empty data (the Mesh uploads &vertices[0], which would
    // crash on an empty vector). If loading failed, return a tiny dummy triangle instead.
    if (outVertices.empty()) {
        std::cout << "WARNING: OBJ produced no geometry: " << path << std::endl;
        Vertex dummy;
        dummy.Position = glm::vec3(0.0f);
        dummy.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
        dummy.TexCoords = glm::vec2(0.0f);
        dummy.Tangent = glm::vec3(0.0f);
        dummy.Bitangent = glm::vec3(0.0f);
        outVertices = { dummy, dummy, dummy };
        outIndices = { 0, 1, 2 };
    }

    return Mesh(outVertices, outIndices);
}
