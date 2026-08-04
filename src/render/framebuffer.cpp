// framebuffer.cpp
// See framebuffer.h for the "why" of each part; here we just implement it.

#include "render/framebuffer.h"

#include <iostream>

Framebuffer::Framebuffer(int width, int height, bool wantColor, bool wantDepth)
    : hasColor(wantColor), hasDepth(wantDepth), width(width), height(height)
{
    create();
}

void Framebuffer::create() {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    if (hasColor) {
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        // GL_RGBA8: 8 bits per channel, plenty for a normal color image (not HDR/fog yet)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    }

    if (hasDepth) {
        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        // GL_DEPTH_COMPONENT: one float per texel, holding the depth value written by
        // the depth test. This is exactly what a shadow map is: "how far is the closest
        // thing the light can see, in each direction".
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // CLAMP_TO_BORDER + a white border is the usual shadow-mapping trick so that
        // anything outside the light's view is treated as "not in shadow" (see the
        // shadow-mapping milestone for where borderColor gets set to 1.0); we leave the
        // wrap mode as CLAMP_TO_EDGE here since we are not doing that lookup yet, and
        // revisit it when shadow mapping is actually implemented.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
    }

    if (!hasColor) {
        // A framebuffer with only a depth attachment (our shadow-map case) has no color
        // buffer to write to. Without these two calls some drivers consider the FBO
        // incomplete, because by default OpenGL expects a color buffer to exist.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "ERROR: framebuffer is not complete (status " << status << ")" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);   // do not leave it bound by accident
}

void Framebuffer::freeGPU() {
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        if (hasColor) glDeleteTextures(1, &colorTex);
        if (hasDepth) glDeleteTextures(1, &depthTex);
        fbo = 0;
        colorTex = 0;
        depthTex = 0;
    }
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : fbo(other.fbo), colorTex(other.colorTex), depthTex(other.depthTex),
      hasColor(other.hasColor), hasDepth(other.hasDepth),
      width(other.width), height(other.height)
{
    // same "steal, then neutralize the source" pattern as Mesh's move constructor
    // (engine/mesh.h): fbo == 0 means "I do not own any GPU object", so setting
    // other.fbo to 0 stops its destructor from deleting the objects we just took.
    other.fbo = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    freeGPU();

    fbo = other.fbo;
    colorTex = other.colorTex;
    depthTex = other.depthTex;
    hasColor = other.hasColor;
    hasDepth = other.hasDepth;
    width = other.width;
    height = other.height;

    other.fbo = 0;
    return *this;
}

Framebuffer::~Framebuffer() noexcept {
    freeGPU();
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void Framebuffer::unbind(int screenWidth, int screenHeight) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenWidth, screenHeight);
}

void Framebuffer::resize(int newWidth, int newHeight) {
    if (newWidth == width && newHeight == height) return;   // nothing to do

    freeGPU();
    width = newWidth;
    height = newHeight;
    create();
}
