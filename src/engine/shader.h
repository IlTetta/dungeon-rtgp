#pragma once

// Shader: reads the shader source files, compiles them and links them into one OpenGL program,
// that we activate with use() before drawing.

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class Shader {
public:
    // id of the linked program
    GLuint program;

    // Vertex + fragment shader: read, compile, link.
    Shader(const char* vertexPath, const char* fragmentPath) {
        std::string vertexCode = readFile(vertexPath);
        std::string fragmentCode = readFile(fragmentPath);

        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();

        GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");

        GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");

        this->program = glCreateProgram();
        glAttachShader(this->program, vertex);
        glAttachShader(this->program, fragment);
        glLinkProgram(this->program);
        checkCompileErrors(this->program, "PROGRAM");

        // after linking the program has its own copy, the shader objects are not needed
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    // Same, with a geometry shader in the middle (vertex -> geometry -> fragment).
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

    void use() {
        glUseProgram(this->program);
    }

    // free the program, once at shutdown
    void clean() {
        glDeleteProgram(this->program);
    }

    // Set a uniform (a value sent from C++ to the shader). The location is looked up by name
    // every time: simple, but a string lookup per call.
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
        // GL_FALSE = no transpose: glm is already column-major like OpenGL
        glUniformMatrix4fv(glGetUniformLocation(this->program, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
    }

private:
    // Read a whole text file into a string. With the stream exceptions turned on, a missing
    // file gives a clear error message instead of an empty shader without explanation.
    std::string readFile(const char* path) {
        std::string code;
        std::ifstream file;
        file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try {
            file.open(path);
            std::stringstream stream;
            stream << file.rdbuf();
            file.close();
            code = stream.str();
        }
        catch (std::ifstream::failure const&) {
            std::cout << "ERROR: shader file not read: " << path << std::endl;
        }
        return code;
    }

    // Print the error log if a shader did not compile or the program did not link. Without it a
    // broken shader just draws nothing and we would not know why.
    void checkCompileErrors(GLuint object, std::string type) {
        GLint success;
        GLchar infoLog[1024];
        if (type != "PROGRAM") {
            glGetShaderiv(object, GL_COMPILE_STATUS, &success);
            if (!success) {
                glGetShaderInfoLog(object, 1024, NULL, infoLog);
                std::cout << "ERROR: shader compilation failed (" << type << ")\n" << infoLog << std::endl;
            }
        }
        else {
            glGetProgramiv(object, GL_LINK_STATUS, &success);
            if (!success) {
                glGetProgramInfoLog(object, 1024, NULL, infoLog);
                std::cout << "ERROR: program linking failed\n" << infoLog << std::endl;
            }
        }
    }
};
