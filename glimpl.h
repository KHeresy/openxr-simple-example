// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A simple OpenXR example
 * @author Christoph Haag <christoph.haag@collabora.com>
 */

#ifndef GLIMPL
#define GLIMPL

#include <GL/gl.h>
#include "xrmath.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <GL/glx.h>

bool
initGLX(Display** xDisplay,
        uint32_t* visualid,
        GLXFBConfig* glxFBConfig,
        GLXDrawable* glxDrawable,
        GLXContext* glxContext,
        int w,
        int h);
#else
#error This example only supports Linux
#endif

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include "openxr_headers/openxr.h"
#include "openxr_headers/openxr_platform.h"

int
initGlImpl();

void
renderFrame(int w,
            int h,
            XrMatrix4x4f projectionmatrix,
            XrMatrix4x4f viewmatrix,
            XrSpaceLocation* leftHand,
            XrSpaceLocation* rightHand,
            GLuint framebuffer,
            GLuint depthbuffer,
            XrSwapchainImageOpenGLKHR image,
            int viewIndex,
            XrTime predictedDisplayTime);

void
genFramebuffers(int count, GLuint* framebuffers);

extern GLuint shaderProgramID;
extern GLuint VAOs[1];

#endif
