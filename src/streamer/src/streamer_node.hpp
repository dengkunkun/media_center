#ifndef STREAMER_NODE_HPP
#define STREAMER_NODE_HPP


#include <rclcpp/rclcpp.hpp>

class StreamerNode : public rclcpp::Node {
public:
	StreamerNode()
		:
			Node("streamer_node")
	{

	}

};


#endif	// STREAMER_NODE_HPP
