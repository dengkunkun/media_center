#ifndef STREAM_HPP
#define STREAM_HPP

#include "rtc/rtc.hpp"
#include <mutex>
#include <rtc/common.hpp>
#include <string>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>


// #define CAPTURE_DATA_TYPE		float
 #define CAPTURE_DATA_TYPE		int16_t

class Stream {
public:
	virtual void start() = 0;
	virtual void stop() = 0;
	virtual void load_next_sample() = 0;

	virtual uint64_t get_sample_time_us() = 0;
	virtual uint64_t get_sample_duration_us() = 0;
	virtual rtc::binary get_sample() = 0;
};


class AudioVoidStream : public Stream {
public:
	AudioVoidStream();
	virtual ~AudioVoidStream();

public:
	virtual void start() override;
	virtual void stop() override;
	virtual void load_next_sample() override;

	rtc::binary get_sample() override;
	uint64_t get_sample_time_us() override;
	uint64_t get_sample_duration_us() override;

public:
	uint64_t _sample_duration_us;
	uint64_t _sample_time_us = 0;

	rtc::binary _sample;
};


extern "C" {
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>

#include <libavutil/mem.h>
#include <libavutil/timestamp.h>
#include <libavutil/avutil.h>

#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>

#include <libavcodec/bsf.h>
#include <libavcodec/avcodec.h>
}


class VideoFileLoader : public Stream {
public:
	VideoFileLoader(const std::string& path);
	virtual ~VideoFileLoader();

	void start() override;
	void stop() override;
	void load_next_sample() override;

	uint64_t get_sample_time_us() override;
	uint64_t get_sample_duration_us() override;
	rtc::binary get_sample() override;


public:
	uint64_t _sample_duration_us;
	uint64_t _sample_time_us = 0;
	rtc::binary _sample;


public:
	void make_nalu_packet(const uint8_t *p, int n);
	std::vector<std::byte> initial_nalus();


	std::optional<std::vector<std::byte>> _previous_unit_type5 = std::nullopt;
	std::optional<std::vector<std::byte>> _previous_unit_type7 = std::nullopt;
	std::optional<std::vector<std::byte>> _previous_unit_type8 = std::nullopt;

public:
	const AVOutputFormat *output_fmt = NULL;
	AVFormatContext *input_fmt_ctx = NULL;
	AVFormatContext *output_fmt_ctx = NULL;
	AVPacket *pkt = NULL;
	std::string in_filename;
	std::string out_filename;
	int e = 0;
	int stream_index = 0;
	int *stream_mapping = NULL;
	int stream_mapping_size = 0;

	AVBSFContext *bsf_ctx = NULL;

};


class CameraStream : public Stream {
public:
	CameraStream(int fps);
	virtual ~CameraStream();

	void start() override;
	void stop() override;
	void load_next_sample() override;

	uint64_t get_sample_time_us() override;
	uint64_t get_sample_duration_us() override;
	rtc::binary get_sample() override;

	void make_nalu_packet(const uint8_t *p, int n);

public:
	uint64_t _sample_duration_us;
	uint64_t _sample_time_us = 0;
	rtc::binary _sample;

public:
	const AVInputFormat *video_input_fmt = NULL;
	AVFormatContext *video_fmt_ctx = NULL;
	AVPacket *video_pkt = NULL;
};



#define AUDIO_SAMPLE_FMT		AV_SAMPLE_FMT_S16
#define AUDIO_SAMPLE_RATE		48000
#define AUDIO_BIT_RATE			64000

class MicphoneStream : public Stream {
public:
	MicphoneStream();
	virtual ~MicphoneStream();

	void start() override;
	void stop() override;
	void load_next_sample() override;

	uint64_t get_sample_time_us() override;
	uint64_t get_sample_duration_us() override;
	rtc::binary get_sample() override;

	void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt);

public:
	uint64_t _sample_duration_us;
	uint64_t _sample_time_us = 0;
	rtc::binary _sample;

public:
	AVFormatContext *_input_fmt_ctx = NULL;
	const char *_input_url;
	AVDictionary *_input_opts = NULL;
	AVPacket *_input_pkt;

	AVCodecContext *_codec_ctx = NULL;
	AVFrame *_frame;
	AVPacket *_output_pkt;

	int16_t _tmp_buffer[48000];
	int _tmp_buffer_index = 0;
	int64_t _next_pts;
};


class PwMicStream : public Stream {
public:
	PwMicStream();
	virtual ~PwMicStream();

	void start() override;
	void stop() override;
	void load_next_sample() override;

	uint64_t get_sample_time_us() override;
	uint64_t get_sample_duration_us() override;
	rtc::binary get_sample() override;

	void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt);

public:
	uint64_t _sample_duration_us;
	uint64_t _sample_time_us = 0;
	rtc::binary _sample;

public:
	AVFormatContext *_input_fmt_ctx = NULL;
	const char *_input_url;
	AVDictionary *_input_opts = NULL;
	AVPacket *_input_pkt;

	AVCodecContext *_codec_ctx = NULL;
	AVFrame *_frame;
	AVPacket *_output_pkt;

	int16_t _tmp_buffer[48000];
	int _tmp_buffer_index = 0;
	int64_t _next_pts;

	std::mutex _packet_q_mutex;
	std::queue<std::vector<CAPTURE_DATA_TYPE>> _packet_q;

public:
	// struct pw_main_loop *loop;
	struct pw_thread_loop *_loop;
	struct pw_stream *_stream;

	struct spa_audio_info _format;

	FILE *_raw_file;

};

#endif	// STREAM_HPP
