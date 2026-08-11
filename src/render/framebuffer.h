#pragma once

// Framebuffer class — SCAFFOLDING for future milestones.
//
// So far we have only ever rendered directly to the screen (the "default framebuffer",
// id 0, that GLFW gives us together with the window). To do shadow mapping we will need
// to first render the scene from the LIGHT's point of view into an off-screen depth
// texture (no color, just depth), and later, to sample that texture as a shadow map
// while rendering the real (camera) view. Volumetric fog similarly benefits from
// rendering into an off-screen color+depth target that a later pass can read from.
//
// This class is the common piece both of those need: create a Frame Buffer Object
// (FBO), attach to it a color texture and/or a depth texture, bind it (so following draw
// calls render INTO its textures instead of onto the screen), and unbind it (go back to
// rendering to the screen). It is not used anywhere yet — Renderer still renders
// straight to the screen — but it is ready for the shadow-mapping milestone to build on.
//
// Like Mesh (engine/mesh.h), a Framebuffer owns GPU objects (the FBO itself, plus its
// texture attachments), so it follows the same "move only" pattern: copying it would
// leave two C++ objects owning the same GPU resources, which is not what we want.

#include <glad/glad.h>

class Framebuffer {
public:
    // Creates the FBO at the given size and attaches:
    //   - a color texture, if "wantColor" is true (RGBA, 8 bits per channel — enough for
    //     a normal color pass; a fog/HDR pass that needs more range can ask for a
    //     floating-point format later, this constructor can grow a parameter for that)
    //   - a depth texture, if "wantDepth" is true (this is what shadow mapping reads
    //     from: the depth seen from the light)
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

    // Recreates the color/depth textures at a new size (e.g. the window was resized, or
    // the shadow map resolution setting changed). Whatever was rendered before is lost,
    // same as with any other texture resize.
    void resize(int width, int height);

    // The GL texture ids of the attachments, so a later shader can bind them with
    // glBindTexture(GL_TEXTURE_2D, ...) to sample them (e.g. the shadow-mapping shader
    // sampling the depth texture, or a fog pass sampling the color texture).
    // Returns 0 if that attachment was not requested in the constructor.
    GLuint colorTexture() const { return colorTex; }
    GLuint depthTexture() const { return depthTex; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

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
