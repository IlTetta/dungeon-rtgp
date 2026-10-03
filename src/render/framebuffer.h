#pragma once

// Framebuffer: an off-screen render target (FBO) with a color texture and/or a depth texture.
//
// The Renderer uses one (sceneFBO) for the color pass: the scene is drawn INTO its textures
// instead of onto the screen (the default framebuffer, id 0), so the fog pass can then read back
// both the color and the depth, composite the fog onto the screen, and copy the depth there
// (blitDepthToScreen). The shadow maps and the SSAO targets have their own FBOs, built in
// ShadowMaps and SsaoPass.
//
// Like Mesh (engine/mesh.h), a Framebuffer owns GPU objects (the FBO and its textures), so it is
// "move only": copying it would leave two C++ objects owning the same GPU resources.

#include <glad/glad.h>

class Framebuffer {
public:
    // Creates the FBO at the given size and attaches:
    //   - a color texture, if "wantColor" is true (RGBA, 8 bits per channel: enough for a
    //     normal color pass)
    //   - a depth texture, if "wantDepth" is true (the fog pass reads it to rebuild the world
    //     position of each pixel)
    // At least one of the two must be true, or the FBO would have nothing to render into.
    Framebuffer(int width, int height, bool wantColor, bool wantDepth);

    // forbid copy (see the comment above / the same reasoning as in Mesh)
    Framebuffer(const Framebuffer& other) = delete;
    Framebuffer& operator=(const Framebuffer& other) = delete;

    // move constructor / assignment: transfer ownership of the GPU objects
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    ~Framebuffer() noexcept;

    // Makes following draw calls render into this framebuffer's textures instead of the
    // screen, and sets the viewport to this framebuffer's size (it is very easy to
    // forget this second part and end up with only a corner of the texture filled in).
    void bind() const;

    // Goes back to rendering to the screen (framebuffer 0), and restores the viewport to
    // the given screen size. The caller has to pass the screen size back in because this
    // class only knows its OWN width/height, not the window's.
    static void unbind(int screenWidth, int screenHeight);

    // Recreates the color/depth textures at a new size (Renderer::setViewport). Whatever was
    // rendered before is lost.
    void resize(int width, int height);

    // The GL texture ids of the attachments, so a later shader can sample them (the fog pass
    // reads both). Returns 0 if that attachment was not requested in the constructor.
    GLuint colorTexture() const { return colorTex; }
    GLuint depthTexture() const { return depthTex; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    // Copy this framebuffer's DEPTH buffer into the default framebuffer (id 0), scaled to the given
    // screen size. Used so things drawn straight to the screen AFTER an off-screen pass (e.g. the
    // particle sparks / debug overlays drawn after the fog composite, which reads this FBO) can
    // still depth-test against this framebuffer's scene depth. Leaves the default framebuffer bound.
    void blitDepthToScreen(int screenWidth, int screenHeight) const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width, height, 0, 0, screenWidth, screenHeight,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

private:
    GLuint fbo = 0;
    GLuint colorTex = 0;
    GLuint depthTex = 0;
    bool hasColor = false;
    bool hasDepth = false;
    int width = 0;
    int height = 0;

    // (re)creates fbo/colorTex/depthTex for the current width/height; used by both the
    // constructor and resize()
    void create();

    // deletes whatever GPU objects we still own (fbo != 0 is our "do I own anything?"
    // flag, same pattern as Mesh::VAO in engine/mesh.h)
    void freeGPU();
};
