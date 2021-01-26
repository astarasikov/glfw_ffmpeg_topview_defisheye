#ifndef __OPENGL_SHADERS__H__
#define __OPENGL_SHADERS__H__

#include "opengl_common.h"

#define SHADER_QUOTE(A) #A
#define GLSL_VERSION "#version 150 core\n"

const char * const FRAG_PROCESS_CAMERA = GLSL_VERSION SHADER_QUOTE(
	in vec3 vert_texcoord;
	out vec4 out_color;

	struct sParams {
		vec2 lensCentre;
		vec2 postScale;
		vec2 trapezeROI[4];
		float strength;
		float zoom;
		float aspectRatio;
	};
	uniform sParams Params;

	uniform sampler2D texture_Y;
	uniform sampler2D texture_U;
	uniform sampler2D texture_V;

	vec2 defisheye_web(vec2 in_texcoord)
	{
		//translate to origin so that center is 0,0
		//const float aspect = (1280 / 960.0);
		//vec2 lensCentre = vec2(-0.15, -0.15);

		float aspect = Params.aspectRatio;
		vec2 lensCentre = Params.lensCentre;

		//translate to the center. maps [0, 1] -> [-0.5, 0.5]
		const vec2 origin = vec2(0.5, 0.5);

		//map [-0.5, 0.5] -> [-1.0, 1.0]
		//also, correct the aspect ratio
		vec2 tc = 2.0 * (in_texcoord * vec2 (1.0, aspect) - origin);

		float strength = Params.strength;
		float zoom = Params.zoom;

		vec2 vd = tc - lensCentre;
		float r = sqrt(dot(vd, vd)) / strength;

		float theta = 1.0;
		if (abs(r) > 0.0) {
			theta = atan(r) / r;
		}

		//map back from [-1.0, 1.0] to [-0.5, 0.5]
		vec2 ret = 0.5 * (tc * theta * zoom);
		ret *= Params.postScale;
		return ret + origin;
	}

	vec3 sample_yuv(vec2 xvert_texcoord)
	{
		vec3 yuv;

		yuv.x = texture(texture_Y, xvert_texcoord).r - 0.0625;
		yuv.y = texture(texture_U, xvert_texcoord).r - 0.5;
		yuv.z = texture(texture_V, xvert_texcoord).r - 0.5;

		mat3 yuv2rgb = mat3(
			1.164, 1.164, 1.164,
			0, -0.391, 2.018,
			1.596, -0.813, 0
		);
		return yuv2rgb * yuv;
	}

	vec2 map_to_quad(vec2 coord)
	{
		/**
		 * For verifying that the function works correctly,
		 * first test with the identity quad
		const vec2 p0 = vec2(0, 0);
		const vec2 p1 = vec2(1, 0);
		const vec2 p2 = vec2(1, 1);
		const vec2 p3 = vec2(0, 1);
		 */

		vec2 p0 = Params.trapezeROI[0];
		vec2 p1 = Params.trapezeROI[1];
		vec2 p2 = Params.trapezeROI[2];
		vec2 p3 = Params.trapezeROI[3];

		vec2 dp1 = p1 - p2;
		vec2 dp2 = p3 - p2;
		vec2 s = p0 - p1 + p2 - p3;

		float g = (s.x * dp2.y - s.y * dp2.x) / (dp1.x * dp2.y - dp1.y * dp2.x);
		float h = (dp1.x * s.y - dp1.y * s.x) / (dp1.x * dp2.y - dp1.y * dp2.x);
		float a = p1.x - p0.x + g * p1.x;
		float b = p3.x - p0.x + h * p3.x;
		float c = p0.x;
		float d = p1.y - p0.y + g * p1.y;
		float e = p3.y - p0.y + h * p3.y;
		float f = p0.y;
		float i = 1.0;

		mat3 mapping = mat3(
				a, d, g,
				b, e, h,
				c, f, i);

		vec3 coord_hom = vec3(coord.xy, 1.0);
		vec3 mapped_hom = mapping * coord_hom;
		return mapped_hom.xy / mapped_hom.z;
	}

	void main(void) {
		vec2 nvert_texcoord = vert_texcoord.xy;
		nvert_texcoord = map_to_quad(nvert_texcoord);
		nvert_texcoord = defisheye_web(nvert_texcoord);
		vec3 rgb = sample_yuv(nvert_texcoord);
		out_color = vec4(rgb, 1.0);
	}
);

const char * const VERT_PASSTHRU = GLSL_VERSION SHADER_QUOTE(
	in vec4 position;
	in vec3 texcoord;
	out vec3 vert_texcoord;

	void main(void) {
		gl_Position = position;
		vert_texcoord = texcoord;
	}
);

const char * const FRAG_MERGE_LAYERS = GLSL_VERSION SHADER_QUOTE(
	in vec3 vert_texcoord;
	out vec4 out_color;

	uniform sampler2D textureSrc0;
	uniform sampler2D textureSrc1;
	uniform sampler2D textureSrc2;
	uniform sampler2D textureSrc3;
	uniform sampler2D textureOverlayCar;

	void main(void) {
		vec4 rgba = vec4(0.0);
		if (vert_texcoord.z == 3.0)
		{
			rgba = texture(textureSrc3, vert_texcoord.xy);
		}
		else if (vert_texcoord.z == 2.0)
		{
			rgba = texture(textureSrc2, vert_texcoord.xy);
		}
		else if (vert_texcoord.z == 1.0)
		{
			rgba = texture(textureSrc1, vert_texcoord.xy);
		}
		else if (vert_texcoord.z == 0.0) {
			rgba = texture(textureSrc0, vert_texcoord.xy);
		}
		else {
			rgba = texture(textureOverlayCar, vert_texcoord.xy);
		}
		out_color = rgba;
	}
);

#undef GLSL_VERSION
#undef SHADER_QUOTE

static inline void oglShaderLog(int sid) {
	GLint logLen;
	GLsizei realLen;

	glGetShaderiv(sid, GL_INFO_LOG_LENGTH, &logLen);
	if (!logLen) {
		return;
	}
	char* log = (char*)malloc(logLen);
	if (!log) {
		fprintf(stderr, "Failed to allocate memory for the shader log");
		return;
	}
	glGetShaderInfoLog(sid, logLen, &realLen, log);
	fprintf(stderr, "shader %d log %s", sid, log);
	free(log);
}

#endif //__OPENGL_SHADERS__H__
