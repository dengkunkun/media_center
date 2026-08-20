// pose_uploader.hpp
// 查询map下base_footprint的位姿进行上传,dataChannel label:map
/*

```json
{
  "type": "robot_pose",
  "robot_id": "R00001",
  "timestamp": 1785772800123,
  "data": {
    "pose": {
      "position": { "x": 9.458, "y": 1.588, "z": 0.0 },
      "orientation": { "x": 0.0, "y": 0.0, "z": 0.0998334, "w": 0.9950042 }
    }
  }
}
```
*/


#pragma once
#include "i_bussiness_handler.hpp"
#include <rclcpp/rclcpp.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>

namespace remote_control {


class PoseUploader : public IBusinessHandler {
  struct Params {
    int publish_hz{5};
    std::string robot_id{"R00001"};
    std::string map_frame{"map"};
    std::string base_frame{"base_footprint"};
  } params_;
public:
  PoseUploader();
  ~PoseUploader() override = default;

  void genParams();

  std::string get_label() override { return "map"; }
  // 返回当前业务的type
  std::string get_type() override { return "robot_pose"; }
  // 这个业务没有接收
  void handleMessage(const nlohmann::json &) override {}

private:
  std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp::TimerBase::SharedPtr publish_timer_;
  void publish_timer_cb();

};

} // namespace remote_control
