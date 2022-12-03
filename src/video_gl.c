/*
Copyright (c) 2020-2022 Rupert Carmichael
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <epoxy/gl.h>
#include <SDL.h>
#include <jg/jg.h>

#define NUMPASSES 2
#define SIZE_GLSLVER 20

#define GLT_IMPLEMENTATION
#define GLT_MANUAL_VIEWPORT
#include "gltext.h"

#include "jgrf.h"
#include "video.h"
#include "video_gl.h"
#include "settings.h"

extern int bmark;

static jgrf_gdata_t *gdata = NULL;
static jg_setting_t *settings = NULL;

// SDL Window management
static SDL_Window *window;
static SDL_Cursor *cursor;
static SDL_GLContext glcontext;

// Pointer to the video buffer when allocated by the frontend
static void *videobuf = NULL;

// Pointer to the core's video information
static jg_videoinfo_t *vidinfo = NULL;

// OpenGL related variables
static GLuint vao[NUMPASSES];
static GLuint vbo[NUMPASSES];
static GLuint shaderprog[NUMPASSES];
static GLuint tex[NUMPASSES];
static GLuint texfilter[NUMPASSES];
static GLuint framebuf; // Framebuffer for rendering offscreen

// Triangle and Texture vertices
static GLfloat vertices[] = {
    -1.0, -1.0, // Vertex 1 (X, Y) Left Bottom
    -1.0, 1.0,  // Vertex 2 (X, Y) Left Top
    1.0, -1.0,  // Vertex 3 (X, Y) Right Bottom
    1.0, 1.0,   // Vertex 4 (X, Y) Right Top

    0.0, 0.0,   // Texture 2 (X, Y) Left Top
    0.0, 1.0,   // Texture 1 (X, Y) Left Bottom
    1.0, 0.0,   // Texture 4 (X, Y) Right Top
    1.0, 1.0,   // Texture 3 (X, Y) Right Bottom
};

static GLTtext *msgtext[3];
static int textframes[3];

// Pixel Format
static struct _pixfmt {
    GLuint format;
    GLuint format_internal;
    GLuint type;
    size_t size;
} pixfmt;

// Dimensions
static struct _dimensions {
    int ww; int wh;
    float rw; float rh;
    float xo; float yo;
    float dpiscale;
} dimensions;

// Create the SDL OpenGL Window
void jgrf_video_gl_create(void) {
    // Set the GL version
    switch (settings[VIDEO_API].val) {
        default: case 0: { // OpenGL - Core Profile
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                SDL_GL_CONTEXT_PROFILE_CORE);
            break;
        }
        case 1: {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                SDL_GL_CONTEXT_PROFILE_ES);
            break;
        }
        case 2: {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
            break;
        }
    }

    // Set window flags
    Uint32 windowflags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL |
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

    // Set up the window
    char title[128];
    gdata = jgrf_gdata_ptr();
    snprintf(title, sizeof(title), "%s", gdata->gamename);

    // Set the window dimensions
    dimensions.ww =
        (vidinfo->aspect * vidinfo->h * settings[VIDEO_SCALE].val) + 0.5;
    dimensions.wh = (vidinfo->h * settings[VIDEO_SCALE].val) + 0.5;
    dimensions.rw = dimensions.ww;
    dimensions.rh = dimensions.wh;

    jgrf_log(JG_LOG_DBG, "Creating window with dimensions: %d x %d\n",
        dimensions.ww, dimensions.wh);

    window = SDL_CreateWindow(
        title,                      // title
        SDL_WINDOWPOS_UNDEFINED,    // initial x position
        SDL_WINDOWPOS_UNDEFINED,    // initial y position
        dimensions.ww,              // width
        dimensions.wh,              // height
        windowflags);

    if (!window)
        jgrf_log(JG_LOG_ERR, "Failed to create window: %s\n", SDL_GetError());

    // Store the DPI scale
    int x, y;
    SDL_GL_GetDrawableSize(window, &x, &y);
    dimensions.dpiscale = (float)x/dimensions.ww;

    jgrf_video_icon_load(window);

    // Set the GL context
    glcontext = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glcontext);
    SDL_GL_SetSwapInterval(!bmark); // Vsync off in Benchmark mode

    if (!glcontext)
        jgrf_log(JG_LOG_WRN, "Failed to create glcontext: %s\n",
            SDL_GetError());

    // Initialize glText after setting GL context
    if (!gltInit(settings[VIDEO_API].val == 1))
        jgrf_log(JG_LOG_WRN, "Failed to initialize glText\n");

    for (int i = 0; i < 3; ++i)
        msgtext[i] = gltCreateText();

    // Do post window creation OpenGL setup
    if (settings[VIDEO_API].val > 1)
        jgrf_video_gl_setup_compat();
    else
        jgrf_video_gl_setup();

    jgrf_log(JG_LOG_INF, "Video: OpenGL %s\n", glGetString(GL_VERSION));

    SDL_ShowCursor(false);

    // Set fullscreen if required
    if (settings[VIDEO_FULLSCREEN].val)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
}

// Initialize video buffer
int jgrf_video_gl_init(void) {
    // Grab settings and global data pointers
    settings = jgrf_settings_ptr();
    gdata = jgrf_gdata_ptr();

    if (!(gdata->hints & JG_HINT_VIDEO_INTERNAL)) {
        // Address of allocated memory owned by frontend but passed to core
        videobuf = (void*)calloc(vidinfo->wmax * vidinfo->hmax, pixfmt.size);
        vidinfo->buf = videobuf;
    }

    if (gdata->hints & JG_HINT_VIDEO_PRESCALED)
        settings[VIDEO_SCALE].val = 1;

    return 1;
}

// Flip the image so that it can be written out with proper orientation
static inline void jgrf_video_gl_ssflip(uint8_t *pixels,
    int width, int height, int bytes) {
    // Flip the pixels
    size_t rowsize = width * bytes;
    uint8_t *row = (uint8_t*)calloc(rowsize, sizeof(uint8_t));
    uint8_t *low = pixels;
    uint8_t *high = &pixels[(height - 1) * rowsize];

    for (; low < high; low += rowsize, high -= rowsize) {
        memcpy(row, low, rowsize);
        memcpy(low, high, rowsize);
        memcpy(high, row, rowsize);
    }
    free(row);
}

// Dump the pixels rendered to the default framebuffer
void *jgrf_video_gl_get_pixels(int *rw, int *rh) {
    uint8_t *pixels = (uint8_t*)calloc(dimensions.rw * dimensions.rh,
        sizeof(uint32_t));

    // Read the pixels and flip them vertically
    glReadPixels(dimensions.xo, dimensions.yo, dimensions.rw, dimensions.rh,
        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    jgrf_video_gl_ssflip(pixels, dimensions.rw, dimensions.rh,
        sizeof(uint32_t));
    *rw = dimensions.rw;
    *rh = dimensions.rh;
    return pixels;
}

// Deinitialize OpenGL Video
void jgrf_video_gl_deinit(void) {
    for (int i = 0; i < 3; ++i)
        gltDeleteText(msgtext[i]);
    gltTerminate();

    if (framebuf) glDeleteFramebuffers(1, &framebuf);

    for (int i = 0; i < NUMPASSES; ++i) {
        if (shaderprog[i]) glDeleteProgram(shaderprog[i]);
        if (tex[i]) glDeleteTextures(1, &tex[i]);
        if (vao[i]) glDeleteVertexArrays(1, &vao[i]);
        if (vbo[i]) glDeleteBuffers(1, &vbo[i]);
    }

    if (!(gdata->hints & JG_HINT_VIDEO_INTERNAL))
        if (videobuf) free(videobuf);

    if (cursor) SDL_FreeCursor(cursor);
    if (glcontext) SDL_GL_DeleteContext(glcontext);
    SDL_DestroyWindow(window);
}

// Toggle between fullscreen and windowed
void jgrf_video_gl_fullscreen(void) {
    settings[VIDEO_FULLSCREEN].val ^= 1;
    SDL_SetWindowFullscreen(window,
        settings[VIDEO_FULLSCREEN].val ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

    // Fullscreen toggle does not trigger SDL_WINDOWEVENT_RESIZED
    jgrf_video_gl_resize();
}

// Refresh any video settings that may have changed
static void jgrf_video_gl_refresh(void) {
    float top = (float)vidinfo->y / vidinfo->hmax;
    float bottom = 1.0 + top -
        ((vidinfo->hmax - (float)vidinfo->h) / vidinfo->hmax);
    float left = (float)vidinfo->x / vidinfo->wmax;
    float right = 1.0 + left -
        ((vidinfo->wmax -(float)vidinfo->w) / vidinfo->wmax);

    // Check if any vertices have changed since last time
    if (vertices[9] != top || vertices[11] != bottom
        || vertices[8] != left || vertices[12] != right) {
        vertices[9] = vertices[13] = top;
        vertices[11] = vertices[15] = bottom;
        vertices[8] = vertices[10] = left;
        vertices[12] = vertices[14] = right;
    }
    else { // If nothing changed, return
        return;
    }

    // Bind the VAO/VBO for the offscreen texture, update with new vertex data
    glBindVertexArray(vao[0]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Resize the offscreen texture
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, pixfmt.format_internal,
        vidinfo->wmax, vidinfo->hmax, 0, pixfmt.format, pixfmt.type, NULL);

    // Set row length
    glUseProgram(shaderprog[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, vidinfo->p);

    // Resize the output texture
    glUseProgram(shaderprog[1]);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, pixfmt.format_internal,
        vidinfo->w, vidinfo->h, 0, pixfmt.format, pixfmt.type, NULL);

    // Update uniforms for post-processing
    glUniform4f(glGetUniformLocation(shaderprog[1], "sourceSize"),
        (float)vidinfo->w, (float)vidinfo->h,
        1.0/(float)vidinfo->w, 1.0/(float)vidinfo->h);
    glUniform4f(glGetUniformLocation(shaderprog[1], "targetSize"),
        dimensions.rw, dimensions.rh,
        1.0/dimensions.rw, 1.0/dimensions.rh);
}

// Render the scene
void jgrf_video_gl_render(int render) {
    jgrf_video_gl_refresh(); // Check for changes

    // Viewport set to size of the input pixel array
    glViewport(0, 0, vidinfo->w, vidinfo->h);

    // Make sure first pass shader program is active
    glUseProgram(shaderprog[0]);

    // Bind user-created framebuffer and draw scene onto it
    glBindFramebuffer(GL_FRAMEBUFFER, framebuf);
    glBindVertexArray(vao[0]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[0]);

    // Render if there is new pixel data, do Black Frame Insertion otherwise
    if (render) {
        glTexSubImage2D(GL_TEXTURE_2D,
                0,
                0, // xoffset
                0, // yoffset
                vidinfo->w + vidinfo->x, // width
                vidinfo->h + vidinfo->y, // height
                pixfmt.format, // format
                pixfmt.type, // type
            vidinfo->buf);
    }

    // Clear the screen to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw a rectangle from the 2 triangles
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Now deal with the actual image to be output
    // Viewport adjusted for output
    glViewport(dimensions.xo, dimensions.yo, dimensions.rw, dimensions.rh);

    // Make sure second pass shader program is active
    glUseProgram(shaderprog[1]);

    // Bind default framebuffer and draw contents of user framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(vao[1]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[1]);

    // Clear the screen to black again
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw framebuffer contents
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Draw any text that needs to be displayed
    if (textframes[0]) {
        gltBeginDraw();
        gltColor(0.831f, 0.333f, 0.0f, 1.0f); // Jolly Good Orange
        gltDrawText2DAligned(msgtext[0],
            0.0, floor(dimensions.rh),
            dimensions.dpiscale * settings[MISC_TEXTSCALE].val,
            GLT_LEFT, GLT_BOTTOM);
        gltEndDraw();
        --textframes[0];
    }

    // Text from the core
    if (textframes[1]) {
        gltBeginDraw();
        gltColor(0.831f, 0.333f, 0.0f, 1.0f); // Jolly Good Orange
        gltDrawText2DAligned(msgtext[1],
            floor(dimensions.rw), floor(dimensions.rh),
            dimensions.dpiscale * settings[MISC_TEXTSCALE].val,
            GLT_RIGHT, GLT_BOTTOM);
        gltEndDraw();
        --textframes[1];
    }

    // Menu and Input Config
    if (textframes[2]) {
        gltBeginDraw();
        gltColor(0.831f, 0.333f, 0.0f, 1.0f); // Jolly Good Orange
        gltDrawText2DAligned(msgtext[2],
            20, 20, dimensions.dpiscale * settings[MISC_TEXTSCALE].val,
            GLT_LEFT, GLT_TOP);
        gltEndDraw();
    }
}

// Render the scene
void jgrf_video_gl_render_compat(int render) {
    jgrf_video_gl_refresh(); // Check for changes

    // Viewport set to size of the output
    glViewport(dimensions.xo, dimensions.yo, dimensions.rw, dimensions.rh);

    // Clear the screen to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[0]);

    // Render if there is new pixel data, do Black Frame Insertion otherwise
    if (render) {
        glTexSubImage2D(GL_TEXTURE_2D,
                0,
                0, // xoffset
                0, // yoffset
                vidinfo->w + vidinfo->x, // width
                vidinfo->h + vidinfo->y, // height
                pixfmt.format, // format
                pixfmt.type, // type
            vidinfo->buf);
    }

    glBegin(GL_QUADS);
        glTexCoord2f(vertices[10], vertices[11]);
        glVertex2f(vertices[0], vertices[1]); // Bottom Left

        glTexCoord2f(vertices[8], vertices[9]);
        glVertex2f(vertices[2], vertices[3]); // Top Left

        glTexCoord2f(vertices[12], vertices[13]);
        glVertex2f(vertices[6], vertices[7]); // Top Right

        glTexCoord2f(vertices[14], vertices[15]);
        glVertex2f(vertices[4], vertices[5]); // Bottom Right
    glEnd();
}

// Handle viewport resizing
void jgrf_video_gl_resize(void) {
    SDL_GL_GetDrawableSize(window, &dimensions.ww, &dimensions.wh);
    dimensions.rw = dimensions.ww;
    dimensions.rh = dimensions.wh;

    // Check which dimension to optimize
    if (dimensions.rh * vidinfo->aspect > dimensions.rw)
        dimensions.rh = dimensions.rw / vidinfo->aspect + 0.5;
    else if (dimensions.rw / vidinfo->aspect > dimensions.rh)
        dimensions.rw = dimensions.rh * vidinfo->aspect + 0.5;

    // Store X and Y offsets
    dimensions.xo = (dimensions.ww - dimensions.rw) / 2;
    dimensions.yo = (dimensions.wh - dimensions.rh) / 2;

    // Update the targetSize uniform
    glUniform4f(glGetUniformLocation(shaderprog[1], "targetSize"),
        dimensions.rw, dimensions.rh,
        1.0/dimensions.rw, 1.0/dimensions.rh);

    // Update the text renderer's viewport size
    gltViewport(dimensions.rw, dimensions.rh);

    // Get current display mode
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &dm);

    // Set the base fps for use in the main loop
    jgrf_set_basefps(dm.refresh_rate);
}

// Retrieve scale parameters for pointing device input
void jgrf_video_gl_get_scale_params(float *xscale, float *yscale,
    float *xo, float *yo) {
    *xscale = dimensions.rw /
        (vidinfo->aspect * vidinfo->h) / dimensions.dpiscale;
    *yscale = dimensions.rh / vidinfo->h / dimensions.dpiscale;
    *xo = dimensions.xo / dimensions.dpiscale;
    *yo = dimensions.yo / dimensions.dpiscale;
}

// Retrieve video information
jg_videoinfo_t* jgrf_video_gl_get_info(void) {
    return vidinfo;
}

// Set the cursor
void jgrf_video_gl_set_cursor(int ctype) {
    cursor = SDL_CreateSystemCursor(ctype);
    SDL_ShowCursor(true);
    SDL_SetCursor(cursor);
}

// Pass a pointer to the video info held in  the core into the frontend
void jgrf_video_gl_set_info(jg_videoinfo_t *ptr) {
    vidinfo = ptr;

    // Also set the GL pixel format at this time
    switch (vidinfo->pixfmt) {
        case JG_PIXFMT_XRGB8888: {
            pixfmt.format = GL_BGRA;
            pixfmt.format_internal = GL_RGBA;
            pixfmt.type = GL_UNSIGNED_BYTE;
            pixfmt.size = sizeof(uint32_t);
            jgrf_log(JG_LOG_DBG, "Pixel format: GL_UNSIGNED_BYTE\n");
            break;
        }
        case JG_PIXFMT_XBGR8888: {
            pixfmt.format = GL_RGBA;
            pixfmt.format_internal = GL_RGBA;
            pixfmt.type = GL_UNSIGNED_BYTE;
            pixfmt.size = sizeof(uint32_t);
            jgrf_log(JG_LOG_DBG, "Pixel format: GL_UNSIGNED_BYTE\n");
            break;
        }
        case JG_PIXFMT_RGB5551: {
            pixfmt.format = GL_RGBA;
            pixfmt.format_internal = GL_RGBA;
            pixfmt.type = GL_UNSIGNED_SHORT_5_5_5_1;
            pixfmt.size = sizeof(uint16_t);
            jgrf_log(JG_LOG_DBG,
                "Pixel format: GL_UNSIGNED_SHORT_5_5_5_1\n");
            break;
        }
        case JG_PIXFMT_RGB565: {
            pixfmt.format = GL_RGB;
            pixfmt.format_internal = GL_RGB;
            pixfmt.type = GL_UNSIGNED_SHORT_5_6_5;
            pixfmt.size = sizeof(uint16_t);
            jgrf_log(JG_LOG_DBG,
                "Pixel format: GL_UNSIGNED_SHORT_5_6_5\n");
            break;
        }
        default: {
            jgrf_log(JG_LOG_ERR, "Unknown pixel format, exiting...\n");
            break;
        }
    }
}

// Load a shader source file into memory
static const GLchar* jgrf_video_gl_shader_load(const char *filename) {
    FILE *file = fopen(filename, "rb");

    if (!file)
        jgrf_log(JG_LOG_ERR, "Could not open shader file, exiting...\n");

    // Get the size of the shader source file
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    // Allocate memory to store the shader source including version string
    GLchar *src = (GLchar*)calloc(size + SIZE_GLSLVER, sizeof(GLchar));
    if (!src)
        jgrf_log(JG_LOG_ERR, "Could not allocate memory, exiting...\n");

    // Allocate memory for the shader source without version string
    GLchar *shader = (GLchar*)calloc(size + 1, sizeof(GLchar));

    // Write version string into the buffer for the full shader source
    snprintf(src, SIZE_GLSLVER, "%s", settings[VIDEO_API].val ?
        "#version 300 es\n" : "#version 330 core\n");

    if (!shader || !fread(shader, size, sizeof(GLchar), file)) {
        free(src);
        jgrf_log(JG_LOG_ERR, "Could not open shader file, exiting...\n");
    }

    // Close file handle after reading
    fclose(file);

    // Append shader source to version string
    src = strncat(src, shader, size + SIZE_GLSLVER);

    // Free the shader source without version string
    free(shader);

    return src;
}

// Create a shader program from a vertex shader and a fragment shader
static GLuint jgrf_video_gl_prog_create(const char *vs, const char *fs) {
    char vspath[192];
    char fspath[192];
    struct stat fbuf; // First find what path to use. Check local first.
    if (stat("shaders/default.vs", &fbuf) == 0) { // Found it locally
        snprintf(vspath, sizeof(vspath), "shaders/%s", vs);
        snprintf(fspath, sizeof(fspath), "shaders/%s", fs);
    }
#if defined(DATADIR)
    else { // Use the system-wide path
        snprintf(vspath, sizeof(vspath),
            "%s/jollygood/jgrf/shaders/%s", DATADIR, vs);
        snprintf(fspath, sizeof(fspath),
            "%s/jollygood/jgrf/shaders/%s", DATADIR, fs);
    }
#endif
    const GLchar *vsrc = jgrf_video_gl_shader_load(vspath);
    const GLchar *fsrc = jgrf_video_gl_shader_load(fspath);
    GLint err;

    // Create and compile the vertex shader
    GLuint vshader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vshader, 1, &vsrc, NULL);
    glCompileShader(vshader);

    // Test if the shader compiled
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &err);
    if (err == GL_FALSE) {
        char shaderlog[1024];
        glGetShaderInfoLog(vshader, 1024, NULL, shaderlog);
        jgrf_log(JG_LOG_WRN, "Vertex shader: %s", shaderlog);
    }

    // Create and compile the fragment shader
    GLuint fshader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fshader, 1, &fsrc, NULL);
    glCompileShader(fshader);

    // Test if the fragment shader compiled
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &err);
    if (err == GL_FALSE) {
        char shaderlog[1024];
        glGetShaderInfoLog(fshader, 1024, NULL, shaderlog);
        jgrf_log(JG_LOG_WRN, "Fragment shader: %s", shaderlog);
    }

    // Free the allocated memory for shader sources
    free((GLchar*)vsrc);
    free((GLchar*)fsrc);

    // Create the shader program
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vshader);
    glAttachShader(prog, fshader);
    glLinkProgram(prog);

    // Clean up fragment and vertex shaders
    glDeleteShader(vshader);
    glDeleteShader(fshader);

    // Return the successfully linked shader program
    return prog;
}

static void jgrf_video_gl_shader_setup(void) {
    for (int i = 0; i < NUMPASSES; ++i) {
        if (shaderprog[i]) glDeleteProgram(shaderprog[i]);
        texfilter[i] = GL_NEAREST;
    }

    // Create the shader program for the first pass (clipping)
    shaderprog[0] =
        jgrf_video_gl_prog_create("default.vs", "default.fs");

    GLint posattrib = glGetAttribLocation(shaderprog[0], "position");
    glEnableVertexAttribArray(posattrib);
    glVertexAttribPointer(posattrib, 2, GL_FLOAT, GL_FALSE, 0, 0);

    GLint texattrib = glGetAttribLocation(shaderprog[0], "vtxCoord");
    glEnableVertexAttribArray(texattrib);
    glVertexAttribPointer(texattrib, 2, GL_FLOAT, GL_FALSE,
        0, (void*)(8 * sizeof(GLfloat)));

    // Set up uniform for input texture
    glUseProgram(shaderprog[0]);
    glUniform1i(glGetUniformLocation(shaderprog[0], "source"), 0);

    switch (settings[VIDEO_SHADER].val) {
        default: case 0: { // Nearest Neighbour
            shaderprog[1] =
                jgrf_video_gl_prog_create("default.vs", "default.fs");
            break;
        }
        case 1: { // Linear
            shaderprog[1] =
                jgrf_video_gl_prog_create("default.vs", "default.fs");
            texfilter[1] = GL_LINEAR;
            break;
        }
        case 2: { // Sharp Bilinear
            shaderprog[1] =
                jgrf_video_gl_prog_create("default.vs", "sharp-bilinear.fs");
            texfilter[0] = GL_LINEAR;
            texfilter[1] = GL_LINEAR;
            break;
        }
        case 3: { // AANN
            shaderprog[1] =
                jgrf_video_gl_prog_create("default.vs", "aann.fs");
            break;
        }
        case 4: { // CRT-Bespoke
            shaderprog[1] =
                jgrf_video_gl_prog_create("default.vs", "crt-bespoke.fs");
            break;
        }
        case 5: { // CRTea
            shaderprog[1] =
                jgrf_video_gl_prog_create("default.vs", "crtea.fs");
            break;
        }
        case 6: { // LCD
            shaderprog[1] =
                jgrf_video_gl_prog_create("default.vs", "lcd.fs");
            break;
        }
    }

    // Set texture parameters for input texture
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texfilter[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texfilter[0]);

    // Bind vertex array and specify layout for second pass
    glBindVertexArray(vao[1]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);

    GLint posattrib_out = glGetAttribLocation(shaderprog[1], "position");
    glEnableVertexAttribArray(posattrib_out);
    glVertexAttribPointer(posattrib_out, 2, GL_FLOAT, GL_FALSE, 0, 0);

    GLint texattrib_out = glGetAttribLocation(shaderprog[1], "vtxCoord");
    glEnableVertexAttribArray(texattrib_out);
    glVertexAttribPointer(texattrib_out, 2, GL_FLOAT, GL_FALSE,
        0, (void*)(8 * sizeof(GLfloat)));

    // Set up uniforms for post-processing texture
    glUseProgram(shaderprog[1]);

    glUniform1i(glGetUniformLocation(shaderprog[1], "source"), 0);
    glUniform4f(glGetUniformLocation(shaderprog[1], "sourceSize"),
        (float)vidinfo->w, (float)vidinfo->h,
        1.0/(float)vidinfo->w, 1.0/(float)vidinfo->h);
    glUniform4f(glGetUniformLocation(shaderprog[1], "targetSize"),
        dimensions.rw, dimensions.rh,
        1.0/dimensions.rw, 1.0/dimensions.rh);

        // Settings for CRTea
    int masktype = 0, maskstr = 0, scanstr = 0, sharpness = 0,
        curve = settings[VIDEO_CRTEA_CURVE].val,
        corner = settings[VIDEO_CRTEA_CORNER].val,
        tcurve = settings[VIDEO_CRTEA_TCURVE].val;

    switch (settings[VIDEO_CRTEA_MODE].val) {
        default: case 0: { // Scanlines
            masktype = 0; maskstr = 0; scanstr = 10; sharpness = 10;
            break;
        }
        case 1: { // Aperture Grille Lite
            masktype = 1; maskstr = 5; scanstr = 6; sharpness = 7;
            break;
        }
        case 2: { // Aperture Grille
            masktype = 2; maskstr = 5; scanstr = 6; sharpness = 7;
            break;
        }
        case 3: { // Shadow Mask
            masktype = 3; maskstr = 5; scanstr = 2; sharpness = 4;
            break;
        }
        case 4: { // Custom
            masktype = settings[VIDEO_CRTEA_MASKTYPE].val;
            maskstr = settings[VIDEO_CRTEA_MASKSTR].val;
            scanstr = settings[VIDEO_CRTEA_SCANSTR].val;
            sharpness = settings[VIDEO_CRTEA_SHARPNESS].val;
            break;
        }
    }
    glUniform1i(glGetUniformLocation(shaderprog[1], "masktype"),
        masktype);
    glUniform1f(glGetUniformLocation(shaderprog[1], "maskstr"),
        maskstr / 10.0);
    glUniform1f(glGetUniformLocation(shaderprog[1], "scanstr"),
        scanstr / 10.0);
    glUniform1f(glGetUniformLocation(shaderprog[1], "sharpness"),
        (float)sharpness);
    glUniform1f(glGetUniformLocation(shaderprog[1], "curve"),
        curve / 100.0);
    glUniform1f(glGetUniformLocation(shaderprog[1], "corner"),
        corner ? (float)corner : -3.0);
    glUniform1f(glGetUniformLocation(shaderprog[1], "tcurve"),
        tcurve / 10.0);

    // Set parameters for output texture
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texfilter[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texfilter[1]);
}

// Set up OpenGL
void jgrf_video_gl_setup(void) {
    // Create Vertex Array Objects
    glGenVertexArrays(1, &vao[0]);
    glGenVertexArrays(1, &vao[1]);

    // Create Vertex Buffer Objects
    glGenBuffers(1, &vbo[0]);
    glGenBuffers(1, &vbo[1]);

    // Bind buffers for vertex buffer objects
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices),
        vertices, GL_STATIC_DRAW);

    GLfloat vertices_out[] = {
        -1.0, -1.0, // Vertex 1 (X, Y) Left Bottom
        -1.0, 1.0,  // Vertex 2 (X, Y) Left Top
        1.0, -1.0,  // Vertex 3 (X, Y) Right Bottom
        1.0, 1.0,   // Vertex 4 (X, Y) Right Top
        0.0, 1.0,   // Texture 1 (X, Y) Left Bottom
        0.0, 0.0,   // Texture 2 (X, Y) Left Top
        1.0, 1.0,   // Texture 3 (X, Y) Right Bottom
        1.0, 0.0,   // Texture 4 (X, Y) Right Top
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices_out),
        vertices_out, GL_STATIC_DRAW);

    // Bind vertex array and specify layout for first pass
    glBindVertexArray(vao[0]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);

    // Generate texture for raw game output
    glGenTextures(1, &tex[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[0]);

    // The full sized source image before any clipping
    glTexImage2D(GL_TEXTURE_2D, 0, pixfmt.format_internal,
        vidinfo->wmax, vidinfo->hmax, 0, pixfmt.format, pixfmt.type,
        vidinfo->buf);

    // Create framebuffer
    glGenFramebuffers(1, &framebuf);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuf);

    // Create texture to hold colour buffer
    glGenTextures(1, &tex[1]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[1]);

    // The framebuffer texture that is being rendered to offscreen, after clip
    glTexImage2D(GL_TEXTURE_2D, 0, pixfmt.format_internal,
        vidinfo->w, vidinfo->h, 0, pixfmt.format, pixfmt.type, NULL);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        tex[1], 0);

    jgrf_video_gl_shader_setup();

    jgrf_video_gl_resize();
    jgrf_video_gl_refresh();
}

// Set up OpenGL - Compatibility Profile
void jgrf_video_gl_setup_compat(void) {
    switch (settings[VIDEO_SHADER].val) {
        case 0: { // Nearest Neighbour
            texfilter[0] = GL_NEAREST;
            break;
        }
        case 1: { // Linear
            texfilter[0] = GL_LINEAR;
            break;
        }
        default: {
            texfilter[0] = GL_LINEAR;
            break;
        }
    }

    // Generate texture for raw game output
    glEnable(GL_TEXTURE_2D);
    glGenTextures(1, &tex[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[0]);

    // The full sized source image before any clipping
    glTexImage2D(GL_TEXTURE_2D, 0, pixfmt.format_internal,
        vidinfo->wmax, vidinfo->hmax, 0, pixfmt.format, pixfmt.type,
        vidinfo->buf);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texfilter[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texfilter[0]);

    jgrf_video_gl_resize();
    jgrf_video_gl_refresh();

    jgrf_log(JG_LOG_WRN,
        "OpenGL Compatibility Profile supports basic functionality only - no "
        "post-processing shaders, menu, or input configuration.\n"
    );
}

// Swap Buffers
void jgrf_video_gl_swapbuffers(void) {
    SDL_GL_SwapWindow(window);
}

// Set a text message to be output for a number of frames
void jgrf_video_gl_text(int index, int frames, const char *msg) {
    gltSetText(msgtext[index], msg);
    textframes[index] = frames;
}

void jgrf_video_gl_rehash(void) {
    jgrf_video_gl_shader_setup();
    SDL_SetWindowFullscreen(window, settings[VIDEO_FULLSCREEN].val ?
        SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    jgrf_video_gl_resize();
}
