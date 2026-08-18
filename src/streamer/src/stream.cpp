#include "stream.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <libavcodec/bsf.h>
#include <libavcodec/packet.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <mutex>
#include <rtc/common.hpp>

#include <arpa/inet.h>
#include <vector>
#include <thread>


#include "pipewire/keys.h"
#include "spa/param/audio/raw.h"



AudioVoidStream::AudioVoidStream()
{
	this->_sample_duration_us = 1000 * 1000 / 50;
}

AudioVoidStream::~AudioVoidStream()
{
	stop();
}

void AudioVoidStream::start()
{
	_sample_time_us = std::numeric_limits<uint64_t>::max() - _sample_duration_us + 1;
	load_next_sample();
}

void AudioVoidStream::stop()
{
	_sample = {};
	_sample_time_us = 0;
}

void AudioVoidStream::load_next_sample()
{
	unsigned char void_sound_frame[] = {
		0xfc,0x7f,0xfb,0xd9,0x82,0xee,0x24,0x59,0x85,0xb5,0x84,0x4e,0xcc,0x14,0x57,0xcd,0xe4,0x24,0x92,0x2a,0xab,0x38,0x23,0xfe,0x8a,0xff,0x31,0xa6,0x44,0x52,0xd6,0x2a,0x90,0x3e,0xd7,0xea,0x3a,0xd9,0x70,0x4d,0xf3,0xdc,0xdc,0x7a,0x66,0x42,0x52,0x3c,0xa9,0xde,0x5c,0xa3,0x84,0x1f,0xab,0x2e,0xb4,0x91,0x6a,0x58,0xfb,0x8,0x98,0xfc,0x50,0x39,0x66,0xac,0x1,0x15,0x7b,0x2f,0xc7,0x27,0x3a,0xc1,0xed,0x5a,0x42,0x53,0x53,0xd4,0x2e,0xc4,0xd0,0x70,0xf7,0x32,0x3f,0x49,0x22,0x20,0x83,0xab,0x3a,0xfc,0x71,0xf6,0x49,0xd2,0x27,0xc0,0xdf,0x65,0x82,0xf8,0xff,0xf9,0xf8,0x4d,0xa4,0xda,0x57,0x31,0xfe,0xc3,0x50,0x84,0x40,0x84,0x91,0x20,0x44,0x32,0x33,
	};
	if (_sample.size() == 0) {
		_sample.assign((rtc::byte *)void_sound_frame, (rtc::byte *)void_sound_frame + sizeof(void_sound_frame));
	}
	_sample_time_us += _sample_duration_us;
}

rtc::binary AudioVoidStream::get_sample()
{
	return _sample;
}

uint64_t AudioVoidStream::get_sample_time_us()
{
	return _sample_time_us;
}

uint64_t AudioVoidStream::get_sample_duration_us()
{
	return _sample_duration_us;
}




 
#if	0
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
	AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

	printf("%s[%d]:pts:%s,%s,dts:%s,%s,duration:%s,%s\n",
			tag, pkt->stream_index,
			(const char *)av_ts2str(pkt->pts), (const char *)av_ts2timestr(pkt->pts, time_base),
			(const char *)av_ts2str(pkt->dts), (const char *)av_ts2timestr(pkt->dts, time_base),
			(const char *)av_ts2str(pkt->duration), (const char *)av_ts2timestr(pkt->duration, time_base));
}
#endif


VideoFileLoader::VideoFileLoader(const std::string& path)
{
	(void)path;
	in_filename = path;
	out_filename = "hs.flv";

	this->_sample_duration_us = 1000 * 1000 / 24;
}

VideoFileLoader::~VideoFileLoader()
{

}

void VideoFileLoader::start()
{
	std::cout << "FileLoader::start\n";

	if (!(pkt = av_packet_alloc())) {
		fprintf(stderr, "av_packet_alloc failed\n");
		return;
	}

	if ((e = avformat_open_input(&input_fmt_ctx, in_filename.c_str(), 0, 0)) < 0) {
		fprintf(stderr, "avformat_open_input failed:%s\n", in_filename.c_str());
		return;
	}

	if ((e = avformat_find_stream_info(input_fmt_ctx, 0)) < 0) {
		fprintf(stderr, "avformat_find_stream_info failed:%d\n", e);
		return;
	}

	av_dump_format(input_fmt_ctx, 0, in_filename.c_str(), 0);

	avformat_alloc_output_context2(&output_fmt_ctx, NULL, NULL, out_filename.c_str());
	if (!output_fmt_ctx) {
		fprintf(stderr, "avformat_alloc_output_context2 failed\n");
		e = AVERROR_UNKNOWN;
		return;
	}

	stream_mapping_size = input_fmt_ctx->nb_streams;
	stream_mapping = (int *)av_calloc(stream_mapping_size, sizeof(*stream_mapping));
	if (!stream_mapping) {
		e = AVERROR(ENOMEM);
		return;
	}

	output_fmt = output_fmt_ctx->oformat;

	for (unsigned int i = 0; i < input_fmt_ctx->nb_streams; i++) {
		AVStream *out_stream;
		AVStream *in_stream = input_fmt_ctx->streams[i];
		AVCodecParameters *in_codecpar = in_stream->codecpar;

		if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
				in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
				in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
			stream_mapping[i] = -1;
			continue;
		}

		stream_mapping[i] = stream_index++;

		if (!(out_stream = avformat_new_stream(output_fmt_ctx, NULL))) {
			fprintf(stderr, "avformat_new_stream failed\n");
			e = AVERROR_UNKNOWN;
			return;
		}

		if ((e = avcodec_parameters_copy(out_stream->codecpar, in_codecpar)) < 0) {
			fprintf(stderr, "avcodec_parameters_copy failed:%d\n", e);
			return;
		}
		out_stream->codecpar->codec_tag = 0;
	}
	av_dump_format(output_fmt_ctx, 0, out_filename.c_str(), 1);




//	if (!(output_fmt->flags & AVFMT_NOFILE)) {
//		if ((e = avio_open(&output_fmt_ctx->pb, out_filename.c_str(), AVIO_FLAG_WRITE)) < 0) {
//			fprintf(stderr, "avio_open failed:%s\n", out_filename.c_str());
//			return;
//		}
//	}

#if	0
	if ((e = avformat_write_header(output_fmt_ctx, NULL)) < 0) {
		fprintf(stderr, "avformat_write_header failed:%d\n", e);
		return;
	}
#endif

	const AVBitStreamFilter *bsf = av_bsf_get_by_name("hevc_mp4toannexb");

	av_bsf_alloc(bsf, &bsf_ctx);
	if (bsf_ctx == NULL) {
		fprintf(stderr, "av_bsf_alloc failed\n");
		return;
	}
	if ((e = avcodec_parameters_copy(bsf_ctx->par_in, input_fmt_ctx->streams[0]->codecpar)) < 0) {
		fprintf(stderr, "avcodec_parameters_copy failed\n");
		return;
	}
	av_bsf_init(bsf_ctx);


	_sample_time_us = std::numeric_limits<uint64_t>::max() - _sample_duration_us + 1;
	load_next_sample();
}

void VideoFileLoader::stop()
{
	std::cout << "FileLoader::stop\n";
	_sample = {};
	_sample_time_us = 0;

	if (pkt != NULL) {
		av_packet_free(&pkt);
		pkt = NULL;
	}

	if (input_fmt_ctx != NULL) {
		avformat_close_input(&input_fmt_ctx);
		input_fmt_ctx = NULL;
	}

	if (output_fmt_ctx != NULL) {
//		if (output_fmt_ctx && !(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
//			avio_closep(&output_fmt_ctx->pb);
//		}
		avformat_free_context(output_fmt_ctx);
		output_fmt_ctx = NULL;
	}

	if (stream_mapping != NULL) {
		av_freep(&stream_mapping);
		stream_mapping = NULL;
	}

	if (bsf_ctx != NULL) {
		av_bsf_free(&bsf_ctx);
		bsf_ctx = NULL;
	}
}

#define __pack_u32(p, i, u)\
{\
	(p)[(i)+0] = ((u)>>24)&0xff;\
	(p)[(i)+1] = ((u)>>16)&0xff;\
	(p)[(i)+2] = ((u)>>8)&0xff;\
	(p)[(i)+3] = ((u)>>0)&0xff;\
}
#define __pack_bytes(p, i, src_p, n)\
	memcpy((p)+(i), (src_p), (n))

void VideoFileLoader::make_nalu_packet(const uint8_t *p, int n)
{
	if (n < 4) {
		return;
	}
	//std::cout << "make_nalu_packet:" << n << "\n";

	int state = 0;
	static uint8_t tmp_buffer[1*1024*1024];
	unsigned base_i = 4;
	unsigned int length = 0;
	int j = 0;
	int i = 4;
	for (; i < n; ++i) {
		switch (state) {
		case 0:
			if (p[i] == 0x00) {
				state = 1;
			}
			break;

		case 1:
			if (p[i] == 0x00) {
				state = 2;
			} else {
				state = 0;
			}
			break;

		case 2:
			if (p[i] == 0x01) {
				length = i+1-3-base_i;
				__pack_u32(tmp_buffer, j, length);
				//std::cout << "length:" << length << "\n";
				j += 4;
				__pack_bytes(tmp_buffer, j, p+base_i, length);
				j += length;

				base_i = i+1;
				state = 0;
			} else if (p[i] == 0x00) {
				state = 3;
			} else {
				state = 0;

			}
			break;

		case 3:
			if (p[i] == 0x01) {
				length = i+1-4-base_i;
				__pack_u32(tmp_buffer, j, length);
				//std::cout << "length:" << length << "\n";
				j += 4;
				__pack_bytes(tmp_buffer, j, p+base_i, length);
				j += length;

				base_i = i+1;
				state = 0;
			} else {
				state = 0;
			}
			break;
		}
	}

	length = i-base_i;
	__pack_u32(tmp_buffer, j, length);
	//				std::cout << "length:" << length << "\n";
	j += 4;
	__pack_bytes(tmp_buffer, j, p+base_i, length);
	j += length;


	_sample.assign((std::byte *)tmp_buffer, (std::byte *)tmp_buffer+j);
	//	std::cout << "sample.size():" << sample.size() << "\n";
	_sample_time_us += _sample_duration_us;
}


void VideoFileLoader::load_next_sample()
{
	AVStream *in_stream;
	AVStream *out_stream;

	for (;;) {
		if ((e = av_read_frame(input_fmt_ctx, pkt)) < 0) {
			return;
		}
		if (pkt->stream_index == 0) {
			break;
		} else {
			// printf("audio:%p,%u,%ld,%d/%d\n", pkt->data, pkt->size, pkt->pts, pkt->time_base.num, pkt->time_base.den);
			av_packet_unref(pkt);
		}
	}

	in_stream  = input_fmt_ctx->streams[pkt->stream_index];
	if (pkt->stream_index >= stream_mapping_size || stream_mapping[pkt->stream_index] < 0) {
		av_packet_unref(pkt);
	}

	pkt->stream_index = stream_mapping[pkt->stream_index];
	out_stream = output_fmt_ctx->streams[pkt->stream_index];
//	log_packet(input_fmt_ctx, pkt, "in");


	/* copy packet */
//	av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
//	pkt->pos = -1;
//	log_packet(output_fmt_ctx, pkt, "out");

#if	0
	for (int i = 0; i < 64; ++i) {
		printf("%02x ", pkt->data[i]);
	}
	printf("\n");
#endif

	if ((e = av_bsf_send_packet(bsf_ctx, pkt)) < 0) {
		av_packet_unref(pkt);
		fprintf(stderr, "av_bsf_send_packet failed:%d\n", e);
	}
	av_packet_unref(pkt);
	while ((e = av_bsf_receive_packet(bsf_ctx, pkt)) == 0) {
		// av_packet_unref(pkt);
	}

#if	0
	//fprintf(stderr, "av_bsf_receive_packet failed:%d\n", e);
	for (int i = 0; i < pkt->size; ++i) {
		printf("%02x ", pkt->data[i]);
	}
	printf("\n");
#endif


	make_nalu_packet(pkt->data, pkt->size);

#if	0
	if ((e = av_interleaved_write_frame(output_fmt_ctx, pkt)) < 0) {
		/* pkt is now blank (av_interleaved_write_frame() takes ownership of
		 * its contents and resets pkt), so that no unreferencing is necessary.
		 * This would be different if one used av_write_frame(). */
		fprintf(stderr, "av_interleaved_write_frame failed:%d\n", e);
	}
#endif
	av_packet_unref(pkt);



	// FIXME 此处代码是处理H264格式，需要修改为支持HEVC格式
#if	1
	size_t i = 0;
	while (i < _sample.size()) {
		assert(i + 4 < _sample.size());
		auto length_ptr = (uint32_t *)(_sample.data() + i);
		uint32_t length;
		std::memcpy(&length, length_ptr, sizeof(uint32_t));
		length = ntohl(length);
		//std::cout << "length:" << length << "\n";

		auto nalu_start_index = i + 4;
		auto nalu_end_index = nalu_start_index + length;
		assert(nalu_end_index <= _sample.size());

		auto header = reinterpret_cast<rtc::NalUnitHeader *>(_sample.data() + nalu_start_index);
		auto type = header->unitType();
		std::cout << "type:" << (unsigned int)(unsigned char)type << "\n";

		switch (type) {
		case 7:
			_previous_unit_type7 = {_sample.begin() + i, _sample.begin() + nalu_end_index};
			break;
		case 8:
			_previous_unit_type8 = {_sample.begin() + i, _sample.begin() + nalu_end_index};;
			break;
		case 5:
			_previous_unit_type5 = {_sample.begin() + i, _sample.begin() + nalu_end_index};;
			break;
		}
		i = nalu_end_index;
	}
#endif

}

uint64_t VideoFileLoader::get_sample_time_us()
{
	return _sample_time_us;
}

uint64_t VideoFileLoader::get_sample_duration_us()
{
	return _sample_duration_us;
}

rtc::binary VideoFileLoader::get_sample()
{
	return _sample;
}

std::vector<std::byte> VideoFileLoader::initial_nalus()
{
	std::cout << "VideoFileLoader::initial_nalus\n";

	std::vector<std::byte> units{};
	if (_previous_unit_type7.has_value()) {
		auto nalu = _previous_unit_type7.value();
		units.insert(units.end(), nalu.begin(), nalu.end());
	}
	if (_previous_unit_type8.has_value()) {
		auto nalu = _previous_unit_type8.value();
		units.insert(units.end(), nalu.begin(), nalu.end());
	}
	if (_previous_unit_type5.has_value()) {
		auto nalu = _previous_unit_type5.value();
		units.insert(units.end(), nalu.begin(), nalu.end());
	}

	return units;
}



void CameraStream::make_nalu_packet(const uint8_t *p, int n)
{
	if (n < 4) {
		return;
	}
//	std::cout << "make_nalu_packet:" << n << "\n";

	int state = 0;
	static uint8_t tmp_buffer[1*1024*1024];
	unsigned base_i = 0;
	unsigned int length = 0;
	int j = 0;
	int i = 0;

	// 寻找00 00 00 01序列
	for (; i < n; ++i) {
		switch (state) {
		case 0:
			if (p[i] == 0x00) {
				state = 1;
			}
			break;

		case 1:
			if (p[i] == 0x00) {
				state = 2;
			} else {
				state = 0;
			}
			break;

		case 2:
			if (p[i] == 0x00) {
				state = 3;
			} else {
				state = 0;
			}
			break;

		case 3:
			if (p[i] == 0x01) {
				if (base_i > 0) {
					length = i+1-4-base_i;
					__pack_u32(tmp_buffer, j, length);
					// std::cout << "length:" << length << ",base_i:" << base_i << "\n";
					j += 4;
					__pack_bytes(tmp_buffer, j, p+base_i, length);
					j += length;
				}

				base_i = i+1;
				state = 0;
			} else {
				// 在00...序列中找最后的01
				if (p[i] == 0x00) {
					state = 3;
				} else {
					state = 0;
				}
			}
			break;
		}
	}

	if (base_i > 0) {
		length = i-base_i;
		__pack_u32(tmp_buffer, j, length);
		// std::cout << "length:" << length << ",base_i:" << base_i << "\n";
		j += 4;
		__pack_bytes(tmp_buffer, j, p+base_i, length);
		j += length;
	}

	if (j > 0) {
		_sample.assign((std::byte *)tmp_buffer, (std::byte *)tmp_buffer+j);
	}
	_sample_time_us += _sample_duration_us;
}



CameraStream::CameraStream(int fps)
{
	// 30fps
	this->_sample_duration_us = 1000 * 1000 / fps;
}

CameraStream::~CameraStream()
{

}

void CameraStream::start()
{
	_sample_time_us = 0;

	// std::cout << "CameraStream::start\n";

	if (!video_fmt_ctx) {
		video_input_fmt = av_find_input_format("v4l2");
		if (!video_input_fmt) {
			fprintf(stderr, "av_find_input_format failed\n");
			return;
		} else {
			printf("av_find_input_format ok:@%s@,@%s@,%d,@%s@,@%s@\n",
					video_input_fmt->name,
					video_input_fmt->long_name,
					video_input_fmt->flags,
					video_input_fmt->extensions,
					video_input_fmt->mime_type);
		}
	}


#if	0
	av_dict_set(&opts, "pixel_format", "yuyv422", 0);
//	av_dict_set(&opts, "input_format", "mjpeg", 0);
//	av_dict_set(&opts, "input_format", "h264", 0);
#endif

	int e = 0;
	AVDictionary *opts = NULL;
	av_dict_set(&opts, "pixel_format", "none", 0);
	av_dict_set(&opts, "input_format", "hevc", 0);


	// av_dict_set(&opts, "video_size", "1280x720", 0);
	av_dict_set(&opts, "video_size", "640x480", 0);
	av_dict_set(&opts, "framerate", "30", 0);

	if ((e = avformat_open_input(&video_fmt_ctx, "/dev/video0", video_input_fmt, &opts)) < 0) {
		// av_log(NULL, AV_LOG_DEBUG, "avformat_open_input failed:%s\n", av_err2str(e));
		return;
	}


	if (!(video_pkt = av_packet_alloc())) {
		fprintf(stderr, "av_packet_alloc failed\n");
		return;
	}
}

void CameraStream::stop()
{
	// std::cout << "CameraStream::stop\n";
	avformat_close_input(&video_fmt_ctx);
	av_packet_free(&video_pkt);
}

void CameraStream::load_next_sample()
{
	if (!video_fmt_ctx) {
		return;
	}

	// std::cout << "CameraStream::load_next_sample\n";
	int e = 0;
	if ((e = av_read_frame(video_fmt_ctx, video_pkt)) == 0) {
#if	0
		av_log(NULL, AV_LOG_DEBUG, "[%10"PRIi64"]%d,%p\n", video_pkt->pts, video_pkt->size, video_pkt->data);
#if	0
		for (int i = 0; i < video_pkt->size; ++i) {
			printf("%02x ", video_pkt->data[i]);
			if (i == 32) {
				break;
			}
		}
		printf("\n");
#endif
#endif
		make_nalu_packet(video_pkt->data, video_pkt->size);

		av_packet_unref(video_pkt);
	} else {

	}
}


uint64_t CameraStream::get_sample_time_us()
{
	return _sample_time_us;
}

uint64_t CameraStream::get_sample_duration_us()
{
	return _sample_duration_us;
}

rtc::binary CameraStream::get_sample()
{
	return _sample;
}




MicphoneStream::MicphoneStream()
{
	_input_url = "hw:0,0";
	this->_sample_duration_us = 1000 * 1000 / 50;

}

MicphoneStream::~MicphoneStream()
{
	stop();
}

void MicphoneStream::start()
{
	int e = 0;
	const AVInputFormat *input_fmt = av_find_input_format("alsa");
	if (!input_fmt) {
		fprintf(stderr, "av_find_input_format failed\n");
		return;
	}

	// 44100,48000,96000
	av_dict_set(&_input_opts, "sample_rate", "48000", 0);
	// 只支持双通道
	// av_dict_set(&_input_opts, "ch_layout", "mono", 0);
	av_dict_set(&_input_opts, "ch_layout", "stereo", 0);
	
	if ((e = avformat_open_input(&_input_fmt_ctx, _input_url, input_fmt, &_input_opts)) < 0) {
		av_log(NULL, AV_LOG_DEBUG, "avformat_open_input failed:%d\n", e);
		av_dict_free(&_input_opts);
		return;
	}
	av_dict_free(&_input_opts);

	if (!(_input_pkt = av_packet_alloc())) {
		fprintf(stderr, "av_packet_alloc failed\n");
		return;
	}

	if (avformat_find_stream_info(_input_fmt_ctx, NULL) < 0) {
		av_log(NULL, AV_LOG_DEBUG, "avformat_find_stream_info failed\n");
		avformat_close_input(&_input_fmt_ctx);
		return;
	}
	av_dump_format(_input_fmt_ctx, 0, _input_url, 0);




	const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
	if (!codec) {
		fprintf(stderr, "avcodec_find_encoder failed\n");
		return;
	}

	if ((_codec_ctx = avcodec_alloc_context3(codec)) == NULL) {
		fprintf(stderr, "avcodec_alloc_context3 failed\n");
		return;
	}

	_codec_ctx->bit_rate = AUDIO_BIT_RATE;
	_codec_ctx->sample_fmt = AUDIO_SAMPLE_FMT;
	_codec_ctx->sample_rate = AUDIO_SAMPLE_RATE;
	AVChannelLayout ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	av_channel_layout_copy(&_codec_ctx->ch_layout, &ch_layout);

	if (avcodec_open2(_codec_ctx, codec, NULL) < 0) {
		fprintf(stderr, "avcodec_open2 failed\n");
		return;
	}

	if (!(_output_pkt = av_packet_alloc())) {
		fprintf(stderr, "av_packet_alloc failed\n");
		return;
	}

	if (!(_frame = av_frame_alloc())) {
		fprintf(stderr, "av_frame_alloc failed\n");
		return;
	}

	_frame->nb_samples		= _codec_ctx->frame_size;
	_frame->format			= _codec_ctx->sample_fmt;
	if ((e = av_channel_layout_copy(&_frame->ch_layout, &_codec_ctx->ch_layout)) < 0) {
		fprintf(stderr, "av_channel_layout_copy failed:%d\n", e);
		return;
	}

	if ((e = av_frame_get_buffer(_frame, 0)) < 0) {
		fprintf(stderr, "av_frame_get_buffer failed:%d\n", e);
		return;
	}



	_sample_time_us = std::numeric_limits<uint64_t>::max() - _sample_duration_us + 1;
	load_next_sample();
}

void MicphoneStream::stop()
{
	avformat_close_input(&_input_fmt_ctx);
	av_packet_free(&_input_pkt);


	av_frame_free(&_frame);
	av_packet_free(&_output_pkt);
	avcodec_free_context(&_codec_ctx);

	_sample = {};
	_sample_time_us = 0;

}

void MicphoneStream::encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt)
{
	int e = 0;

	if ((e = avcodec_send_frame(ctx, frame)) < 0) {
		fprintf(stderr, "avcodec_send_frame failed:%d\n", e);
		exit(1);
	}

	// FIXME 如果一个frame产生多个packet，这里未做处理
	while (e >= 0) {
		e = avcodec_receive_packet(ctx, pkt);
		if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) {
			return;
		} else if (e < 0) {
			fprintf(stderr, "avcodec_receive_packet failed:%d\n", e);
			exit(1);
		}


		_sample.assign((rtc::byte *)pkt->data, (rtc::byte *)pkt->data + pkt->size);
		_sample_time_us += _sample_duration_us;


		av_packet_unref(pkt);
	}
}



void MicphoneStream::load_next_sample()
{
	int16_t *q = (int16_t*)_frame->data[0];

	int frame_index = 0;
	int e;
	int pkt_size;
	int frame_size = _frame->nb_samples*_codec_ctx->ch_layout.nb_channels;
	int delta;

	int16_t *pkt_data = NULL;
	int16_t stereo_buffer[48000];

	while ((e = av_read_frame(_input_fmt_ctx, _input_pkt)) == 0) {
		pkt_data = (int16_t *)_input_pkt->data;
		pkt_size = _input_pkt->size/sizeof(int16_t);

#if	0
		// MONO
		for (int i = 0; i < pkt_size; ++i) {
			stereo_buffer[i*2] = pkt_data[i];
			stereo_buffer[i*2+1] = pkt_data[i];
		}
		pkt_size *= 2;
#else
		memcpy(stereo_buffer, pkt_data, pkt_size*sizeof(int16_t));
#endif
		

		if (_tmp_buffer_index > 0) {
			memcpy(q, _tmp_buffer, _tmp_buffer_index*sizeof(int16_t));
			frame_index += _tmp_buffer_index;
			_tmp_buffer_index = 0;
		}
		if (frame_index+pkt_size < frame_size) {
			delta = pkt_size;
			//memcpy(q+frame_index, _input_pkt->data, delta*sizeof(int16_t));
			memcpy(q+frame_index, stereo_buffer, delta*sizeof(int16_t));
			frame_index += delta;
		} else {
			// 收完一包
			delta = frame_size - frame_index;
			//memcpy(q+frame_index, _input_pkt->data, delta*sizeof(int16_t));
			memcpy(q+frame_index, stereo_buffer, delta*sizeof(int16_t));
			frame_index += delta;

			// 剩余数据塞入缓冲区
			if (pkt_size - delta > 0) {
				// memcpy(_tmp_buffer, _input_pkt->data+delta*sizeof(int16_t), (pkt_size-delta)*sizeof(int16_t));
				memcpy(_tmp_buffer, stereo_buffer+delta, (pkt_size-delta)*sizeof(int16_t));
				_tmp_buffer_index = pkt_size - delta;
			}
			av_packet_unref(_input_pkt);
			break;
		}

		av_packet_unref(_input_pkt);
	}


	encode(_codec_ctx, _frame, _output_pkt);


	_frame->pts = _next_pts;
	_next_pts += _frame->nb_samples;
}

uint64_t MicphoneStream::get_sample_time_us()
{
	return _sample_time_us;
}

uint64_t MicphoneStream::get_sample_duration_us()
{
	return _sample_duration_us;
}

rtc::binary MicphoneStream::get_sample()
{
	return _sample;
}





 
static void on_process(void *userdata)
{
	PwMicStream *ctx = (PwMicStream *)userdata;
	struct pw_buffer *b;
	struct spa_buffer *buf;
	CAPTURE_DATA_TYPE *samples;
	uint32_t n_channels, n_samples;

	if ((b = pw_stream_dequeue_buffer(ctx->_stream)) == NULL) {
		pw_log_warn("pw_stream_dequeue_buffer:%m");
		return;
	}

	buf = b->buffer;
	if ((samples = (CAPTURE_DATA_TYPE *)buf->datas[0].data) == NULL) {
		return;
	}

	n_channels = ctx->_format.info.raw.channels;
	n_samples = buf->datas[0].chunk->size / sizeof(CAPTURE_DATA_TYPE);


	// LOCK?
	{
		std::unique_lock<std::mutex> lock(ctx->_packet_q_mutex);
		ctx->_packet_q.push(std::vector<CAPTURE_DATA_TYPE>(samples, samples+n_samples));
	}

#if	0
	printf("%d,%d\n", n_channels, n_samples);
	fwrite(samples, sizeof(CAPTURE_DATA_TYPE), n_samples, ctx->_raw_file);
	fflush(ctx->_raw_file);
#endif


	pw_stream_queue_buffer(ctx->_stream, b);
}
 
static void on_param_changed(void *_data, uint32_t id, const struct spa_pod *param)
{
	PwMicStream *data = (PwMicStream *)_data;

	/* NULL means to clear the format */
	if (param == NULL || id != SPA_PARAM_Format) {
		return;
	}

	if (spa_format_parse(param, &data->_format.media_type, &data->_format.media_subtype) < 0) {
		return;
	}

	/* only accept raw audio */
	if (data->_format.media_type != SPA_MEDIA_TYPE_audio || data->_format.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
		return;
	}

	/* call a helper function to parse the format for us. */
	spa_format_audio_raw_parse(param, &data->_format.info.raw);

	fprintf(stdout, "rate:%d,channels:%d\n", data->_format.info.raw.rate, data->_format.info.raw.channels);
}
 
static struct pw_stream_events stream_events = {
};
 
static void do_quit(void *userdata, int signal_number)
{
	(void)signal_number;
	PwMicStream *ctx = (PwMicStream *)userdata;
	pw_thread_loop_stop(ctx->_loop);
}


PwMicStream::PwMicStream()
{
	_input_url = "hw:0,0";
	this->_sample_duration_us = 1000 * 1000 / 50;

}

PwMicStream::~PwMicStream()
{
	stop();
}

void PwMicStream::start()
{
	int e = 0;


	const struct spa_pod *params[1];
	uint32_t n_params = 0;
	uint8_t buffer[1024];
	struct pw_properties *props;
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	/* make a main loop. If you already have another main loop, you can add
	 * the fd of this pipewire mainloop to it. */
	_loop = pw_thread_loop_new(NULL, NULL);
#if	0
	_raw_file = fopen("test.raw", "wb");
	if (this->_raw_file == NULL) {
		return;
	}
#endif

	pw_loop_add_signal(pw_thread_loop_get_loop(_loop), SIGINT, do_quit, this);
	pw_loop_add_signal(pw_thread_loop_get_loop(_loop), SIGTERM, do_quit, this);

	props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
			PW_KEY_MEDIA_CATEGORY, "Capture",
			PW_KEY_MEDIA_ROLE, "Music",
			PW_KEY_TARGET_OBJECT, "echo-cancel-source",
			// PW_KEY_TARGET_OBJECT, "alsa_input.usb-JinAudio_UGREEN_USB_MIC-CM379_202410160011-00.analog-stereo",
			NULL);


	/* uncomment if you want to capture from the sink monitor ports */
	/* pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true"); */

	stream_events.version = PW_VERSION_STREAM_EVENTS;
	stream_events.param_changed = on_param_changed;
	stream_events.process = on_process;
	_stream = pw_stream_new_simple(
			pw_thread_loop_get_loop(_loop),
			"audio-capture",
			props,
			&stream_events,
			this);

	/* Make one parameter with the supported formats. The SPA_PARAM_EnumFormat
	 * id means that this is a format enumeration (of 1 value).
	 * We leave the channels and rate empty to accept the native graph
	 * rate and channels. */
	struct spa_audio_info_raw info = {
		.format = SPA_AUDIO_FORMAT_S16,
		.flags = 0,
		.rate = 48000,
		.channels = 2,
	};
	params[n_params++] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

	pw_stream_connect(_stream,
			PW_DIRECTION_INPUT,
			PW_ID_ANY,
			(enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
			params, n_params);


#if	0
	const AVInputFormat *input_fmt = av_find_input_format("alsa");
	if (!input_fmt) {
		fprintf(stderr, "av_find_input_format failed\n");
		return;
	}

	// 44100,48000,96000
	av_dict_set(&_input_opts, "sample_rate", "48000", 0);
	// 只支持双通道
	// av_dict_set(&_input_opts, "ch_layout", "mono", 0);
	av_dict_set(&_input_opts, "ch_layout", "stereo", 0);
	
	if ((e = avformat_open_input(&_input_fmt_ctx, _input_url, input_fmt, &_input_opts)) < 0) {
		av_log(NULL, AV_LOG_DEBUG, "avformat_open_input failed:%d\n", e);
		av_dict_free(&_input_opts);
		return;
	}
	av_dict_free(&_input_opts);

	if (!(_input_pkt = av_packet_alloc())) {
		fprintf(stderr, "av_packet_alloc failed\n");
		return;
	}

	if (avformat_find_stream_info(_input_fmt_ctx, NULL) < 0) {
		av_log(NULL, AV_LOG_DEBUG, "avformat_find_stream_info failed\n");
		avformat_close_input(&_input_fmt_ctx);
		return;
	}
	av_dump_format(_input_fmt_ctx, 0, _input_url, 0);
#endif




	const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
	if (!codec) {
		fprintf(stderr, "avcodec_find_encoder failed\n");
		return;
	}

	if ((_codec_ctx = avcodec_alloc_context3(codec)) == NULL) {
		fprintf(stderr, "avcodec_alloc_context3 failed\n");
		return;
	}

	_codec_ctx->bit_rate = AUDIO_BIT_RATE;
	_codec_ctx->sample_fmt = AUDIO_SAMPLE_FMT;
	_codec_ctx->sample_rate = AUDIO_SAMPLE_RATE;
	AVChannelLayout ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	av_channel_layout_copy(&_codec_ctx->ch_layout, &ch_layout);

	if (avcodec_open2(_codec_ctx, codec, NULL) < 0) {
		fprintf(stderr, "avcodec_open2 failed\n");
		return;
	}

	if (!(_output_pkt = av_packet_alloc())) {
		fprintf(stderr, "av_packet_alloc failed\n");
		return;
	}

	if (!(_frame = av_frame_alloc())) {
		fprintf(stderr, "av_frame_alloc failed\n");
		return;
	}

	_frame->nb_samples		= _codec_ctx->frame_size;
	_frame->format			= _codec_ctx->sample_fmt;
	if ((e = av_channel_layout_copy(&_frame->ch_layout, &_codec_ctx->ch_layout)) < 0) {
		fprintf(stderr, "av_channel_layout_copy failed:%d\n", e);
		return;
	}

	if ((e = av_frame_get_buffer(_frame, 0)) < 0) {
		fprintf(stderr, "av_frame_get_buffer failed:%d\n", e);
		return;
	}


	pw_thread_loop_start(_loop);



	_sample_time_us = std::numeric_limits<uint64_t>::max() - _sample_duration_us + 1;
	load_next_sample();
}

void PwMicStream::stop()
{
	pw_thread_loop_stop(_loop);

	pw_stream_destroy(_stream);
	pw_thread_loop_destroy(_loop);

//	fclose(_raw_file);



	avformat_close_input(&_input_fmt_ctx);
	av_packet_free(&_input_pkt);


	av_frame_free(&_frame);
	av_packet_free(&_output_pkt);
	avcodec_free_context(&_codec_ctx);

	_sample = {};
	_sample_time_us = 0;

}

void PwMicStream::encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt)
{
	int e = 0;

	if ((e = avcodec_send_frame(ctx, frame)) < 0) {
		fprintf(stderr, "avcodec_send_frame failed:%d\n", e);
		exit(1);
	}

	// FIXME 如果一个frame产生多个packet，这里未做处理
	while (e >= 0) {
		e = avcodec_receive_packet(ctx, pkt);
		if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) {
			return;
		} else if (e < 0) {
			fprintf(stderr, "avcodec_receive_packet failed:%d\n", e);
			exit(1);
		}


		_sample.assign((rtc::byte *)pkt->data, (rtc::byte *)pkt->data + pkt->size);
		_sample_time_us += _sample_duration_us;


		av_packet_unref(pkt);
	}
}



void PwMicStream::load_next_sample()
{
#if	1
	int16_t *q = (int16_t*)_frame->data[0];

	int frame_index = 0;
	int e;
	int pkt_size;
	int frame_size = _frame->nb_samples*_codec_ctx->ch_layout.nb_channels;
	int delta;

	CAPTURE_DATA_TYPE *pkt_data = NULL;
	CAPTURE_DATA_TYPE stereo_buffer[48000];

	// while ((e = av_read_frame(_input_fmt_ctx, _input_pkt)) == 0) {
	std::vector<CAPTURE_DATA_TYPE> samples;
	
	while (_packet_q.size() > 5) {
		printf("pop\n");
	std::unique_lock<std::mutex> lock(_packet_q_mutex);
	_packet_q.pop();
	}
	while (1) {
		// LOCK?
		{
			if (_packet_q.size() > 0) {
				std::unique_lock<std::mutex> lock(_packet_q_mutex);
				samples = std::move(_packet_q.front());
				_packet_q.pop();
			} else {
				// 继续等待
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}
		}

		pkt_data = samples.data();
		pkt_size = samples.size();

#if	0
		// MONO
		for (int i = 0; i < pkt_size; ++i) {
			stereo_buffer[i*2] = pkt_data[i];
			stereo_buffer[i*2+1] = pkt_data[i];
		}
		pkt_size *= 2;
#else
		memcpy(stereo_buffer, pkt_data, pkt_size*sizeof(int16_t));
#endif
		

		if (_tmp_buffer_index > 0) {
			memcpy(q, _tmp_buffer, _tmp_buffer_index*sizeof(int16_t));
			frame_index += _tmp_buffer_index;
			_tmp_buffer_index = 0;
		}
		if (frame_index+pkt_size < frame_size) {
			delta = pkt_size;
			//memcpy(q+frame_index, _input_pkt->data, delta*sizeof(int16_t));
			memcpy(q+frame_index, stereo_buffer, delta*sizeof(int16_t));
			frame_index += delta;
		} else {
			// 收完一包
			delta = frame_size - frame_index;
			//memcpy(q+frame_index, _input_pkt->data, delta*sizeof(int16_t));
			memcpy(q+frame_index, stereo_buffer, delta*sizeof(int16_t));
			frame_index += delta;

			// 剩余数据塞入缓冲区
			if (pkt_size - delta > 0) {
				// memcpy(_tmp_buffer, _input_pkt->data+delta*sizeof(int16_t), (pkt_size-delta)*sizeof(int16_t));
				memcpy(_tmp_buffer, stereo_buffer+delta, (pkt_size-delta)*sizeof(int16_t));
				_tmp_buffer_index = pkt_size - delta;
			}
			// av_packet_unref(_input_pkt);
			break;
		}

		// av_packet_unref(_input_pkt);
	}


	encode(_codec_ctx, _frame, _output_pkt);
#endif


	_frame->pts = _next_pts;
	_next_pts += _frame->nb_samples;
}

uint64_t PwMicStream::get_sample_time_us()
{
	return _sample_time_us;
}

uint64_t PwMicStream::get_sample_duration_us()
{
	return _sample_duration_us;
}

rtc::binary PwMicStream::get_sample()
{
	return _sample;
}


