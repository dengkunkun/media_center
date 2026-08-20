#include "pose_uploader.hpp"
#include "data_transceiver.hpp"

#include <chrono>

namespace remote_control {

PoseUploader::PoseUploader() {
  genParams();

  tf_buffer_ = std::make_shared<tf2_ros::Buffer>(
      DataTransceiver::get_node_instance()->get_clock());
  tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);
  publish_timer_ = DataTransceiver::get_node_instance()->create_wall_timer(
      std::chrono::milliseconds(1000 / params_.publish_hz),
      std::bind(&PoseUploader::publish_timer_cb, this));
}

void PoseUploader::genParams() {
  auto node = DataTransceiver::get_node_instance();
  params_.publish_hz =
      node->declare_parameter<int>("pose_uploader.publish_hz", 5);
  params_.robot_id = node->declare_parameter<std::string>(
      "pose_uploader.robot_id", "R00001");
  params_.map_frame = node->declare_parameter<std::string>(
      "pose_uploader.map_frame", "map");
  params_.base_frame = node->declare_parameter<std::string>(
      "pose_uploader.base_frame", "base_footprint");

  if (params_.publish_hz <= 0) {
    RCLCPP_WARN(DataTransceiver::get_logger(),
                "pose_uploader.publish_hz must be positive; using 5 Hz");
    params_.publish_hz = 5;
  }
}

void PoseUploader::publish_timer_cb() {
  geometry_msgs::msg::TransformStamped transform;
  try {
    transform = tf_buffer_->lookupTransform(
        params_.map_frame, params_.base_frame, tf2::TimePointZero);
  } catch (const tf2::TransformException &exception) {
    RCLCPP_WARN_THROTTLE(
        DataTransceiver::get_logger(),
        *DataTransceiver::get_node_instance()->get_clock(), 5000,
        "Cannot get transform from %s to %s: %s", params_.map_frame.c_str(),
        params_.base_frame.c_str(), exception.what());
    return;
  }

  const auto &translation = transform.transform.translation;
  const auto &rotation = transform.transform.rotation;
  const auto timestamp_ms =
      DataTransceiver::get_node_instance()->get_clock()->now().nanoseconds() /
      1000000;
  nlohmann::json message;
  message["type"] = get_type();
  message["robot_id"] = params_.robot_id;
  message["timestamp"] = timestamp_ms;
  message["data"]["pose"]["position"] = {
      {"x", translation.x}, {"y", translation.y}, {"z", translation.z}};
  message["data"]["pose"]["orientation"] = {
      {"x", rotation.x},
      {"y", rotation.y},
      {"z", rotation.z},
      {"w", rotation.w}};

  DataTransceiver::instance()->sendMessage(get_label(), message.dump());
}
} // namespace remote_control
