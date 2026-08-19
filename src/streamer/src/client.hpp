#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <shared_mutex>
#include "rtc/rtc.hpp"

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>

#include <pipewire/pipewire.h>
}


#define DEFAULT_RATE			48000
#define DEFAULT_CHANNELS		2
#define DEFAULT_VOLUME			0.7

#define SAMPLE_DATA_TYPE		float
// #define SAMPLE_DATA_TYPE		int16_t

struct ClientTrack {
    ClientTrack(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sr_reporter);

    std::shared_ptr<rtc::Track> _track;
    std::shared_ptr<rtc::RtcpSrReporter> _sr_reporter;
};

struct Client {
    enum class State {
        Waiting,
        WaitingForVideo,
        WaitingForAudio,
        Ready
    };

    const std::shared_ptr<rtc::PeerConnection>& peer_conn = _peer_conn;
    Client(std::shared_ptr<rtc::PeerConnection> pc) {
        _peer_conn = pc;
    }
	virtual ~Client();

    void set_state(State new_state);
    State get_state();

	int init_codec_ctx();
	int free_codec_ctx();
	void decode(AVCodecContext *decoder_ctx, AVPacket *pkt, AVFrame *frame);

	int start_pw_thread();
	int stop_pw_thread();

	void on_frame(rtc::binary& frame, rtc::FrameInfo& info);


public:
    std::optional<std::shared_ptr<ClientTrack>> _video_track;
    std::optional<std::shared_ptr<ClientTrack>> _audio_track;
    std::optional<std::shared_ptr<rtc::DataChannel>> _data_channel;

    uint32_t rtpStartTimestamp = 0;

private:
    std::shared_mutex _mutex;
    State _state = State::Waiting;
    std::string _id;
    std::shared_ptr<rtc::PeerConnection> _peer_conn;

public:
	// 播放声音
	const AVCodec *_codec = NULL;
	AVCodecContext *_codec_ctx = NULL;
	AVPacket *_pkt = NULL;
	AVFrame *_frame = NULL;

	std::queue<std::vector<float>> _frame_q;
	std::mutex _frame_q_mutex;
	std::condition_variable _frame_q_cv;

	struct pw_thread_loop *_loop;
	struct pw_stream *_stream;
	struct pw_stream_events _stream_events;

	std::vector<float> _samples;
	int _samples_index = 0;
};

struct ClientIdTrack {
    ClientIdTrack(std::string client_id, std::shared_ptr<ClientTrack> track);

    std::string _client_id;
    std::shared_ptr<ClientTrack> _track;
};

uint64_t get_time_ms();

#endif	// CLIENT_HPP
