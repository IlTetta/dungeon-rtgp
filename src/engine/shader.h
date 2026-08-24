#pragma once

// Shader class.
// It loads a vertex shader and a fragment shader from two text files, compiles them,
// and links them together into a single OpenGL "shader program" that we can activate
// before drawing.
//
// This is our own rewrite of the lab Shader class (which was based on LearnOpenGL).
// There's also an overload taking a geometry shader path (used by the point-light shadow
// pass), see below.
//
// Difference from the lab version: this header includes glad by itself, so you do NOT
// have to remember to include <glad/glad.h> before including this file.

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>   // glm::value_ptr: turns a glm vec/mat into a raw float pointer for OpenGL

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class Shader {
public:
    // The id (a number) that OpenGL gives us for the linked program.
    // It is public because the rest of the code sometimes needs it directly.
    GLuint program;

    // Constructor: does everything in one go (read the files, compile, link).
    // We pass the two file paths (for example "shaders/basic.vert" and "shaders/basic.frag").
    Shader(const char* vertexPath, const char* fragmentPath) {
        // --- Step 1: read the two shader files into strings ---
        std::string vertexCode = readFile(vertexPath);
        std::string fragmentCode = readFile(fragmentPath);

        // OpenGL wants a plain C string (const char*), not a std::string,
        // so we get the internal C string out of each std::string.
        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();

        // --- Step 2: compile the two shaders one by one ---
        // vertex shader
        GLuint vertex = glCreateShader(GL_VERTEX_SHADER);   // create an empty shader object
        glShaderSource(vertex, 1, &vShaderCode, NULL);      // give it the source code
        glCompileShader(vertex);                            // compile it
        checkCompileErrors(vertex, "VERTEX");               // print errors if any

        // fragment shader (same steps)
        GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");

        // --- Step 3: link the two compiled shaders into one program ---
        this->program = glCreateProgram();
        glAttachShader(this->program, vertex);
        glAttachShader(this->program, fragment);
        glLinkProgram(this->program);
        checkCompileErrors(this->program, "PROGRAM");

        // --- Step 4: the single shaders are now copied inside the program, ---
        // we do not need the separate objects anymore, so we delete them.
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    // Same idea, with a geometry shader in the middle (vertex -> geometry -> fragment).
    Shader(const char* vertexPath, const char* geometryPath, const char* fragmentPath) {
        std::string vertexCode = readFile(vertexPath);
        std::string geometryCode = readFile(geometryPath);
        std::string fragmentCode = readFile(fragmentPath);

        const char* vShaderCode = vertexCode.c_str();
        const char* gShaderCode = geometryCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();

        GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");

        GLuint geometry = glCreateShader(GL_GEOMETRY_SHADER);
        glShaderSource(geometry, 1, &gShaderCode, NULL);
        glCompileShader(geometry);
        checkCompileErrors(geometry, "GEOMETRY");

        GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");

        this->program = glCreateProgram();
        glAttachShader(this->program, vertex);
        glAttachShader(this->program, geometry);
        glAttachShader(this->program, fragment);
        glLinkProgram(this->program);
        checkCompileErrors(this->program, "PROGRAM");

        glDeleteShader(vertex);
        glDeleteShader(geometry);
        glDeleteShader(fragment);
    }

    // Activate this program: from now on, the draw calls use these shaders.
    void use() {
        glUseProgram(this->program);
    }

    // Free the program on the GPU. Call it once, when we close the application.
    void clean() {
        glDeleteProgram(this->program);
    }

    // --- Small helpers to set uniform variables. ---
    // A "uniform" is a value we send from C++ to the shader (matrices, colors, ...).
    void setInt(const std::string& name, int value) {
        glUniform1i(glGetUniformLocation(this->program, name.c_str()), value);
    }
    void setFloat(const std::string& name, float value) {
        glUniform1f(glGetUniformLocation(this->program, name.c_str()), value);
    }
    void setVec2(const std::string& name, const glm::vec2& value) {
        glUniform2fv(glGetUniformLocation(this->program, name.c_str()), 1, glm::value_ptr(value));
    }
    void setVec3(const std::string& name, const glm::vec3& value) {
        glUniform3fv(glGetUniformLocation(this->program, name.c_str()), 1, glm::value_ptr(value));
    }
    void setMat3(const std::string& name, const glm::mat3& value) {
        glUniformMatrix3fv(glGetUniformLocation(this->program, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
    }
    void setMat4(const std::string& name, const glm::mat4& value) {
        // the GL_FALSE means "do not transpose the matrix", glm already stores it the way OpenGL wants
        glUniformMatrix4fv(glGetUniformLocation(this->program, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
    }

private:
    // Read a whole text file and return its content as a single string.
    std::string readFile(const char* path) {
        std::string code;
        std::ifstream file;
        // we tell the stream to throw an exception if the reading fails,
        // so we can catch it and print a clear message instead of getting a silent bug.
        file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try {
            file.open(path);
            std::stringstream stream;
            stream << file.rdbuf();   // copy the whole file content into the string stream
            file.close();
            code = stream.str();      // turn the stream into a std::string
        }
        catch (std::ifstream::failure const&) {
            std::cout << "ERROR: shader file not read: " << path << std::endl;
        }
        return code;
    }

    // Check if a shader compiled correctly, or if the program linked correctly, and print
    // the error message if not. This is very important while developing: without it, a wrong
    // shader just draws nothing (black screen) and you have no clue why.
    void checkCompileErrors(GLuint object, std::string type) {
        GLint success;
        GLchar infoLog[1024];
        if (type != "PROGRAM") {
            // here "object" is a single shader -> check the COMPILE status
            glGetShaderiv(object, GL_COMPILE_STATUS, &success);
            if (!success) {
                glGetShaderInfoLog(object, 1024, NULL, infoLog);
                std::cout << "ERROR: shader compilation failed (" << type << ")\n" << infoLog << std::endl;
            }
        }
        else {
            // here "object" is the program -> check the LINK status
            glGetProgramiv(object, GL_LINK_STATUS, &success);
            if (!success) {
                glGetProgramInfoLog(object, 1024, NULL, infoLog);
                std::cout << "ERROR: program linking failed\n" << infoLog << std::endl;
            }
        }
    }
};
