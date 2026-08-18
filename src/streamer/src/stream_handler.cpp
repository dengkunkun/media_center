#include "stream_handler.hpp"
#include "client.hpp"
#include <sys/wait.h>
#include <unistd.h>



StreamHandler::StreamHandler(std::shared_ptr<Stream> video_stream, std::shared_ptr<Stream> audio_stream)
	:
		std::enable_shared_from_this<StreamHandler>(),
		_audio_stream(audio_stream),
		_video_stream(video_stream)
{

}

StreamHandler::~StreamHandler()
{
    stop();
}


void StreamHandler::send_sample()
{
	std::lock_guard lock(_mutex);
	if (!is_running) {
		return;
	}


	std::shared_ptr<Stream> stream;
	StreamSourceType stream_type;
	uint64_t next_time;
	if (_audio_stream->get_sample_time_us() < _video_stream->get_sample_time_us()) {
		stream = _audio_stream;
		stream_type = StreamSourceType::Audio;
		next_time = _audio_stream->get_sample_time_us();
		//std::cout << "a:" << next_time << "\n";
	} else {
		stream = _video_stream;
		stream_type = StreamSourceType::Video;
		next_time = _video_stream->get_sample_time_us();
		//std::cout << "v:" << next_time << "\n";
	}

	auto current_time = get_time_ms();

	auto elapsed = current_time - _start_time;
	if (next_time > elapsed) {
		auto wait_time = next_time - elapsed;
		//std::cout << "w[" << (int)stream_type << "]:" << wait_time << "\n";
		_mutex.unlock();
		usleep(wait_time);
		_mutex.lock();
	}


	auto sample = stream->get_sample();
	_sample_handler(stream_type, stream->get_sample_time_us(), sample);

	stream->load_next_sample();

	_dispatch_queue.dispatch(
		[this]() {
			this->send_sample();
		}
	);
}

void StreamHandler::set_sample_handler(std::function<void (StreamSourceType, uint64_t, rtc::binary)> handler)
{
    _sample_handler = handler;
}

void StreamHandler::start()
{
	std::lock_guard lock(_mutex);
	if (is_running) {
		return;
	}
	_is_running = true;
	_start_time = get_time_ms();

	std::cout << "StreamHandler::start\n";
	_dispatch_queue.dispatch([this]() {
	std::cout << "_audio_stream->start\n";
	_audio_stream->start();
	std::cout << "_video_stream->start\n";
	_video_stream->start();
	});


	_dispatch_queue.dispatch([this]() {
		this->send_sample();
	});
}

void StreamHandler::stop()
{
    std::lock_guard lock(_mutex);
    if (!is_running) {
        return;
    }
    _is_running = false;
    _dispatch_queue.remove_pending();

	_dispatch_queue.dispatch([this]() {
		_audio_stream->stop();
	    _video_stream->stop();
	});
};

