#include "streamer_node.hpp"
#include "nlohmann/json.hpp"
#include "stream.hpp"
#include "client.hpp"
#include "stream_handler.hpp"

#include <chrono>
#include <condition_variable>
#include <libavdevice/avdevice.h>
#include <memory>
#include <optional>
#include <rtc/description.hpp>
#include <rtc/frameinfo.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rclcpp/executors/multi_threaded_executor.hpp>
#include <sys/time.h>
#include <thread>


#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>
#include <vector>
 


using namespace std::chrono_literals;

// std::string g_server_address = "dev.qiwu.link";
//std::string g_server_address = "192.168.1.210";

// std::string g_server_address = "leilong.cloud";
std::string g_server_address = "bot.leilonganquan.com";
uint16_t g_server_port = 8888;

std::string g_stun_server_address = "bot.leilonganquan.com";

DispatchQueue g_main_thread("main");
std::unordered_map<std::string, std::shared_ptr<Client>> g_clients{};

std::optional<std::shared_ptr<StreamHandler>> g_stream_handler = std::nullopt;



std::shared_ptr<Client> create_peer_connection(const rtc::Configuration &config,
		std::weak_ptr<rtc::WebSocket> ws, const std::string& client_id);
std::shared_ptr<StreamHandler> create_stream_handler();
void add_to_stream(std::shared_ptr<Client> client, bool is_adding_video);

template <class T>
std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr)
{
	return ptr;
}


void ws_on_message(nlohmann::json message, rtc::Configuration config, std::shared_ptr<rtc::WebSocket> ws)
{
	auto it = message.find("human_username");
	if (it == message.end()) {
		return;
	}
	std::string human_username = it->get<std::string>();

	it = message.find("type");
	if (it == message.end()) {
		return;
	}
	std::string type = it->get<std::string>();

	std::cout << "ws_on_message:" << type << "\n";

	if (type == "request") {
		std::shared_ptr<Client> client(create_peer_connection(config, make_weak_ptr(ws), human_username));
		g_clients.emplace(human_username, client);

	} else if (type == "answer") {
		if (auto client = g_clients.find(human_username); client != g_clients.end()) {
			auto pc = client->second->peer_conn;
			auto sdp = message["sdp"].get<std::string>();
			auto description = rtc::Description(sdp, type);
			pc->setRemoteDescription(description);
		}
	}
}



std::string g_robot_id = "R00001";

int64_t get_microseconds(struct timeval *tv)
{
	return tv->tv_sec*1000000 + tv->tv_usec;
}


int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	av_log_set_level(AV_LOG_DEBUG);
	avdevice_register_all();

	bool enableDebugLogs = false;
	if (enableDebugLogs) {
		rtc::InitLogger(rtc::LogLevel::Debug);
	}

	rtc::Configuration config;
	// std::string stun_server = "stun:stun.l.google.com:19302";
	// config.iceServers.emplace_back(stun_server);

	config.iceServers.emplace_back("stun:"+g_stun_server_address+":3478");
	config.iceServers.emplace_back(g_stun_server_address, 3478, "robot", "20260706");

	config.disableAutoNegotiation = true;


	auto ws = std::make_shared<rtc::WebSocket>();

	ws->onOpen([&ws]() {
		std::cout << "ws->onOpen" << std::endl;
		nlohmann::json message = {
			{"type", "login"},
			{"username", g_robot_id},
			{"password", g_robot_id},
		};
		ws->send(message.dump());
	});

	ws->onClosed([]() {
		std::cout << "ws->onClosed" << std::endl;
	});

	ws->onError([](const std::string &error) {
		std::cout << "ws->onError:" << error << std::endl;
	});

	ws->onMessage([&](rtc::variant<rtc::binary, std::string> data) {
		std::cout << "ws->onMessage:@" << std::get<std::string>(data) << "@\n";
		if (!std::holds_alternative<std::string>(data)) {
			return;
		}

		nlohmann::json message = nlohmann::json::parse(std::get<std::string>(data));
		g_main_thread.dispatch([message, config, ws]() {
			ws_on_message(message, config, ws);
		});
	});

	// const std::string url = "ws://" + g_server_address + ":" + std::to_string(g_server_port) + "/"+local_id;
	const std::string url = "wss://" + g_server_address + "/ws/"+g_robot_id;
	std::cout << "opening @" << url << "@" << std::endl;
	ws->open(url);

	std::cout << "waiting" << std::ends;
	while (!ws->isOpen()) {
		if (ws->isClosed()) {
			return 1;
		}
		std::cout << ".";
		std::cout.flush();
		std::this_thread::sleep_for(100ms);
	}
	std::cout << "!\n";

	pw_init(&argc, &argv);

	
	rclcpp::init(argc, argv);
	{
		rclcpp::executors::MultiThreadedExecutor executor;
		executor.add_node(std::make_shared<StreamerNode>());
		executor.spin();
	}
	rclcpp::shutdown();


	for (;;) {
		if (ws->isClosed()) {
			break;
		}
		std::this_thread::sleep_for(100ms);
	}

	std::cout << "cleaning up..." << std::endl;

	pw_deinit();

	return 0;
}


std::shared_ptr<ClientTrack> create_video_track(const std::shared_ptr<rtc::PeerConnection> pc,
		const uint8_t payload_type,
		const uint32_t ssrc, const std::string cname, const std::string msid,
		const std::function<void (void)> on_open)
{
	auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payload_type,
			rtc::H265RtpPacketizer::ClockRate);
	auto packetizer = std::make_shared<rtc::H265RtpPacketizer>(rtc::NalUnit::Separator::Length, rtp_config);

	auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
	auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();

	packetizer->addToChain(sr_reporter);
	packetizer->addToChain(nack_responder);


	auto video = rtc::Description::Video(cname);
	video.addH265Codec(payload_type);
	video.addSSRC(ssrc, cname, msid, cname);

	auto track = pc->addTrack(video);
	track->setMediaHandler(packetizer);
	track->onOpen(on_open);

	return std::make_shared<ClientTrack>(track, sr_reporter);
}

std::shared_ptr<ClientTrack> create_audio_track(std::shared_ptr<Client> client, const std::shared_ptr<rtc::PeerConnection> pc,
		const uint8_t payload_type,
		const uint32_t ssrc, const std::string cname, const std::string msid,
		const std::function<void (void)> on_open)
{
	auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payload_type, rtc::OpusRtpPacketizer::DefaultClockRate);
	auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtp_config);

	auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
	packetizer->addToChain(sr_reporter);
	auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
	packetizer->addToChain(nack_responder);


	auto audio = rtc::Description::Audio(cname, rtc::Description::Direction::SendRecv);
	// auto audio = rtc::Description::Audio("audio", rtc::Description::Direction::RecvOnly);
	audio.addOpusCodec(payload_type);
	// audio.setBitrate(192);
	//audio.addAACCodec(payloadType);
	audio.addSSRC(ssrc, cname, msid, cname);

	auto track = pc->addTrack(audio);
	track->setMediaHandler(packetizer);

	auto depacketizer = std::make_shared<rtc::OpusRtpDepacketizer>(rtc::OpusRtpPacketizer::DefaultClockRate);
	depacketizer->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
	//track->chainMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
	// track->setMediaHandler(depacketizer);
	track->chainMediaHandler(depacketizer);


	// 初始化客户端语音播放
	client->init_codec_ctx();
	client->start_pw_thread();

	track->onOpen(on_open);
	
	track->onFrame([wc = make_weak_ptr(client)](rtc::binary frame, rtc::FrameInfo info) {
		//std::cout << (unsigned short)info.payloadType << ":" << frame.size() << "\n";
		if (auto client = wc.lock()) {
			client->on_frame(frame, info);
		}
	});
	

	return std::make_shared<ClientTrack>(track, sr_reporter);
}

std::shared_ptr<Client> create_peer_connection(const rtc::Configuration &config,
		std::weak_ptr<rtc::WebSocket> wws,
		const std::string& id)
{
	auto pc = std::make_shared<rtc::PeerConnection>(config);
	auto client = std::make_shared<Client>(pc);

	pc->onStateChange(
		[id](rtc::PeerConnection::State state) {
			std::cout << "pc->onStateChange:" << state << std::endl;
			if (state == rtc::PeerConnection::State::Disconnected ||
				state == rtc::PeerConnection::State::Failed ||
				state == rtc::PeerConnection::State::Closed) {

				g_main_thread.dispatch([id]() {
					g_clients.erase(id);
				});
			}
		}
	);

	pc->onGatheringStateChange(
		[wpc = make_weak_ptr(pc), id, wws](rtc::PeerConnection::GatheringState state) {
			std::cout << "pc->onGatheringStateChange:" << state << std::endl;
			if (state == rtc::PeerConnection::GatheringState::Complete) {
				if (auto pc = wpc.lock()) {
					auto description = pc->localDescription();
					nlohmann::json message = {
						{"human_username", id},
						{"robot_username", g_robot_id},
						{"type", description->typeString()},
						{"sdp", std::string(description.value())}
					};

					if (auto ws = wws.lock()) {
						std::cout << "ws->send:" << id << "," << description->typeString() << "\n";
						ws->send(message.dump());
					}
				}
			}
		}
	);

	// ssrc=102
	// payload_type=1
	// cname="video-stream"
	// msid="stream1"
	client->_video_track = create_video_track(pc, 102, 1, "video-stream", "stream1",
		[id, wc = make_weak_ptr(client)]() {
			std::cout << "track->onOpen:" << id << ",video" << std::endl;
			g_main_thread.dispatch([wc]() {
				if (auto client = wc.lock()) {
					add_to_stream(client, true);
				}
			});
		});

	client->_audio_track = create_audio_track(client, pc, 111, 2, "audio-stream", "stream1",
		[id, wc = make_weak_ptr(client)]() {
			std::cout << "track->onOpen:" << id << ",audio" << std::endl;
			g_main_thread.dispatch([wc]() {
				if (auto client = wc.lock()) {
					add_to_stream(client, false);
				}
			});
		});


	auto dc = pc->createDataChannel("ping-pong");
	dc->onOpen(
		[id, wdc = make_weak_ptr(dc)]() {
			std::cout << "dc->onOpen\n";
			if (auto dc = wdc.lock()) {
				dc->send("$$$ping");
			}
		}
	);

	dc->onMessage(nullptr,
		[id, wdc = make_weak_ptr(dc)](std::string msg) {
			std::cout << "dc->onMessage:" << id << "," << msg << std::endl;
			if (auto dc = wdc.lock()) {
				dc->send("ping");
			}
		}
	);

	client->_data_channel = dc;


	pc->setLocalDescription();

	return client;
};

std::optional<std::shared_ptr<ClientTrack>> get_opt_track(StreamHandler::StreamSourceType type, std::shared_ptr<Client> client)
{
	return type == StreamHandler::StreamSourceType::Video ? client->_video_track : client->_audio_track;
}

std::shared_ptr<StreamHandler> create_stream_handler(void)
{
	//auto video_stream = std::make_shared<VideoFileLoader>("./samples/1_test.mp4");
	// auto video_stream = std::make_shared<VideoFileLoader>("../hs_hevc.mp4");
	//auto video_stream = std::make_shared<VideoFileLoader>("./hs.mp4");
	auto video_stream = std::make_shared<CameraStream>(30);

	// auto audio_stream = std::make_shared<AudioVoidStream>();
	// auto audio_stream = std::make_shared<MicphoneStream>();
	auto audio_stream = std::make_shared<PwMicStream>();

	auto stream_handler = std::make_shared<StreamHandler>(video_stream, audio_stream);

	stream_handler->set_sample_handler(
		[ws = make_weak_ptr(stream_handler)](StreamHandler::StreamSourceType type, uint64_t sample_time, rtc::binary sample) {
			std::vector<ClientIdTrack> tracks{};
			std::string stream_type = (type == StreamHandler::StreamSourceType::Video ? "v" : "a");

			for(auto p: g_clients) {
				auto client_id = p.first;
				auto client = p.second;
				auto opt_track = get_opt_track(type, client);
				if (client->get_state() == Client::State::Ready && opt_track.has_value()) {
					auto track = opt_track.value();
					tracks.push_back(ClientIdTrack(client_id, track));
				}
			}

			for (auto client_id_track: tracks) {
				auto client_id = client_id_track._client_id;
				auto track = client_id_track._track;

				std::cout << "[" << stream_type << "]" << std::to_string(sample.size()) << " -> " << client_id << std::endl;
				try {
					// 发送数据至客户端！
					track->_track->sendFrame(sample, std::chrono::duration<double, std::micro>(sample_time));
				} catch (const std::exception &e) {
					std::cerr << "[" << stream_type << "]track->sendFrame failed:" << e.what() << std::endl;
				}
			}

			g_main_thread.dispatch(
				[ws]() {
					if (g_clients.empty()) {
						if (auto stream = ws.lock()) {
							stream->stop();
						}
					}
				}
			);
		}
	);

	return stream_handler;
}


void send_initial_nalus(std::shared_ptr<StreamHandler> stream, std::shared_ptr<ClientTrack> video_track)
{
#if	0
	auto video_stream = dynamic_cast<VideoFileLoader *>(stream->_video_stream.get());
	auto initial_nalus = video_stream->initial_nalus();

	if (!initial_nalus.empty()) {
		const double frame_duration_s =
			double(video_stream->get_sample_duration_us()) / (1000 * 1000);
		const uint32_t frame_timestamp_duration =
			video_track->_sr_reporter->rtpConfig->secondsToTimestamp(frame_duration_s);

		video_track->_sr_reporter->rtpConfig->timestamp =
			video_track->_sr_reporter->rtpConfig->startTimestamp - frame_timestamp_duration * 2;
		video_track->_track->send(initial_nalus);

		video_track->_sr_reporter->rtpConfig->timestamp += frame_timestamp_duration;
		video_track->_track->send(initial_nalus);
	}
#endif
}

void add_to_stream(std::shared_ptr<Client> client, bool is_adding_video)
{
	std::cout << "add_to_stream:" << (is_adding_video ? "video" : "audio") << "\n";

	if (client->get_state() == Client::State::Waiting) {
		client->set_state(is_adding_video ? Client::State::WaitingForAudio : Client::State::WaitingForVideo);

	} else if ((client->get_state() == Client::State::WaitingForAudio && !is_adding_video)
			|| (client->get_state() == Client::State::WaitingForVideo && is_adding_video)) {

		assert(client->_video_track.has_value() && client->_audio_track.has_value());
		auto video = client->_video_track.value();

		if (g_stream_handler.has_value()) {
			send_initial_nalus(g_stream_handler.value(), video);
		}

		client->set_state(Client::State::Ready);
	}

	if (client->get_state() == Client::State::Ready) {
		std::shared_ptr<StreamHandler> stream_handler;
		if (g_stream_handler.has_value()) {
			stream_handler = g_stream_handler.value();
			if (stream_handler->is_running) {
				return;
			}
		} else {
			stream_handler = create_stream_handler();
			g_stream_handler = stream_handler;
		}

		std::cout << "stream->start!\n";
		stream_handler->start();
	}
}

