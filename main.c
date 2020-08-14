// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A simple OpenXR example
 * @author Christoph Haag <christoph.haag@collabora.com>
 */


#include <stdio.h>
#include <stdbool.h>

#include "glimpl.h" // factored out rendering of a simple scene

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include "openxr_headers/openxr.h"

#include "xrmath.h" // math glue between OpenXR and OpenGL
#include "math_3d.h"

#include <SDL2/SDL_events.h>

// we need an identity pose for creating spaces without offsets
static XrPosef identity_pose = {.orientation = {.x = 0, .y = 0, .z = 0, .w = 1.0},
                                .position = {.x = 0, .y = 0, .z = 0}};

// small helper so we don't forget whether we treat 0 as left or right hand
enum OPENXR_HANDS
{
	HAND_LEFT = 0,
	HAND_RIGHT = 1,
	HAND_COUNT
};

char*
h_str(int hand)
{
	if (hand == HAND_LEFT)
		return "left";
	else if (hand == HAND_RIGHT)
		return "right";
	else
		return "invalid";
}

char*
h_p_str(int hand)
{
	if (hand == HAND_LEFT)
		return "/user/hand/left";
	else if (hand == HAND_RIGHT)
		return "/user/hand/right";
	else
		return "invalid";
}

typedef struct xr_example
{
	// every OpenXR app that displays something needs at least an instance and a
	// session
	XrInstance instance;
	XrSession session;
	XrSystemId system_id;
	XrSessionState state;

	// Play space is usually local (head is origin, seated) or stage (room scale)
	XrSpace play_space;

	// Each physical Display/Eye is described by a view
	uint32_t view_count;
	XrViewConfigurationView* viewconfig_views;
	XrCompositionLayerProjectionView* projection_views;
	XrView* views;

	// The runtime interacts with the OpenGL images (textures) via a Swapchain.
	XrGraphicsBindingOpenGLXlibKHR graphics_binding_gl;

	int64_t swapchain_format;
	// length of the swapchain per view. Usually all the same, but not required.
	uint32_t* swapchain_lengths;
	// one array of images per view.
	XrSwapchainImageOpenGLKHR** images;
	// one swapchain per view. Using only one and rendering l/r to the same image is also possible.
	XrSwapchain* swapchains;

	int64_t depth_swapchain_format;
	uint32_t* depth_swapchain_lengths;
	XrSwapchainImageOpenGLKHR** depth_images;
	XrSwapchain* depth_swapchains;

	// quad layers are placed into world space, no need to render them per eye
	int64_t quad_swapchain_format;
	uint32_t quad_pixel_width, quad_pixel_height;
	uint32_t quad_swapchain_length;
	XrSwapchainImageOpenGLKHR* quad_images;
	XrSwapchain quad_swapchain;

	// To render into a texture we need a framebuffer (one per texture to make it easy)
	GLuint** framebuffers;

	XrPath hand_paths[HAND_COUNT];

	// whether the runtime supports the hand tracking extension at all
	bool hand_tracking_ext;
	// whether the current VR system in use has hand tracking
	bool hand_tracking_supported;
	PFN_xrLocateHandJointsEXT pfnLocateHandJointsEXT;
	XrHandTrackerEXT hand_trackers[HAND_COUNT];
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
	char *formatRes = malloc (sizeof(char) * (len1 + len2 + 4)); // + " []\n"
	sprintf(formatRes, "%s [%s]\n", format, resultString);

	va_list args;
	va_start(args, format);
	vprintf(formatRes, args);
	va_end(args);

	free(formatRes);
	return false;
}

void
sdl_handle_events(SDL_Event event, bool* running);

// some optional OpenXR calls demonstrated in functions to clutter the main app less
void
get_instance_properties(XrInstance instance)
{
	XrResult result;
	XrInstanceProperties instance_props = {
	    .type = XR_TYPE_INSTANCE_PROPERTIES,
	    .next = NULL,
	};

	result = xrGetInstanceProperties(instance, &instance_props);
	if (!xr_result(NULL, result, "Failed to get instance info"))
		return;

	printf("Runtime Name: %s\n", instance_props.runtimeName);
	printf("Runtime Version: %d.%d.%d\n", XR_VERSION_MAJOR(instance_props.runtimeVersion),
	       XR_VERSION_MINOR(instance_props.runtimeVersion),
	       XR_VERSION_PATCH(instance_props.runtimeVersion));
}

void
print_system_properties(XrSystemProperties* system_properties, bool hand_tracking_ext)
{
	printf("System properties for system %lu: \"%s\", vendor ID %d\n", system_properties->systemId,
	       system_properties->systemName, system_properties->vendorId);
	printf("\tMax layers          : %d\n", system_properties->graphicsProperties.maxLayerCount);
	printf("\tMax swapchain height: %d\n",
	       system_properties->graphicsProperties.maxSwapchainImageHeight);
	printf("\tMax swapchain width : %d\n",
	       system_properties->graphicsProperties.maxSwapchainImageWidth);
	printf("\tOrientation Tracking: %d\n", system_properties->trackingProperties.orientationTracking);
	printf("\tPosition Tracking   : %d\n", system_properties->trackingProperties.positionTracking);

	if (hand_tracking_ext) {
		XrSystemHandTrackingPropertiesEXT* ht = system_properties->next;
		printf("\tHand Tracking       : %d\n", ht->supportsHandTracking);
	}
}

void
print_supported_view_configs(xr_example* self)
{
	XrResult result;

	uint32_t view_config_count;
	result =
	    xrEnumerateViewConfigurations(self->instance, self->system_id, 0, &view_config_count, NULL);
	if (!xr_result(self->instance, result, "Failed to get view configuration count"))
		return;

	printf("Runtime supports %d view configurations\n", view_config_count);

	XrViewConfigurationType view_configs[view_config_count];
	result = xrEnumerateViewConfigurations(self->instance, self->system_id, view_config_count,
	                                       &view_config_count, view_configs);
	if (!xr_result(self->instance, result, "Failed to enumerate view configurations!"))
		return;

	printf("Runtime supports view configurations:\n");
	for (uint32_t i = 0; i < view_config_count; ++i) {
		XrViewConfigurationProperties props = {.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES,
		                                       .next = NULL};

		result =
		    xrGetViewConfigurationProperties(self->instance, self->system_id, view_configs[i], &props);
		if (!xr_result(self->instance, result, "Failed to get view configuration info %d!", i))
			return;

		printf("%d: FOV mutable: %d\n", props.viewConfigurationType, props.fovMutable);
	}
}

void
print_viewconfig_view_info(xr_example* self)
{
	for (uint32_t i = 0; i < self->view_count; i++) {
		printf("View Configuration View %d:\n", i);
		printf("\tResolution       : Recommended %dx%d, Max: %dx%d\n",
		       self->viewconfig_views[0].recommendedImageRectWidth,
		       self->viewconfig_views[0].recommendedImageRectHeight,
		       self->viewconfig_views[0].maxImageRectWidth,
		       self->viewconfig_views[0].maxImageRectHeight);
		printf("\tSwapchain Samples: Recommended: %d, Max: %d)\n",
		       self->viewconfig_views[0].recommendedSwapchainSampleCount,
		       self->viewconfig_views[0].maxSwapchainSampleCount);
	}
}

bool
check_opengl_version(XrGraphicsRequirementsOpenGLKHR* opengl_reqs)
{
	XrVersion desired_opengl_version = XR_MAKE_VERSION(3, 3, 0);
	if (desired_opengl_version > opengl_reqs->maxApiVersionSupported ||
	    desired_opengl_version < opengl_reqs->minApiVersionSupported) {
		printf(
		    "We want OpenGL %d.%d.%d, but runtime only supports OpenGL "
		    "%d.%d.%d - %d.%d.%d!\n",
		    XR_VERSION_MAJOR(desired_opengl_version), XR_VERSION_MINOR(desired_opengl_version),
		    XR_VERSION_PATCH(desired_opengl_version),
		    XR_VERSION_MAJOR(opengl_reqs->minApiVersionSupported),
		    XR_VERSION_MINOR(opengl_reqs->minApiVersionSupported),
		    XR_VERSION_PATCH(opengl_reqs->minApiVersionSupported),
		    XR_VERSION_MAJOR(opengl_reqs->maxApiVersionSupported),
		    XR_VERSION_MINOR(opengl_reqs->maxApiVersionSupported),
		    XR_VERSION_PATCH(opengl_reqs->maxApiVersionSupported));
		return false;
	}
	return true;
}

void
print_reference_spaces(xr_example* self)
{
	XrResult result;

	uint32_t ref_space_count;
	result = xrEnumerateReferenceSpaces(self->session, 0, &ref_space_count, NULL);
	if (!xr_result(self->instance, result, "Getting number of reference spaces failed!"))
		return;

	XrReferenceSpaceType ref_spaces[ref_space_count];
	result = xrEnumerateReferenceSpaces(self->session, ref_space_count, &ref_space_count, ref_spaces);
	if (!xr_result(self->instance, result, "Enumerating reference spaces failed!"))
		return;

	printf("Runtime supports %d reference spaces:\n", ref_space_count);
	for (uint32_t i = 0; i < ref_space_count; i++) {
		if (ref_spaces[i] == XR_REFERENCE_SPACE_TYPE_LOCAL) {
			printf("\tXR_REFERENCE_SPACE_TYPE_LOCAL\n");
		} else if (ref_spaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
			printf("\tXR_REFERENCE_SPACE_TYPE_STAGE\n");
		} else if (ref_spaces[i] == XR_REFERENCE_SPACE_TYPE_VIEW) {
			printf("\tXR_REFERENCE_SPACE_TYPE_VIEW\n");
		} else {
			printf("\tOther (extension?) refspace %u\\n", ref_spaces[i]);
		}
	}
}

int
init_openxr(xr_example* self)
{
	XrResult result;

	// --- Make sure runtime supports the OpenGL extension

	// xrEnumerate*() functions are usually called once with CapacityInput = 0.
	// The function will write the required amount into CountOutput. We then have
	// to allocate an array to hold CountOutput elements and call the function
	// with CountOutput as CapacityInput.
	uint32_t ext_count = 0;
	result = xrEnumerateInstanceExtensionProperties(NULL, 0, &ext_count, NULL);

	/* TODO: instance null will not be able to convert XrResult to string */
	if (!xr_result(NULL, result, "Failed to enumerate number of extension properties"))
		return 1;

	printf("Runtime supports %d extensions\n", ext_count);

	XrExtensionProperties extensionProperties[ext_count];
	for (uint16_t i = 0; i < ext_count; i++) {
		// we usually have to fill in the type (for validation) and set
		// next to NULL (or a pointer to an extension specific struct)
		extensionProperties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		extensionProperties[i].next = NULL;
	}

	result = xrEnumerateInstanceExtensionProperties(NULL, ext_count, &ext_count, extensionProperties);
	if (!xr_result(NULL, result, "Failed to enumerate extension properties"))
		return 1;

	bool opengl_ext = false;
	for (uint32_t i = 0; i < ext_count; i++) {
		printf("\t%s v%d\n", extensionProperties[i].extensionName,
		       extensionProperties[i].extensionVersion);
		if (strcmp(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, extensionProperties[i].extensionName) == 0) {
			opengl_ext = true;
		}
		if (strcmp(XR_EXT_HAND_TRACKING_EXTENSION_NAME, extensionProperties[i].extensionName) == 0) {
			self->hand_tracking_ext = true;
		}
	}

	// A graphics extension like OpenGL is required to draw anything in VR
	if (!opengl_ext) {
		printf("Runtime does not support OpenGL extension!\n");
		return 1;
	}

	printf("Runtime supports extensions:\n");
	printf("%s: %d\n", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, opengl_ext);
	printf("%s: %d\n", XR_EXT_HAND_TRACKING_EXTENSION_NAME, self->hand_tracking_ext);

	// --- Create XrInstance
	int enabled_ext_count = 1;
	const char* enabled_exts[2] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};

	if (self->hand_tracking_ext) {
		enabled_exts[enabled_ext_count++] = XR_EXT_HAND_TRACKING_EXTENSION_NAME;
	}

	// same can be done for API layers, but API layers can also be enabled by env var

	XrInstanceCreateInfo instance_create_info = {
	    .type = XR_TYPE_INSTANCE_CREATE_INFO,
	    .next = NULL,
	    .createFlags = 0,
	    .enabledExtensionCount = enabled_ext_count,
	    .enabledExtensionNames = enabled_exts,
	    .enabledApiLayerCount = 0,
	    .enabledApiLayerNames = NULL,
	    .applicationInfo =
	        {
	            // some compilers have trouble with char* initialization
	            .applicationName = "",
	            .engineName = "",
	            .applicationVersion = 1,
	            .engineVersion = 0,
	            .apiVersion = XR_CURRENT_API_VERSION,
	        },
	};
	strncpy(instance_create_info.applicationInfo.applicationName, "OpenXR OpenGL Example",
	        XR_MAX_APPLICATION_NAME_SIZE);
	strncpy(instance_create_info.applicationInfo.engineName, "Custom", XR_MAX_ENGINE_NAME_SIZE);

	result = xrCreateInstance(&instance_create_info, &self->instance);
	if (!xr_result(NULL, result, "Failed to create XR instance."))
		return 1;

	// Optionally get runtime name and version
	get_instance_properties(self->instance);

	// --- Create XrSystem
	XrSystemGetInfo system_get_info = {.type = XR_TYPE_SYSTEM_GET_INFO,
	                                   .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
	                                   .next = NULL};

	result = xrGetSystem(self->instance, &system_get_info, &self->system_id);
	if (!xr_result(self->instance, result, "Failed to get system for HMD form factor."))
		return 1;

	printf("Successfully got XrSystem with id %lu for HMD form factor\n", self->system_id);


	// checking system properties is generally  optional, but we are interested in hand tracking
	// support
	{
		XrSystemProperties system_props = {
		    .type = XR_TYPE_SYSTEM_PROPERTIES,
		    .next = NULL,
		    .graphicsProperties = {0},
		    .trackingProperties = {0},
		};

		XrSystemHandTrackingPropertiesEXT ht = {.type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
		                                        .next = NULL};
		if (self->hand_tracking_ext) {
			system_props.next = &ht;
		}

		result = xrGetSystemProperties(self->instance, self->system_id, &system_props);
		if (!xr_result(self->instance, result, "Failed to get System properties"))
			return 1;

		self->hand_tracking_supported = self->hand_tracking_ext && ht.supportsHandTracking;

		print_system_properties(&system_props, self->hand_tracking_ext);
	}

	print_supported_view_configs(self);
	// Stereo is most common for VR. We could check if stereo is supported and maybe choose another
	// one, but as this app is only tested with stereo, we assume it is (next call will error anyway
	// if not).
	XrViewConfigurationType view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	result = xrEnumerateViewConfigurationViews(self->instance, self->system_id, view_type, 0,
	                                           &self->view_count, NULL);
	if (!xr_result(self->instance, result, "Failed to get view configuration view count!"))
		return 1;

	self->viewconfig_views = malloc(sizeof(XrViewConfigurationView) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		self->viewconfig_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		self->viewconfig_views[i].next = NULL;
	}

	result = xrEnumerateViewConfigurationViews(self->instance, self->system_id, view_type,
	                                           self->view_count, &self->view_count,
	                                           self->viewconfig_views);
	if (!xr_result(self->instance, result, "Failed to enumerate view configuration views!"))
		return 1;
	print_viewconfig_view_info(self);


	// A stereo view config implies two views, but our code is set up for a dynamic amount of views.
	// So we need to allocate a bunch of memory for data structures dynamically.
	self->views = (XrView*)malloc(sizeof(XrView) * self->view_count);
	self->projection_views = (XrCompositionLayerProjectionView*)malloc(
	    sizeof(XrCompositionLayerProjectionView) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		self->views[i].type = XR_TYPE_VIEW;
		self->views[i].next = NULL;

		self->projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		self->projection_views[i].next = NULL;
	};



	// OpenXR requires checking graphics requirements before creating a session.
	XrGraphicsRequirementsOpenGLKHR opengl_reqs = {.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR,
	                                               .next = NULL};

	PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = NULL;
	{
		result = xrGetInstanceProcAddr(self->instance, "xrGetOpenGLGraphicsRequirementsKHR",
		                               (PFN_xrVoidFunction*)&pfnGetOpenGLGraphicsRequirementsKHR);
		if (!xr_result(self->instance, result, "Failed to get OpenGL graphics requirements function!"))
			return 1;
	}

	result = pfnGetOpenGLGraphicsRequirementsKHR(self->instance, self->system_id, &opengl_reqs);
	if (!xr_result(self->instance, result, "Failed to get OpenGL graphics requirements!"))
		return 1;

	// On OpenGL we never fail this check because the version requirement is not useful.
	// Other APIs may have more useful requirements.
	check_opengl_version(&opengl_reqs);


	// --- Create session
	self->graphics_binding_gl = (XrGraphicsBindingOpenGLXlibKHR){
	    .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
	};

	// create SDL window the size of the left eye & fill GL graphics binding info
	if (!init_sdl_window(&self->graphics_binding_gl.xDisplay, &self->graphics_binding_gl.visualid,
	                     &self->graphics_binding_gl.glxFBConfig,
	                     &self->graphics_binding_gl.glxDrawable,
	                     &self->graphics_binding_gl.glxContext,
	                     self->viewconfig_views[0].recommendedImageRectWidth,
	                     self->viewconfig_views[0].recommendedImageRectHeight)) {
		printf("GLX init failed!\n");
		return 1;
	}

	printf("Using OpenGL version: %s\n", glGetString(GL_VERSION));
	printf("Using OpenGL Renderer: %s\n", glGetString(GL_RENDERER));

	// Set up rendering (compile shaders, ...)
	if (init_gl() != 0) {
		printf("OpenGl setup failed!\n");
		return 1;
	}

	self->state = XR_SESSION_STATE_UNKNOWN;

	XrSessionCreateInfo session_create_info = {.type = XR_TYPE_SESSION_CREATE_INFO,
	                                           .next = &self->graphics_binding_gl,
	                                           .systemId = self->system_id};

	result = xrCreateSession(self->instance, &session_create_info, &self->session);
	if (!xr_result(self->instance, result, "Failed to create session"))
		return 1;

	printf("Successfully created a session with OpenGL!\n");

	if (self->hand_tracking_supported) {
		result = xrGetInstanceProcAddr(self->instance, "xrLocateHandJointsEXT",
		                               (PFN_xrVoidFunction*)&self->pfnLocateHandJointsEXT);

		xr_result(self->instance, result, "Failed to get xrLocateHandJointsEXT function!");

		PFN_xrCreateHandTrackerEXT pfnCreateHandTrackerEXT = NULL;
		result = xrGetInstanceProcAddr(self->instance, "xrCreateHandTrackerEXT",
		                               (PFN_xrVoidFunction*)&pfnCreateHandTrackerEXT);

		if (!xr_result(self->instance, result, "Failed to get xrCreateHandTrackerEXT function!"))
			return 1;

		{
			XrHandTrackerCreateInfoEXT hand_tracker_create_info = {
			    .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
			    .next = NULL,
			    .hand = XR_HAND_LEFT_EXT,
			    .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT};
			result = pfnCreateHandTrackerEXT(self->session, &hand_tracker_create_info,
			                                 &self->hand_trackers[0]);
			if (!xr_result(self->instance, result, "Failed to create left hand tracker")) {
				return 1;
			}
			printf("Created hand tracker for left hand\n");
		}
		{
			XrHandTrackerCreateInfoEXT hand_tracker_create_info = {
			    .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
			    .next = NULL,
			    .hand = XR_HAND_RIGHT_EXT,
			    .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT};
			result = pfnCreateHandTrackerEXT(self->session, &hand_tracker_create_info,
			                                 &self->hand_trackers[1]);
			if (!xr_result(self->instance, result, "Failed to create right hand tracker")) {
				return 1;
			}
			printf("Created hand tracker for right hand\n");
		}
	}

	XrReferenceSpaceType play_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
	// We could check if our ref space type is supported, but next call will error anyway if not
	print_reference_spaces(self);

	XrReferenceSpaceCreateInfo play_space_create_info = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
	                                                     .next = NULL,
	                                                     .referenceSpaceType = play_space_type,
	                                                     .poseInReferenceSpace = identity_pose};

	result = xrCreateReferenceSpace(self->session, &play_space_create_info, &self->play_space);
	if (!xr_result(self->instance, result, "Failed to create play space!"))
		return 1;

	// --- Begin session
	XrSessionBeginInfo session_begin_info = {
	    .type = XR_TYPE_SESSION_BEGIN_INFO, .next = NULL, .primaryViewConfigurationType = view_type};
	result = xrBeginSession(self->session, &session_begin_info);
	if (!xr_result(self->instance, result, "Failed to begin session!"))
		return 1;
	printf("Session started!\n");



	// --- Create Swapchains
	uint32_t swapchain_format_count;
	result = xrEnumerateSwapchainFormats(self->session, 0, &swapchain_format_count, NULL);
	if (!xr_result(self->instance, result, "Failed to get number of supported swapchain formats"))
		return 1;

	printf("Runtime supports %d swapchain formats\n", swapchain_format_count);
	int64_t swapchain_formats[swapchain_format_count];
	result = xrEnumerateSwapchainFormats(self->session, swapchain_format_count,
	                                     &swapchain_format_count, swapchain_formats);
	if (!xr_result(self->instance, result, "Failed to enumerate swapchain formats"))
		return 1;

	// SRGB is usually the best choice. Selection logic should be expanded though.
	int64_t preferred_swapchain_format = GL_SRGB8_ALPHA8_EXT;
	int64_t preferred_depth_swapchain_format = GL_DEPTH_COMPONENT32;
	int64_t preferred_quad_swapchain_format = GL_RGBA8_EXT;


	self->swapchain_format = swapchain_formats[0];
	self->depth_swapchain_format = -1;
	for (uint32_t i = 0; i < swapchain_format_count; i++) {
		printf("Supported GL format: %#lx\n", swapchain_formats[i]);
		if (swapchain_formats[i] == preferred_swapchain_format) {
			self->swapchain_format = swapchain_formats[i];
			printf("Using preferred swapchain format %#lx\n", self->swapchain_format);
		}
		if (swapchain_formats[i] == preferred_depth_swapchain_format) {
			self->depth_swapchain_format = swapchain_formats[i];
			printf("Using preferred depth swapchain format %#lx\n", self->depth_swapchain_format);
		}
		if (swapchain_formats[i] == preferred_quad_swapchain_format) {
			self->quad_swapchain_format = swapchain_formats[i];
			printf("Using preferred quad swapchain format %#lx\n", self->quad_swapchain_format);
		}
	}
	if (self->swapchain_format != preferred_swapchain_format) {
		printf("Using non preferred swapchain format %#lx\n", self->swapchain_format);
	}
	/* All OpenGL textures that will be submitted in xrEndFrame are created by the runtime here.
	 * The runtime will give us a number (not controlled by us) of OpenGL textures per swapchain
	 * and tell us with xrAcquireSwapchainImage, which of those we can render to per frame.
	 * Here we use one swapchain per view (eye), and for example 3 ("triple buffering") images per
	 * swapchain.
	 */
	self->swapchains = malloc(sizeof(XrSwapchain) * self->view_count);
	self->swapchain_lengths = malloc(sizeof(uint32_t) * self->view_count);
	self->images = malloc(sizeof(XrSwapchainImageOpenGLKHR*) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		XrSwapchainCreateInfo swapchain_create_info = {
		    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
		    .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
		    .createFlags = 0,
		    .format = self->swapchain_format,
		    .sampleCount = self->viewconfig_views[i].recommendedSwapchainSampleCount,
		    .width = self->viewconfig_views[i].recommendedImageRectWidth,
		    .height = self->viewconfig_views[i].recommendedImageRectHeight,
		    .faceCount = 1,
		    .arraySize = 1,
		    .mipCount = 1,
		    .next = NULL,
		};

		result = xrCreateSwapchain(self->session, &swapchain_create_info, &self->swapchains[i]);
		if (!xr_result(self->instance, result, "Failed to create swapchain %d!", i))
			return 1;

		result = xrEnumerateSwapchainImages(self->swapchains[i], 0, &self->swapchain_lengths[i], NULL);
		if (!xr_result(self->instance, result, "Failed to enumerate swapchains"))
			return 1;

		// these are wrappers for the actual OpenGL texture id
		self->images[i] = malloc(sizeof(XrSwapchainImageOpenGLKHR) * self->swapchain_lengths[i]);
		for (uint32_t j = 0; j < self->swapchain_lengths[i]; j++) {
			self->images[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			self->images[i][j].next = NULL;
		}
		result = xrEnumerateSwapchainImages(self->swapchains[i], self->swapchain_lengths[i],
		                                    &self->swapchain_lengths[i],
		                                    (XrSwapchainImageBaseHeader*)self->images[i]);
		if (!xr_result(self->instance, result, "Failed to enumerate swapchain images"))
			return 1;
	}

	/* Allocate resources that we use for our own rendering.
	 * We will bind framebuffers to the runtime provided textures for rendering.
	 * For this, we create one framebuffer per OpenGL texture.
	 * This is not mandated by OpenXR, other ways to render to textures will work too.
	 */
	self->framebuffers = malloc(sizeof(GLuint*) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		self->framebuffers[i] = malloc(sizeof(GLuint) * self->swapchain_lengths[i]);
		glGenFramebuffers(self->swapchain_lengths[i], self->framebuffers[i]);
	}

	if (self->depth_swapchain_format == -1) {
		// TODO: uh oh.
		printf("Preferred depth swapchain format %#lx not supported!\n",
		       preferred_depth_swapchain_format);
		return 1;
	}
	self->depth_swapchains = malloc(sizeof(XrSwapchain) * self->view_count);
	self->depth_swapchain_lengths = malloc(sizeof(uint32_t) * self->view_count);
	self->depth_images = malloc(sizeof(XrSwapchainImageOpenGLKHR*) * self->view_count);
	for (uint32_t i = 0; i < self->view_count; i++) {
		XrSwapchainCreateInfo swapchain_create_info = {
		    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
		    .usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		    .createFlags = 0,
		    .format = self->depth_swapchain_format,
		    .sampleCount = self->viewconfig_views[i].recommendedSwapchainSampleCount,
		    .width = self->viewconfig_views[i].recommendedImageRectWidth,
		    .height = self->viewconfig_views[i].recommendedImageRectHeight,
		    .faceCount = 1,
		    .arraySize = 1,
		    .mipCount = 1,
		    .next = NULL,
		};

		result = xrCreateSwapchain(self->session, &swapchain_create_info, &self->depth_swapchains[i]);
		if (!xr_result(self->instance, result, "Failed to create swapchain %d!", i))
			return 1;

		result = xrEnumerateSwapchainImages(self->depth_swapchains[i], 0,
		                                    &self->depth_swapchain_lengths[i], NULL);
		if (!xr_result(self->instance, result, "Failed to enumerate swapchains"))
			return 1;

		// these are wrappers for the actual OpenGL texture id
		self->depth_images[i] =
		    malloc(sizeof(XrSwapchainImageOpenGLKHR) * self->depth_swapchain_lengths[i]);
		for (uint32_t j = 0; j < self->depth_swapchain_lengths[i]; j++) {
			self->depth_images[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			self->depth_images[i][j].next = NULL;
		}
		result = xrEnumerateSwapchainImages(self->depth_swapchains[i], self->depth_swapchain_lengths[i],
		                                    &self->depth_swapchain_lengths[i],
		                                    (XrSwapchainImageBaseHeader*)self->depth_images[i]);
		if (!xr_result(self->instance, result, "Failed to enumerate swapchain images"))
			return 1;
	}

	self->quad_pixel_width = 800;
	self->quad_pixel_height = 600;
	{
		XrSwapchainCreateInfo swapchain_create_info = {
		    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
		    .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
		    .createFlags = 0,
		    .format = self->quad_swapchain_format,
		    .sampleCount = 1,
		    .width = self->quad_pixel_width,
		    .height = self->quad_pixel_height,
		    .faceCount = 1,
		    .arraySize = 1,
		    .mipCount = 1,
		    .next = NULL,
		};

		result = xrCreateSwapchain(self->session, &swapchain_create_info, &self->quad_swapchain);
		if (!xr_result(self->instance, result, "Failed to create swapchain!"))
			return 1;

		result =
		    xrEnumerateSwapchainImages(self->quad_swapchain, 0, &self->quad_swapchain_length, NULL);
		if (!xr_result(self->instance, result, "Failed to enumerate swapchains"))
			return 1;

		// these are wrappers for the actual OpenGL texture id
		self->quad_images = malloc(sizeof(XrSwapchainImageOpenGLKHR) * self->quad_swapchain_length);
		for (uint32_t j = 0; j < self->quad_swapchain_length; j++) {
			self->quad_images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			self->quad_images[j].next = NULL;
		}
		result = xrEnumerateSwapchainImages(self->quad_swapchain, self->quad_swapchain_length,
		                                    &self->quad_swapchain_length,
		                                    (XrSwapchainImageBaseHeader*)self->quad_images);
		if (!xr_result(self->instance, result, "Failed to enumerate swapchain images"))
			return 1;
	}

	return 0;
}

void
main_loop(xr_example* self)
{
	XrResult result;

	XrActionSetCreateInfo main_actionset_info = {
	    .type = XR_TYPE_ACTION_SET_CREATE_INFO, .next = NULL, .priority = 0};
	strcpy(main_actionset_info.actionSetName, "mainactions");
	strcpy(main_actionset_info.localizedActionSetName, "Main Actions");

	XrActionSet main_actionset;
	result = xrCreateActionSet(self->instance, &main_actionset_info, &main_actionset);
	if (!xr_result(self->instance, result, "failed to create actionset"))
		return;

	xrStringToPath(self->instance, "/user/hand/left", &self->hand_paths[HAND_LEFT]);
	xrStringToPath(self->instance, "/user/hand/right", &self->hand_paths[HAND_RIGHT]);

	XrAction grab_action_float;
	{
		XrActionCreateInfo action_info = {.type = XR_TYPE_ACTION_CREATE_INFO,
		                                  .next = NULL,
		                                  .actionType = XR_ACTION_TYPE_FLOAT_INPUT,
		                                  .countSubactionPaths = HAND_COUNT,
		                                  .subactionPaths = self->hand_paths};
		strcpy(action_info.actionName, "grabobjectfloat");
		strcpy(action_info.localizedActionName, "Grab Object");

		result = xrCreateAction(main_actionset, &action_info, &grab_action_float);
		if (!xr_result(self->instance, result, "failed to create grab action"))
			return;
	}

	// just an example that could sensibly use one axis of e.g. a thumbstick
	XrAction throttle_action_float;
	{
		XrActionCreateInfo action_info = {.type = XR_TYPE_ACTION_CREATE_INFO,
		                                  .next = NULL,
		                                  .actionType = XR_ACTION_TYPE_FLOAT_INPUT,
		                                  .countSubactionPaths = HAND_COUNT,
		                                  .subactionPaths = self->hand_paths};
		strcpy(action_info.actionName, "throttle");
		strcpy(action_info.localizedActionName, "Use Throttle forward/backward");

		result = xrCreateAction(main_actionset, &action_info, &throttle_action_float);
		if (!xr_result(self->instance, result, "failed to create throttle action"))
			return;
	}

	XrAction pose_action;
	{
		XrActionCreateInfo action_info = {.type = XR_TYPE_ACTION_CREATE_INFO,
		                                  .next = NULL,
		                                  .actionType = XR_ACTION_TYPE_POSE_INPUT,
		                                  .countSubactionPaths = HAND_COUNT,
		                                  .subactionPaths = self->hand_paths};
		strcpy(action_info.actionName, "handpose");
		strcpy(action_info.localizedActionName, "Hand Pose");

		result = xrCreateAction(main_actionset, &action_info, &pose_action);
		if (!xr_result(self->instance, result, "failed to create pose action"))
			return;
	}

	XrAction haptic_action;
	{
		XrActionCreateInfo action_info = {.type = XR_TYPE_ACTION_CREATE_INFO,
		                                  .next = NULL,
		                                  .actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT,
		                                  .countSubactionPaths = HAND_COUNT,
		                                  .subactionPaths = self->hand_paths};
		strcpy(action_info.actionName, "haptic");
		strcpy(action_info.localizedActionName, "Haptic Vibration");
		result = xrCreateAction(main_actionset, &action_info, &haptic_action);
		if (!xr_result(self->instance, result, "failed to create haptic action"))
			return;
	}

	XrPath select_click_path[HAND_COUNT];
	xrStringToPath(self->instance, "/user/hand/left/input/select/click",
	               &select_click_path[HAND_LEFT]);
	xrStringToPath(self->instance, "/user/hand/right/input/select/click",
	               &select_click_path[HAND_RIGHT]);

	XrPath trigger_value_path[HAND_COUNT];
	xrStringToPath(self->instance, "/user/hand/left/input/trigger/value",
	               &trigger_value_path[HAND_LEFT]);
	xrStringToPath(self->instance, "/user/hand/right/input/trigger/value",
	               &trigger_value_path[HAND_RIGHT]);

	XrPath thumbstick_y_path[HAND_COUNT];
	xrStringToPath(self->instance, "/user/hand/left/input/thumbstick/y",
	               &thumbstick_y_path[HAND_LEFT]);
	xrStringToPath(self->instance, "/user/hand/right/input/thumbstick/y",
	               &thumbstick_y_path[HAND_RIGHT]);

	XrPath grip_pose_path[HAND_COUNT];
	xrStringToPath(self->instance, "/user/hand/left/input/grip/pose", &grip_pose_path[HAND_LEFT]);
	xrStringToPath(self->instance, "/user/hand/right/input/grip/pose", &grip_pose_path[HAND_RIGHT]);

	XrPath haptic_path[HAND_COUNT];
	xrStringToPath(self->instance, "/user/hand/left/output/haptic", &haptic_path[HAND_LEFT]);
	xrStringToPath(self->instance, "/user/hand/right/output/haptic", &haptic_path[HAND_RIGHT]);

	{
		XrPath interaction_profile_path;
		result = xrStringToPath(self->instance, "/interaction_profiles/khr/simple_controller",
		                        &interaction_profile_path);
		if (!xr_result(self->instance, result, "failed to get interaction profile"))
			return;

		const XrActionSuggestedBinding bindings[] = {
		    {.action = pose_action, .binding = grip_pose_path[HAND_LEFT]},
		    {.action = pose_action, .binding = grip_pose_path[HAND_RIGHT]},
		    {.action = grab_action_float, .binding = select_click_path[HAND_LEFT]},
		    {.action = grab_action_float, .binding = select_click_path[HAND_RIGHT]},
		    {.action = haptic_action, .binding = haptic_path[HAND_LEFT]},
		    {.action = haptic_action, .binding = haptic_path[HAND_RIGHT]},
		};

		const XrInteractionProfileSuggestedBinding suggested_bindings = {
		    .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
		    .next = NULL,
		    .interactionProfile = interaction_profile_path,
		    .countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]),
		    .suggestedBindings = bindings};

		xrSuggestInteractionProfileBindings(self->instance, &suggested_bindings);
		if (!xr_result(self->instance, result, "failed to suggest bindings"))
			return;
	}

	{
		XrPath interaction_profile_path;
		result = xrStringToPath(self->instance, "/interaction_profiles/valve/index_controller",
		                        &interaction_profile_path);
		if (!xr_result(self->instance, result, "failed to get interaction profile"))
			return;

		const XrActionSuggestedBinding bindings[] = {
		    {.action = pose_action, .binding = grip_pose_path[HAND_LEFT]},
		    {.action = pose_action, .binding = grip_pose_path[HAND_RIGHT]},
		    {.action = grab_action_float, .binding = trigger_value_path[HAND_LEFT]},
		    {.action = grab_action_float, .binding = trigger_value_path[HAND_RIGHT]},
		    {.action = throttle_action_float, .binding = thumbstick_y_path[HAND_LEFT]},
		    {.action = throttle_action_float, .binding = thumbstick_y_path[HAND_RIGHT]},
		    {.action = haptic_action, .binding = haptic_path[HAND_LEFT]},
		    {.action = haptic_action, .binding = haptic_path[HAND_RIGHT]},
		};

		const XrInteractionProfileSuggestedBinding suggested_bindings = {
		    .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
		    .next = NULL,
		    .interactionProfile = interaction_profile_path,
		    .countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]),
		    .suggestedBindings = bindings};

		xrSuggestInteractionProfileBindings(self->instance, &suggested_bindings);
		if (!xr_result(self->instance, result, "failed to suggest bindings"))
			return;
	}

	// poses can't be queried directly, we need to create a space for each
	XrSpace pose_action_spaces[HAND_COUNT];
	{
		XrActionSpaceCreateInfo action_space_info = {.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
		                                             .next = NULL,
		                                             .action = pose_action,
		                                             .poseInActionSpace = identity_pose,
		                                             .subactionPath = self->hand_paths[HAND_LEFT]};

		result = xrCreateActionSpace(self->session, &action_space_info, &pose_action_spaces[HAND_LEFT]);
		if (!xr_result(self->instance, result, "failed to create left hand pose space"))
			return;
	}
	{
		XrActionSpaceCreateInfo action_space_info = {.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
		                                             .next = NULL,
		                                             .action = pose_action,
		                                             .poseInActionSpace = identity_pose,
		                                             .subactionPath = self->hand_paths[HAND_RIGHT]};

		result =
		    xrCreateActionSpace(self->session, &action_space_info, &pose_action_spaces[HAND_RIGHT]);
		if (!xr_result(self->instance, result, "failed to create left hand pose space"))
			return;
	}

	XrSessionActionSetsAttachInfo actionset_attach_info = {
	    .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
	    .next = NULL,
	    .countActionSets = 1,
	    .actionSets = &main_actionset};
	result = xrAttachSessionActionSets(self->session, &actionset_attach_info);
	if (!xr_result(self->instance, result, "failed to attach action set"))
		return;



	while (true) {

		// --- Poll SDL for events so we can exit with esc
		SDL_Event sdl_event;
		bool sdl_should_exit = false;
		while (SDL_PollEvent(&sdl_event)) {
			sdl_handle_events(sdl_event, &sdl_should_exit);
		}
		if (sdl_should_exit) {
			printf("Requesting exit...\n");
			xrRequestExitSession(self->session);
		}


		bool session_stopping = false;

		// --- Handle runtime Events
		// we do this before xrWaitFrame() so we can go idle or
		// break out of the main render loop as early as possible and don't have to
		// uselessly render or submit one. Calling xrWaitFrame commits you to
		// calling xrBeginFrame eventually.
		XrEventDataBuffer runtime_event = {.type = XR_TYPE_EVENT_DATA_BUFFER, .next = NULL};
		XrResult poll_result = xrPollEvent(self->instance, &runtime_event);
		while (poll_result == XR_SUCCESS) {
			switch (runtime_event.type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
				XrEventDataEventsLost* event = (XrEventDataEventsLost*)&runtime_event;
				printf("EVENT: %d events data lost!\n", event->lostEventCount);
				// do we care if the runtime loses events?
				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				XrEventDataInstanceLossPending* event = (XrEventDataInstanceLossPending*)&runtime_event;
				printf("EVENT: instance loss pending at %lu! Destroying instance.\n", event->lossTime);
				session_stopping = true;
				break;
			}
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				XrEventDataSessionStateChanged* event = (XrEventDataSessionStateChanged*)&runtime_event;
				printf("EVENT: session state changed from %d to %d\n", self->state, event->state);

				self->state = event->state;

				if (event->state >= XR_SESSION_STATE_STOPPING) {
					printf("Session is stopping...\n");
					// still handle rest of the events instead of immediately quitting
					session_stopping = true;
				}
				break;
			}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				printf("EVENT: reference space change pending!\n");
				XrEventDataReferenceSpaceChangePending* event =
				    (XrEventDataReferenceSpaceChangePending*)&runtime_event;
				(void)event;
				// TODO: do something
				break;
			}
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				printf("EVENT: interaction profile changed!\n");
				XrEventDataInteractionProfileChanged* event =
				    (XrEventDataInteractionProfileChanged*)&runtime_event;
				(void)event;

				XrInteractionProfileState state = {.type = XR_TYPE_INTERACTION_PROFILE_STATE};

				for (int i = 0; i < 2; i++) {
					XrResult res = xrGetCurrentInteractionProfile(self->session, self->hand_paths[i], &state);
					if (!xr_result(self->instance, res, "Failed to get interaction profile for %d", i))
						continue;

					XrPath prof = state.interactionProfile;

					uint32_t strl;
					char profile_str[XR_MAX_PATH_LENGTH];
					res = xrPathToString(self->instance, prof, XR_MAX_PATH_LENGTH, &strl, profile_str);
					if (!xr_result(self->instance, res, "Failed to get interaction profile path str for %s",
					               h_p_str(i)))
						continue;

					printf("Event: Interaction profile changed for %s: %s\n", h_p_str(i), profile_str);
				}
				// TODO: do something
				break;
			}

			case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR: {
				printf("EVENT: visibility mask changed!!\n");
				XrEventDataVisibilityMaskChangedKHR* event =
				    (XrEventDataVisibilityMaskChangedKHR*)&runtime_event;
				(void)event;
				// this event is from an extension
				break;
			}
			case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
				printf("EVENT: perf settings!\n");
				XrEventDataPerfSettingsEXT* event = (XrEventDataPerfSettingsEXT*)&runtime_event;
				(void)event;
				// this event is from an extension
				break;
			}
			default: printf("Unhandled event type %d\n", runtime_event.type);
			}

			runtime_event.type = XR_TYPE_EVENT_DATA_BUFFER;
			poll_result = xrPollEvent(self->instance, &runtime_event);
		}
		if (poll_result == XR_EVENT_UNAVAILABLE) {
			// processed all events in the queue
		} else {
			printf("Failed to poll events!\n");
			break;
		}

		if (session_stopping) {
			printf("Quitting main render loop\n");
			return;
		}

		// --- Wait for our turn to do head-pose dependent computation and render a frame
		XrFrameState frameState = {.type = XR_TYPE_FRAME_STATE, .next = NULL};
		XrFrameWaitInfo frameWaitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO, .next = NULL};
		result = xrWaitFrame(self->session, &frameWaitInfo, &frameState);
		if (!xr_result(self->instance, result, "xrWaitFrame() was not successful, exiting..."))
			break;


		XrHandJointLocationEXT joints[2][XR_HAND_JOINT_COUNT_EXT];
		XrHandJointLocationsEXT joint_locations[2] = {{0}};
		if (self->hand_tracking_supported) {

			for (int i = 0; i < 2; i++) {

				joint_locations[i] = (XrHandJointLocationsEXT){
				    .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
				    .jointCount = XR_HAND_JOINT_COUNT_EXT,
				    .jointLocations = joints[i],
				};

				if (self->hand_trackers[i] == NULL)
					continue;

				XrHandJointsLocateInfoEXT locateInfo = {.type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
				                                        .next = NULL,
				                                        .baseSpace = self->play_space,
				                                        .time = frameState.predictedDisplayTime};

				result =
				    self->pfnLocateHandJointsEXT(self->hand_trackers[i], &locateInfo, &joint_locations[i]);
				if (!xr_result(self->instance, result, "failed to locate hand %d joints!", i))
					break;

				/*
				if (joint_locations[i].isActive) {
				  printf("located hand %d joints", i);
				  for (uint32_t j = 0; j < joint_locations[i].jointCount; j++) {
				    printf("%f ", joint_locations[i].jointLocations[j].radius);
				  }
				  printf("\n");
				} else {
				  printf("hand %d joints inactive\n", i);
				}
				*/
			}
		}

		// --- Create projection matrices and view matrices for each eye
		XrViewLocateInfo view_locate_info = {.type = XR_TYPE_VIEW_LOCATE_INFO,
		                                     .next = NULL,
		                                     .viewConfigurationType =
		                                         XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		                                     .displayTime = frameState.predictedDisplayTime,
		                                     .space = self->play_space};

		XrView views[self->view_count];
		for (uint32_t i = 0; i < self->view_count; i++) {
			views[i].type = XR_TYPE_VIEW;
			views[i].next = NULL;
		};

		XrViewState view_state = {.type = XR_TYPE_VIEW_STATE, .next = NULL};
		uint32_t view_count;
		result = xrLocateViews(self->session, &view_locate_info, &view_state, self->view_count,
		                       &view_count, views);
		if (!xr_result(self->instance, result, "Could not locate views"))
			break;

		//! @todo Move this action processing to before xrWaitFrame, probably.
		const XrActiveActionSet active_actionsets[] = {
		    {.actionSet = main_actionset, .subactionPath = XR_NULL_PATH}};

		XrActionsSyncInfo actions_sync_info = {
		    .type = XR_TYPE_ACTIONS_SYNC_INFO,
		    .countActiveActionSets = sizeof(active_actionsets) / sizeof(active_actionsets[0]),
		    .activeActionSets = active_actionsets,
		};
		result = xrSyncActions(self->session, &actions_sync_info);
		xr_result(self->instance, result, "failed to sync actions!");

		// query each value / location with a subaction path != XR_NULL_PATH
		// resulting in individual values per hand/.
		XrActionStateFloat grab_value[HAND_COUNT];
		XrActionStateFloat throttle_value[HAND_COUNT];
		XrSpaceLocation hand_locations[HAND_COUNT];
		bool hand_locations_valid[HAND_COUNT];

		for (int i = 0; i < HAND_COUNT; i++) {
			XrActionStatePose pose_state = {.type = XR_TYPE_ACTION_STATE_POSE, .next = NULL};
			{
				XrActionStateGetInfo get_info = {.type = XR_TYPE_ACTION_STATE_GET_INFO,
				                                 .next = NULL,
				                                 .action = pose_action,
				                                 .subactionPath = self->hand_paths[i]};
				result = xrGetActionStatePose(self->session, &get_info, &pose_state);
				xr_result(self->instance, result, "failed to get pose value!");
			}
			// printf("Hand pose %d active: %d\n", i, poseState.isActive);

			hand_locations[i].type = XR_TYPE_SPACE_LOCATION;
			hand_locations[i].next = NULL;

			result = xrLocateSpace(pose_action_spaces[i], self->play_space,
			                       frameState.predictedDisplayTime, &hand_locations[i]);
			xr_result(self->instance, result, "failed to locate space %d!", i);
			hand_locations_valid[i] =
			    //(spaceLocation[i].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			    (hand_locations[i].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

			/*
			printf("Pose %d valid %d: %f %f %f %f, %f %f %f\n", i,
			spaceLocationValid[i], spaceLocation[0].pose.orientation.x,
			spaceLocation[0].pose.orientation.y, spaceLocation[0].pose.orientation.z,
			spaceLocation[0].pose.orientation.w, spaceLocation[0].pose.position.x,
			spaceLocation[0].pose.position.y, spaceLocation[0].pose.position.z
			);
			*/

			grab_value[i].type = XR_TYPE_ACTION_STATE_FLOAT;
			grab_value[i].next = NULL;
			{
				XrActionStateGetInfo get_info = {.type = XR_TYPE_ACTION_STATE_GET_INFO,
				                                 .next = NULL,
				                                 .action = grab_action_float,
				                                 .subactionPath = self->hand_paths[i]};

				result = xrGetActionStateFloat(self->session, &get_info, &grab_value[i]);
				xr_result(self->instance, result, "failed to get grab value!");
			}

			// printf("Grab %d active %d, current %f, changed %d\n", i,
			// grabValue[i].isActive, grabValue[i].currentState,
			// grabValue[i].changedSinceLastSync);

			if (grab_value[i].isActive && grab_value[i].currentState > 0.75) {
				XrHapticVibration vibration = {.type = XR_TYPE_HAPTIC_VIBRATION,
				                               .next = NULL,
				                               .amplitude = 0.5,
				                               .duration = XR_MIN_HAPTIC_DURATION,
				                               .frequency = XR_FREQUENCY_UNSPECIFIED};

				XrHapticActionInfo haptic_action_info = {.type = XR_TYPE_HAPTIC_ACTION_INFO,
				                                         .next = NULL,
				                                         .action = haptic_action,
				                                         .subactionPath = self->hand_paths[i]};
				result = xrApplyHapticFeedback(self->session, &haptic_action_info,
				                               (const XrHapticBaseHeader*)&vibration);
				xr_result(self->instance, result, "failed to apply haptic feedback!");
				// printf("Sent haptic output to hand %d\n", i);
			}


			throttle_value[i].type = XR_TYPE_ACTION_STATE_FLOAT;
			throttle_value[i].next = NULL;
			{
				XrActionStateGetInfo get_info = {.type = XR_TYPE_ACTION_STATE_GET_INFO,
				                                 .next = NULL,
				                                 .action = throttle_action_float,
				                                 .subactionPath = self->hand_paths[i]};

				result = xrGetActionStateFloat(self->session, &get_info, &throttle_value[i]);
				xr_result(self->instance, result, "failed to get throttle value!");
			}
			if (throttle_value[i].isActive && throttle_value[i].currentState != 0) {
				printf("Throttle value %d: changed %d: %f\n", i, throttle_value[i].changedSinceLastSync,
				       throttle_value[i].currentState);
			}
		};

		// --- Begin frame
		XrFrameBeginInfo frame_begin_info = {.type = XR_TYPE_FRAME_BEGIN_INFO, .next = NULL};

		result = xrBeginFrame(self->session, &frame_begin_info);
		if (!xr_result(self->instance, result, "failed to begin frame!"))
			break;


		// render each eye and fill projection_views with the result
		for (uint32_t i = 0; i < self->view_count; i++) {
			XrMatrix4x4f projection_matrix;
			XrMatrix4x4f_CreateProjectionFov(&projection_matrix, GRAPHICS_OPENGL, views[i].fov, 0.05f,
			                                 100.0f);

			XrMatrix4x4f view_matrix;
			XrMatrix4x4f_CreateViewMatrix(&view_matrix, &views[i].pose.position,
			                              &views[i].pose.orientation);

			XrSwapchainImageAcquireInfo acquire_info = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
			                                            .next = NULL};
			uint32_t acquired_index;
			result = xrAcquireSwapchainImage(self->swapchains[i], &acquire_info, &acquired_index);
			if (!xr_result(self->instance, result, "failed to acquire swapchain image!"))
				break;

			XrSwapchainImageWaitInfo wait_info = {
			    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .next = NULL, .timeout = 1000};
			result = xrWaitSwapchainImage(self->swapchains[i], &wait_info);
			if (!xr_result(self->instance, result, "failed to wait for swapchain image!"))
				break;


			XrSwapchainImageAcquireInfo depth_acquire_info = {
			    .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, .next = NULL};
			uint32_t depth_acquired_index;
			result = xrAcquireSwapchainImage(self->depth_swapchains[i], &depth_acquire_info,
			                                 &depth_acquired_index);
			if (!xr_result(self->instance, result, "failed to acquire swapchain image!"))
				break;

			XrSwapchainImageWaitInfo depth_wait_info = {
			    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .next = NULL, .timeout = 1000};
			result = xrWaitSwapchainImage(self->depth_swapchains[i], &depth_wait_info);
			if (!xr_result(self->instance, result, "failed to wait for swapchain image!"))
				break;


			self->projection_views[i].pose = views[i].pose;
			self->projection_views[i].fov = views[i].fov;
			self->projection_views[i].subImage.swapchain = self->swapchains[i];
			self->projection_views[i].subImage.imageArrayIndex = 0;
			self->projection_views[i].subImage.imageRect.offset.x = 0;
			self->projection_views[i].subImage.imageRect.offset.y = 0;
			self->projection_views[i].subImage.imageRect.extent.width =
			    self->viewconfig_views[i].recommendedImageRectWidth;
			self->projection_views[i].subImage.imageRect.extent.height =
			    self->viewconfig_views[i].recommendedImageRectHeight;

			render_frame(self->viewconfig_views[i].recommendedImageRectWidth,
			             self->viewconfig_views[i].recommendedImageRectHeight, projection_matrix,
			             view_matrix, hand_locations, hand_locations_valid, joint_locations,
			             self->framebuffers[i][acquired_index],
			             self->depth_images[i][depth_acquired_index].image,
			             self->images[i][acquired_index], i, frameState.predictedDisplayTime);
			glFinish();
			XrSwapchainImageReleaseInfo release_info = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
			                                            .next = NULL};
			result = xrReleaseSwapchainImage(self->swapchains[i], &release_info);
			if (!xr_result(self->instance, result, "failed to release swapchain image!"))
				break;

			XrSwapchainImageReleaseInfo depth_release_info = {
			    .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, .next = NULL};
			result = xrReleaseSwapchainImage(self->depth_swapchains[i], &depth_release_info);
			if (!xr_result(self->instance, result, "failed to release swapchain image!"))
				break;
		}

		XrSwapchainImageAcquireInfo acquire_info = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
		                                            .next = NULL};
		uint32_t acquired_index;
		result = xrAcquireSwapchainImage(self->quad_swapchain, &acquire_info, &acquired_index);
		if (!xr_result(self->instance, result, "failed to acquire swapchain image!"))
			break;

		XrSwapchainImageWaitInfo wait_info = {
		    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .next = NULL, .timeout = 1000};
		result = xrWaitSwapchainImage(self->quad_swapchain, &wait_info);
		if (!xr_result(self->instance, result, "failed to wait for swapchain image!"))
			break;

		render_quad(self->quad_pixel_width, self->quad_pixel_height, self->swapchain_format,
		            self->quad_images[acquired_index], frameState.predictedDisplayTime);

		XrSwapchainImageReleaseInfo release_info = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
		                                            .next = NULL};
		result = xrReleaseSwapchainImage(self->quad_swapchain, &release_info);
		if (!xr_result(self->instance, result, "failed to release swapchain image!"))
			break;


		// projectionLayers struct reused for every frame
		XrCompositionLayerProjection projection_layer = {
		    .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
		    .next = NULL,
		    .layerFlags = 0,
		    .space = self->play_space,
		    .viewCount = self->view_count,
		    .views = self->projection_views,
		};

		float aspect = self->projection_views[0].subImage.imageRect.extent.width /
		               self->projection_views[0].subImage.imageRect.extent.width;
		XrCompositionLayerQuad quad_layer = {
		    .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
		    .next = NULL,
		    .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
		    .space = self->play_space,
		    .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
		    .pose = {.orientation = {.x = 0.f, .y = 0.f, .z = 0.f, .w = 1.f},
		             .position = {.x = 1.f, .y = 1.f, .z = -15.f}},
		    .size = {.width = (float)self->quad_pixel_width * 0.005f,
		             .height = quad_layer.size.width / aspect},
		    .subImage = {
		        .swapchain = self->quad_swapchain,
		        .imageRect = {
		            .offset = {.x = 0, .y = 0},
		            .extent = {.width = self->quad_pixel_width, .height = self->quad_pixel_height},
		        }}};

		const XrCompositionLayerBaseHeader* const submittedLayers[] = {
		    (const XrCompositionLayerBaseHeader* const) & projection_layer,
		    (const XrCompositionLayerBaseHeader* const) & quad_layer};
		XrFrameEndInfo frameEndInfo = {.type = XR_TYPE_FRAME_END_INFO,
		                               .displayTime = frameState.predictedDisplayTime,
		                               .layerCount =
		                                   sizeof(submittedLayers) / sizeof(submittedLayers[0]),
		                               .layers = submittedLayers,
		                               .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		                               .next = NULL};
		result = xrEndFrame(self->session, &frameEndInfo);
		if (!xr_result(self->instance, result, "failed to end frame!"))
			break;
	}
}

void
sdl_handle_events(SDL_Event event, bool* request_exit)
{
	if (event.type == SDL_QUIT ||
	    (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
		*request_exit = true;
	}
}

void
cleanup(xr_example* self)
{
	XrResult result;

	xrEndSession(self->session);

	if (self->hand_tracking_supported) {
		PFN_xrDestroyHandTrackerEXT pfnDestroyHandTrackerEXT = NULL;
		result = xrGetInstanceProcAddr(self->instance, "xrDestroyHandTrackerEXT",
		                               (PFN_xrVoidFunction*)&pfnDestroyHandTrackerEXT);

		xr_result(self->instance, result, "Failed to get xrDestroyHandTrackerEXT function!");

		if (self->hand_trackers[0]) {
			result = pfnDestroyHandTrackerEXT(self->hand_trackers[0]);
			if (xr_result(self->instance, result, "Failed to destroy left hand tracker")) {
				printf("Destroyed hand tracker for left hand\n");
			}
		}
		if (self->hand_trackers[1]) {
			result = pfnDestroyHandTrackerEXT(self->hand_trackers[1]);
			if (xr_result(self->instance, result, "Failed to destroy left hand tracker")) {
				printf("Destroyed hand tracker for left hand\n");
			}
		}
	}

	xrDestroySession(self->session);
	xrDestroyInstance(self->instance);

	for (uint32_t i = 0; i < self->view_count; i++) {
		free(self->images[i]);
		free(self->depth_images[i]);

		glDeleteFramebuffers(self->swapchain_lengths[i], self->framebuffers[i]);
		free(self->framebuffers[i]);
	}

	free(self->viewconfig_views);
	free(self->projection_views);
	free(self->views);
	free(self->swapchains);
	free(self->depth_swapchains);
	free(self->images);
	free(self->depth_images);
	free(self->framebuffers);
	free(self->swapchain_lengths);
	free(self->depth_swapchain_lengths);

	cleanup_gl();
}

int
main()
{
	xr_example self = {
	    .instance = XR_NULL_HANDLE,
	};
	int ret = init_openxr(&self);
	if (ret != 0)
		return ret;
	main_loop(&self);
	cleanup(&self);
	return 0;
}
