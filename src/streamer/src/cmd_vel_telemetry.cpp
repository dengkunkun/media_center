#include "cmd_vel_telemetry.hpp"
#include "data_transceiver.hpp"

namespace remote_control {
CmdVelTeleMetry::CmdVelTeleMetry() {
  cmd_vel_pub_ =
      DataTransceiver::get_node_instance()
          ->create_publisher<geometry_msgs::msg::Twist>("cmd_vel_teleop", 10);
  client_ = rclcpp_action::create_client<nav2_msgs::action::AssistedTeleop>(
      DataTransceiver::get_node_instance(), "assisted_teleop");
}
void CmdVelTeleMetry::handleMessage(const nlohmann::json &json) {
  if (!teleopStart.load()) {
    startTeleop();
    return;
  }

  geometry_msgs::msg::Twist cmd_vel = geometry_msgs::msg::Twist();
  cmd_vel.linear.x = json["data"]["linear"]["x"];
  cmd_vel.angular.z = json["data"]["angular"]["z"];
  cmd_vel_pub_->publish(cmd_vel);
}

void CmdVelTeleMetry::startTeleop() {
  if (teleopStarting.exchange(true)) {
    return;
  }

  if (!client_->action_server_is_ready()) {
    teleopStarting.store(false);
    RCLCPP_WARN(DataTransceiver::get_logger(),
                "AssistedTeleop action server is not ready");
    return;
  }

  using GoalHandle =
      rclcpp_action::ClientGoalHandle<nav2_msgs::action::AssistedTeleop>;
  auto send_goal_options = rclcpp_action::Client<
      nav2_msgs::action::AssistedTeleop>::SendGoalOptions();
  // AssistedTeleop在接收action且没有reject，就进入了遥控状态，没有达到请求的time_allowance时间不会返回result
  send_goal_options.goal_response_callback =
      [this](const GoalHandle::SharedPtr &goal_handle) {
        teleopStarting.store(false);
        teleopStart.store(true);
        if (!goal_handle) {
          teleopStart.store(false);
          RCLCPP_WARN(DataTransceiver::get_logger(),
                      "AssistedTeleop goal was rejected");
        }
      };

  // send_goal_options.feedback_callback =
  //     [this](GoalHandle::SharedPtr,
  //            const std::shared_ptr<
  //                const nav2_msgs::action::AssistedTeleop::Feedback>
  //                feedback) {
  //       if (!feedback) {
  //         return;
  //       }
  //      这个feedback的频率很高，约10hz
  //       RCLCPP_INFO(DataTransceiver::get_logger(),
  //                   "AssistedTeleop feedback current_teleop_duration sec:%d",
  //                   feedback->current_teleop_duration.sec);
  //     };

  send_goal_options.result_callback =
      [this](const GoalHandle::WrappedResult &result) {
        teleopStart.store(false);
        teleopStarting.store(false);
        // if (result.result && result.result->error_code == 0) {
        //   teleopStart.store(true);
        // }
        RCLCPP_INFO(DataTransceiver::get_logger(),
                    "AssistedTeleop finished with result code %d msg:%s",
                    static_cast<int>(result.code),
                    result.result->error_msg.c_str());
      };

  auto goal_msg = nav2_msgs::action::AssistedTeleop::Goal();
  goal_msg.time_allowance.sec = std::numeric_limits<int>::max();
  RCLCPP_INFO(DataTransceiver::get_logger(), "AssistedTeleop sending goal");
  client_->async_send_goal(goal_msg, send_goal_options);
}

} // namespace remote_control
