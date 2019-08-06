// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A simple OpenXR example
 * @author Christoph Haag <christoph.haag@collabora.com>
 */


#include <stdio.h>
#include <stdbool.h>

#include <X11/Xlib.h>
#include <GL/glx.h>

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include "openxr_headers/openxr.h"

#include "xrmath.h" // math glue between OpenXR and OpenGL
#include "glimpl.h" // factored out rendering of a simple scene
#include "math_3d.h"

#include <SDL2/SDL_events.h>

// upstream validation layer might not work at this time
// enable at your own risk
bool useCoreValidationLayer = false;
bool useApiDumpLayer = false;

typedef struct xr_example
{
	// every OpenXR app that displays something needs at least an instance and a
	// session
	XrInstance instance;
	XrSession session;

	// local space is used for "simple" small scale tracking.
	// A room scale VR application with bounds would use stage space.
	XrSpace local_space;

	// The runtime interacts with the OpenGL images (textures) via a Swapchain.
	XrGraphicsBindingOpenGLXlibKHR graphics_binding_gl;
	XrSwapchainImageOpenGLKHR** images;
	XrSwapchain* swapchains;

	// Each physical Display/Eye is described by a view
	uint32_t view_count;
	XrViewConfigurationView* configuration_views;

	// To render into a texture we need a framebuffer (one per texture to make it
	// easy)
	GLuint** framebuffers;

	// Only used to enable OpenGL depth testing
	GLuint depthbuffer;
} xr_example;

bool
xr_result(XrInstance instance, XrResult result, const char* format, ...)
{
	if (XR_SUCCEEDED(result))
		return true;

	char resultString[XR_MAX_RESULT_STRING_SIZE];
	xrResultToString(instance, result, resultString);

	size_t len1 = strlen(format);
	size_t len2 = strlen(resultString) + 1;
	char formatRes[len1 + len2 + 4]; // + " []\n"
	sprintf(formatRes, "%s [%s]\n", format, resultString);

	va_list args;
	va_start(args, format);
	vprintf(formatRes, args);
	va_end(args);
	return false;
}

void
sdl_handle_events(SDL_Event event, bool* running);

bool
isExtensionSupported(char* extensionName,
                     XrExtensionProperties* instanceExtensionProperties,
                     uint32_t instanceExtensionCount)
{
	for (uint32_t supportedIndex = 0; supportedIndex < instanceExtensionCount;
	     supportedIndex++) {
		if (!strcmp(extensionName,
		            instanceExtensionProperties[supportedIndex].extensionName)) {
			return true;
		}
	}
	return false;
}

void
main_loop(xr_example* self);

int
init_openxr(xr_example* self)
{
	XrResult result;

	// --- Make sure runtime supports the OpenGL extension

	// xrEnumerate*() functions are usually called once with CapacityInput = 0.
	// The function will write the required amount into CountOutput. We then have
	// to allocate an array to hold CountOutput elements and call the function
	// with CountOutput as CapacityInput.
	uint32_t extensionCount = 0;
	result =
	    xrEnumerateInstanceExtensionProperties(NULL, 0, &extensionCount, NULL);

	/* TODO: instance null will not be able to convert XrResult to string */
	if (!xr_result(NULL, result,
	               "Failed to enumerate number of extension properties"))
		return 1;

	printf("Runtime supports %d extensions\n", extensionCount);

	XrExtensionProperties extensionProperties[extensionCount];
	for (uint16_t i = 0; i < extensionCount; i++) {
		// we usually have to fill in the type (for validation) and set
		// next to NULL (or a pointer to an extension specific struct)
		extensionProperties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		extensionProperties[i].next = NULL;
	}

	result = xrEnumerateInstanceExtensionProperties(
	    NULL, extensionCount, &extensionCount, extensionProperties);
	if (!xr_result(NULL, result, "Failed to enumerate extension properties"))
		return 1;

	if (!isExtensionSupported(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME,
	                          extensionProperties, extensionCount)) {
		printf("Runtime does not support OpenGL extension!\n");
		return 1;
	}

	printf("Runtime supports required extension %s\n",
	       XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);

	// --- Enumerate API layers
	bool lunargApiDumpSupported = false;
	bool lunargCoreValidationSupported = false;
	uint32_t apiLayerCount;
	xrEnumerateApiLayerProperties(0, &apiLayerCount, NULL);
	printf("Loader found %d api layers%s", apiLayerCount,
	       apiLayerCount == 0 ? "\n" : ": ");
	XrApiLayerProperties apiLayerProperties[apiLayerCount];
	memset(apiLayerProperties, 0, apiLayerCount * sizeof(XrApiLayerProperties));

	for (uint32_t i = 0; i < apiLayerCount; i++) {
		apiLayerProperties[i].type = XR_TYPE_API_LAYER_PROPERTIES;
		apiLayerProperties[i].next = NULL;
	}
	xrEnumerateApiLayerProperties(apiLayerCount, &apiLayerCount,
	                              apiLayerProperties);
	for (uint32_t i = 0; i < apiLayerCount; i++) {
		if (strcmp(apiLayerProperties[i].layerName,
		           "XR_APILAYER_LUNARG_api_dump") == 0) {
			lunargApiDumpSupported = true;
		} else if (strcmp(apiLayerProperties[i].layerName,
		                  "XR_APILAYER_LUNARG_core_validation") == 0) {
			lunargCoreValidationSupported = true;
		}
		printf("%s%s", apiLayerProperties[i].layerName,
		       i < apiLayerCount - 1 ? ", " : "\n");
	}

	// --- Create XrInstance
	const char* const enabledExtensions[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};

	XrInstanceCreateInfo instanceCreateInfo = {
	    .type = XR_TYPE_INSTANCE_CREATE_INFO,
	    .next = NULL,
	    .createFlags = 0,
	    .enabledExtensionCount = 1,
	    .enabledExtensionNames = enabledExtensions,
	    .enabledApiLayerCount = 0,
	    .applicationInfo =
	        {
	            .applicationName = "OpenXR OpenGL Example",
	            .engineName = "Example Engine",
	            .applicationVersion = XR_MAKE_VERSION(1, 0, 0),
	            .engineVersion = XR_MAKE_VERSION(1, 0, 0),
	            .apiVersion = XR_CURRENT_API_VERSION,
	        },
	};

	// TODO: should be possible to enable more than one
	if (lunargApiDumpSupported && useApiDumpLayer) {
		instanceCreateInfo.enabledApiLayerCount = 1;
		const char* const enabledApiLayers[] = {"XR_APILAYER_LUNARG_api_dump"};
		instanceCreateInfo.enabledApiLayerNames = enabledApiLayers;
	}
	if (lunargCoreValidationSupported && useCoreValidationLayer) {
		instanceCreateInfo.enabledApiLayerCount = 1;
		const char* const enabledApiLayers[] = {
		    "XR_APILAYER_LUNARG_core_validation"};
		instanceCreateInfo.enabledApiLayerNames = enabledApiLayers;
	}


	result = xrCreateInstance(&instanceCreateInfo, &self->instance);
	if (!xr_result(NULL, result, "Failed to create XR instance."))
		return 1;

	// Checking instance properties is optional!
	{
		XrInstanceProperties instanceProperties = {
		    .type = XR_TYPE_INSTANCE_PROPERTIES,
		    .next = NULL,
		};

		result = xrGetInstanceProperties(self->instance, &instanceProperties);
		if (!xr_result(NULL, result, "Failed to get instance info"))
			return 1;

		printf("Runtime Name: %s\n", instanceProperties.runtimeName);
		printf("Runtime Version: %d.%d.%d\n",
		       XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
		       XR_VERSION_MINOR(instanceProperties.runtimeVersion),
		       XR_VERSION_PATCH(instanceProperties.runtimeVersion));
	}

	// --- Create XrSystem
	XrSystemGetInfo systemGetInfo = {.type = XR_TYPE_SYSTEM_GET_INFO,
	                                 .formFactor =
	                                     XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
	                                 .next = NULL};

	XrSystemId systemId;
	result = xrGetSystem(self->instance, &systemGetInfo, &systemId);
	if (!xr_result(self->instance, result,
	               "Failed to get system for HMD form factor."))
		return 1;

	printf("Successfully got XrSystem %lu for HMD form factor\n", systemId);

	// checking system properties is optional!
	{
		XrSystemProperties systemProperties = {
		    .type = XR_TYPE_SYSTEM_PROPERTIES,
		    .next = NULL,
		    .graphicsProperties = {0},
		    .trackingProperties = {0},
		};

		result = xrGetSystemProperties(self->instance, systemId, &systemProperties);
		if (!xr_result(self->instance, result, "Failed to get System properties"))
			return 1;

		printf("System properties for system %lu: \"%s\", vendor ID %d\n",
		       systemProperties.systemId, systemProperties.systemName,
		       systemProperties.vendorId);
		printf("\tMax layers          : %d\n",
		       systemProperties.graphicsProperties.maxLayerCount);
		printf("\tMax swapchain height: %d\n",
		       systemProperties.graphicsProperties.maxSwapchainImageHeight);
		printf("\tMax swapchain width : %d\n",
		       systemProperties.graphicsProperties.maxSwapchainImageWidth);
		printf("\tMax views           : %d\n",
		       systemProperties.graphicsProperties.maxViewCount);
		printf("\tOrientation Tracking: %d\n",
		       systemProperties.trackingProperties.orientationTracking);
		printf("\tPosition Tracking   : %d\n",
		       systemProperties.trackingProperties.positionTracking);
	}

	// --- Enumerate and set up Views
	uint32_t viewConfigurationCount;
	result = xrEnumerateViewConfigurations(self->instance, systemId, 0,
	                                       &viewConfigurationCount, NULL);
	if (!xr_result(self->instance, result,
	               "Failed to get view configuration count"))
		return 1;

	printf("Runtime supports %d view configurations\n", viewConfigurationCount);

	XrViewConfigurationType viewConfigurations[viewConfigurationCount];
	for (uint32_t i = 0; i < viewConfigurationCount; ++i)
		viewConfigurations[i] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	result = xrEnumerateViewConfigurations(
	    self->instance, systemId, viewConfigurationCount, &viewConfigurationCount,
	    viewConfigurations);
	if (!xr_result(self->instance, result,
	               "Failed to enumerate view configurations!"))
		return 1;

	XrViewConfigurationType stereoViewConfigType =
	    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	/* Checking if the runtime supports the view configuration we want to use is
	 * optional! If stereoViewConfigType.type is unset after the loop, the runtime
	 * does not support Stereo VR. */
	{
		XrViewConfigurationProperties stereoViewConfigProperties = {0};
		for (uint32_t i = 0; i < viewConfigurationCount; ++i) {
			XrViewConfigurationProperties properties = {
			    .type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES, .next = NULL};

			result = xrGetViewConfigurationProperties(
			    self->instance, systemId, viewConfigurations[i], &properties);
			if (!xr_result(self->instance, result,
			               "Failed to get view configuration info %d!", i))
				return 1;

			if (viewConfigurations[i] == stereoViewConfigType &&
			    /* just to verify */ properties.viewConfigurationType ==
			        stereoViewConfigType) {
				printf("Runtime supports our VR view configuration, yay!\n");
				stereoViewConfigProperties = properties;
			} else {
				printf(
				    "Runtime supports a view configuration we are not interested in: "
				    "%d\n",
				    properties.viewConfigurationType);
			}
		}
		if (stereoViewConfigProperties.type !=
		    XR_TYPE_VIEW_CONFIGURATION_PROPERTIES) {
			printf("Couldn't get VR View Configuration from Runtime!\n");
			return 1;
		}

		printf("VR View Configuration:\n");
		printf("\tview configuratio type: %d\n",
		       stereoViewConfigProperties.viewConfigurationType);
		printf("\tFOV mutable           : %s\n",
		       stereoViewConfigProperties.fovMutable ? "yes" : "no");
	}

	result = xrEnumerateViewConfigurationViews(self->instance, systemId,
	                                           stereoViewConfigType, 0,
	                                           &self->view_count, NULL);
	if (!xr_result(self->instance, result,
	               "Failed to get view configuration view count!"))
		return 1;

	self->configuration_views =
	    malloc(sizeof(XrViewConfigurationView) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		self->configuration_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		self->configuration_views[i].next = NULL;
	}

	result = xrEnumerateViewConfigurationViews(
	    self->instance, systemId, stereoViewConfigType, self->view_count,
	    &self->view_count, self->configuration_views);
	if (!xr_result(self->instance, result,
	               "Failed to enumerate view configuration views!"))
		return 1;

	printf("View count: %d\n", self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		printf("View %d:\n", i);
		printf("\tResolution       : Recommended %dx%d, Max: %dx%d\n",
		       self->configuration_views[0].recommendedImageRectWidth,
		       self->configuration_views[0].recommendedImageRectHeight,
		       self->configuration_views[0].maxImageRectWidth,
		       self->configuration_views[0].maxImageRectHeight);
		printf("\tSwapchain Samples: Recommended: %d, Max: %d)\n",
		       self->configuration_views[0].recommendedSwapchainSampleCount,
		       self->configuration_views[0].maxSwapchainSampleCount);
	}

	// Checking if the runtime supports the Graphics API we want to use is
	// optional! For OpenGL this is not too useful because all versions should
	// work. Other APIs have more useful requirements.
	{
		XrGraphicsRequirementsOpenGLKHR opengl_reqs = {
		    .type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR, .next = NULL};
		result = xrGetOpenGLGraphicsRequirementsKHR(self->instance, systemId,
		                                            &opengl_reqs);
		if (!xr_result(self->instance, result,
		               "Failed to get OpenGL graphics requirements!"))
			return 1;

		uint32_t desired_opengl_version = XR_MAKE_VERSION(4, 5, 0);
		if (desired_opengl_version > opengl_reqs.maxApiVersionSupported ||
		    desired_opengl_version < opengl_reqs.minApiVersionSupported) {
			printf(
			    "We want OpenGL %d.%d.%d, but runtime only supports OpenGL "
			    "%d.%d.%d - %d.%d.%d!\n",
			    XR_VERSION_MAJOR(desired_opengl_version),
			    XR_VERSION_MINOR(desired_opengl_version),
			    XR_VERSION_PATCH(desired_opengl_version),
			    XR_VERSION_MAJOR(opengl_reqs.minApiVersionSupported),
			    XR_VERSION_MINOR(opengl_reqs.minApiVersionSupported),
			    XR_VERSION_PATCH(opengl_reqs.minApiVersionSupported),
			    XR_VERSION_MAJOR(opengl_reqs.maxApiVersionSupported),
			    XR_VERSION_MINOR(opengl_reqs.maxApiVersionSupported),
			    XR_VERSION_PATCH(opengl_reqs.maxApiVersionSupported));
			return 1;
		}
	}

	// --- Create session

	self->graphics_binding_gl = (XrGraphicsBindingOpenGLXlibKHR){
	    .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
	};

	if (!initGLX(&self->graphics_binding_gl.xDisplay,
	             &self->graphics_binding_gl.visualid,
	             &self->graphics_binding_gl.glxFBConfig,
	             &self->graphics_binding_gl.glxDrawable,
	             &self->graphics_binding_gl.glxContext,
	             self->configuration_views[0].recommendedImageRectWidth,
	             self->configuration_views[0].recommendedImageRectHeight)) {
		printf("GLX init failed!\n");
		return 1;
	}


	// Set up rendering (compile shaders, ...)
	if (initGlImpl() != 0) {
		printf("OpenGl setup failed!\n");
		return 1;
	}

	XrSessionCreateInfo session_create_info = {.type =
	                                               XR_TYPE_SESSION_CREATE_INFO,
	                                           .next = &self->graphics_binding_gl,
	                                           .systemId = systemId};


	result =
	    xrCreateSession(self->instance, &session_create_info, &self->session);
	if (!xr_result(self->instance, result, "Failed to create session"))
		return 1;

	printf("Successfully created a session with OpenGL!\n");

	const GLubyte* renderer_string = glGetString(GL_RENDERER);
	const GLubyte* version_string = glGetString(GL_VERSION);
	printf("Using OpenGL version: %s\n", version_string);
	printf("Using OpenGL Renderer: %s\n", renderer_string);

	// --- Check supported reference spaces
	// we don't *need* to check the supported reference spaces if we're confident
	// the runtime will support whatever we use
	{
		uint32_t referenceSpacesCount;
		result = xrEnumerateReferenceSpaces(self->session, 0, &referenceSpacesCount,
		                                    NULL);
		if (!xr_result(self->instance, result,
		               "Getting number of reference spaces failed!"))
			return 1;

		XrReferenceSpaceType referenceSpaces[referenceSpacesCount];
		for (uint32_t i = 0; i < referenceSpacesCount; i++)
			referenceSpaces[i] = XR_REFERENCE_SPACE_TYPE_VIEW;
		result = xrEnumerateReferenceSpaces(self->session, referenceSpacesCount,
		                                    &referenceSpacesCount, referenceSpaces);
		if (!xr_result(self->instance, result,
		               "Enumerating reference spaces failed!"))
			return 1;

		bool stageSpaceSupported = false;
		bool localSpaceSupported = false;
		printf("Runtime supports %d reference spaces: ", referenceSpacesCount);
		for (uint32_t i = 0; i < referenceSpacesCount; i++) {
			if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_LOCAL) {
				printf("/space/local%s", i == referenceSpacesCount - 1 ? "\n" : ", ");
				localSpaceSupported = true;
			} else if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
				printf("/space/stage%s", i == referenceSpacesCount - 1 ? "\n" : ", ");
				stageSpaceSupported = true;
			} else if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_VIEW) {
				printf("/user/head%s", i == referenceSpacesCount - 1 ? "\n" : ", ");
			}
		}

		if (/* !stageSpaceSupported || */ !localSpaceSupported) {
			printf(
			    "runtime does not support required spaces! stage: %s, "
			    "local: %s\n",
			    stageSpaceSupported ? "supported" : "NOT SUPPORTED",
			    localSpaceSupported ? "supported" : "NOT SUPPORTED");
			return 1;
		}
	}

	XrPosef identityPose = {.orientation = {.x = 0, .y = 0, .z = 0, .w = 1.0},
	                        .position = {.x = 0, .y = 0, .z = 0}};

	XrReferenceSpaceCreateInfo localSpaceCreateInfo = {
	    .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
	    .next = NULL,
	    .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
	    .poseInReferenceSpace = identityPose};

	result = xrCreateReferenceSpace(self->session, &localSpaceCreateInfo,
	                                &self->local_space);
	if (!xr_result(self->instance, result, "Failed to create local space!"))
		return 1;

	// --- Begin session
	XrSessionBeginInfo sessionBeginInfo = {.type = XR_TYPE_SESSION_BEGIN_INFO,
	                                       .next = NULL,
	                                       .primaryViewConfigurationType =
	                                           stereoViewConfigType};
	result = xrBeginSession(self->session, &sessionBeginInfo);
	if (!xr_result(self->instance, result, "Failed to begin session!"))
		return 1;
	printf("Session started!\n");



	// --- Create Swapchains
	uint32_t swapchainFormatCount;
	result = xrEnumerateSwapchainFormats(self->session, 0, &swapchainFormatCount,
	                                     NULL);
	if (!xr_result(self->instance, result,
	               "Failed to get number of supported swapchain formats"))
		return 1;

	printf("Runtime supports %d swapchain formats\n", swapchainFormatCount);
	int64_t swapchainFormats[swapchainFormatCount];
	result = xrEnumerateSwapchainFormats(self->session, swapchainFormatCount,
	                                     &swapchainFormatCount, swapchainFormats);
	if (!xr_result(self->instance, result,
	               "Failed to enumerate swapchain formats"))
		return 1;

	// TODO: Determine which format we want to use instead of using the first one
	int64_t swapchainFormatToUse = swapchainFormats[0];

	/* First create swapchains and query the length for each swapchain. */
	self->swapchains = malloc(sizeof(XrSwapchain) * self->view_count);

	uint32_t swapchainLength[self->view_count];

	for (uint32_t i = 0; i < self->view_count; i++) {
		XrSwapchainCreateInfo swapchainCreateInfo = {
		    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
		    .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
		                  XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
		    .createFlags = 0,
		    .format = swapchainFormatToUse,
		    .sampleCount = 1,
		    .width = self->configuration_views[i].recommendedImageRectWidth,
		    .height = self->configuration_views[i].recommendedImageRectHeight,
		    .faceCount = 1,
		    .arraySize = 1,
		    .mipCount = 1,
		    .next = NULL,
		};

		result = xrCreateSwapchain(self->session, &swapchainCreateInfo,
		                           &self->swapchains[i]);
		if (!xr_result(self->instance, result, "Failed to create swapchain %d!", i))
			return 1;

		result = xrEnumerateSwapchainImages(self->swapchains[i], 0,
		                                    &swapchainLength[i], NULL);
		if (!xr_result(self->instance, result, "Failed to enumerate swapchains"))
			return 1;
	}

	// most likely all swapchains have the same length, but let's not fail
	// if they are not: create 2d array with the longest chain as second dim
	uint32_t maxSwapchainLength = 0;
	for (uint32_t i = 0; i < self->view_count; i++) {
		if (swapchainLength[i] > maxSwapchainLength) {
			maxSwapchainLength = swapchainLength[i];
		}
	}

	self->images = malloc(sizeof(XrSwapchainImageOpenGLKHR*) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++)
		self->images[i] =
		    malloc(sizeof(XrSwapchainImageOpenGLKHR) * maxSwapchainLength);

	self->framebuffers = malloc(sizeof(GLuint*) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++)
		self->framebuffers[i] = malloc(sizeof(GLuint) * maxSwapchainLength);


	for (uint32_t i = 0; i < self->view_count; i++) {
		for (uint32_t j = 0; j < swapchainLength[i]; j++) {
			self->images[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			self->images[i][j].next = NULL;
		}
		result = xrEnumerateSwapchainImages(
		    self->swapchains[i], swapchainLength[i], &swapchainLength[i],
		    (XrSwapchainImageBaseHeader*)self->images[i]);
		if (!xr_result(self->instance, result,
		               "Failed to enumerate swapchain images"))
			return 1;

		genFramebuffers(swapchainLength[i], self->framebuffers[i]);
	}

	// we also create one depth buffer that we need for OpenGL's depth testing.
	// TODO: One depth buffer per view because the size could theoretically be
	// different

	glGenTextures(1, &self->depthbuffer);
	glBindTexture(GL_TEXTURE_2D, self->depthbuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
	             self->configuration_views[0].recommendedImageRectWidth,
	             self->configuration_views[0].recommendedImageRectHeight, 0,
	             GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);

	return 0;
}

void
main_loop(xr_example* self)
{
	XrEventDataBuffer runtimeEvent = {.type = XR_TYPE_EVENT_DATA_BUFFER,
	                                  .next = NULL};

	SDL_Event sdlEvent;

	bool running = true;
	bool isVisible = true;

	// projectionLayers struct reused for every frame
	XrCompositionLayerProjection projectionLayer = {
	    .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	    .next = NULL,
	    .layerFlags = 0,
	    .space = self->local_space,
	    .viewCount = self->view_count,
	    // views is const and can't be changed, has to be created new every time
	    .views = NULL,
	};

	XrResult result;

	while (running) {

		// --- Wait for our turn to render a frame
		XrFrameState frameState = {.type = XR_TYPE_FRAME_STATE, .next = NULL};
		XrFrameWaitInfo frameWaitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO,
		                                 .next = NULL};
		result = xrWaitFrame(self->session, &frameWaitInfo, &frameState);
		if (!xr_result(self->instance, result,
		               "xrWaitFrame() was not successful, exiting..."))
			break;

		// --- Handle runtime Events
		// we do this right after xrWaitFrame() so we can go idle or
		// break out of the main render loop as early as possible into
		// the frame and don't have to uselessly render or submit one
		XrResult pollResult = xrPollEvent(self->instance, &runtimeEvent);
		if (pollResult == XR_SUCCESS) {
			switch (runtimeEvent.type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
				printf("EVENT: events data lost!\n");
				XrEventDataEventsLost* event = (XrEventDataEventsLost*)&runtimeEvent;
				// do we care if the runtmime loses events?
				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				printf("EVENT: instance loss pending!\n");
				XrEventDataInstanceLossPending* event =
				    (XrEventDataInstanceLossPending*)&runtimeEvent;
				running = false;
				break;
			}
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				printf("EVENT: session state changed ");
				XrEventDataSessionStateChanged* event =
				    (XrEventDataSessionStateChanged*)&runtimeEvent;
				XrSessionState state = event->state;

				// it would be better to handle each state change
				isVisible = event->state <= XR_SESSION_STATE_FOCUSED;
				printf("to %d. Visible: %d", state, isVisible);
				if (event->state >= XR_SESSION_STATE_STOPPING) {
					printf("Abort Mission!");
					running = false;
				}
				printf("\n");
				break;
			}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				printf("EVENT: reference space change pengind!\n");
				XrEventDataReferenceSpaceChangePending* event =
				    (XrEventDataReferenceSpaceChangePending*)&runtimeEvent;
				// TODO: do something
				break;
			}
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				printf("EVENT: interaction profile changed!\n");
				XrEventDataInteractionProfileChanged* event =
				    (XrEventDataInteractionProfileChanged*)&runtimeEvent;
				// TODO: do something
				break;
			}

			case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR: {
				printf("EVENT: visibility mask changed!!\n");
				XrEventDataVisibilityMaskChangedKHR* event =
				    (XrEventDataVisibilityMaskChangedKHR*)&runtimeEvent;
				// this event is from an extension
				break;
			}
			case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
				printf("EVENT: perf settings!\n");
				XrEventDataPerfSettingsEXT* event =
				    (XrEventDataPerfSettingsEXT*)&runtimeEvent;
				// this event is from an extension
				break;
			}
			default: printf("Unhandled event type %d\n", runtimeEvent.type);
			}
		} else if (pollResult == XR_EVENT_UNAVAILABLE) {
			// this is the usual case
		} else {
			printf("Failed to poll events!\n");
			break;
		}
		if (!isVisible) {
			continue;
		}

		// --- Poll SDL for events so we can exit with esc
		while (SDL_PollEvent(&sdlEvent)) {
			sdl_handle_events(sdlEvent, &running);
		}

		// --- Create projection matrices and view matrices for each eye
		XrViewLocateInfo viewLocateInfo = {.type = XR_TYPE_VIEW_LOCATE_INFO,
		                                   .displayTime =
		                                       frameState.predictedDisplayTime,
		                                   .space = self->local_space};

		XrView views[self->view_count];
		for (uint32_t i = 0; i < self->view_count; i++) {
			views[i].type = XR_TYPE_VIEW;
			views[i].next = NULL;
		};

		XrViewState viewState = {.type = XR_TYPE_VIEW_STATE, .next = NULL};
		uint32_t viewCountOutput;
		result = xrLocateViews(self->session, &viewLocateInfo, &viewState,
		                       self->view_count, &viewCountOutput, views);
		if (!xr_result(self->instance, result, "Could not locate views"))
			break;

		// --- Begin frame
		XrFrameBeginInfo frameBeginInfo = {.type = XR_TYPE_FRAME_BEGIN_INFO,
		                                   .next = NULL};

		result = xrBeginFrame(self->session, &frameBeginInfo);
		if (!xr_result(self->instance, result, "failed to begin frame!"))
			break;

		XrCompositionLayerProjectionView projection_views[self->view_count];

		// render each eye and fill projection_views with the result
		for (uint32_t i = 0; i < self->view_count; i++) {
			XrMatrix4x4f projectionMatrix;
			XrMatrix4x4f_CreateProjectionFov(&projectionMatrix, GRAPHICS_OPENGL,
			                                 views[i].fov, 0.05f, 100.0f);

			const XrVector3f uniformScale = {.x = 1.f, .y = 1.f, .z = 1.f};

			XrMatrix4x4f viewMatrix;
			XrMatrix4x4f_CreateTranslationRotationScaleRotate(
			    &viewMatrix, &views[i].pose.position, &views[i].pose.orientation,
			    &uniformScale);

			// Calculates the inverse of a rigid body transform.
			XrMatrix4x4f inverseViewMatrix;
			XrMatrix4x4f_InvertRigidBody(&inverseViewMatrix, &viewMatrix);

			XrSwapchainImageAcquireInfo swapchainImageAcquireInfo = {
			    .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, .next = NULL};
			uint32_t bufferIndex;
			result = xrAcquireSwapchainImage(
			    self->swapchains[i], &swapchainImageAcquireInfo, &bufferIndex);
			if (!xr_result(self->instance, result,
			               "failed to acquire swapchain image!"))
				break;

			XrSwapchainImageWaitInfo swapchainImageWaitInfo = {
			    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
			    .next = NULL,
			    .timeout = 1000};
			result =
			    xrWaitSwapchainImage(self->swapchains[i], &swapchainImageWaitInfo);
			if (!xr_result(self->instance, result,
			               "failed to wait for swapchain image!"))
				break;

			projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projection_views[i].next = NULL;
			projection_views[i].pose = views[i].pose;
			projection_views[i].fov = views[i].fov;
			projection_views[i].subImage.swapchain = self->swapchains[i];
			projection_views[i].subImage.imageArrayIndex = bufferIndex;
			projection_views[i].subImage.imageRect.offset.x = 0;
			projection_views[i].subImage.imageRect.offset.y = 0;
			projection_views[i].subImage.imageRect.extent.width =
			    self->configuration_views[i].recommendedImageRectWidth;
			projection_views[i].subImage.imageRect.extent.height =
			    self->configuration_views[i].recommendedImageRectHeight;

			// TODO: add left and right hand pose
			renderFrame(self->configuration_views[i].recommendedImageRectWidth,
			            self->configuration_views[i].recommendedImageRectHeight,
			            projectionMatrix, inverseViewMatrix, NULL, NULL,
			            self->framebuffers[i][bufferIndex], self->depthbuffer,
			            self->images[i][bufferIndex], i,
			            frameState.predictedDisplayTime);
			XrSwapchainImageReleaseInfo swapchainImageReleaseInfo = {
			    .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, .next = NULL};
			result = xrReleaseSwapchainImage(self->swapchains[i],
			                                 &swapchainImageReleaseInfo);
			if (!xr_result(self->instance, result,
			               "failed to release swapchain image!"))
				break;
		}

		projectionLayer.views = projection_views;

		const XrCompositionLayerBaseHeader* const projectionlayers[1] = {
		    (const XrCompositionLayerBaseHeader* const) & projectionLayer};
		XrFrameEndInfo frameEndInfo = {
		    .type = XR_TYPE_FRAME_END_INFO,
		    .displayTime = frameState.predictedDisplayTime,
		    .layerCount = 1,
		    .layers = projectionlayers,
		    .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		    .next = NULL};
		result = xrEndFrame(self->session, &frameEndInfo);
		if (!xr_result(self->instance, result, "failed to end frame!"))
			break;
	}
}

void
sdl_handle_events(SDL_Event event, bool* running)
{
	if (event.type == SDL_QUIT ||
	    (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
		*running = false;
	}
}

void
cleanup(xr_example* self)
{
	xrDestroySession(self->session);
	xrDestroyInstance(self->instance);
}

int
main()
{
	xr_example self;
	int ret = init_openxr(&self);
	if (ret != 0)
		return ret;
	main_loop(&self);
	cleanup(&self);
	return 0;
}
