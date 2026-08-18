#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <shared_mutex>
#include "rtc/rtc.hpp"

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

    void set_state(State new_state);
    State get_state();


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
};

struct ClientIdTrack {
    ClientIdTrack(std::string client_id, std::shared_ptr<ClientTrack> track);

    std::string _client_id;
    std::shared_ptr<ClientTrack> _track;
};

uint64_t get_time_ms();

#endif	// CLIENT_HPP
