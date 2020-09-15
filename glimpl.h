// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A simple OpenXR example
 * @author Christoph Haag <christoph.haag@collabora.com>
 */

#ifndef GLIMPL
#define GLIMPL

#include <Windows.h>

#define NO_SDL_GLEXT
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>

#include "xrmath.h"

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

bool
init_sdl_window(HDC& xDisplay, HGLRC& glxContext,
                int w,
                int h);

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
