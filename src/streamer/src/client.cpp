#include "client.hpp"
#include <ctime>

#include <sys/time.h>

ClientTrack::ClientTrack(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sr_reporter)
{
	this->_track = track;
	this->_sr_reporter = sr_reporter;
}

void Client::set_state(State new_state)
{
	std::unique_lock lock(_mutex);
	this->_state = new_state;
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


