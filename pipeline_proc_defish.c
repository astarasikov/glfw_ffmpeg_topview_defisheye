#include "defish_app.h"
#include "opengl_shaders.h"
#include "opengl_utils.h"
#include "qlib.h"
#include "bmp_loader.h"

/******************************************************************************
 * How many source streams (cameras) we have
 *****************************************************************************/
enum {
	NUM_TEXTURES_DEFISH_SRC = 3,
	NUM_FB_ARRAY_LAYERS = NUM_SRC_STREAMS,
};

/******************************************************************************
 * Geometry - common data
 *****************************************************************************/
#define RECT_INDICES(base_vertex) \
	(base_vertex), (base_vertex + 1), (base_vertex + 2), \
	(base_vertex), (base_vertex + 2), (base_vertex + 3)

static const size_t VertexStride = 3;
static const size_t TexCoordStride = 3;

#define RECT_COORDINATES_LAYERED(x0, y0, x1, y1, layer) \
	x0, y0, layer, \
	x1, y0, layer, \
	x1, y1, layer, \
	x0, y1, layer


#define RECT_COORDINATES(x0, y0, x1, y1) RECT_COORDINATES_LAYERED(x0, y0, x1, y1, 0.0f)

#define QUAD_COORDINATES(x0, y0, x1, y1, x2, y2, x3, y3) \
	x0, y0, 0, \
	x1, y1, 0, \
	x2, y2, 0, \
	x3, y3, 0

/******************************************************************************
 * Geometry for the YUV2RGB and Defisheye (single Quad)
 *****************************************************************************/

static GLfloat QuadData[] = {
	//vertex coordinates
	RECT_COORDINATES(-1.0, -1.0, 1.0, 1.0),

	//texture coordinates
	0, 1, 0,
	1, 1, 0,
	1, 0, 0,
	0, 0, 0,
};

static GLuint QuadIndices[] = {
	RECT_INDICES(0),
};

static const size_t CoordOffset = 0;
static const size_t TexCoordOffset = 12;

static const size_t NumIndices = sizeof(QuadIndices) / sizeof(QuadIndices[0]);

/******************************************************************************
 * The geometry is defined by the corners of the two rectangles.
 * The inner rectangle is the car image overlay.
 * The outer rectangle can be outside the screen space to allow
 * changing the proportions of the camera trapezoids
 *
 * Order ir CCW
 *
 * Car overlay size is 230x610. 230/610.0 is ~= 0.377
 * Let's use 20% of width for the car, and 0.40 for left/right
 * Then, 54% of height for the car, and 0.25 for front/rear
 */

/**
 * If we want to draw an arbitrary textured quad, we will have to adjust
 * the texture coordinates accordingly because OpenGL will only do the
 * linear mapping, not the homographic which would be needed for arbitrary
 * quadriliteral.
 *
 * Vertex coordinates are in the [-1.0, 1.0] range while texture coordinates
 * are in the [0.0, 1.0] range so we need a simple transformation to convert
 * between vertex and texture coordinates
 */
#define COORD_V2T(val) (((val) + 1.0f) / 2.0f)

#define VTXCOORD_LEFT \
	QUAD_COORDINATES(\
		-1.0, -1.0, \
		-0.2, -0.5,\
		-0.2, 0.5,\
		-1.0, 1.0)

#define TEXCOORD_LEFT \
	COORD_V2T(-1.0), COORD_V2T(-1.0), 0, \
	COORD_V2T(1.0), COORD_V2T(-0.5), 0, \
	COORD_V2T(1.0), COORD_V2T(0.5), 0, \
	COORD_V2T(-1.0), COORD_V2T(1.0), 0

#define VTXCOORD_RIGHT \
	QUAD_COORDINATES(\
		1.0, -1.0, \
		0.2, -0.5,\
		0.2, 0.5,\
		1.0, 1.0)

#define TEXCOORD_RIGHT \
	COORD_V2T(-1.0), COORD_V2T(1.0), 1, \
	COORD_V2T(1.0), COORD_V2T(0.5), 1, \
	COORD_V2T(1.0), COORD_V2T(-0.5), 1, \
	COORD_V2T(-1.0), COORD_V2T(-1.0), 1

/**
 * Currently, for rear and front textures, X and Y texcoords are
 * swapped relatively to the vertex coordinates.
 *
 * Since GLSL shader already implements homographic transform,
 * this is actually redundant and can be reworked/simplified in future
 */

#define VTXCOORD_REAR \
	QUAD_COORDINATES(\
		0.2, 0.5, \
		-0.2, 0.5,\
		-1.0, 1.0,\
		1.0, 1.0)

#define TEXCOORD_REAR \
	COORD_V2T(1.0), COORD_V2T(0.2), 3, \
	COORD_V2T(1.0), COORD_V2T(-0.2), 3, \
	COORD_V2T(-1.0), COORD_V2T(-1.0), 3, \
	COORD_V2T(-1.0), COORD_V2T(1.0), 3

#define VTXCOORD_FRONT \
	QUAD_COORDINATES(\
		-1.0, -1.0,\
		1.0, -1.0, \
		0.2, -0.5, \
		-0.2, -0.5)

#define TEXCOORD_FRONT \
	COORD_V2T(1.0), COORD_V2T(-1.0), 2, \
	COORD_V2T(1.0), COORD_V2T(1.0), 2, \
	COORD_V2T(-1.0), COORD_V2T(0.2), 2, \
	COORD_V2T(-1.0), COORD_V2T(-0.2), 2

#define VTXCOORD_CAR_OVERLAY RECT_COORDINATES(-0.2, -0.5, 0.2, 0.5)

#define TEXCOORD_CAR_OVERLAY \
	0, 1, 4, \
	1, 1, 4, \
	1, 0, 4, \
	0, 0, 4

/******************************************************************************
 * Geometry for the YUV2RGB and Defisheye (single Quad)
 *****************************************************************************/
static GLfloat QuadData_Merge[] = {
	//vertex coordinates

	VTXCOORD_LEFT,
	VTXCOORD_RIGHT,
	VTXCOORD_FRONT,
	VTXCOORD_REAR,
	VTXCOORD_CAR_OVERLAY,

	//texture coordinates
	TEXCOORD_LEFT,
	TEXCOORD_RIGHT,
	TEXCOORD_FRONT,
	TEXCOORD_REAR,
	TEXCOORD_CAR_OVERLAY,
};

static GLuint QuadIndices_Merge[] = {
	RECT_INDICES(0),
	RECT_INDICES(4),
	RECT_INDICES(8),
	RECT_INDICES(12),
	RECT_INDICES(16),
};

static const size_t CoordOffset_Merge = 0;
static const size_t TexCoordOffset_Merge = 5 * 12;
static const size_t NumIndices_Merge = sizeof(QuadIndices_Merge) / sizeof(QuadIndices_Merge[0]);

/******************************************************************************
 * Camera Parameters
 *****************************************************************************/

struct CameraParams {
	GLfloat lensCentre[2];
	GLfloat postScale[2];
	GLfloat aspectRatio;
	GLfloat strength;
	GLfloat zoom;
	GLfloat trapezeROI[8];
};

struct CameraParams AllCameraParams[] = {
	/* left */
	{
		.lensCentre = { -0.15f, -0.15f, },
		.postScale = { 0.2f, 0.3f, },
		.aspectRatio = 1280.0f / 960.0f,
		.strength = 0.4468,
		.zoom = 6.8180,
		.trapezeROI = {
			276.0f / 1280.0f, 312.0f / 960.0f,
			-200.0f / 1280.0f, 572.0f / 960.0f,
			1272.0f / 1280.0f, 500.0f / 960.0f,
			822.0f / 1280.0f, 320.0f / 960.0f,
		},
	},

	/* right */
	{
		.lensCentre = { -0.0f, -0.15f, },
		.postScale = { 0.2f, 0.3f, },
		.aspectRatio = 1280.0f / 960.0f,
		.strength = 0.6468,
		.zoom = 4.6180,

		.trapezeROI = {
			-0.2500, 0.2500,
			-0.2500, 0.7500,
			1.2500, 0.7500,
			1.2500, 0.2500,
		},
	},

	/* front */
	{
		.lensCentre = { 0.10f, -0.15f, },
		.postScale = { 0.3f, 0.3f, },
		.aspectRatio = 1280.0f / 960.0f,
		.strength = 0.4468,
		.zoom = 5.4180,
		.trapezeROI = {
			0, 0,
			0, 0.4,
			1, 0.4,
			1, 0,
		},
	},
	/* rear */
	{
		.lensCentre = { -0.15f, -0.15f, },
		.postScale = { 0.15f, 0.2f, },
		.aspectRatio = 1280.0f / 960.0f,
		.strength = 0.6668,
		.zoom = 5.9180,
		.trapezeROI = {
			0.0f / 1280.0f, 350.0f / 960.0f,
			0.0f / 1280.0f, 550.0f / 960.0f,
			1780.0f / 1280.0f, 550.0f / 960.0f,
			1780.0f / 1280.0f, 350.0f / 960.0f,
		},
	},
};

/******************************************************************************
 * OpenGL Context
 *****************************************************************************/
typedef struct RenderingContext
{
	/**
	 * Shared variables
	 */
	GLuint _vao;
	GLuint _vbo;
	GLuint _vbo_idx;

	/**
	 * For the source processing shader
	 * (YUV2RGB, De-fisheye, remapping, crop)
	 */
	GLuint _programId_ProcessOneCamera;

	GLuint _positionAttr;
	GLuint _texCoordAttr;

	GLuint _paramLensCentreUniform;
	GLuint _paramPostScaleUniform;
	GLuint _paramTrapezeROI;
	GLuint _paramStrengthUniform;
	GLuint _paramZoomUniform;
	GLuint _paramAspectRatioUniform;

	GLuint _textureLocationUniform[NUM_TEXTURES_DEFISH_SRC];
	GLuint _textures[NUM_TEXTURES_DEFISH_SRC];

	/**
	 * The layered framebuffer and the merging shader
	 */
	GLuint _layeredFramebuffers[NUM_FB_ARRAY_LAYERS];
	GLuint _textureFbColorbuffer[NUM_FB_ARRAY_LAYERS];
	GLuint _textureFbUniform[NUM_FB_ARRAY_LAYERS];
	GLuint _programId_MergeSources;

	/**
	 * The car overlay for the merging shader
	 */
	GLuint _textureCarOverlay;
	GLuint _textureCarOverlayUniform;

	/**
	 * The flag indicating that the context was initialized
	 */
	int _initDone;
} RenderingContext_t;

static RenderingContext_t gRenderingContext;

/******************************************************************************
 * Merging four streams into one picture
 *****************************************************************************/
static void DownloadFramebuffer(struct RenderingContext *rctx)
{
	ogl(glPixelStorei(GL_PACK_ALIGNMENT, 1));
	ogl(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
	ogl(glPixelStorei(GL_PACK_ROW_LENGTH, 0));
	ogl(glPixelStorei(GL_PACK_SKIP_ROWS, 0));
	ogl(glPixelStorei(GL_PACK_SKIP_PIXELS, 0));

	struct FrameData frameData = {};
	if (!TryGetEncoderInputBuffer(&frameData))
	{
		return;
	}
	if (!frameData.rawPixelData)
	{
		fprintf(stderr, "%s: frameData.rawPixelData is NULL\n", __func__);
		return;
	}
	ogl(glReadPixels(0, 0, OUTPUT_WIDTH, OUTPUT_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, frameData.rawPixelData));

	SubmitEncoderInputBuffer(&frameData);
}

static void BindTextureUniformsForMerging(struct RenderingContext *rctx)
{
	size_t fbIdx;
	for (fbIdx = 0; fbIdx < NUM_FB_ARRAY_LAYERS; fbIdx++)
	{
		static const char *texNames[4] = {
			"textureSrc0",
			"textureSrc1",
			"textureSrc2",
			"textureSrc3",
		};
		ogl(rctx->_textureFbUniform[fbIdx] = glGetUniformLocation(rctx->_programId_MergeSources, texNames[fbIdx]));
		ogl(glUniform1i(rctx->_textureFbUniform[fbIdx], rctx->_textureFbColorbuffer[fbIdx]));
	}

	ogl(rctx->_textureCarOverlayUniform = glGetUniformLocation(rctx->_programId_MergeSources, "textureOverlayCar"));
	ogl(glUniform1i(rctx->_textureCarOverlayUniform, rctx->_textureCarOverlay));
}

static void InitializeCarOverlay(struct RenderingContext *rctx)
{
	ogl(glGenTextures(1, &rctx->_textureCarOverlay));
	ogl(glActiveTexture(GL_TEXTURE0));
	ogl(glBindTexture(GL_TEXTURE_2D, rctx->_textureCarOverlay));
	ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
	ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));

	ogl(glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGB,
		OVL_WIDTH,
		OVL_HEIGHT,
		0,
		GL_BGRA,
		GL_UNSIGNED_BYTE,
		NULL));

	Retcode rc = RC_FAILED;
	void *bmp_data = NULL;
	struct BmpHeader bmp_header = {};
	size_t data_size = OVL_WIDTH * OVL_HEIGHT * 4;

	bmp_data = malloc(data_size);
	CHECK(NULL != bmp_data);

	rc = BmpRead("car.bmp", BMP_RGBA8888, &bmp_header, bmp_data, data_size);
	CHECK(RC_FAILED != rc);

	ogl(glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGB,
		OVL_WIDTH,
		OVL_HEIGHT,
		0,
		GL_BGRA,
		GL_UNSIGNED_BYTE,
		bmp_data));

fail:
	if (bmp_data)
	{
		free(bmp_data);
		bmp_data = NULL;
	}
	ogl(glActiveTexture(GL_TEXTURE0));
	ogl(glBindTexture(GL_TEXTURE_2D, 0));
	return;
}

static void InitializeLayeredFramebuffer(struct RenderingContext *rctx)
{
	InitializeCarOverlay(rctx);

	ogl(glGenTextures(NUM_FB_ARRAY_LAYERS, rctx->_textureFbColorbuffer));
	ogl(glGenFramebuffers(NUM_FB_ARRAY_LAYERS, rctx->_layeredFramebuffers));

	size_t fbIdx;
	for (fbIdx = 0; fbIdx < NUM_FB_ARRAY_LAYERS; fbIdx++)
	{
		ogl(glActiveTexture(GL_TEXTURE0));
		ogl(glBindTexture(GL_TEXTURE_2D, rctx->_textureFbColorbuffer[fbIdx]));

		ogl(glTexImage2D(
					GL_TEXTURE_2D,
					0,
					GL_RGB,
					OUTPUT_WIDTH,
					OUTPUT_HEIGHT,
					0,
					GL_RGB,
					GL_UNSIGNED_BYTE,
					NULL));

		ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
        ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        ogl(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        ogl(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

		ogl(glBindFramebuffer(GL_FRAMEBUFFER, rctx->_layeredFramebuffers[fbIdx]));

		ogl(glFramebufferTexture2D(
				GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_2D,
				rctx->_textureFbColorbuffer[fbIdx],
				0));

		GLenum fbStatus = 0;
		ogl(fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER));
		assert(fbStatus == GL_FRAMEBUFFER_COMPLETE);
	}

	ogl(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	ogl(glActiveTexture(GL_TEXTURE0));
	ogl(glBindTexture(GL_TEXTURE_2D, 0));

	ogl(rctx->_programId_MergeSources = glCreateProgram());
	const char * const vsrc = VERT_PASSTHRU;
	const char * const fsrc = FRAG_MERGE_LAYERS;
	GLuint vert, frag;
	ogl(vert = glCreateShader(GL_VERTEX_SHADER));
	ogl(frag = glCreateShader(GL_FRAGMENT_SHADER));

	ogl(glShaderSource(vert, 1, &vsrc, NULL));
	ogl(glCompileShader(vert));
	oglShaderLog(vert);

	ogl(glShaderSource(frag, 1, &fsrc, NULL));
	ogl(glCompileShader(frag));
	oglShaderLog(frag);

	ogl(glAttachShader(rctx->_programId_MergeSources, frag));
	ogl(glAttachShader(rctx->_programId_MergeSources, vert));

	ogl(glBindAttribLocation(rctx->_programId_MergeSources, 0, "position"));
	ogl(glBindAttribLocation(rctx->_programId_MergeSources, 2, "texcoord"));
	ogl(glBindFragDataLocation(rctx->_programId_MergeSources, 0, "out_color"));

	ogl(glLinkProgram(rctx->_programId_MergeSources));
	ogl(oglProgramLog(rctx->_programId_MergeSources));

	ogl(glUseProgram(rctx->_programId_MergeSources));
	BindTextureUniformsForMerging(rctx);
}

static void BindTargetFramebufferLayer(struct RenderingContext *rctx, size_t layer)
{
	ogl(glBindFramebuffer(GL_FRAMEBUFFER, rctx->_layeredFramebuffers[layer]));
	ogl(glClearColor(0, 1, 1, 0.0));
	ogl(glViewport(0, 0, OUTPUT_WIDTH, OUTPUT_HEIGHT));
	ogl(glClear(GL_COLOR_BUFFER_BIT));
}

static void BindOnscreenFramebuffer(struct RenderingContext *rctx)
{
	ogl(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	ogl(glViewport(0, 0, OUTPUT_WIDTH, OUTPUT_HEIGHT));
	for (size_t fbIdx = 0; fbIdx < NUM_FB_ARRAY_LAYERS; fbIdx++)
	{
		ogl(glActiveTexture(GL_TEXTURE0 + rctx->_textureFbColorbuffer[fbIdx]));
		ogl(glBindTexture(GL_TEXTURE_2D, rctx->_textureFbColorbuffer[fbIdx]));
	}
	ogl(glActiveTexture(GL_TEXTURE0 + rctx->_textureCarOverlay));
	ogl(glBindTexture(GL_TEXTURE_2D, rctx->_textureCarOverlay));

	ogl(glUseProgram(rctx->_programId_MergeSources));
	BindTextureUniformsForMerging(rctx);
}

static void renderLayeredFbToScreen(struct RenderingContext *rctx)
{
	ogl(glBindVertexArray(rctx->_vao));

	ogl(glBindBuffer(GL_ARRAY_BUFFER, rctx->_vbo));
	ogl(glBufferData(GL_ARRAY_BUFFER,
		sizeof(QuadData_Merge), QuadData_Merge, GL_STATIC_DRAW));

	ogl(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rctx->_vbo_idx));
	ogl(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		sizeof(QuadIndices_Merge), QuadIndices_Merge, GL_STATIC_DRAW));

	ogl(glVertexAttribPointer(rctx->_positionAttr, VertexStride,
		GL_FLOAT, GL_FALSE, 0,
		(GLvoid*)(CoordOffset_Merge * sizeof(GLfloat))));
	ogl(glVertexAttribPointer(rctx->_texCoordAttr, TexCoordStride,
		GL_FLOAT, GL_FALSE, 0,
		(GLvoid*)(TexCoordOffset_Merge * sizeof(GLfloat))));

	ogl(glEnableVertexAttribArray(rctx->_positionAttr));
	ogl(glEnableVertexAttribArray(rctx->_texCoordAttr));

	ogl(glDrawElements(GL_TRIANGLES, NumIndices_Merge, GL_UNSIGNED_INT, 0));

	ogl(glDisableVertexAttribArray(rctx->_texCoordAttr));
	ogl(glDisableVertexAttribArray(rctx->_positionAttr));

	ogl(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
	ogl(glBindBuffer(GL_ARRAY_BUFFER, 0));
	ogl(glBindVertexArray(0));
}

/******************************************************************************
 * De-fisheye algorithm for one YUV input
 *****************************************************************************/
static void SetupProgramUniforms(RenderingContext_t *rctx)
{
	ogl(glUseProgram(rctx->_programId_ProcessOneCamera));

	const char * const texNames[3] = {
		"texture_Y",
		"texture_U",
		"texture_V",
	};
	for (size_t i = 0; i < 3; i++) {
		ogl(rctx->_textureLocationUniform[i] = glGetUniformLocation(rctx->_programId_ProcessOneCamera, texNames[i]));
	}

	ogl(rctx->_paramLensCentreUniform = glGetUniformLocation(rctx->_programId_ProcessOneCamera, "Params.lensCentre"));
	ogl(rctx->_paramPostScaleUniform = glGetUniformLocation(rctx->_programId_ProcessOneCamera, "Params.postScale"));
	ogl(rctx->_paramTrapezeROI = glGetUniformLocation(rctx->_programId_ProcessOneCamera, "Params.trapezeROI"));
	ogl(rctx->_paramStrengthUniform = glGetUniformLocation(rctx->_programId_ProcessOneCamera, "Params.strength"));
	ogl(rctx->_paramZoomUniform = glGetUniformLocation(rctx->_programId_ProcessOneCamera, "Params.zoom"));
	ogl(rctx->_paramAspectRatioUniform = glGetUniformLocation(rctx->_programId_ProcessOneCamera, "Params.aspectRatio"));
}

static void InitializeRenderingContext(RenderingContext_t *rctx)
{
	if (rctx->_initDone) {
		return;
	}

	ogl(glGenVertexArrays(1, &rctx->_vao));
	ogl(glBindVertexArray(rctx->_vao));
	ogl(glGenBuffers(1, &rctx->_vbo));
	ogl(glGenBuffers(1, &rctx->_vbo_idx));

	ogl(rctx->_programId_ProcessOneCamera = glCreateProgram());

	const char * const vsrc = VERT_PASSTHRU;
	const char * const fsrc = FRAG_PROCESS_CAMERA;

	GLuint vert, frag;
	ogl(vert = glCreateShader(GL_VERTEX_SHADER));
	ogl(frag = glCreateShader(GL_FRAGMENT_SHADER));

	ogl(glShaderSource(vert, 1, &vsrc, NULL));
	ogl(glCompileShader(vert));
	oglShaderLog(vert);

	ogl(glShaderSource(frag, 1, &fsrc, NULL));
	ogl(glCompileShader(frag));
	oglShaderLog(frag);

	ogl(glAttachShader(rctx->_programId_ProcessOneCamera, frag));
	ogl(glAttachShader(rctx->_programId_ProcessOneCamera, vert));

	ogl(glBindAttribLocation(rctx->_programId_ProcessOneCamera, 0, "position"));
	ogl(glBindAttribLocation(rctx->_programId_ProcessOneCamera, 2, "texcoord"));
	ogl(glBindFragDataLocation(rctx->_programId_ProcessOneCamera, 0, "out_color"));

	ogl(glLinkProgram(rctx->_programId_ProcessOneCamera));
	ogl(oglProgramLog(rctx->_programId_ProcessOneCamera));

	ogl(glGenTextures(NUM_TEXTURES_DEFISH_SRC, rctx->_textures));

	ogl(glDisable(GL_BLEND));
	ogl(glDisable(GL_DEPTH_TEST));

	ogl(rctx->_positionAttr = glGetAttribLocation(rctx->_programId_ProcessOneCamera, "position"));
	ogl(rctx->_texCoordAttr = glGetAttribLocation(rctx->_programId_ProcessOneCamera, "texcoord"));

	SetupProgramUniforms(rctx);

	/**
	 * Initialize the multi-layered framebuffer used for rendering
	 * each source stream into a separate layer
	 */
	InitializeLayeredFramebuffer(&gRenderingContext);
	rctx->_initDone = 1;
}

static void renderQuadWithParams(struct CameraParams *params, RenderingContext_t *rctx)
{
	ogl(glUseProgram(rctx->_programId_ProcessOneCamera));

	for (size_t i = 0; i < NUM_TEXTURES_DEFISH_SRC; i++) {
		ogl(glUniform1i(rctx->_textureLocationUniform[i], rctx->_textures[i]));
	}

	ogl(glUniform2fv(rctx->_paramLensCentreUniform, 1, params->lensCentre));
	ogl(glUniform2fv(rctx->_paramPostScaleUniform, 1, params->postScale));
	ogl(glUniform2fv(rctx->_paramTrapezeROI, 4, params->trapezeROI));
	ogl(glUniform1f(rctx->_paramAspectRatioUniform, params->aspectRatio));
	ogl(glUniform1f(rctx->_paramStrengthUniform, params->strength));
	ogl(glUniform1f(rctx->_paramZoomUniform, params->zoom));

	ogl(glBindVertexArray(rctx->_vao));

	ogl(glBindBuffer(GL_ARRAY_BUFFER, rctx->_vbo));
	ogl(glBufferData(GL_ARRAY_BUFFER,
		sizeof(QuadData), QuadData, GL_STATIC_DRAW));

	ogl(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rctx->_vbo_idx));
	ogl(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		sizeof(QuadIndices), QuadIndices, GL_STATIC_DRAW));

	ogl(glVertexAttribPointer(rctx->_positionAttr, VertexStride,
		GL_FLOAT, GL_FALSE, 0,
		(GLvoid*)(CoordOffset * sizeof(GLfloat))));
	ogl(glVertexAttribPointer(rctx->_texCoordAttr, TexCoordStride,
		GL_FLOAT, GL_FALSE, 0,
		(GLvoid*)(TexCoordOffset * sizeof(GLfloat))));

	ogl(glEnableVertexAttribArray(rctx->_positionAttr));
	ogl(glEnableVertexAttribArray(rctx->_texCoordAttr));

	ogl(glDrawElements(GL_TRIANGLES, NumIndices, GL_UNSIGNED_INT, 0));

	ogl(glDisableVertexAttribArray(rctx->_texCoordAttr));
	ogl(glDisableVertexAttribArray(rctx->_positionAttr));

	ogl(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
	ogl(glBindBuffer(GL_ARRAY_BUFFER, 0));
	ogl(glBindVertexArray(0));
}

static void uploadGlTexture(AVFrame *src_frame, int src_idx)
{
	AVFrame *frame = NULL;
	if (!src_frame)
	{
		return;
	}
	frame = src_frame;

	for (size_t i = 0; i < NUM_TEXTURES_DEFISH_SRC; i++) {
		ogl(glActiveTexture(GL_TEXTURE0 + gRenderingContext._textures[i]));
		ogl(glBindTexture(GL_TEXTURE_2D, gRenderingContext._textures[i]));

		ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
		ogl(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));

		size_t height = frame->height;
		if (i) {
			//divide by two for U and V components
			height >>= 1;
		}

		ogl(glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RGB,
			frame->linesize[i],
			height,
			0, GL_RED,
			GL_UNSIGNED_BYTE,
			frame->data[i]));
	}
}

void RenderPipelineWithGL(void)
{
	/**
	 * FPS counter for debugging (when enabled)
	 */
	double timeStart;
	double timeEnd;
	static double lastFPS = 0.0;

	if (PRINT_DEBUG_FPS)
	{
		timeStart = glfwGetTime();
	}

	InitializeRenderingContext(&gRenderingContext);

	ogl(glClearColor(1, 0.9, 1, 0.0));
	ogl(glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT));

	size_t src_idx = 0;
	for (src_idx = 0; src_idx < NUM_SRC_STREAMS;  src_idx++)
	{
		struct FrameData frameData = {};
		if (TryReceiveDecodedFrame(&frameData, src_idx))
		{
			DPRINT_RENDERER("frame data=%p", frameData.frame->data[0]);
			uploadGlTexture(frameData.frame, src_idx);

			/**
			 * This function should never block because it is inside the rendering loop.
			 * The fact that we will not get blocked is guaranteed by the following
			 * conditions:
			 *
			 * If the decoder is too fast, it will block on the
			 * FrameQueuesDecoded.
			 *
			 * Otherwise, FrameQueuesReturned can not be full since
			 * we are owning one frame
			 */
			ReturnFrameToDecoderQueue(frameData.frame, src_idx);

			/**
			 * Render the input source to the corresponding
			 * framebuffer (the layer in the array texture).
			 *
			 * Do this only when we receive a decoded frame
			 * because framebuffer is cleared before drawing.
			 */
			BindTargetFramebufferLayer(&gRenderingContext, src_idx);
			renderQuadWithParams(&AllCameraParams[src_idx], &gRenderingContext);
		}
	};

	/**
	 * Merge all input images into a single one and draw to the screen
	 */
	BindOnscreenFramebuffer(&gRenderingContext);
	renderLayeredFbToScreen(&gRenderingContext);

	DownloadFramebuffer(&gRenderingContext);

	if (PRINT_DEBUG_FPS)
	{
		timeEnd = glfwGetTime();
		double dt = (timeEnd - timeStart);
		if (dt > 0.001) {
			double FPS = 1.0 / dt;
			double avgFPS = (0.8 * lastFPS + 0.2 * FPS);
			lastFPS = avgFPS;
			DPRINT_FPS("frame time=%4.4f ms, FPS %4.4f AVG=%4.4f",
					dt,
					FPS,
					avgFPS);
		}
	}
}
