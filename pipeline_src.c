#include "defish_app.h"

/******************************************************************************
 * Decoding pipeline queues
 *****************************************************************************/

static MSG_Q_ID FrameQueuesDecoded[NUM_SRC_STREAMS];

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
static MSG_Q_ID FrameQueuesReturnedToDecoder[NUM_SRC_STREAMS];

/**
 * The decoder must call this function to indicate to the rendering thread
 * that the source texture must be updated.
 */
static void SubmitFrameFromDecoder(AVFrame *frame, int index)
{
	struct FrameData frameData = {};
	frameData.frame = frame;
	frameData.rawPixelData = NULL;
	msgQSend(FrameQueuesDecoded[index],
			(char*)&frameData,
			sizeof(struct FrameData),
			MSG_Q_WAIT_FOREVER,
			MSG_PRI_NORMAL);
}

/**
 * The rendering thread calls this function after uploading the GPU texture
 * to return the AVFrame resource to the decoder thread.
 */

void ReturnFrameToDecoderQueue(AVFrame *frame, int index)
{
	struct FrameData frameData = {};
	frameData.frame = frame;
	frameData.rawPixelData = NULL;
	msgQSend(FrameQueuesReturnedToDecoder[index],
		(char*)&frameData,
		sizeof(struct FrameData),
		MSG_Q_WAIT_FOREVER,
		MSG_PRI_NORMAL);
}

bool TryReceiveDecodedFrame(struct FrameData *frameData, int src_idx)
{
	int q_status = -1;
	q_status = msgQReceive(FrameQueuesDecoded[src_idx],
			(char*)frameData,
			sizeof(struct FrameData),
			MSG_Q_NO_WAIT);
	DPRINT_RENDERER("msgQReceive src=%zu status=%d", src_idx, q_status);
	return (q_status == sizeof(struct FrameData));
}

/******************************************************************************
 * FFMpeg GLUE for reading video frames
 *****************************************************************************/
struct Demo_VideoContext {
	const char *stream_paths[NUM_SRC_STREAMS];

	AVFormatContext *format_contexts[NUM_SRC_STREAMS];
	AVCodecContext *codec_contexts[NUM_SRC_STREAMS];
	int stream_indices[NUM_SRC_STREAMS];
	AVFrame *frames[NUM_SRC_STREAMS];
};

static struct Demo_VideoContext video_context = {
	.stream_paths = SRC_PATHS_INITIALIZER,
};

#define IS_VIDEO(stream) (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)

static bool InitOneSourceAtIndex(struct Demo_VideoContext *video_context,
		size_t thisDecoderIndex)
{
	int stream = -1;
	AVFormatContext *format_context = NULL;
	AVCodecContext *codec_context = NULL;
	AVCodec *codec = NULL;

	const char *path = video_context->stream_paths[thisDecoderIndex];

	AVDictionary *dict = NULL;
	av_dict_set(&dict, "protocol_whitelist", "file,crypto,rtp,udp,tcp", 0);
	if (avformat_open_input(&format_context, path, NULL, &dict) < 0) {
		DPRINT_DECODER("Failed to open the input '%s'", path);
		goto fail;
	}

	if (avformat_find_stream_info(format_context, NULL) < 0) {
		DPRINT_DECODER("Failed to find stream info");
		goto fail;
	}

	av_dump_format(format_context, 0, path, 0);
	for (stream = 0; stream < format_context->nb_streams; stream++) {
		if (IS_VIDEO(format_context->streams[stream])) {
			break;
		}
	}

	if (stream == format_context->nb_streams) {
		DPRINT_DECODER("Failed to find a video stream");
		goto fail;
	}

	codec_context = format_context->streams[stream]->codec;
	if (!codec_context) {
		DPRINT_DECODER("No codec context found");
		goto fail;
	}

	codec = avcodec_find_decoder(codec_context->codec_id);
	if (!codec) {
		DPRINT_DECODER("No codec decoder found");
		goto fail;
	}

	if (avcodec_open2(codec_context, codec, NULL) < 0) {
		DPRINT_DECODER("Failed to open codec");
		goto fail;
	}

	AVFrame *frame = av_frame_alloc();
	if (!frame) {
		DPRINT_DECODER("Failed to allocate a frame");
		goto fail;
	}

	video_context->format_contexts[thisDecoderIndex] = format_context;
	video_context->codec_contexts[thisDecoderIndex] = codec_context;
	video_context->frames[thisDecoderIndex] = frame;
	video_context->stream_indices[thisDecoderIndex] = stream;

	return true;

fail:
	return false;
}

static bool InitVideoSources(struct Demo_VideoContext *video_context)
{
	size_t i;
	for (i = 0; i < NUM_SRC_STREAMS; i++)
	{
		if (true != InitOneSourceAtIndex(video_context, i)) {
			return false;
		}
	}

	return true;
}

static void FreeVideoSources(struct Demo_VideoContext *video_context)
{
	size_t i;
	for (i = 0; i < NUM_SRC_STREAMS; i++)
	{
		if (NULL != video_context->format_contexts[i]) {
			avformat_close_input(&video_context->format_contexts[i]);
		}
		if (NULL != video_context->codec_contexts[i]) {
			avcodec_close(video_context->codec_contexts[i]);
		}
		if (NULL != video_context->frames[i])
		{
			av_frame_free(&video_context->frames[i]);
		}
	}
}

struct DecoderThreadContext {
	struct Demo_VideoContext *video_context;
	size_t decoderIndex;
};

static void *DecoderThreadRoutine(void *context)
{
	size_t thisDecoderIndex = 0;
	AVPacket packet = {};
	struct Demo_VideoContext *video_context = NULL;
	struct DecoderThreadContext *thread_context = NULL;

	if (!context)
	{
		DPRINT_DECODER("context is NULL");
		goto done;
	}

	thread_context = (struct DecoderThreadContext*)context;
	if (!thread_context->video_context)
	{
		DPRINT_DECODER("video_context is NULL");
		goto done;
	}

	video_context = (struct Demo_VideoContext *)thread_context->video_context;
	thisDecoderIndex = thread_context->decoderIndex;

	if (true != InitOneSourceAtIndex(video_context, thisDecoderIndex))
	{
		DPRINT_DECODER("failed to initialize the input");
		goto done;
	}

	size_t tmpFrame = 0;
	for (tmpFrame = 0; tmpFrame < DECODER_QUEUE_DEPTH; tmpFrame++)
	{
		AVFrame *frame = av_frame_alloc();
		if (!frame) {
			DPRINT_DECODER("Failed to allocate a frame");
			goto done;
		}
		ReturnFrameToDecoderQueue(frame, thisDecoderIndex);
	}

	size_t decodedFrameIndex = 0;
	while (1)
	{
		DPRINT_DECODER("decodedFrameIndex=%zu", decodedFrameIndex);
		++decodedFrameIndex;
		if (av_read_frame(video_context->format_contexts[thisDecoderIndex], &packet) < 0) {
			DPRINT_DECODER("failed to read the frame");
			goto done;
		}
		if (video_context->stream_indices[thisDecoderIndex] != packet.stream_index) {
			DPRINT_DECODER("invalid stream index");
			continue;
		}

		struct FrameData frameData = {};
		int q_status = -1;
		q_status = msgQReceive(FrameQueuesReturnedToDecoder[thisDecoderIndex],
				(char*)&frameData,
				sizeof(frameData),
				MSG_Q_WAIT_FOREVER);
		if (q_status != sizeof(struct FrameData))
		{
			DPRINT_DECODER("failed to get the temporary frame");
			continue;
		}

		AVFrame *frame = NULL;
		frame = frameData.frame;

		int frame_done = 0;
		avcodec_decode_video2(
				video_context->codec_contexts[thisDecoderIndex],
				frame,
				&frame_done,
				&packet);
		if (!frame_done) {
			DPRINT_DECODER("failed to decode the frame");

			/**
			 * after initialization, for some time the frames are not decoded
			 * perhaps FFMPEG uses some kind of a non-blocking queue internally
			 */
			if (decodedFrameIndex < 100) {
				ReturnFrameToDecoderQueue(frame, thisDecoderIndex);
				continue;
			}

			/**
			 * should we break the loop if we still have not received
			 * a frame after 100 iterations?
			 */
		}

		DPRINT_DECODER("Decoded frame data=%p fmt=%x width=%d height=%d",
			frame->data[0],
			frame->format,
			frame->width,
			frame->height);

		SubmitFrameFromDecoder(frame, thisDecoderIndex);
	}

done:
	DPRINT_DECODER("done");
	return NULL;
}

static pthread_t DecoderThreadHandles[NUM_SRC_STREAMS];
static struct DecoderThreadContext decoderThreadContexts[NUM_SRC_STREAMS] = {};

extern void InitializeDecoders(void) {
	size_t streamIndex;
	av_register_all();
	avformat_network_init();

	for (streamIndex = 0; streamIndex < NUM_SRC_STREAMS; streamIndex++)
	{
		FrameQueuesDecoded[streamIndex] = msgQCreate(
				DECODER_QUEUE_DEPTH,
				sizeof(struct FrameData),
				MSG_Q_FIFO);
		assert(NULL != FrameQueuesDecoded[streamIndex]);

		FrameQueuesReturnedToDecoder[streamIndex] = msgQCreate(
				DECODER_QUEUE_DEPTH,
				sizeof(struct FrameData),
				MSG_Q_FIFO);
		assert(NULL != FrameQueuesReturnedToDecoder[streamIndex]);
	}

	for (streamIndex = 0; streamIndex < NUM_SRC_STREAMS; streamIndex++)
	{
		decoderThreadContexts[streamIndex].video_context = &video_context;
		decoderThreadContexts[streamIndex].decoderIndex = streamIndex;
		assert(0 == pthread_create(DecoderThreadHandles + streamIndex,
				NULL,
				DecoderThreadRoutine,
				decoderThreadContexts + streamIndex));
	}

done:
	return;
}

extern void WaitAndReleaseDecoders(void)
{
	size_t streamIndex;
	for (streamIndex = 0; streamIndex < NUM_SRC_STREAMS; streamIndex++)
	{
		/**
		 * msgQDelete currently does not release memory but instead
		 * unblocks all waiters and returns an error code to them.
		 *
		 * This way we can unblock all the decoder threads and wait
		 * until they terminate using pthread_join
		 */
		msgQDelete(FrameQueuesDecoded[streamIndex]);
		msgQDelete(FrameQueuesReturnedToDecoder[streamIndex]);

		void *retval;
		pthread_join(DecoderThreadHandles[streamIndex], &retval);
	}
}
