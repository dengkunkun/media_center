#pragma once
#include "nlohmann/json.hpp"
#include <string>

namespace remote_control {
class IBusinessHandler {
public:
  virtual ~IBusinessHandler() = default;
  // 返回当前模块使用哪个label
  virtual std::string get_label() = 0;
  // 返回当前业务的type
  virtual std::string get_type() = 0;
  // 处理接收到的消息
  virtual void handleMessage(const nlohmann::json &message) = 0;
  //
};

} // namespace remote_control
