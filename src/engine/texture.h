#pragma once

// Loads an image file into an OpenGL 2D texture and returns its id.
//
// The decoding of the PNG/JPG bytes is done by the stb_image library (used as-is); the OpenGL
// part (create the texture, upload the pixels, set filtering/wrapping, build mipmaps) is ours -
// it is the same kind of thin GPU wrapper as our Mesh class.
//
// We return a plain GLuint (not a RAII class): the few textures we load live for the whole run
// of the program, so we let the driver free them at exit. Returns 0 if the file could not be read.

#include <glad/glad.h>
#include <stb/stb_image.h>

#include <iostream>

inline GLuint loadTexture(const char* path) {
    GLuint id;
    glGenTextures(1, &id);

    // OpenGL expects the first row of the image to be the BOTTOM of the picture, but image files
    // store the top row first, so we ask stb to flip it vertically on load.
    stbi_set_flip_vertically_on_load(true);

    int width, height, channels;
    unsigned char* data = stbi_load(path, &width, &height, &channels, 0);
    if (data) {
        // pick the OpenGL format from the number of channels in the image
        GLenum format = GL_RGB;
        if (channels == 1)      format = GL_RED;
        else if (channels == 4) format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);   // smaller versions used when the surface is far away

        // REPEAT so the texture tiles across large surfaces; mipmaps for smooth minification
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);
    }
    else {
        std::cout << "ERROR: could not load texture: " << path << std::endl;
    }

    return id;
}
