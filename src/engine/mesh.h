#pragma once

// Mesh class.
// A Mesh owns the geometry of one object ON THE GPU: the list of vertices, the list of
// indices (which vertices form each triangle), and the three OpenGL buffers that hold
// this data on the graphics card:
//   VBO (Vertex Buffer Object)  -> the vertex data (position, normal, uv, ...)
//   EBO (Element Buffer Object) -> the indices of the triangles
//   VAO (Vertex Array Object)   -> the "recipe" that tells OpenGL how to read the VBO
//
// Like our Shader, this header includes glad by itself.

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>

// One vertex of the mesh, with all its attributes.
// The order of these fields matters: in setupMesh() below we describe them to OpenGL
// using their offset inside this struct.
struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;   // uv coordinates for texturing
    glm::vec3 Tangent;     // needed for normal mapping (M2); computed by Assimp when loading
    glm::vec3 Bitangent;
};

class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    GLuint VAO;

    // --- Why "move only"? ---
    // A Mesh owns GPU buffers. If we allowed copying a Mesh, we would have two C++ objects
    // pointing at the SAME GPU buffers; when the first one is destroyed it would delete
    // those buffers, and the second one would be left with invalid ids -> bugs / crashes.
    // So we FORBID copying, and instead we allow MOVING (transferring the ownership of the
    // buffers from one object to another). This way there is always exactly one owner.

    // forbid copy: these two lines make "Mesh a = b;" and "a = b;" not compile
    Mesh(const Mesh& other) = delete;
    Mesh& operator=(const Mesh& other) = delete;

    // Constructor.
    // N.B. it takes the vectors by reference and MOVES their content into the mesh, so after
    // you build a Mesh the vectors you passed become empty. This avoids copying all the
    // vertex data (which can be big).
    Mesh(std::vector<Vertex>& vertices, std::vector<GLuint>& indices) noexcept
        : vertices(std::move(vertices)), indices(std::move(indices))
    {
        setupMesh();
    }

    // Move constructor: the new mesh steals the buffers from "other".
    Mesh(Mesh&& other) noexcept
        : vertices(std::move(other.vertices)),
          indices(std::move(other.indices)),
          VAO(other.VAO), VBO(other.VBO), EBO(other.EBO)
    {
        // we set other.VAO to 0 so that when "other" is destroyed it does NOT delete our
        // buffers (see freeGPU(): it only deletes if VAO != 0). We use VAO as the "do I
        // still own the buffers?" flag.
        other.VAO = 0;
    }

    // Move assignment: same idea, but on an object that already exists.
    Mesh& operator=(Mesh&& other) noexcept {
        // first free the buffers we may already own, otherwise we would leak them
        freeGPU();

        if (other.VAO) {   // does "other" actually own buffers?
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

    // Destructor: frees the GPU buffers (if we still own them).
    ~Mesh() noexcept {
        freeGPU();
    }

    // Draw the mesh: bind its VAO and ask OpenGL to draw the triangles from the indices.
    void draw() {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, this->indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

private:
    GLuint VBO, EBO;

    // Create the GPU buffers and tell OpenGL how the vertex data is laid out.
    void setupMesh() {
        // create the three buffers
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        // from now on, we configure things "inside" this VAO
        glBindVertexArray(VAO);

        // upload the vertex data into the VBO
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

        // upload the indices into the EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), &indices[0], GL_STATIC_DRAW);

        // Now we describe each vertex attribute. The number (0, 1, 2, ...) is the "location"
        // we will use in the vertex shader with "layout (location = N)".
        // For each attribute we say: location, how many floats, the type, the stride
        // (= size of one whole Vertex), and the offset of that field inside the Vertex.

        // location 0 = Position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        // location 1 = Normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        // location 2 = TexCoords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        // location 3 = Tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        // location 4 = Bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

        // unbind, to avoid modifying this VAO by mistake later
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // Delete the GPU buffers, but only if we still own them (VAO != 0).
    void freeGPU() {
        if (VAO) {
            glDeleteVertexArrays(1, &VAO);
            glDeleteBuffers(1, &VBO);
            glDeleteBuffers(1, &EBO);
        }
    }
};
