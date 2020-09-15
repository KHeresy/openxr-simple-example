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

static SDL_Window* desktop_window;
static SDL_GLContext gl_context;

void GLAPIENTRY
MessageCallback(GLenum source,
				GLenum type,
				GLuint id,
				GLenum severity,
				GLsizei length,
				const GLchar* message,
				const void* userParam)
{
	fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
			(type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity, message);
}

bool
init_sdl_window(HDC& xDisplay, HGLRC& glxContext,
				int w,
				int h)
{

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("Unable to initialize SDL");
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

	/* Create our window centered at half the VR resolution */
	desktop_window = SDL_CreateWindow("OpenXR Example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
		w / 2, h / 2, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!desktop_window) {
		printf("Unable to create window");
		return false;
	}

	gl_context = SDL_GL_CreateContext(desktop_window);
	auto err = glewInit();

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(MessageCallback, 0);

	SDL_GL_SetSwapInterval(0);


	// HACK? OpenXR wants us to report these values, so "work around" SDL a
	// bit and get the underlying glx stuff. Does this still work when e.g.
	// SDL switches to xcb?
	xDisplay = wglGetCurrentDC();
	glxContext = wglGetCurrentContext();

	return true;
}

int
init_gl()
{
	GLuint vertex_shader_id = glCreateShader(GL_VERTEX_SHADER);
	const GLchar* vertex_shader_source[1];
	vertex_shader_source[0] = vertexshader;
	// printf("Vertex Shader:\n%s\n", vertexShaderSource);
	glShaderSource(vertex_shader_id, 1, vertex_shader_source, NULL);
	glCompileShader(vertex_shader_id);
	int vertex_compile_res;
	glGetShaderiv(vertex_shader_id, GL_COMPILE_STATUS, &vertex_compile_res);
	if (!vertex_compile_res) {
		char info_log[512];
		glGetShaderInfoLog(vertex_shader_id, 512, NULL, info_log);
		printf("Vertex Shader failed to compile: %s\n", info_log);
		return 1;
	} else {
		printf("Successfully compiled vertex shader!\n");
	}

	GLuint fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER);
	const GLchar* fragment_shader_source[1];
	fragment_shader_source[0] = fragmentshader;
	glShaderSource(fragment_shader_id, 1, fragment_shader_source, NULL);
	glCompileShader(fragment_shader_id);
	int fragment_compile_res;
	glGetShaderiv(fragment_shader_id, GL_COMPILE_STATUS, &fragment_compile_res);
	if (!fragment_compile_res) {
		char info_log[512];
		glGetShaderInfoLog(fragment_shader_id, 512, NULL, info_log);
		printf("Fragment Shader failed to compile: %s\n", info_log);
		return 1;
	} else {
		printf("Successfully compiled fragment shader!\n");
	}

	shaderProgramID = glCreateProgram();
	glAttachShader(shaderProgramID, vertex_shader_id);
	glAttachShader(shaderProgramID, fragment_shader_id);
	glLinkProgram(shaderProgramID);
	GLint shader_program_res;
	glGetProgramiv(shaderProgramID, GL_LINK_STATUS, &shader_program_res);
	if (!shader_program_res) {
		char info_log[512];
		glGetProgramInfoLog(shaderProgramID, 512, NULL, info_log);
		printf("Shader Program failed to link: %s\n", info_log);
		return 1;
	} else {
		printf("Successfully linked shader program!\n");
	}

	glDeleteShader(vertex_shader_id);
	glDeleteShader(fragment_shader_id);

	float vertices[] = {-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 0.0f,
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
	glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(5);

	glEnable(GL_DEPTH_TEST);

	return 0;
}

void
render_cube(
	vec3_t position, float scale, float rotation, float* view_matrix, float* projection_matrix)
{

	mat4_t modelmatrix = m4_mul(m4_translation(position), m4_scaling(vec3(scale, scale, scale)));

	mat4_t rotationmatrix = m4_rotation_y(degreesToRadians(rotation));
	modelmatrix = m4_mul(modelmatrix, rotationmatrix);

	glUseProgram(shaderProgramID);
	glBindVertexArray(VAOs[0]);


	int color = glGetUniformLocation(shaderProgramID, "uniformColor");
	// the color (0, 0, 0) will get replaced by some UV color in the shader
	glUniform3f(color, 0.0, 0.0, 0.0);

	int viewLoc = glGetUniformLocation(shaderProgramID, "view");
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view_matrix);
	int projLoc = glGetUniformLocation(shaderProgramID, "proj");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, projection_matrix);

	int modelLoc = glGetUniformLocation(shaderProgramID, "model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)modelmatrix.m);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

void
render_quad(int w,
			int h,
			int64_t swapchain_format,
			XrSwapchainImageOpenGLKHR image,
			XrTime predictedDisplayTime)
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, image.image);

	glViewport(0, 0, w, h);
	glScissor(0, 0, w, h);

	uint8_t* rgb = new uint8_t[w * h * 4];
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			uint8_t* base = &rgb[(row * w * 4 + col * 4)];
			*(base + 0) = (((float)row / (float)h)) * 255.;
			*(base + 1) = 0;
			*(base + 2) = 0;
			*(base + 3) = 255;

			if (abs(row - col) < 3) {
				*(base + 0) = 255.;
				*(base + 1) = 255;
				*(base + 2) = 255;
				*(base + 3) = 255;
			}

			if (abs((w - col) - (row)) < 3) {
				*(base + 0) = 0.;
				*(base + 1) = 0;
				*(base + 2) = 0;
				*(base + 3) = 255;
			}
		}
	}

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE,
					(GLvoid*)rgb);
	delete [] rgb;
}

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
			 XrTime predictedDisplayTime)
{
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	glViewport(0, 0, w, h);
	glScissor(0, 0, w, h);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image.image, 0);
	if (depthbuffer != UINT32_MAX) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthbuffer, 0);
	} else {
		// TODO: need a depth attachment for depth test when rendering to fbo
	}

	glClearColor(.0f, 0.0f, 0.2f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	double display_time_seconds = ((double)predictedDisplayTime) / (1000. * 1000. * 1000.);
	const float rotations_per_sec = .25;
	float rotation = ((long)(display_time_seconds * 360. * rotations_per_sec)) % 360;

	float dist = 1.5f;
	float height = 0.5f;
	render_cube(vec3(0, height, -dist), .33f, rotation, viewmatrix.m, projectionmatrix.m);
	render_cube(vec3(0, height, dist), .33f, rotation, viewmatrix.m, projectionmatrix.m);
	render_cube(vec3(dist, height, 0), .33f, rotation, viewmatrix.m, projectionmatrix.m);
	render_cube(vec3(-dist, height, 0), .33f, rotation, viewmatrix.m, projectionmatrix.m);

	glUseProgram(shaderProgramID);
	glBindVertexArray(VAOs[0]);

	int color = glGetUniformLocation(shaderProgramID, "uniformColor");
	// the color (0, 0, 0) will get replaced by some UV color in the shader
	glUniform3f(color, 0.0, 0.0, 0.0);

	int viewLoc = glGetUniformLocation(shaderProgramID, "view");
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, (float*)viewmatrix.m);
	int projLoc = glGetUniformLocation(shaderProgramID, "proj");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, (float*)projectionmatrix.m);

	int modelLoc = glGetUniformLocation(shaderProgramID, "model");
	for (int hand = 0; hand < 2; hand++) {
		if (hand == 0) {
			glUniform3f(color, 1.0, 0.5, 0.5);
		} else {
			glUniform3f(color, 0.5, 1.0, 0.5);
		}

		// draw blocks for controller locations if hand tracking is not available
		if (!joint_locations[hand].isActive) {

			if (!hand_locations_valid[hand])
				continue;

			XrMatrix4x4f matrix;
			XrVector3f scale = {.x = .05f, .y = .05f, .z = .2f};
			XrMatrix4x4f_CreateModelMatrix(&matrix, &hand_locations[hand].pose.position,
										   &hand_locations[hand].pose.orientation, &scale);
			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)matrix.m);

			glDrawArrays(GL_TRIANGLES, 0, 36);
			continue;
		}

		for (uint32_t i = 0; i < joint_locations[hand].jointCount; i++) {
			struct XrHandJointLocationEXT* joint_location = &joint_locations[hand].jointLocations[i];

			if (!(joint_location->locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
				continue;
			}

			float size = joint_location->radius;

			XrVector3f scale = {.x = size, .y = size, .z = size};
			XrMatrix4x4f joint_matrix;
			XrMatrix4x4f_CreateModelMatrix(&joint_matrix, &joint_location->pose.position,
										   &joint_location->pose.orientation, &scale);
			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)joint_matrix.m);
			glDrawArrays(GL_TRIANGLES, 0, 36);
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (view_index == 0) {
		//glBlitNamedFramebuffer((GLuint)framebuffer,             // readFramebuffer
		//						(GLuint)0,                       // backbuffer     // drawFramebuffer
		//						(GLint)0,                        // srcX0
		//						(GLint)0,                        // srcY0
		//						(GLint)w,                        // srcX1
		//						(GLint)h,                        // srcY1
		//						(GLint)0,                        // dstX0
		//						(GLint)0,                        // dstY0
		//						(GLint)w / 2,                    // dstX1
		//						(GLint)h / 2,                    // dstY1
		//						(GLbitfield)GL_COLOR_BUFFER_BIT, // mask
		//						(GLenum)GL_LINEAR);              // filter

		SDL_GL_SwapWindow(desktop_window);
	}
}

void
cleanup_gl()
{
	// TODO clean up gl stuff
}
