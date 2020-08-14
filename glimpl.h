// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A simple OpenXR example
 * @author Christoph Haag <christoph.haag@collabora.com>
 */

#ifndef GLIMPL
#define GLIMPL

#define GL_GLEXT_PROTOTYPES
#define GL3_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "xrmath.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <GL/glx.h>

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include "openxr_headers/openxr.h"
#include "openxr_headers/openxr_platform.h"

bool
init_sdl_window(Display** xDisplay,
                uint32_t* visualid,
                GLXFBConfig* glxFBConfig,
                GLXDrawable* glxDrawable,
                GLXContext* glxContext,
                int w,
                int h);
#else

#include "openxr_headers/openxr.h"
#include "openxr_headers/openxr_platform.h"

#error This example only supports Linux/XLIB
#endif

int
init_gl();

void
render_quad(int w,
            int h,
            int64_t swapchain_format,
            XrSwapchainImageOpenGLKHR image,
            XrTime predictedDisplayTime);

void
render_frame(int w,
             int h,
             XrMatrix4x4f projectionmatrix,
             XrMatrix4x4f viewmatrix,
             XrSpaceLocation* hand_locations,
             bool* hand_locations_valid,
             XrHandJointLocationsEXT* joint_locations,
             GLuint framebuffer,
             GLuint depthbuffer,
             XrSwapchainImageOpenGLKHR image,
             int view_index,
             XrTime predictedDisplayTime);

void
cleanup_gl();

#endif
