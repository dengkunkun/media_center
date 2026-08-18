#ifndef STREAM_HANDLER_HPP
#define STREAM_HANDLER_HPP

#include "dispatch_queue.hpp"
#include "rtc/rtc.hpp"
#include "stream.hpp"


class StreamHandler: public std::enable_shared_from_this<StreamHandler> {
public:
    StreamHandler(std::shared_ptr<Stream> video_stream, std::shared_ptr<Stream> audio_stream);
    ~StreamHandler();


    DispatchQueue _dispatch_queue = DispatchQueue("stream");
    bool _is_running = false;
    uint64_t _start_time = 0;
    std::mutex _mutex;

    const std::shared_ptr<Stream> _audio_stream;
    const std::shared_ptr<Stream> _video_stream;


    enum class StreamSourceType {
        Audio,
        Video
    };

private:
    rtc::synchronized_callback<StreamSourceType, uint64_t, rtc::binary> _sample_handler;

    void send_sample();

public:
    void set_sample_handler(std::function<void (StreamSourceType, uint64_t, rtc::binary)> handler);
    void start();
    void stop();
    const bool& is_running = _is_running;
};


#endif	// STREAM_HANDLER_HPP
