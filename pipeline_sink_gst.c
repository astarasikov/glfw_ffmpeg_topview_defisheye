#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>

#include "defish_app.h"

/******************************************************************************
 * Encoding pipeline queues
 *****************************************************************************/

static MSG_Q_ID FrameQueuesEncoderInput[1];

/**
 * After we render the frame on the screen, we return it to the decoder
 * thread.
 *
 * This serves a dual purpose.
 * Firstly, it allows to have a fixed number of buffers without the dynamic
 * allocation.
 * Secondly, the frame data (AVFrame) can only be destroyed from the thread
 * in which it was created (it is not thread-safe).
 */
static MSG_Q_ID FrameQueuesReturnedToEncoder[1];

/**
 * The decoder must call this function to indicate to the rendering thread
 * that the source texture must be updated.
 */
static void ReturnFrameFromEncoder(struct FrameData *frameData)
{
	msgQSend(FrameQueuesEncoderInput[0],
			(char*)frameData,
			sizeof(struct FrameData),
			MSG_Q_WAIT_FOREVER,
			MSG_PRI_NORMAL);
}

static bool GetFrameForEncoder(struct FrameData *frameData)
{
	int q_status = -1;
	q_status = msgQReceive(FrameQueuesReturnedToEncoder[0],
			(char*)frameData,
			sizeof(struct FrameData),
			MSG_Q_WAIT_FOREVER);
	DPRINT_RENDERER("msgQReceive status=%d", q_status);
	return (q_status == sizeof(struct FrameData));
}

/**
 * The rendering thread calls this function after downloading the GPU texture
 * to return the AVFrame resource to the encoder thread.
 */
void SubmitEncoderInputBuffer(struct FrameData *frameData)
{
	msgQSend(FrameQueuesReturnedToEncoder[0],
		(char*)frameData,
		sizeof(struct FrameData),
		MSG_Q_WAIT_FOREVER,
		MSG_PRI_NORMAL);
}

bool TryGetEncoderInputBuffer(struct FrameData *frameData)
{
	int q_status = -1;
	q_status = msgQReceive(FrameQueuesEncoderInput[0],
			(char*)frameData,
			sizeof(struct FrameData),
			MSG_Q_WAIT_FOREVER);
	DPRINT_RENDERER("msgQReceive status=%d", q_status);
	return (q_status == sizeof(struct FrameData));
}

static void InitEncoderQueues(void)
{
	FrameQueuesEncoderInput[0] = msgQCreate(
			ENCODER_QUEUE_DEPTH,
			sizeof(struct FrameData),
			MSG_Q_FIFO);
	assert(NULL != FrameQueuesEncoderInput[0]);

	FrameQueuesReturnedToEncoder[0] = msgQCreate(
			ENCODER_QUEUE_DEPTH,
			sizeof(struct FrameData),
			MSG_Q_FIFO);
	assert(NULL != FrameQueuesReturnedToEncoder[0]);

	size_t i;
	for (i = 0; i < ENCODER_QUEUE_DEPTH; i++)
	{
		void *buffer = malloc(OUTPUT_WIDTH * OUTPUT_HEIGHT * 3);
		assert(NULL != buffer);
		struct FrameData frameData = {};
		frameData.rawPixelData = buffer;
		SubmitEncoderInputBuffer(&frameData);
	}
}

/******************************************************************************
 * Bridging the GStreamer and the renderer.
 *****************************************************************************/
static void *get_next_image(void)
{
	void *fbData = NULL;
	struct FrameData frameData = {};
	bool ok = false;

	DPRINT_ENCODER("+++");
	ok = GetFrameForEncoder(&frameData);
	DPRINT_ENCODER("---");
	if (!ok)
	{
		fprintf(stderr, "%s: failed to obtain the buffer for the encoder\n", __func__);
		goto done;
	}
	fbData = frameData.rawPixelData;

done:
	return fbData;
}

/******************************************************************************
 * Generic GStreamer appsrc pipeline
 *****************************************************************************/
typedef struct {
    GstClockTime timestamp;
    guint sourceid;
    GstElement *appsrc;
} StreamContext;

static StreamContext *stream_context_new(GstElement *appsrc)
{
    StreamContext *ctx = g_new0(StreamContext, 1);
    ctx->timestamp = 0;
    ctx->sourceid = 0;
    ctx->appsrc = appsrc;
    return ctx;
}

static void encoder_buffer_destroy_notify(gpointer data)
{
	DPRINT_ENCODER("data=%p", data);
	struct FrameData frameData = {};
	frameData.rawPixelData = data;
	ReturnFrameFromEncoder(&frameData);
}

static gboolean read_data(StreamContext *ctx)
{
    static const gsize size = OUTPUT_WIDTH * OUTPUT_HEIGHT * 3;

    guchar *pixels = (guchar*)get_next_image();
	if (!pixels)
	{
		return FALSE;
	}

    GstBuffer *buffer = gst_buffer_new_wrapped_full(
			0,
			pixels,
			size,
			0,
			size,
			pixels,
			encoder_buffer_destroy_notify);

    GST_BUFFER_PTS(buffer) = ctx->timestamp;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, OUTPUT_FRAMERATE);
    ctx->timestamp += GST_BUFFER_DURATION(buffer);

	GstFlowReturn ret = TRUE;
	g_signal_emit_by_name(ctx->appsrc, "push-buffer", buffer, &ret);
	gst_buffer_unref(buffer);

    return TRUE;
}

static void enough_data(GstElement *appsrc, StreamContext *ctx)
{
    if (ctx->sourceid != 0) {
        g_source_remove(ctx->sourceid);
        ctx->sourceid = 0;
    }
}

static void need_data(GstElement *appsrc, guint size, StreamContext *ctx)
{
    if (ctx->sourceid == 0) {
        ctx->sourceid = g_idle_add((GSourceFunc)read_data, ctx);
    }
}

static GMainLoop *loop;

static void *threadTopViewGStreamerServer(void *arg)
{
	int argc = 0;
	char **argv = NULL;
    gst_init(&argc, &argv);

	InitEncoderQueues();
    loop = g_main_loop_new(NULL, FALSE);

    GstElement *pipeline = gst_parse_launch(GetGstPipelineString(), NULL);
    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "imagesrc");

    gst_util_set_object_arg(G_OBJECT(appsrc), "format", "time");
    gst_app_src_set_caps(GST_APP_SRC(appsrc), gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "RGB",
        "width", G_TYPE_INT, OUTPUT_WIDTH,
        "height", G_TYPE_INT, OUTPUT_HEIGHT,
        "framerate", GST_TYPE_FRACTION, OUTPUT_FRAMERATE, 1, NULL));

    StreamContext *ctx = stream_context_new(appsrc);

    g_signal_connect(appsrc, "need-data", G_CALLBACK(need_data), ctx);
    g_signal_connect(appsrc, "enough-data", G_CALLBACK(enough_data), ctx);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_main_loop_run(loop);
	DPRINT_ENCODER("finished");

    gst_element_set_state(pipeline, GST_STATE_NULL);
	DPRINT_ENCODER("STATE_NULL");

    g_free(ctx);
    g_free(loop);

    return NULL;
}

static pthread_t ServerThreadHandle;

void InitializeGStreamerServer(void)
{
	assert(0 == pthread_create(&ServerThreadHandle,
				NULL,
				threadTopViewGStreamerServer,
				NULL));
}

void WaitAndReleaseGStreamerServer(void)
{
	pthread_kill(ServerThreadHandle, SIGKILL);
	void *retval = NULL;
	pthread_join(ServerThreadHandle, &retval);
}
