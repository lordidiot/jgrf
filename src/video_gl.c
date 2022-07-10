/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>

#include <epoxy/gl.h>
#include <SDL.h>
#include <jg/jg.h>

#define GLT_IMPLEMENTATION
#define GLT_MANUAL_VIEWPORT
#include "gltext.h"

#include "jgrf.h"
#include "video.h"
#include "video_gl.h"
#include "settings.h"

extern int bmark;

static jgrf_gdata_t *gdata = NULL;
static settings_t *settings = NULL;

// SDL Window management
static SDL_Window *window;
static SDL_Cursor *cursor;
static SDL_GLContext glcontext;

// Pointer to the video buffer when allocated by the frontend
static void *videobuf = NULL;

// Pointer to the core's video information
static jg_videoinfo_t *vidinfo = NULL;

// OpenGL related variables
static GLuint vao;
static GLuint vao_out;
static GLuint vbo;
static GLuint vbo_out;
static GLuint shaderProgram; // First pass shader program
static GLuint shaderProgram_out; // Post-processing shader program
static GLuint frameBuffer; // Framebuffer for rendering offscreen
static GLuint texGame; // Game texture, clipped in first pass
static GLuint texOutput; // Output texture used for post-processing
static GLint texfilter_in = GL_NEAREST;
static GLint texfilter_out = GL_NEAREST;

static GLTtext *msgtext[3];
static int textframes[3];

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

static GLfloat vertices_out[] = {
    -1.0, -1.0, // Vertex 1 (X, Y) Left Bottom
    -1.0, 1.0,  // Vertex 2 (X, Y) Left Top
    1.0, -1.0,  // Vertex 3 (X, Y) Right Bottom
    1.0, 1.0,   // Vertex 4 (X, Y) Right Top

    0.0, 1.0,   // Texture 1 (X, Y) Left Bottom
    0.0, 0.0,   // Texture 2 (X, Y) Left Top
    1.0, 1.0,   // Texture 3 (X, Y) Right Bottom
    1.0, 0.0,   // Texture 4 (X, Y) Right Top
};

// Pixel Format
static struct pixfmt {
    GLuint format;
    GLuint type;
    size_t size;
} pixfmt = { GL_BGRA, GL_UNSIGNED_BYTE, sizeof(uint32_t) };

// Dimensions
static struct dimensions {
    int ww; int wh;
    float rw; float rh;
    float xo; float yo;
    float dpiscale;
} dimensions = {0};

// Create the SDL OpenGL Window
void jgrf_video_gl_create(void) {
    // Grab settings pointer
    settings = jgrf_get_settings();

    // Set the GL version
    switch (settings->video_api.val) {
        default: case 0: { // OpenGL - Core Profile
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                SDL_GL_CONTEXT_PROFILE_CORE);
            break;
        }
        case 1: {
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
    char title[256];
    gdata = jgrf_gdata_ptr();
    snprintf(title, sizeof(title), "%s", gdata->gamename);

    // Set the window dimensions
    dimensions.ww =
        (vidinfo->aspect * vidinfo->h * settings->video_scale.val) + 0.5;
    dimensions.wh = (vidinfo->h * settings->video_scale.val) + 0.5;
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
    int xdpi;
    SDL_GL_GetDrawableSize(window, &xdpi, NULL);
    dimensions.dpiscale = (float)xdpi/dimensions.ww;

    jgrf_video_icon_load(window);

    // Set the GL context
    glcontext = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glcontext);
    SDL_GL_SetSwapInterval(!bmark); // Vsync off in Benchmark mode

    if (!glcontext)
        jgrf_log(JG_LOG_WRN, "Failed to create glcontext: %s\n",
            SDL_GetError());

    // Initialize glText after setting GL context
    if (!gltInit())
        jgrf_log(JG_LOG_WRN, "Failed to initialize glText\n");

    for (int i = 0; i < 3; ++i)
        msgtext[i] = gltCreateText();

    // Do post window creation OpenGL setup
    if (settings->video_api.val)
        jgrf_video_gl_setup_compat();
    else
        jgrf_video_gl_setup();

    jgrf_log(JG_LOG_INF, "Video: OpenGL %s\n", glGetString(GL_VERSION));

    SDL_ShowCursor(false);

    // Set fullscreen if required
    if (settings->video_fullscreen.val)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
}

// Initialize video buffer
int jgrf_video_gl_init(void) {
    gdata = jgrf_gdata_ptr();
    if (!(gdata->hints & JG_HINT_VIDEO_INTERNAL)) {
        // Address of allocated memory owned by frontend but passed to core
        videobuf = (void*)calloc(vidinfo->wmax * vidinfo->hmax, pixfmt.size);
        vidinfo->buf = videobuf;
    }
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
    uint8_t *pixels;
    pixels = (uint8_t*)calloc(dimensions.rw * dimensions.rh,
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

    if (texGame) glDeleteTextures(1, &texGame);
    if (texOutput) glDeleteTextures(1, &texOutput);

    if (frameBuffer) glDeleteFramebuffers(1, &frameBuffer);

    if (shaderProgram) glDeleteProgram(shaderProgram);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);

    if (shaderProgram_out) glDeleteProgram(shaderProgram_out);
    if (vao_out) glDeleteVertexArrays(1, &vao_out);
    if (vbo_out) glDeleteBuffers(1, &vbo_out);

    if (!(gdata->hints & JG_HINT_VIDEO_INTERNAL))
        if (videobuf) free(videobuf);

    if (cursor) SDL_FreeCursor(cursor);
    if (glcontext) SDL_GL_DeleteContext(glcontext);
    SDL_DestroyWindow(window);
}

// Make the window fullscreen
void jgrf_video_gl_fullscreen(void) {
    SDL_SetWindowFullscreen(window,
        SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP ?
        0 : SDL_WINDOW_FULLSCREEN_DESKTOP);

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
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Resize the offscreen texture
    glBindTexture(GL_TEXTURE_2D, texGame);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vidinfo->wmax, vidinfo->hmax, 0,
        pixfmt.format, pixfmt.type, NULL);

    // Set row length
    glUseProgram(shaderProgram);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, vidinfo->p);

    // Resize the output texture
    glUseProgram(shaderProgram_out);
    glBindTexture(GL_TEXTURE_2D, texOutput);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vidinfo->w, vidinfo->h, 0,
        pixfmt.format, pixfmt.type, NULL);

    // Update uniforms for post-processing
    glUniform4f(glGetUniformLocation(shaderProgram_out, "sourceSize"),
        (float)vidinfo->w, (float)vidinfo->h,
        1.0/(float)vidinfo->w, 1.0/(float)vidinfo->h);
    glUniform4f(glGetUniformLocation(shaderProgram_out, "targetSize"),
        dimensions.rw, dimensions.rh,
        1.0/dimensions.rw, 1.0/dimensions.rh);
}

// Render the scene
void jgrf_video_gl_render(int render) {
    jgrf_video_gl_refresh(); // Check for changes

    // Viewport set to size of the input pixel array
    glViewport(0, 0, vidinfo->w, vidinfo->h);

    // Make sure first pass shader program is active
    glUseProgram(shaderProgram);

    // Bind user-created framebuffer and draw scene onto it
    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
    glBindVertexArray(vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texGame);

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
    glUseProgram(shaderProgram_out);

    // Bind default framebuffer and draw contents of user framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(vao_out);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texOutput);

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
            dimensions.dpiscale * 2.0f, GLT_LEFT, GLT_BOTTOM);
        gltEndDraw();
        --textframes[0];
    }

    // Text from the core
    if (textframes[1]) {
        gltBeginDraw();
        gltColor(0.831f, 0.333f, 0.0f, 1.0f); // Jolly Good Orange
        gltDrawText2DAligned(msgtext[1],
            floor(dimensions.rw), floor(dimensions.rh),
            dimensions.dpiscale * 2.0f, GLT_RIGHT, GLT_BOTTOM);
        gltEndDraw();
        --textframes[1];
    }

    // Input Config
    if (textframes[2]) {
        gltBeginDraw();
        gltColor(0.831f, 0.333f, 0.0f, 1.0f); // Jolly Good Orange
        gltDrawText2DAligned(msgtext[2],
            dimensions.rw / 2, dimensions.rh / 2,
            dimensions.dpiscale * 2.0f, GLT_CENTER, GLT_CENTER);
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
    glBindTexture(GL_TEXTURE_2D, texGame);

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
    glUniform4f(glGetUniformLocation(shaderProgram_out, "targetSize"),
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
        case JG_PIXFMT_XRGB8888:
            pixfmt.format = GL_BGRA;
            pixfmt.type = GL_UNSIGNED_BYTE;
            pixfmt.size = sizeof(uint32_t);
            jgrf_log(JG_LOG_DBG, "Pixel format: GL_UNSIGNED_BYTE\n");
            break;
        case JG_PIXFMT_XBGR8888:
            pixfmt.format = GL_RGBA;
            pixfmt.type = GL_UNSIGNED_BYTE;
            pixfmt.size = sizeof(uint32_t);
            jgrf_log(JG_LOG_DBG, "Pixel format: GL_UNSIGNED_BYTE\n");
            break;
        case JG_PIXFMT_RGB1555:
            pixfmt.format = GL_BGRA;
            pixfmt.type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
            pixfmt.size = sizeof(uint16_t);
            jgrf_log(JG_LOG_DBG,
                "Pixel format: GL_UNSIGNED_SHORT_1_5_5_5_REV\n");
            break;
        case JG_PIXFMT_RGB565:
            pixfmt.format = GL_RGB;
            pixfmt.type = GL_UNSIGNED_SHORT_5_6_5;
            pixfmt.size = sizeof(uint16_t);
            jgrf_log(JG_LOG_DBG,
                "Pixel format: GL_UNSIGNED_SHORT_5_6_5\n");
            break;
        default:
            jgrf_log(JG_LOG_ERR, "Unknown pixel format, exiting...\n");
            break;
    }
}

// Load a shader source file into memory
static const GLchar* jgrf_video_gl_shader_load(const char *filename) {
    FILE *file = fopen(filename, "rb");

    if (!file)
        jgrf_log(JG_LOG_ERR, "Could not open shader file, exiting...\n");

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    GLchar *src = (GLchar*)calloc(size + 1, sizeof(GLchar));
    if (!src || !fread(src, size, sizeof(GLchar), file))
        jgrf_log(JG_LOG_ERR, "Could not open shader file, exiting...\n");

    fclose(file);
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
    const GLchar *vertexSource = jgrf_video_gl_shader_load(vspath);
    const GLchar *fragmentSource = jgrf_video_gl_shader_load(fspath);
    GLint err;

    // Create and compile the vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, NULL);
    glCompileShader(vertexShader);

    // Test if the shader compiled
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &err);
    if (err == GL_FALSE) {
        char shaderlog[1024];
        glGetShaderInfoLog(vertexShader, 1024, NULL, shaderlog);
        jgrf_log(JG_LOG_WRN, "Vertex shader: %s", shaderlog);
    }

    // Create and compile the fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
    glCompileShader(fragmentShader);

    // Test if the fragment shader compiled
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &err);
    if (err == GL_FALSE) {
        char shaderlog[1024];
        glGetShaderInfoLog(fragmentShader, 1024, NULL, shaderlog);
        jgrf_log(JG_LOG_WRN, "Fragment shader: %s", shaderlog);
    }

    // Free the allocated memory for shader sources
    free((GLchar*)vertexSource);
    free((GLchar*)fragmentSource);

    // Create the shader program
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vertexShader);
    glAttachShader(prog, fragmentShader);
    glLinkProgram(prog);

    // Clean up fragment and vertex shaders
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Return the successfully linked shader program
    return prog;
}

static void jgrf_video_gl_shader_setup(void) {
    if (shaderProgram)
        glDeleteProgram(shaderProgram);
    if (shaderProgram_out)
        glDeleteProgram(shaderProgram_out);

    texfilter_in = GL_NEAREST;
    texfilter_out = GL_NEAREST;

    // Create the shader program for the first pass (clipping)
    shaderProgram =
        jgrf_video_gl_prog_create("default.vs", "default.fs");

    GLint posAttrib = glGetAttribLocation(shaderProgram, "position");
    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);

    GLint texAttrib = glGetAttribLocation(shaderProgram, "texCoord");
    glEnableVertexAttribArray(texAttrib);
    glVertexAttribPointer(texAttrib, 2, GL_FLOAT, GL_FALSE,
        0, (void*)(8 * sizeof(GLfloat)));

    // Set up uniform for input texture
    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "source"), 0);

    switch (settings->video_shader.val) {
        default: case 0: // Nearest Neighbour
            shaderProgram_out =
                jgrf_video_gl_prog_create("default.vs", "default.fs");
            break;
        case 1: // Linear
            shaderProgram_out =
                jgrf_video_gl_prog_create("default.vs", "default.fs");
            texfilter_out = GL_LINEAR;
            break;
        case 2: // Sharp Bilinear
            shaderProgram_out =
                jgrf_video_gl_prog_create("default.vs", "sharp-bilinear.fs");
            texfilter_in = GL_LINEAR;
            texfilter_out = GL_LINEAR;
            break;
        case 3: // AANN
            shaderProgram_out =
                jgrf_video_gl_prog_create("default.vs", "aann.fs");
            break;
        case 4: // CRT-Bespoke
            shaderProgram_out =
                jgrf_video_gl_prog_create("default.vs", "crt-bespoke.fs");
            break;
        case 5: // CRTea
            shaderProgram_out =
                jgrf_video_gl_prog_create("default.vs", "crtea.fs");
            break;
        case 6: // LCD
            shaderProgram_out =
                jgrf_video_gl_prog_create("default.vs", "lcd.fs");
            break;
    }

    // Set texture parameters for input texture
    glBindTexture(GL_TEXTURE_2D, texGame);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texfilter_in);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texfilter_in);

    // Bind vertex array and specify layout for second pass
    glBindVertexArray(vao_out);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_out);

    GLint posAttrib_out = glGetAttribLocation(shaderProgram_out, "position");
    glEnableVertexAttribArray(posAttrib_out);
    glVertexAttribPointer(posAttrib_out, 2, GL_FLOAT, GL_FALSE, 0, 0);

    GLint texAttrib_out = glGetAttribLocation(shaderProgram_out, "texCoord");
    glEnableVertexAttribArray(texAttrib_out);
    glVertexAttribPointer(texAttrib_out, 2, GL_FLOAT, GL_FALSE,
        0, (void*)(8 * sizeof(GLfloat)));

    // Set up uniforms for post-processing texture
    glUseProgram(shaderProgram_out);

    glUniform1i(glGetUniformLocation(shaderProgram_out, "source"), 0);
    glUniform4f(glGetUniformLocation(shaderProgram_out, "sourceSize"),
        (float)vidinfo->w, (float)vidinfo->h,
        1.0/(float)vidinfo->w, 1.0/(float)vidinfo->h);
    glUniform4f(glGetUniformLocation(shaderProgram_out, "targetSize"),
        dimensions.rw, dimensions.rh,
        1.0/dimensions.rw, 1.0/dimensions.rh);

        // Settings for CRTea
    int masktype = 0, maskstr = 0, scanstr = 0, sharpness = 0,
        curve = settings->video_crtea_curve.val,
        corner = settings->video_crtea_corner.val,
        tcurve = settings->video_crtea_tcurve.val;

    switch (settings->video_crtea_mode.val) {
        default: case 0: // Scanlines
            masktype = 0; maskstr = 0; scanstr = 10; sharpness = 10;
            break;
        case 1: // Aperture Grille Lite
            masktype = 1; maskstr = 5; scanstr = 6; sharpness = 7;
            break;
        case 2: // Aperture Grille
            masktype = 2; maskstr = 5; scanstr = 6; sharpness = 7;
            break;
        case 3: // Shadow Mask
            masktype = 3; maskstr = 5; scanstr = 2; sharpness = 4;
            break;
        case 4: // Custom
            masktype = settings->video_crtea_masktype.val;
            maskstr = settings->video_crtea_maskstr.val;
            scanstr = settings->video_crtea_scanstr.val;
            sharpness = settings->video_crtea_sharpness.val;
            break;
    }
    glUniform1i(glGetUniformLocation(shaderProgram_out, "masktype"),
        masktype);
    glUniform1f(glGetUniformLocation(shaderProgram_out, "maskstr"),
        maskstr / 10.0);
    glUniform1f(glGetUniformLocation(shaderProgram_out, "scanstr"),
        scanstr / 10.0);
    glUniform1f(glGetUniformLocation(shaderProgram_out, "sharpness"),
        (float)sharpness);
    glUniform1f(glGetUniformLocation(shaderProgram_out, "curve"),
        curve / 100.0);
    glUniform1f(glGetUniformLocation(shaderProgram_out, "corner"),
        corner ? (float)corner : -3.0);
    glUniform1f(glGetUniformLocation(shaderProgram_out, "tcurve"),
        tcurve / 10.0);

    // Set parameters for output texture
    glBindTexture(GL_TEXTURE_2D, texOutput);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texfilter_out);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texfilter_out);
}

// Set up OpenGL
void jgrf_video_gl_setup(void) {
    // Create Vertex Array Objects
    glGenVertexArrays(1, &vao);
    glGenVertexArrays(1, &vao_out);

    // Create Vertex Buffer Objects
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &vbo_out);

    // Bind buffers for vertex buffer objects
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices),
        vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_out);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices_out),
        vertices_out, GL_STATIC_DRAW);

    // Bind vertex array and specify layout for first pass
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Generate texture for raw game output
    glGenTextures(1, &texGame);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texGame);

    // The full sized source image before any clipping
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vidinfo->wmax, vidinfo->hmax,
        0, pixfmt.format, pixfmt.type, vidinfo->buf);

    // Create framebuffer
    glGenFramebuffers(1, &frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

    // Create texture to hold colour buffer
    glGenTextures(1, &texOutput);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texOutput);

    // The framebuffer texture that is being rendered to offscreen, after clip
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vidinfo->w, vidinfo->h, 0,
        pixfmt.format, pixfmt.type, NULL);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        texOutput, 0);

    jgrf_video_gl_shader_setup();

    jgrf_video_gl_resize();
    jgrf_video_gl_refresh();
}

// Set up OpenGL - Compatibility Profile
void jgrf_video_gl_setup_compat(void) {
    switch (settings->video_shader.val) {
        case 0: // Nearest Neighbour
            texfilter_in = GL_NEAREST;
            break;
        case 1: // Linear
            texfilter_in = GL_LINEAR;
            break;
        default:
            jgrf_log(JG_LOG_WRN, "Post-processing shaders are not available in "
                "OpenGL Compatibility Profile, defaulting to Linear\n");
    }

    // Generate texture for raw game output
    glEnable(GL_TEXTURE_2D);
    glGenTextures(1, &texGame);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texGame);

    // The full sized source image before any clipping
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vidinfo->wmax, vidinfo->hmax,
        0, pixfmt.format, pixfmt.type, vidinfo->buf);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texfilter_in);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texfilter_in);

    jgrf_video_gl_resize();
    jgrf_video_gl_refresh();
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
