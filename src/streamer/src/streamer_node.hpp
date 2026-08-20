#ifndef STREAMER_NODE_HPP
#define STREAMER_NODE_HPP

#include <memory>
#include <rclcpp/rclcpp.hpp>
#include "data_transceiver.hpp"

class StreamerNode : public rclcpp::Node {
public:
  StreamerNode() : Node("streamer_node") {
    
  }
  void init(){
    data_transceiver_ = std::make_unique<remote_control::DataTransceiver>(shared_from_this());
    data_transceiver_->init();
  }

private:
  std::unique_ptr<remote_control::DataTransceiver> data_transceiver_;
};

#endif // STREAMER_NODE_HPP
