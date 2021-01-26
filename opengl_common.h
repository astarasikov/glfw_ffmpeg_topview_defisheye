#ifndef __OPENGL_COMMON__H__
#define __OPENGL_COMMON__H__

#if defined(__APPLE__)
	#include <OpenGL/gl3.h>
#else
	#define GL_GLEXT_PROTOTYPES 1
	#include <GL/gl.h>
	#include <GL/glext.h>
#endif

#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#endif //__OPENGL_COMMON__H__
