/*
接收遥控命令，dataChannel label:control
```json
{
  "type": "cmd_vel_teleop",
  "robot_id": "R00001",
  "timestamp": 1785772800123,
  "data": {
    "linear": { "x": 0.5, "y": 0.0, "z": 0.0 },
    "angular": { "x": 0.0, "y": 0.0, "z": 0.3 }
  }
}
```

*/
#pragma once
#include "geometry_msgs/msg/twist.hpp"
#include "i_bussiness_handler.hpp"
#include "nav2_msgs/action/assisted_teleop.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include <atomic>

namespace remote_control {

class CmdVelTeleMetry : public IBusinessHandler {
public:
  CmdVelTeleMetry();
  ~CmdVelTeleMetry() override { cmd_vel_pub_.reset(); };

  std::string get_label() override { return "control"; }
  // 返回当前业务的type
  std::string get_type() override { return "cmd_vel_teleop"; }
  // 处理接收到的消息
  void handleMessage(const nlohmann::json &json) override;

private:
  // 发布topic  /cmd_vel_teleop: geometry_msgs/msg/Twist
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;

  //  启动action /assisted_teleop: nav2_msgs/action/AssistedTeleop
  std::shared_ptr<rclcpp_action::Client<nav2_msgs::action::AssistedTeleop>> client_;
  std::atomic<bool> teleopStart{false};
  std::atomic<bool> teleopStarting{false};
  void startTeleop();
};

} // namespace remote_control
