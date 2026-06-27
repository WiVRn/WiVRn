#include "pico_decoder.h"

#include <spdlog/spdlog.h>
#include <cstring>
#include <chrono>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace
{
const char * mime(wivrn::video_codec codec)
{
	switch (codec)
	{
		case wivrn::video_codec::h264:
			return "video/avc";
		case wivrn::video_codec::h265:
			return "video/hevc";
		case wivrn::video_codec::av1:
			return "video/av01";
		case wivrn::video_codec::raw:
			break;
	}
	__builtin_unreachable();
}

void check(media_status_t status, const char * msg)
{
	if (status != AMEDIA_OK)
	{
		spdlog::error("{}: MediaCodec error {}", msg, (int)status);
		throw std::runtime_error("MediaCodec error");
	}
}

PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHRProc = nullptr;
PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHRProc = nullptr;
PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROIDProc = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOESProc = nullptr;

void load_egl_procs()
{
	if (eglCreateImageKHRProc)
		return;

	eglCreateImageKHRProc = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	eglDestroyImageKHRProc = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
	eglGetNativeClientBufferANDROIDProc = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
	glEGLImageTargetTexture2DOESProc = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

	if (!eglCreateImageKHRProc || !eglDestroyImageKHRProc)
		spdlog::warn("EGL_KHR_image not available, video may not work");
	if (!eglGetNativeClientBufferANDROIDProc)
		spdlog::warn("EGL_ANDROID_get_native_client_buffer not available, video may not work");
	if (!glEGLImageTargetTexture2DOESProc)
		spdlog::warn("GL_OES_EGL_image not available, video may not work");
}

void release_hardware_buffer(AHardwareBuffer * hb)
{
	if (hb)
		AHardwareBuffer_release(hb);
}
} // namespace

pico_video_decoder::pico_video_decoder(
	const wivrn::to_headset::video_stream_description & desc,
	uint8_t stream_idx,
	frame_callback callback) :
	stream_index(stream_idx),
	on_frame_decoded(std::move(callback))
{
	load_egl_procs();

	auto width = desc.width;
	auto height = desc.height / (stream_index == 2 ? 2 : 1);

	AImageReader * ir;
	check(AImageReader_newWithUsage(
		      width,
		      height,
		      AIMAGE_FORMAT_PRIVATE,
		      AHARDWAREBUFFER_USAGE_CPU_READ_NEVER | AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
		      8,
		      &ir),
	      "AImageReader_newWithUsage");
	image_reader.reset(ir, [](AImageReader * r) { AImageReader_delete(r); });

	AImageReader_ImageListener listener{this, on_image_available_cb};
	check(AImageReader_setImageListener(ir, &listener), "AImageReader_setImageListener");

	AMediaFormat * format = AMediaFormat_new();
	AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime(desc.codec[stream_index]));
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
#if __ANDROID_API__ >= 28
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_OPERATING_RATE, (int32_t)std::ceil(desc.frame_rate));
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PRIORITY, 0);
#endif

	media_codec = AMediaCodec_createDecoderByType(mime(desc.codec[stream_index]));
	if (!media_codec)
		throw std::runtime_error(std::string("Cannot create decoder for ") + mime(desc.codec[stream_index]));

	spdlog::warn("Created MediaCodec decoder for stream {}", stream_index);

	ANativeWindow * window;
	check(AImageReader_getWindow(image_reader.get(), &window), "AImageReader_getWindow");

	check(AMediaCodec_configure(media_codec, format, window, nullptr, 0), "AMediaCodec_configure");
	check(AMediaCodec_start(media_codec), "AMediaCodec_start");

	AMediaFormat_delete(format);

	worker = std::thread([this]() { worker_loop(); });
}

pico_video_decoder::~pico_video_decoder()
{
	exiting = true;
	pending_cv.notify_all();

	if (media_codec)
	{
		AMediaCodec_stop(media_codec);
		AMediaCodec_delete(media_codec);
	}

	if (worker.joinable())
		worker.join();
}

void pico_video_decoder::worker_loop()
{
	while (!exiting)
	{
		ssize_t in_idx = AMediaCodec_dequeueInputBuffer(media_codec, 1000);
		if (in_idx >= 0)
		{
			size_t buf_size;
			uint8_t * buf = AMediaCodec_getInputBuffer(media_codec, in_idx, &buf_size);
			if (!buf)
			{
				AMediaCodec_queueInputBuffer(media_codec, in_idx, 0, 0, 0, 0);
				continue;
			}

			pending_frame frame;
			bool has_frame = false;
			{
				std::unique_lock lock(pending_mutex);
				if (pending_cv.wait_for(lock, std::chrono::milliseconds(10),
				    [&]() { return !pending_frames.empty() || exiting; }))
				{
					if (!pending_frames.empty())
					{
						frame = std::move(pending_frames.front());
						pending_frames.erase(pending_frames.begin());
						has_frame = true;
					}
				}
			}

			if (has_frame)
			{
				size_t copy_size = std::min(frame.data.size(), buf_size);
				memcpy(buf, frame.data.data(), copy_size);
				uint64_t timestamp = frame.frame_index * 10'000;
				auto status = AMediaCodec_queueInputBuffer(
					media_codec, in_idx, 0, copy_size, timestamp, 0);
				if (status != AMEDIA_OK)
					spdlog::error("AMediaCodec_queueInputBuffer: error {}", (int)status);
			}
			else
			{
				AMediaCodec_queueInputBuffer(media_codec, in_idx, 0, 0, 0, 0);
			}
		}

		AMediaCodecBufferInfo info;
		ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(media_codec, &info, 1000);
		if (out_idx >= 0)
		{
			auto status = AMediaCodec_releaseOutputBuffer(media_codec, out_idx, true);
			if (status != AMEDIA_OK)
				spdlog::error("AMediaCodec_releaseOutputBuffer: error {}", (int)status);
		}
		else if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
		{
			AMediaFormat * fmt = AMediaCodec_getOutputFormat(media_codec);
			spdlog::warn("MediaCodec output format changed: {}", AMediaFormat_toString(fmt));
			AMediaFormat_delete(fmt);
		}
	}
}

void pico_video_decoder::push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial)
{
	std::lock_guard lock(pending_mutex);

	pending_frame * pf = nullptr;
	if (!pending_frames.empty() && pending_frames.back().frame_index == frame_index)
	{
		pf = &pending_frames.back();
	}
	else
	{
		pending_frames.push_back({.frame_index = frame_index});
		pf = &pending_frames.back();
	}

	for (const auto & sub : data)
	{
		size_t old_size = pf->data.size();
		pf->data.resize(old_size + sub.size());
		memcpy(pf->data.data() + old_size, sub.data(), sub.size());
	}

	if (!partial)
		pending_cv.notify_one();
}

void pico_video_decoder::frame_completed(
	const wivrn::from_headset::feedback & feedback,
	const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info)
{
	std::lock_guard lock(frame_info_mutex);
	pending_frame_infos.push_back(frame_info{
		.frame_index = feedback.frame_index,
		.feedback = feedback,
		.view_info = view_info,
	});
}

void pico_video_decoder::on_image_available_cb(void * ctx, AImageReader * reader)
{
	auto * self = static_cast<pico_video_decoder *>(ctx);
	try
	{
		self->on_image_available(reader);
	}
	catch (std::exception & e)
	{
		spdlog::error("on_image_available: {}", e.what());
	}
}

void pico_video_decoder::on_image_available(AImageReader * reader)
{
	AImage * tmp;
	check(AImageReader_acquireLatestImage(image_reader.get(), &tmp), "AImageReader_acquireLatestImage");
	std::shared_ptr<AImage> image(tmp, [](AImage * img) { AImage_delete(img); });

	int64_t fake_ts;
	check(AImage_getTimestamp(image.get(), &fake_ts), "AImage_getTimestamp");
	uint64_t frame_index = (fake_ts + 5'000'000) / 10'000'000;

	frame_info info{};
	bool found = false;
	{
		std::lock_guard lock(frame_info_mutex);
		for (auto it = pending_frame_infos.begin(); it != pending_frame_infos.end(); ++it)
		{
			if (it->frame_index == frame_index)
			{
				info = *it;
				found = true;
				pending_frame_infos.erase(it);
				break;
			}
		}
		while (!pending_frame_infos.empty() && pending_frame_infos.front().frame_index < frame_index)
			pending_frame_infos.erase(pending_frame_infos.begin());
	}

	if (!found)
	{
		spdlog::warn("No frame info for decoded frame {}, dropping", frame_index);
		return;
	}

	AHardwareBuffer * hb = nullptr;
	check(AImage_getHardwareBuffer(image.get(), &hb), "AImage_getHardwareBuffer");
	if (!hb)
	{
		spdlog::warn("No hardware buffer in decoded image");
		return;
	}

	AHardwareBuffer_Desc desc{};
	AHardwareBuffer_describe(hb, &desc);

	AHardwareBuffer_acquire(hb);

	auto frame = std::make_shared<pico_decoded_frame>();
	frame->hardware_buffer = hb;
	frame->width = desc.width;
	frame->height = desc.height;
	frame->frame_index = frame_index;
	frame->valid = true;

	spdlog::warn("Decoded frame {} available ({}x{})", frame_index, desc.width, desc.height);

	if (on_frame_decoded)
		on_frame_decoded(frame);
}

void pico_video_decoder::supported_codecs(std::vector<wivrn::video_codec> & result)
{
	for (auto codec : {wivrn::video_codec::av1, wivrn::video_codec::h265, wivrn::video_codec::h264})
	{
		AMediaCodec * mc = AMediaCodec_createDecoderByType(mime(codec));
		bool supported = mc != nullptr;
		if (mc)
			AMediaCodec_delete(mc);
		if (supported)
			result.push_back(codec);
		spdlog::info("Video codec {}: {}supported", (int)codec, supported ? "" : "NOT ");
	}
}
