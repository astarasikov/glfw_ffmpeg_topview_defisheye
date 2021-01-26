#ifndef __DEFISH_APP__H__
#define __DEFISH_APP__H__

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "qlib.h"

/******************************************************************************
 * TODO:
 * [] Use multiple buffers for texture upload and FBO, use async xfers
 * [] Research libva(VAAPI) decoding and encoding with zero-copy
 *
 * [] Refactor common code from buffer queues from decoder and encoder
 * [] Consider using GStreamer and dropping FFMPEG decoding and queues
 * [] Replace assert() with error handling
 * [] Implement graceful shutdown, clean queues and deallocate memory
 *
 *
 * [] qlib: add proper timeout for msgQSend
 *****************************************************************************/

/******************************************************************************
 * Tunable parameters for the application
 *****************************************************************************/
enum {
	OUTPUT_WIDTH = 1280,
	OUTPUT_HEIGHT = 960,
	OUTPUT_FRAMERATE = 25,

	OVL_WIDTH = 230,
	OVL_HEIGHT = 610,
};

enum {
	DECODER_QUEUE_DEPTH = 2,
	ENCODER_QUEUE_DEPTH = 4,

	NUM_SRC_STREAMS = 4,

	PRINT_DEBUG_DECODER = 0,
	PRINT_DEBUG_ENCODER = 0,
	PRINT_DEBUG_RENDERER = 0,
	PRINT_DEBUG_FPS = 1,
};

#if defined(__APPLE__)
	#define SRC_FILE_PREFIX "/Users/alexander/Documents/topview/"
#else
	#define SRC_FILE_PREFIX "/home/alexander/Documents/topview/"
#endif

#define SRC_FILE_PATH(suffix) SRC_FILE_PREFIX suffix

#define SRC_PATHS_INITIALIZER {\
	SRC_FILE_PATH("left.mp4"), \
	SRC_FILE_PATH("right.mp4"), \
	SRC_FILE_PATH("front.mp4"), \
	SRC_FILE_PATH("rear.mp4"), \
}

static inline const char *GetGstPipelineString(void)
{
    //return "appsrc name=imagesrc ! ffmpegcolorspace ! x264enc ! rtph264pay ! udpsink host=127.0.0.1";
    //return "appsrc name=imagesrc ! ffmpegcolorspace ! x264enc ! rtph264pay ! filesink location=out.mp4";
    //return "appsrc name=imagesrc ! filesink location=out.mp4";
    //return "appsrc name=imagesrc ! fakesink";
	return "appsrc name=imagesrc ! autovideoconvert ! avenc_mjpeg bitrate=3000000 ! filesink location=out.mp4";
}

/******************************************************************************
 * Debug print macros
 *****************************************************************************/
#define DPRINT_DECODER(fmt, args...) do { \
	if (PRINT_DEBUG_DECODER) { \
		fprintf(stderr, "Decoder[%zu]: %s:%d " fmt "\n", \
				thisDecoderIndex, __func__, __LINE__, ##args); \
	} \
} while (0)

#define DPRINT_SUBSYS(subsys, fmt, args...) do {\
	if (PRINT_DEBUG_##subsys) { \
		fprintf(stderr, "%s:%d " fmt "\n", \
				__func__, __LINE__, ##args); \
	} \
} while (0)

#define DPRINT_RENDERER(fmt, args...) DPRINT_SUBSYS(RENDERER, fmt, ##args)
#define DPRINT_ENCODER(fmt, args...) DPRINT_SUBSYS(ENCODER, fmt, ##args)
#define DPRINT_FPS(fmt, args...) DPRINT_SUBSYS(FPS, fmt, ##args)

/******************************************************************************
 * Buffer management
 *****************************************************************************/

/**
 * The structure which wraps AVFrame and contains additional metadata.
 * The intention is to add data such as frame number or PTS to synchronize
 * the input streams.
 *
 * Currently, for decoding side (FFMPEG) it stores the AVFrame pointer.
 * For the encoding/streaming side (GStreamer) it stores the raw data pointer.
 */
struct FrameData
{
	/**
	 * The decoder part of the pipeline (FFMPEG) uses the AVFrame structure
	 * for communication with the processing part (defisheye renderer)
	 */
	AVFrame *frame;

	/**
	 * The encoder part of the pipeline (GStreamer) uses the raw data pointer
	 * to communicate with the processing part (defisheye renderer)
	 */
	void *rawPixelData;
};


/******************************************************************************
 * Decoding/Source through FFMPEG
 *****************************************************************************/
void InitializeDecoders(void);
void WaitAndReleaseDecoders(void);
void ReturnFrameToDecoderQueue(AVFrame *frame, int index);
bool TryReceiveDecodedFrame(struct FrameData *frameData, int src_idx);

/******************************************************************************
 * Encoding/Streaming through GStreamer
 *****************************************************************************/

void InitializeGStreamerServer(void);
void WaitAndReleaseGStreamerServer(void);

/**
 * The processing pipeline will drop frames if the encoder
 * (GStreamer) is not fast enough to encode.
 *
 * This design can be changed inside the implementation of
 * TryGetEncoderInputBuffer by changing msgQReceive timeout value.
 */
bool TryGetEncoderInputBuffer(struct FrameData *frameData);
void SubmitEncoderInputBuffer(struct FrameData *frameData);

/******************************************************************************
 * Misc pipeline functions
 *****************************************************************************/
void RenderPipelineWithGL(void);

#endif
