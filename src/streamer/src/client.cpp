#include "client.hpp"
#include <ctime>

#include <libavcodec/avcodec.h>
#include <sys/time.h>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

ClientTrack::ClientTrack(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sr_reporter)
{
	this->_track = track;
	this->_sr_reporter = sr_reporter;
}

Client::~Client()
{
	// 停止播放线程
	stop_pw_thread();

	// 释放资源
	free_codec_ctx();
}

void Client::set_state(State new_state)
{
	std::unique_lock lock(_mutex);
	this->_state = new_state;
}


static void on_process(void *userdata)
{
	Client *client = (Client *)userdata;
	struct pw_buffer *buffer;
	struct spa_buffer *sb;
	uint64_t n_frames;
	int stride;
	SAMPLE_DATA_TYPE *dst;

	if ((buffer = pw_stream_dequeue_buffer(client->_stream)) == NULL) {
		pw_log_warn("pw_stream_dequeue_buffer:%m");
		return;
	}

	sb = buffer->buffer;
	if ((dst = (SAMPLE_DATA_TYPE *)sb->datas[0].data) == NULL) {
		return;
	}

	stride = sizeof(SAMPLE_DATA_TYPE) * DEFAULT_CHANNELS;
	n_frames = sb->datas[0].maxsize / stride;
	// printf("n_frames:%d,%lu,%d\n", stride, buffer->requested, n_frames);

	if (buffer->requested) {
		n_frames = SPA_MIN(buffer->requested, n_frames);
	}

#if	0
	if (feof(ctx->raw_file)) {
		fseek(ctx->raw_file, 0, SEEK_SET);
	}

	if (fread(dst, stride, n_frames, ctx->raw_file) != (unsigned long)stride*n_frames) {

	} else {

	}
#endif

	
	std::vector<float> samples;
	if (!client->_frame_q.empty()) {
		samples = client->_frame_q.front();
		memcpy(dst, samples.data(), stride*n_frames);
		client->_frame_q.pop();
	} else {

	}

	sb->datas[0].chunk->offset = 0;
	sb->datas[0].chunk->stride = stride;
	sb->datas[0].chunk->size = n_frames * stride;

	pw_stream_queue_buffer(client->_stream, buffer);
}


int Client::init_codec_ctx()
{
	int e = 0;
	AVDictionary *options = NULL;
	av_dict_set(&options, "ch_layout", "stereo", 0);
	av_dict_set(&options, "sample_rate", "48000", 0);
	// av_dict_set(&options, "sample_fmt", "s16", 0);
	// printf("@%s@\n", av_get_sample_fmt_name(AV_SAMPLE_FMT_S16));

	std::cout << "init_codec_ctx\n";

	if (!(_pkt = av_packet_alloc())) {
		std::cerr << "av_packet_alloc failed\n";
		return 1;
	}

	if (!(_frame = av_frame_alloc())) {
		std::cerr << "av_frame_alloc failed\n";
		return 1;
	}

	if ((_codec = avcodec_find_decoder(AV_CODEC_ID_OPUS)) == NULL) {
		std::cerr << "avcodec_find_decoder failed\n";
		return 1;
	}

	if ((_codec_ctx = avcodec_alloc_context3(_codec)) == NULL) {
		std::cerr << "avcodec_alloc_context3 failed\n";
		return 1;
	}

	if ((e = avcodec_open2(_codec_ctx, _codec, &options)) < 0) {
		std::cerr << "avcodec_open2 failed\n";
		return 1;
	}

	return 0;
}

int Client::free_codec_ctx()
{
	std::cout << "free_codec_ctx\n";
	avcodec_free_context(&_codec_ctx);
	av_frame_free(&_frame);
	av_packet_free(&_pkt);

	return 0;
}




void Client::decode(AVCodecContext *decoder_ctx, AVPacket *pkt, AVFrame *frame)
{
	int e = 0;
	int data_size = 0;

	std::cout << "decode:" << pkt << "," << frame << "\n";

	/* send the packet with the compressed data to the decoder */
	if ((e = avcodec_send_packet(decoder_ctx, pkt)) < 0) {
		fprintf(stderr, "avcodec_send_packet failed:%d\n", e);
		exit(1);
	}

	/* read all the output frames (in general there may be any number of them */
	while (e >= 0) {
		e = avcodec_receive_frame(decoder_ctx, frame);
		if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) {
			return;
		} else if (e < 0) {
			fprintf(stderr, "avcodec_receive_frame failed:%d\n", e);
			exit(1);
		}
		data_size = av_get_bytes_per_sample(decoder_ctx->sample_fmt);
		if (data_size < 0) {
			/* This should not occur, checking just for paranoia */
			fprintf(stderr, "av_get_bytes_per_sample failed\n");
			exit(1);
		} else {
			//printf("data_size:%d\n", data_size);
		}

		size_t new_size = (frame->nb_samples*decoder_ctx->ch_layout.nb_channels*2);
		if (_samples.size() < new_size) {
			std::cout << "new_size:" << new_size << "\n";
			_samples.resize(new_size);
		}

		if (av_sample_fmt_is_planar(decoder_ctx->sample_fmt)) {
			printf("planar[%d]:%d,%d,%ld\n", decoder_ctx->sample_fmt, frame->nb_samples, decoder_ctx->ch_layout.nb_channels, _samples.size());
			printf("samples_index:%d\n", _samples_index);
#if	1
			for (int i = 0; i < frame->nb_samples; i++) {
				for (int ch = 0; ch < decoder_ctx->ch_layout.nb_channels; ch++) {
					// fwrite(frame->data[ch] + data_size*i, 1, data_size, g_audio_file);
					_samples[_samples_index+i*decoder_ctx->ch_layout.nb_channels+ch] = ((float *)(frame->data[ch]))[i];
				}
			}
			_samples_index += frame->nb_samples*decoder_ctx->ch_layout.nb_channels;
			// fwrite(samples.data(), frame->nb_samples*decoder_ctx->ch_layout.nb_channels, data_size, g_audio_file);
			if (1) {
				std::unique_lock<std::mutex> lock(_frame_q_mutex);
				int i;
				for (i = 0; i+256*2 <= _samples_index; i += 256*2) {
					_frame_q.push(std::vector<float>(_samples.data()+i,_samples.data()+i+256*2));
				}
				int n = _samples_index - i;
				printf("n:%d\n", n);
				memmove(&_samples[0], &_samples[i], n*sizeof(SAMPLE_DATA_TYPE));
				_samples_index = n;
			}
#endif
		} else {
			printf("packed\n");
			//fwrite(frame->data[0], frame->nb_samples*decoder_ctx->ch_layout.nb_channels, data_size, g_audio_file);
		}
	}
}

void Client::on_frame(rtc::binary& frame, rtc::FrameInfo& info)
{
	if (!_pkt) {
		return;
	}

	av_packet_from_data(_pkt, (uint8_t *)frame.data(), frame.size());
	decode(_codec_ctx, _pkt, _frame);
}

int Client::start_pw_thread()
{
	const struct spa_pod *params[1];
	uint8_t buffer[960*8];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	spa_zero(_stream_events);
	_stream_events.version = PW_VERSION_STREAM_EVENTS;
	_stream_events.process = on_process;

	_loop = pw_thread_loop_new(NULL, NULL);

	_stream = pw_stream_new_simple(
			pw_thread_loop_get_loop(_loop),
			"audio-src",
			pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
				PW_KEY_MEDIA_CATEGORY, "Playback",
				PW_KEY_MEDIA_ROLE, "Music",
				// PW_KEY_TARGET_OBJECT, "bluez_output.90_F2_60_53_B3_54.1",
				PW_KEY_TARGET_OBJECT, "echo-cancel-sink",
				NULL),
			&_stream_events,
			this);

	struct spa_audio_info_raw info = {
		// .format = SPA_AUDIO_FORMAT_S16,
		.format = SPA_AUDIO_FORMAT_F32,
		.flags = 0,
		.rate = DEFAULT_RATE,
		.channels = DEFAULT_CHANNELS,
		.position = {0},
	};
	params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

	pw_stream_connect(_stream,
			PW_DIRECTION_OUTPUT,
			PW_ID_ANY,
			(enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
			params, 1);


	std::cout << "start_pw_thread\n";
	pw_thread_loop_start(_loop);


	return 0;
}


int Client::stop_pw_thread()
{
	std::cout << "stop_pw_thread\n";
	pw_thread_loop_stop(_loop);

	// When the PipeWire connection has been terminated,
	// the thread must be stopped and the resources freed.
	// Stopping the thread is done using pw_thread_loop_stop(),
	// which must be called without the lock (see below) held.
	// When that function returns, the thread is stopped
	// and the Thread Loop object can be freed using pw_thread_loop_destroy().

	pw_stream_destroy(_stream);
	pw_thread_loop_destroy(_loop);

	return 0;
}


Client::State Client::get_state()
{
	std::shared_lock lock(_mutex);
	return _state;
}

ClientIdTrack::ClientIdTrack(std::string id, std::shared_ptr<ClientTrack> track)
{
	this->_client_id = id;
	this->_track = track;
}

uint64_t get_time_ms()
{
	struct timeval time;
	gettimeofday(&time, NULL);
	return uint64_t(time.tv_sec) * 1000 * 1000 + time.tv_usec;
}


