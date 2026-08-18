#include "rclcpp/rclcpp.hpp"  // IWYU pragma: keep
#include "media_interfaces/srv/play_audio.hpp"

#include <memory>
#include <rclcpp/logger.hpp>

using namespace std::chrono_literals;

int main(int argc, char *argv[])
{
	if (argc < 3) {
		RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "usage: ros2 run speaker play_audio_client [command] [?]");
		return 1;
	}
    rclcpp::init(argc, argv);

    std::shared_ptr<rclcpp::Node> node = rclcpp::Node::make_shared("play_audio_client");
    rclcpp::Client<media_interfaces::srv::PlayAudio>::SharedPtr client =
        node->create_client<media_interfaces::srv::PlayAudio>("play_audio");

    auto request = std::make_shared<media_interfaces::srv::PlayAudio::Request>();
    request->command = argv[1];
    request->client_id = "test_client";

	if (request->command == "play") {
		if (argc < 5) {
			RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "usage: ros2 run speaker play_audio_client play [asset_url] [volume] [mode]");
			return 1;
		}
		request->asset_url = argv[2];
		request->device_id = "0";
	    request->volume = atoll(argv[3]);
		request->mode = atoll(argv[4]);
	} else if (request->command == "stop" || request->command == "pause" || request->command == "resume") {
		request->session_id = argv[2];
	} else {
		return 1;
	}

    while (!client->wait_for_service(1s)) {
        if (!rclcpp::ok()) {
            RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "client->wait_for_service failed");
            return 0;
        }
        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), ".");
    }

    auto result = client->async_send_request(request);

    if (rclcpp::spin_until_future_complete(node, result) == rclcpp::FutureReturnCode::SUCCESS) {
        auto response = result.get();
        RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "session_id:%s,error_code:%ld,error_message:%s",
				response->session_id.c_str(),
                response->error_code,
                response->error_message.c_str());
    } else {
        RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "failed");
    }

    rclcpp::shutdown();

    return 0;
}
