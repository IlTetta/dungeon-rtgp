#pragma once

// Mesh: the geometry of one object, on the GPU. It keeps the vertices and the indices
// and owns the three OpenGL objects that hold them:
//   VBO (Vertex Buffer Object): the vertex data (position, normal, uv, ...)
//   EBO (Element Buffer Object): the triangle indices
//   VAO (Vertex Array Object): remembers how to read the VBO (the attribute layout) and the EBO

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>

// One vertex
struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
};

class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    GLuint VAO;

    // Why MOVE ONLY: a Mesh owns GPU buffers. If we could copy it, two objects would have the
    // same buffer ids: the first one destroyed would delete the buffers and the other one would
    // keep invalid ids. So copying is forbidden and moving is allowed (the ownership passes to
    // the new object): there is always exactly one owner.
    Mesh(const Mesh& other) = delete;
    Mesh& operator=(const Mesh& other) = delete;

    // The constructor MOVES the two vectors into the mesh (no copy of the vertex data), so the
    // vectors passed in are empty afterwards.
    Mesh(std::vector<Vertex>& vertices, std::vector<GLuint>& indices) noexcept
        : vertices(std::move(vertices)), indices(std::move(indices))
    {
        setupMesh();
    }

    // Move constructor: take the buffers of "other" and set other.VAO = 0, so its destructor does
    // not delete them. VAO != 0 is our "I own the buffers" flag.
    Mesh(Mesh&& other) noexcept
        : vertices(std::move(other.vertices)),
          indices(std::move(other.indices)),
          VAO(other.VAO), VBO(other.VBO), EBO(other.EBO)
    {
        other.VAO = 0;
    }

    // Move assignment: same idea, but first we free the buffers we already own, otherwise
    // they would leak.
    Mesh& operator=(Mesh&& other) noexcept {
        freeGPU();

        if (other.VAO) {
            vertices = std::move(other.vertices);
            indices = std::move(other.indices);
            VAO = other.VAO;
            VBO = other.VBO;
            EBO = other.EBO;
            other.VAO = 0;
        }
        else {
            VAO = 0;
        }
        return *this;
    }

    ~Mesh() noexcept {
        freeGPU();
    }

    // Bind the VAO and draw all the indexed triangles. It is const because the renderer gets
    // the Scene as const Scene&, and drawing does not change the Mesh.
    void draw() const {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, this->indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

private:
    GLuint VBO, EBO;

    // Create the buffers, upload the data and describe the vertex layout.
    void setupMesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        // from here on the buffer and attribute settings are stored in this VAO
        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), &indices[0], GL_STATIC_DRAW);

        // One attribute per field. The number is the location used in the vertex shader
        // ("layout (location = N)"); then how many floats, the type, the stride (the size of a
        // whole Vertex) and the offset of the field inside the Vertex.
        glEnableVertexAttribArray(0);   // Position
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(1);   // Normal
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        glEnableVertexAttribArray(2);   // TexCoords
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        glEnableVertexAttribArray(3);   // Tangent
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        glEnableVertexAttribArray(4);   // Bitangent
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

        // unbind, so later calls cannot change this VAO by mistake
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // delete the GPU buffers, only if we still own them
    void freeGPU() {
        if (VAO) {
            glDeleteVertexArrays(1, &VAO);
            glDeleteBuffers(1, &VBO);
            glDeleteBuffers(1, &EBO);
        }
    }
};
