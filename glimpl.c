// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A simple OpenXR example
 * @author Christoph Haag <christoph.haag@collabora.com>
 */

#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define degreesToRadians(angleDegrees) ((angleDegrees)*M_PI / 180.0)
#define radiansToDegrees(angleRadians) ((angleRadians)*180.0 / M_PI)

#define MATH_3D_IMPLEMENTATION
#include "math_3d.h"

#define GL_GLEXT_PROTOTYPES
#define GL3_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include "glimpl.h"

GLuint shaderProgramID = 0;
GLuint VAOs[1] = {0};

static const char* vertexshader =
    "#version 330 core\n"
    "#extension GL_ARB_explicit_uniform_location : require\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 2) uniform mat4 model;\n"
    "layout(location = 3) uniform mat4 view;\n"
    "layout(location = 4) uniform mat4 proj;\n"
    "layout(location = 5) in vec2 aColor;\n"
    "out vec2 vertexColor;\n"
    "void main() {\n"
    "	gl_Position = proj * view * model * vec4(aPos.x, aPos.y, aPos.z, "
    "1.0);\n"
    "	vertexColor = aColor;\n"
    "}\n";

static const char* fragmentshader =
    "#version 330 core\n"
    "#extension GL_ARB_explicit_uniform_location : require\n"
    "layout(location = 0) out vec4 FragColor;\n"
    "layout(location = 1) uniform vec3 uniformColor;\n"
    "in vec2 vertexColor;\n"
    "void main() {\n"
    "	FragColor = (uniformColor.x < 0.01 && uniformColor.y < 0.01 && "
    "uniformColor.z < 0.01) ? vec4(vertexColor, 1.0, 1.0) : vec4(uniformColor, "
    "1.0);\n"
    "}\n";

static SDL_Window* mainwindow;
static SDL_GLContext maincontext;

// don't need a gl loader for just one function, just load it ourselves'
PFNGLBLITNAMEDFRAMEBUFFERPROC _glBlitNamedFramebuffer;

#ifdef __linux__
bool
initGLX(Display** xDisplay,
        uint32_t* visualid,
        GLXFBConfig* glxFBConfig,
        GLXDrawable* glxDrawable,
        GLXContext* glxContext,
        int w,
        int h)
{

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("Unable to initialize SDL");
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

	/* Create our window centered at half the VR resolution */
	mainwindow = SDL_CreateWindow("OpenXR Example", SDL_WINDOWPOS_CENTERED,
	                              SDL_WINDOWPOS_CENTERED, w / 2, h / 2,
	                              SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!mainwindow) {
		printf("Unable to create window");
		return false;
	}

	maincontext = SDL_GL_CreateContext(mainwindow);

	SDL_GL_SetSwapInterval(0);

	_glBlitNamedFramebuffer = (PFNGLBLITNAMEDFRAMEBUFFERPROC)glXGetProcAddressARB(
	    (GLubyte*)"glBlitNamedFramebuffer");

	// HACK? OpenXR wants us to report these values, so "work around" SDL a
	// bit and get the underlying glx stuff. Does this still work when e.g.
	// SDL switches to xcb?
	*xDisplay = XOpenDisplay(NULL);
	*glxContext = glXGetCurrentContext();
	*glxDrawable = glXGetCurrentDrawable();

	return true;
}
#endif

int
initGlImpl()
{
	GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
	const GLchar* vertexShaderSource[1];
	vertexShaderSource[0] = vertexshader;
	// printf("Vertex Shader:\n%s\n", vertexShaderSource);
	glShaderSource(vertexShaderId, 1, vertexShaderSource, NULL);
	glCompileShader(vertexShaderId);
	int vertexcompilesuccess;
	glGetShaderiv(vertexShaderId, GL_COMPILE_STATUS, &vertexcompilesuccess);
	if (!vertexcompilesuccess) {
		char infoLog[512];
		glGetShaderInfoLog(vertexShaderId, 512, NULL, infoLog);
		printf("Vertex Shader failed to compile: %s\n", infoLog);
		return 1;
	} else {
		printf("Successfully compiled vertex shader!\n");
	}

	GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
	const GLchar* fragmentShaderSource[1];
	fragmentShaderSource[0] = fragmentshader;
	glShaderSource(fragmentShaderId, 1, fragmentShaderSource, NULL);
	glCompileShader(fragmentShaderId);
	int fragmentcompilesuccess;
	glGetShaderiv(fragmentShaderId, GL_COMPILE_STATUS, &fragmentcompilesuccess);
	if (!fragmentcompilesuccess) {
		char infoLog[512];
		glGetShaderInfoLog(fragmentShaderId, 512, NULL, infoLog);
		printf("Fragment Shader failed to compile: %s\n", infoLog);
		return 1;
	} else {
		printf("Successfully compiled fragment shader!\n");
	}

	shaderProgramID = glCreateProgram();
	glAttachShader(shaderProgramID, vertexShaderId);
	glAttachShader(shaderProgramID, fragmentShaderId);
	glLinkProgram(shaderProgramID);
	GLint shaderprogramsuccess;
	glGetProgramiv(shaderProgramID, GL_LINK_STATUS, &shaderprogramsuccess);
	if (!shaderprogramsuccess) {
		char infoLog[512];
		glGetProgramInfoLog(shaderProgramID, 512, NULL, infoLog);
		printf("Shader Program failed to link: %s\n", infoLog);
		return 1;
	} else {
		printf("Successfully linked shader program!\n");
	}

	glDeleteShader(vertexShaderId);
	glDeleteShader(fragmentShaderId);

	float vertices[] = {
	    -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 0.0f,
	    0.5f,  0.5f,  -0.5f, 1.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
	    -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,

	    -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  1.0f, 0.0f,
	    0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
	    -0.5f, 0.5f,  0.5f,  0.0f, 1.0f, -0.5f, -0.5f, 0.5f,  0.0f, 0.0f,

	    -0.5f, 0.5f,  0.5f,  1.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 1.0f, 1.0f,
	    -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
	    -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, -0.5f, 0.5f,  0.5f,  1.0f, 0.0f,

	    0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
	    0.5f,  -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 0.0f, 1.0f,
	    0.5f,  -0.5f, 0.5f,  0.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

	    -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 1.0f,
	    0.5f,  -0.5f, 0.5f,  1.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  1.0f, 0.0f,
	    -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,

	    -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
	    0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
	    -0.5f, 0.5f,  0.5f,  0.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f};

	GLuint VBOs[1];
	glGenBuffers(1, VBOs);

	glGenVertexArrays(1, &VAOs[0]);

	glBindVertexArray(VAOs[0]);
	glBindBuffer(GL_ARRAY_BUFFER, VBOs[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
	                      (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(5);

	glEnable(GL_DEPTH_TEST);

	return 0;
}

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
            XrTime predictedDisplayTime)
{

	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	glViewport(0, 0, w, h);
	glScissor(0, 0, w, h);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
	                       image.image, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
	                       depthbuffer, 0);

	glClearColor(.0f, 0.0f, 0.2f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	float cubedist = 1.5f;
	float cubescale = 0.33f;
	float cubeele = .5f;
	mat4_t modelmatrix_front =
	    m4_mul(m4_translation(vec3(0.0f, cubeele, -cubedist)),
	           m4_scaling(vec3(cubescale, cubescale, cubescale)));
	mat4_t modelmatrix_back =
	    m4_mul(m4_translation(vec3(0.0f, cubeele, cubedist)),
	           m4_scaling(vec3(cubescale, cubescale, cubescale)));
	mat4_t modelmatrix_left =
	    m4_mul(m4_translation(vec3(-cubedist, cubeele, 0.0f)),
	           m4_scaling(vec3(cubescale, cubescale, cubescale)));
	mat4_t modelmatrix_right =
	    m4_mul(m4_translation(vec3(cubedist, cubeele, 0.0f)),
	           m4_scaling(vec3(cubescale, cubescale, cubescale)));

	double displaytimeSeconds =
	    ((double)predictedDisplayTime) / (1000. * 1000. * 1000.);
	const float rotations_per_sec = .25;
	float rotation =
	    ((long)(displaytimeSeconds * 360. * rotations_per_sec)) % 360;
	mat4_t rotationmatrix = m4_rotation_y(degreesToRadians(rotation));
	modelmatrix_front = m4_mul(modelmatrix_front, rotationmatrix);
	modelmatrix_back = m4_mul(modelmatrix_back, rotationmatrix);
	modelmatrix_left = m4_mul(modelmatrix_left, rotationmatrix);
	modelmatrix_right = m4_mul(modelmatrix_right, rotationmatrix);

	glUseProgram(shaderProgramID);
	glBindVertexArray(VAOs[0]);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);

	int color = glGetUniformLocation(shaderProgramID, "uniformColor");
	// the color (0, 0, 0) will get replaced by some UV color in the shader
	glUniform3f(color, 0.0, 0.0, 0.0);

	int viewLoc = glGetUniformLocation(shaderProgramID, "view");
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, (float*)viewmatrix.m);
	int projLoc = glGetUniformLocation(shaderProgramID, "proj");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, (float*)projectionmatrix.m);

	int modelLoc = glGetUniformLocation(shaderProgramID, "model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)modelmatrix_front.m);
	glDrawArrays(GL_TRIANGLES, 0, 36);

	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)modelmatrix_back.m);
	glDrawArrays(GL_TRIANGLES, 0, 36);

	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)modelmatrix_left.m);
	glDrawArrays(GL_TRIANGLES, 0, 36);

	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)modelmatrix_right.m);
	glDrawArrays(GL_TRIANGLES, 0, 36);


	// controllers
	if (leftHand) {
		XrMatrix4x4f leftMatrix;
		XrVector3f uniformScale = {.x = .05f, .y = .05f, .z = .2f};
		XrMatrix4x4f_CreateTranslationRotationScaleRotate(
		    &leftMatrix, &leftHand->pose.position, &leftHand->pose.orientation,
		    &uniformScale);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)leftMatrix.m);
		glUniform3f(color, 1.0, 0.5, 0.5);
		glDrawArrays(GL_TRIANGLES, 0, 36);
	}

	if (rightHand) {
		XrMatrix4x4f rightMatrix;
		XrVector3f uniformScale = {.x = .05f, .y = .05f, .z = .2f};
		XrMatrix4x4f_CreateTranslationRotationScaleRotate(
		    &rightMatrix, &rightHand->pose.position, &rightHand->pose.orientation,
		    &uniformScale);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)rightMatrix.m);
		glUniform3f(color, 0.5, 1.0, 0.5);
		glDrawArrays(GL_TRIANGLES, 0, 36);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (viewIndex == 0) {
		_glBlitNamedFramebuffer((GLuint)framebuffer, // readFramebuffer
		                        (GLuint)0,    // backbuffer     // drawFramebuffer
		                        (GLint)0,     // srcX0
		                        (GLint)0,     // srcY0
		                        (GLint)w,     // srcX1
		                        (GLint)h,     // srcY1
		                        (GLint)0,     // dstX0
		                        (GLint)0,     // dstY0
		                        (GLint)w / 2, // dstX1
		                        (GLint)h / 2, // dstY1
		                        (GLbitfield)GL_COLOR_BUFFER_BIT, // mask
		                        (GLenum)GL_LINEAR);              // filter

		SDL_GL_SwapWindow(mainwindow);
	}
}

void
genFramebuffers(int count, GLuint* framebuffers)
{
	glGenFramebuffers(count, framebuffers);
}
