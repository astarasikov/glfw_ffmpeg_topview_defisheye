#include <stdio.h>
#include <stdlib.h>

#include "opengl_common.h"
#include "opengl_utils.h"
#include "defish_app.h"

//#define SHOW_IMAGE

#if defined(__APPLE__)
	#define PREVIEW_WIDTH 640
	#define PREVIEW_HEIGHT 480
#else
	#define PREVIEW_WIDTH 1280
	#define PREVIEW_HEIGHT 960
#endif

static void glfw_error_callback(int error, const char *description)
{
	fprintf(stderr, "GL error [%d]: '%s'\n", error, description);
}

int main(void) {
		/**
		 * Initialize FFMPEG source
		 */

		InitializeDecoders();

		/**
		 * Initialize GStreamer server
		 */
		InitializeGStreamerServer();

		/**
		 * Create OpenGL Core Profile (3.2) context
		 */

        glfwInit();
		glfwSetErrorCallback(glfw_error_callback);
        glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
#ifndef SHOW_IMAGE
        glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
#endif
        glfwWindowHint(GLFW_ALPHA_BITS, 0);
        glfwWindowHint(GLFW_DEPTH_BITS, 0);
        glfwWindowHint(GLFW_STENCIL_BITS, 0);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        GLFWwindow* window = glfwCreateWindow(PREVIEW_WIDTH, PREVIEW_HEIGHT,
                "OpenGL", NULL, NULL);
        glfwMakeContextCurrent(window);

		/**
		 * At this point we can invoke the actual processing pipeline
		 */

#ifdef SHOW_IMAGE
        while (!glfwWindowShouldClose(window)) {
				ogl(glViewport(0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT));
				RenderPipelineWithGL();
                glfwSwapBuffers(window);
                glfwPollEvents();
        }
#endif
        glfwTerminate();

		/**
		 * Wait for the FFMPEG decoders to terminate and cleanup
		 */
		WaitAndReleaseDecoders();

		/**
		 * Wait for the GStreamer to terminate and cleanup
		 */
		WaitAndReleaseGStreamerServer();
        return 0;
}
