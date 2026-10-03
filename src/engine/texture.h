#pragma once

// Load an image file into an OpenGL 2D texture and return its id.
//
// stb_image (used as it is) decodes the PNG/JPG; the OpenGL part is ours: create the texture,
// upload the pixels, build the mipmaps, set filtering and wrapping.

#include <glad/glad.h>
#include <stb/stb_image.h>

#include <iostream>

inline GLuint loadTexture(const char* path) {
    GLuint id;
    glGenTextures(1, &id);

    // OpenGL wants the first row of pixels to be the BOTTOM of the image, image files start
    // from the top, so stb flips the image while loading
    stbi_set_flip_vertically_on_load(true);

    int width, height, channels;
    unsigned char* data = stbi_load(path, &width, &height, &channels, 0);
    if (data) {
        GLenum format = GL_RGB;
        if (channels == 1)
            format = GL_RED;
        else if (channels == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);   // smaller copies, used for surfaces far away

        // REPEAT so the texture tiles over big surfaces (uvScale); trilinear filtering with the
        // mipmaps when the texture is shrunk, linear when it is enlarged
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
