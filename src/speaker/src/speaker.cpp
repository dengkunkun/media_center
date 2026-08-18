#include "pipewire/core.h"
#include "pipewire/node.h"
#include "pipewire/stream.h"
#include "rclcpp/rclcpp.hpp"	// IWYU pragma: keep
#include "media_interfaces/srv/play_audio.hpp"
// #include "rcl_interfaces/msg/parameter_event.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <atomic>
#include <rclcpp/logger.hpp>
#include <rclcpp/logging.hpp>
#include <rcutils/error_handling.h>
#include <thread>


extern "C" {
#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <pipewire/thread-loop.h>
}

// ros2 run media_center media_center

enum {
	PLAY_STATE_NONE = 0,
	PLAY_STATE_PLAYING = 1,
	PLAY_STATE_PAUSED = 2,
};


#define AUDIO_SAMPLE_RATE		48000
#define AUDIO_CHANNEL			2

// #define AUDIO_SAMPLE_FORMAT		SND_PCM_FORMAT_FLOAT_LE
// #define SAMPLE_DATA_TYPE			float
#define AUDIO_SAMPLE_FORMAT			SND_PCM_FORMAT_S16_LE
#define SAMPLE_DATA_TYPE			int16_t



int get_master_volume(double *percentage)
{
	// 调用外部命令获取默认音量
    FILE *f;
    char buffer[128];
    f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@", "r");
    fgets(buffer, sizeof(buffer), f);
    pclose(f);

	const char *p = buffer;
	while (*p != '\0') {
		if (*p == ' ') {
			break;
		}
		++p;
	}
	if (*p == ' ') {
		++p;
		*percentage = atof(p)*100.0;
	}

	return 0;
}


int set_master_volume(double percentage)
{
	// 调用外部命令设置默认音量
	char buffer[1024];
	int n;
	n = snprintf(buffer, sizeof(buffer), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %f", percentage/100);
	system(buffer);

	return 0;
}


class PlayAudioContext;

struct PlayAudioSession {
	int _state;

	std::string _client_id;
	std::string _session_id;
	std::string _device_id;
	int64_t _volume;
	int64_t _mode;
	std::string _asset_url;

	std::weak_ptr<PlayAudioContext> _play_ctx;
};

std::atomic_int g_session_count = 0;
std::mutex g_sessions_mutex;
std::unordered_map<std::string, std::shared_ptr<PlayAudioSession>> g_sessions;


extern "C" {

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixdesc.h>


}


static const char *s_filter_desc = "aresample=48000,aformat=sample_fmts=s16:channel_layouts=stereo";



class PlayAudioContext {
public:
	int open_input_file(const char *filename);
	int init_filters(const char *filters_descr);

	bool do_play_audio(const std::shared_ptr<PlayAudioSession>& session);
	bool fill_data(PlayAudioSession *session, uint8_t *data, int n);


	struct pw_main_loop *_loop;
	struct pw_thread_loop *_thread_loop;
	struct pw_stream *_stream;

	AVFormatContext *_fmt_ctx = NULL;
	AVCodecContext *_decoder_ctx = NULL;
	AVFilterContext *_sink_filter_ctx = NULL;
	AVFilterContext *_src_filter_ctx = NULL;
	AVFilterGraph *_filter_graph = NULL;
	int _audio_stream_index = -1;

	AVPacket *_packet;
	AVFrame *_frame;
	AVFrame *_filter_frame;

	uint8_t _buffer[48000*4];
	int _buffer_index = 0;
	int _buffer_size = 0;
};


int PlayAudioContext::open_input_file(const char *filename)
{
	const AVCodec *decoder = NULL;
	int e = 0;

	if ((e = avformat_open_input(&_fmt_ctx, filename, NULL, NULL)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "avformat_open_input failed:%d\n", e);
		return e;
	}

	if ((e = avformat_find_stream_info(_fmt_ctx, NULL)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info failed:%d", e);
		return e;
	}

	if ((e = av_find_best_stream(_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "av_find_best_stream failed:%d\n", e);
		return e;
	}
	_audio_stream_index = e;

	if (!(_decoder_ctx = avcodec_alloc_context3(decoder))) {
		return AVERROR(ENOMEM);
	}
	avcodec_parameters_to_context(_decoder_ctx, _fmt_ctx->streams[_audio_stream_index]->codecpar);

	if ((e = avcodec_open2(_decoder_ctx, decoder, NULL)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_open2 failed:%d\n", e);
		return e;
	}

	return 0;
}
 
int PlayAudioContext::init_filters(const char *filters_descr)
{
	char args[512];
	int e = 0;
	const AVFilter *abuffer_filter  = avfilter_get_by_name("abuffer");
	const AVFilter *abuffersink_filter = avfilter_get_by_name("abuffersink");
	AVFilterInOut *filter_inout_out = avfilter_inout_alloc();
	AVFilterInOut *filter_inout_in  = avfilter_inout_alloc();

	static const int out_sample_rate = 48000;
	const AVFilterLink *filter_link = NULL;
	AVRational time_base = _fmt_ctx->streams[_audio_stream_index]->time_base;

	_filter_graph = avfilter_graph_alloc();
	if (!filter_inout_out || !filter_inout_in || !_filter_graph) {
		e = AVERROR(ENOMEM);
		goto end;
	}

	/* buffer audio source: the decoded frames from the decoder will be inserted here. */
	if (_decoder_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
		av_channel_layout_default(&_decoder_ctx->ch_layout, _decoder_ctx->ch_layout.nb_channels);
	}
	e = snprintf(args, sizeof(args),
			"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=",
			time_base.num, time_base.den, _decoder_ctx->sample_rate,
			av_get_sample_fmt_name(_decoder_ctx->sample_fmt));
	av_channel_layout_describe(&_decoder_ctx->ch_layout, args + e, sizeof(args) - e);

	fprintf(stderr, "input:%s\n", args);


	if ((e = avfilter_graph_create_filter(&_src_filter_ctx, abuffer_filter, "in",
					args, NULL, _filter_graph)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "avfilter_graph_create_filter failed:%d\n", e);
		goto end;
	}

	/* buffer audio sink: to terminate the filter chain. */
	if (!(_sink_filter_ctx = avfilter_graph_alloc_filter(_filter_graph, abuffersink_filter, "out"))) {
		av_log(NULL, AV_LOG_ERROR, "avfilter_graph_alloc_filter failed\n");
		e = AVERROR(ENOMEM);
		goto end;
	}

	if ((e = av_opt_set(_sink_filter_ctx, "sample_formats", "s16", AV_OPT_SEARCH_CHILDREN)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "av_opt_set failed:sample_formats\n");
		goto end;
	}

	if ((e = av_opt_set(_sink_filter_ctx, "channel_layouts", "stereo", AV_OPT_SEARCH_CHILDREN)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "av_opt_set failed:channel_layouts\n");
		goto end;
	}

	if ((e = av_opt_set_array(_sink_filter_ctx, "samplerates", AV_OPT_SEARCH_CHILDREN,
					0, 1, AV_OPT_TYPE_INT, &out_sample_rate)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "av_opt_set_array failed:samplerates\n");
		goto end;
	}

	if ((e = avfilter_init_dict(_sink_filter_ctx, NULL)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "avfilter_init_dict failed\n");
		goto end;
	}

	/*
	 * Set the endpoints for the filter graph. The filter_graph will
	 * be linked to the graph described by filters_descr.
	 */

	/*
	 * The buffer source output must be connected to the input pad of
	 * the first filter described by filters_descr; since the first
	 * filter input label is not specified, it is set to "in" by
	 * default.
	 */
	filter_inout_out->name			= av_strdup("in");
	filter_inout_out->filter_ctx	= _src_filter_ctx;
	filter_inout_out->pad_idx		= 0;
	filter_inout_out->next			= NULL;

	/*
	 * The buffer sink input must be connected to the output pad of
	 * the last filter described by filters_descr; since the last
	 * filter output label is not specified, it is set to "out" by
	 * default.
	 */
	filter_inout_in->name			= av_strdup("out");
	filter_inout_in->filter_ctx		= _sink_filter_ctx;
	filter_inout_in->pad_idx		= 0;
	filter_inout_in->next			= NULL;

	if ((e = avfilter_graph_parse_ptr(_filter_graph, filters_descr,
					&filter_inout_in, &filter_inout_out, NULL)) < 0) {
		goto end;
	}

	if ((e = avfilter_graph_config(_filter_graph, NULL)) < 0) {
		goto end;
	}

	/* Print summary of the sink buffer
	 * Note: args buffer is reused to store channel layout string */
	filter_link = _sink_filter_ctx->inputs[0];
	av_channel_layout_describe(&filter_link->ch_layout, args, sizeof(args));

	av_log(NULL, AV_LOG_INFO, "output:sample_rate:%dHz,sample_format:%s,channel_layout:%s\n",
			(int)filter_link->sample_rate,
			(char *)av_x_if_null(av_get_sample_fmt_name((enum AVSampleFormat)filter_link->format), "?"),
			args);

end:
	avfilter_inout_free(&filter_inout_in);
	avfilter_inout_free(&filter_inout_out);

	return e;
}

static void on_process(void *userdata)
{
	PlayAudioSession *session = (PlayAudioSession *)userdata;
	std::shared_ptr<PlayAudioContext> play_ctx(session->_play_ctx.lock());
	if (!play_ctx) {
		return;
	}

	if (session->_state == PLAY_STATE_NONE) {
		// pw_main_loop_quit(play_ctx->_loop);
		pw_thread_loop_stop(play_ctx->_thread_loop);
		return;
	}

	struct pw_buffer *buffer;
	struct spa_buffer *sb;
	uint64_t n_frames;
	int stride;
	int16_t *dst;

	if ((buffer = pw_stream_dequeue_buffer(play_ctx->_stream)) == NULL) {
		pw_log_warn("pw_stream_dequeue_buffer:%m");
		return;
	}

	sb = buffer->buffer;
	if ((dst = (int16_t *)sb->datas[0].data) == NULL) {
		printf("%d\n", __LINE__);
		return;
	}

	stride = sizeof(int16_t) * AUDIO_CHANNEL;
	n_frames = sb->datas[0].maxsize / stride;

	if (buffer->requested) {
		n_frames = SPA_MIN(buffer->requested, n_frames);
	}


#if	0
	// FILL DATA
	if (feof(ctx->raw_file)) {
		fseek(ctx->raw_file, 0, SEEK_SET);
	}

	if (fread(dst, stride, n_frames, ctx->raw_file) != (unsigned long)stride*n_frames) {

	} else {

	}
#endif

#if	1
		if (session->_volume != 0) {
			int e;
			float v = (float)session->_volume/100;
			float volumes[2] = {v, v};
			pw_thread_loop_lock(play_ctx->_thread_loop);
			if ((e = pw_stream_set_control(play_ctx->_stream, SPA_PROP_channelVolumes, 2, volumes, 0)) != 0) {
				std::cerr << "pw_stream_set_control failed:" << e << "\n";
			}
			pw_thread_loop_unlock(play_ctx->_thread_loop);
			session->_volume = 0;
		}
#endif


	if (play_ctx->fill_data(session, (uint8_t *)dst, stride*n_frames)) {
#if	0
		if (session->_volume != 100) {
			int n = stride*n_frames/sizeof(int16_t);
			for (int i = 0; i < n; ++i) {
				dst[i] = dst[i]*session->_volume/100;
			}
		}
#endif

	} else {

		// pw_main_loop_quit(play_ctx->_loop);
		pw_thread_loop_stop(play_ctx->_thread_loop);
	}


	sb->datas[0].chunk->offset = 0;
	sb->datas[0].chunk->stride = stride;
	sb->datas[0].chunk->size = n_frames * stride;

	pw_stream_queue_buffer(play_ctx->_stream, buffer);


}

static struct pw_stream_events stream_events = {
};

bool PlayAudioContext::fill_data(PlayAudioSession *session, uint8_t *data, int n)
{
	int e;

	if (session->_state == PLAY_STATE_PAUSED) {
		memset(data, 0, n);
		return true;
	}

	// 解码得到足够多的数据！
	while (_buffer_size < n) {
		if ((e = av_read_frame(_fmt_ctx, _packet)) < 0) {
			av_log(NULL, AV_LOG_ERROR, "av_read_frame failed:%d\n", e);
			if (session->_state != PLAY_STATE_NONE && e == AVERROR_EOF) {
				/* signal EOF to the filtergraph */
				if (session->_mode == 0) {
					if (av_buffersrc_add_frame_flags(_src_filter_ctx, NULL, 0) < 0) {
						av_log(NULL, AV_LOG_ERROR, "av_buffersrc_add_frame_flags failed\n");
						return false;
					}
				}

				/* pull remaining frames from the filtergraph */
				while (1) {
					e = av_buffersink_get_frame(_sink_filter_ctx, _filter_frame);
					if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) {
						break;
					}
					if (e < 0) {
						return false;
					}

					int data_size = _filter_frame->nb_samples*_filter_frame->ch_layout.nb_channels*sizeof(int16_t);
					memcpy(_buffer+_buffer_size, _filter_frame->data[0], data_size);
					_buffer_size += data_size;

					av_frame_unref(_filter_frame);
				}

				if (session->_state != PLAY_STATE_NONE && session->_mode == 1) {
					// 返回原点
					av_seek_frame(_fmt_ctx, _audio_stream_index, 0, AVSEEK_FLAG_BACKWARD);
				} else {

				}
			}

		} else {

			if (_packet->stream_index == _audio_stream_index) {
				// printf("[% 10ld][%d]\n", _packet->pts, _packet->size);

				if ((e = avcodec_send_packet(_decoder_ctx, _packet)) < 0) {
					av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet failed:%d\n", e);
					return false;
				}

				while (e >= 0) {
					e = avcodec_receive_frame(_decoder_ctx, _frame);
					if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) {
						break;
					} else if (e < 0) {
						av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame failed:%d\n", e);
						return false;
					}

					if (e >= 0) {
						/* push the audio data from decoded frame into the filtergraph */
						if (av_buffersrc_add_frame_flags(_src_filter_ctx, _frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
							av_log(NULL, AV_LOG_ERROR, "av_buffersrc_add_frame_flags failed\n");
							return false;
						}

						/* pull filtered audio from the filtergraph */
						while (1) {
							e = av_buffersink_get_frame(_sink_filter_ctx, _filter_frame);
							if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) {
								break;
							}
							if (e < 0) {
								return false;
							}
							int data_size = _filter_frame->nb_samples*_filter_frame->ch_layout.nb_channels*sizeof(int16_t);
							memcpy(_buffer+_buffer_size, _filter_frame->data[0], data_size);
							_buffer_size += data_size;

							av_frame_unref(_filter_frame);
						}
						av_frame_unref(_frame);
					}
				}
			}
			av_packet_unref(_packet);
		}


	}


	if (_buffer_size >= n) {
		memcpy(data, _buffer+_buffer_index, n);

		_buffer_index += n;
		_buffer_size -= n;

		// 剩余数据移动到开头
		if (_buffer_size > 0) {
			memmove(_buffer, _buffer+_buffer_index, _buffer_size);
		}
		_buffer_index = 0;

	} else {
		memcpy(data, _buffer+_buffer_index, _buffer_size);
		_buffer_index = 0;
		_buffer_size = 0;
	}

	

	return true;
}

bool PlayAudioContext::do_play_audio(const std::shared_ptr<PlayAudioSession>& session)
{
	int e = 0;

	const struct spa_pod *params[1];
	uint8_t buffer[960*4];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));


	// _loop = pw_main_loop_new(NULL);
	_thread_loop = pw_thread_loop_new(NULL, NULL);

	stream_events.version = PW_VERSION_STREAM_EVENTS,
	stream_events.process = on_process,

	_stream = pw_stream_new_simple(
			// pw_main_loop_get_loop(_loop),
			pw_thread_loop_get_loop(_thread_loop),
			"audio-src",
			pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
				PW_KEY_MEDIA_CATEGORY, "Playback",
				PW_KEY_MEDIA_ROLE, "Music",
				// PW_KEY_TARGET_OBJECT, "bluez_output.90_F2_60_53_B3_54.1",
				PW_KEY_TARGET_OBJECT, "echo-cancel-sink",
				NULL),
			&stream_events,
			session.get());


	struct spa_audio_info_raw info = {
		.format = SPA_AUDIO_FORMAT_S16,
		// .format = SPA_AUDIO_FORMAT_F32,
		.flags = 0,
		.rate = AUDIO_SAMPLE_RATE,
		.channels = AUDIO_CHANNEL,
		.position = {0},
	};
	params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);



	pw_stream_connect(_stream,
			PW_DIRECTION_OUTPUT,
			PW_ID_ANY,
			(pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
			params, 1);




	if (!(_packet = av_packet_alloc())) {
		fprintf(stderr, "av_packet_alloc failed\n");
		goto end;
	}
	if (!(_frame = av_frame_alloc())) {
		fprintf(stderr, "av_frame_alloc failed\n");
		goto end;
	}
	if (!(_filter_frame = av_frame_alloc())) {
		fprintf(stderr, "av_frame_alloc failed\n");
		goto end;
	}

	if ((e = open_input_file(session->_asset_url.c_str())) < 0) {
		goto end;
	}
	if ((e = init_filters(s_filter_desc)) < 0) {
		goto end;
	}


	// pw_main_loop_run(_loop);
	pw_thread_loop_start(_thread_loop);



	while (session->_state != PLAY_STATE_NONE) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	pw_stream_destroy(_stream);
	// pw_main_loop_destroy(_loop);

	pw_thread_loop_stop(_thread_loop);
	pw_thread_loop_destroy(_thread_loop);

end:
	avfilter_graph_free(&_filter_graph);
	avcodec_free_context(&_decoder_ctx);
	avformat_close_input(&_fmt_ctx);

	av_packet_free(&_packet);
	av_frame_free(&_frame);
	av_frame_free(&_filter_frame);

	return true;
}




void play_audio(const std::shared_ptr<media_interfaces::srv::PlayAudio::Request> request,
          std::shared_ptr<media_interfaces::srv::PlayAudio::Response> response)
{
	if (!request->session_id.empty()) {
		response->session_id = request->session_id;
	}
	if (request->command == "play") {
		RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "command:%s,asset_url:%s",
				request->command.c_str(),
				request->asset_url.c_str());

		response->session_id = std::to_string(g_session_count.fetch_add(1));
		response->error_code = 0;
		response->error_message = std::string("play ok");

		request->session_id = response->session_id;

		std::thread play_thread([request]() {
			auto session = std::make_shared<PlayAudioSession>();
			session->_state = 1;
			session->_session_id = request->session_id;
			session->_client_id = request->client_id;
			session->_device_id = request->device_id;
			session->_volume = request->volume;
			session->_mode = request->mode;
			session->_asset_url = request->asset_url;

			{
				std::unique_lock<std::mutex> lock(g_sessions_mutex);
				g_sessions[session->_session_id] = session;
			}

			auto play_ctx = std::make_shared<PlayAudioContext>();
			session->_play_ctx = play_ctx;
			play_ctx->do_play_audio(session);

			{
				std::unique_lock<std::mutex> lock(g_sessions_mutex);
				g_sessions.erase(session->_session_id);
			}
		});
		play_thread.detach();

	} else {
		std::shared_ptr<PlayAudioSession> session;

		{
			std::unique_lock<std::mutex> lock;
			auto it = g_sessions.find(request->session_id);
			if (it != g_sessions.end()) {
				session = it->second;
			}
		}

		if (session) {
			if (request->command == "stop") {
				session->_state = PLAY_STATE_NONE;

				response->error_code = 0;
				response->error_message = "stop ok";
			} else if (request->command == "pause") {
				session->_state = PLAY_STATE_PAUSED;

				response->error_code = 0;
				response->error_message = "pause ok";

			} else if (request->command == "resume") {
				session->_state = PLAY_STATE_PLAYING;

				response->error_code = 0;
				response->error_message = "resume ok";

			} else {
				response->error_code = 2;
				response->error_message = "invalid command";
			}
		} else {
			response->error_code = 1;
			response->error_message = "session not found";
		}
	}

	RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "response:%ld,%s", response->error_code, response->error_message.c_str());
}



class MediaCenterNode : public rclcpp::Node {
public:
	MediaCenterNode()
		:
			Node("speaker_node")
	{
		// ros2 param set speaker_node volume 80
		// ros2 param get speaker_node volume
		double volume = 0.0;
		if (get_master_volume(&volume) < 0) {
			this->declare_parameter("volume", 75.0);
		} else {
			this->declare_parameter("volume", volume);
			RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "volume:%f", volume);
		}

		_param_event_handler =
			std::make_shared<rclcpp::ParameterEventHandler>(this);

		auto callback = [this](const rclcpp::Parameter& p) {
			RCLCPP_INFO(this->get_logger(), "callback:%s,%s,%f",
					p.get_name().c_str(),
					p.get_type_name().c_str(),
					p.as_double());

			double volume = this->get_parameter("volume").as_double();
			RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "volume:%f", volume);

			set_master_volume(p.as_double());
		};
		_volume_callback_handle = _param_event_handler->add_parameter_callback("volume", callback);




#if	0
		auto event_callback = [this](const rcl_interfaces::msg::ParameterEvent& event) {
			RCLCPP_INFO(this->get_logger(), "from node:%s", event.node.c_str());

			for (const auto& p : event.changed_parameters) {
				RCLCPP_INFO(this->get_logger(), "%s:%s",
						p.name.c_str(),
						rclcpp::Parameter::from_parameter_msg(p).value_to_string().c_str());
			}
		};

		_event_callback_handle = _param_event_handler->add_parameter_event_callback(event_callback);
#endif

		_play_audio_service = this->create_service<media_interfaces::srv::PlayAudio>("play_audio", &play_audio);
		RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "play_audio ready");
	}


private:
	std::shared_ptr<rclcpp::ParameterEventHandler> _param_event_handler;
	std::shared_ptr<rclcpp::ParameterCallbackHandle> _volume_callback_handle;
	// std::shared_ptr<rclcpp::ParameterEventCallbackHandle> _event_callback_handle;


	rclcpp::Service<media_interfaces::srv::PlayAudio>::SharedPtr _play_audio_service;
};







int main(int argc, char *argv[])
{
	pw_init(&argc, &argv);

	rclcpp::init(argc, argv);
	rclcpp::spin(std::make_shared<MediaCenterNode>());
	rclcpp::shutdown();

	pw_deinit();

	return 0;
}
