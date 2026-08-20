
/*
保存各个data channel的句柄
提供一个单例接口
分发接收到的数据
使用单例接口发送数据
*/
#pragma once
// #include "i_business_handler.hpp"
#include "rtc/rtc.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <unordered_map>
#include <utility>

#include "i_bussiness_handler.hpp"
#include "cmd_vel_telemetry.hpp"
#include "nlohmann/json.hpp"

namespace remote_control {

struct Params {
  int i; // todo 待补充
};

struct DataChannelCtx {
  DataChannelCtx() = default;
  DataChannelCtx(const std::string &id, std::shared_ptr<rtc::DataChannel> dc)
      : dc_(dc), id_(id) {}
  DataChannelCtx(DataChannelCtx &&other) noexcept
      : id_(std::move(other.id_)), dc_(std::move(other.dc_)),
        is_open_(other.is_open_.load()) {}
  DataChannelCtx(const DataChannelCtx &) = delete;
  DataChannelCtx &operator=(const DataChannelCtx &) = delete;

  std::string id_;
  std::shared_ptr<rtc::DataChannel> dc_;
  std::atomic<bool> is_open_{false};
};

class DataTransceiver {
public:
  DataTransceiver(std::shared_ptr<rclcpp::Node> node) : node_(node) { 
    p_instance = this; 
  }
  ~DataTransceiver() { p_instance = nullptr; }
  void init(){registerBussinessHandler(std::make_unique<CmdVelTeleMetry>());}

  // // 保存IBusinessHandler
  void registerBussinessHandler(std::unique_ptr<IBusinessHandler> handler);
  // // 保存dataChannel
  void saveDataChannel(const std::string &id,
                      std::shared_ptr<rtc::DataChannel> dc);

  // bool sendMessage(const std::string& label,const std::string& message);

  static DataTransceiver *instance();
  static rclcpp::Logger get_logger();
  static std::shared_ptr<rclcpp::Node> get_node_instance();

  std::shared_ptr<rclcpp::Node> get_node(){return node_;}
private:
  std::shared_ptr<rclcpp::Node> node_;
  static DataTransceiver *p_instance;

  std::mutex dataChannelMapMutex_;
  std::unordered_map<std::string, std::shared_ptr<DataChannelCtx>>
      dataChannelMap_; // key是label
  DataChannelCtx *getDataChannelCtxById(std::string id);
  DataChannelCtx *getDataChannelCtxByLabel(std::string label);
  DataChannelCtx *getDataChannelCtx(std::shared_ptr<rtc::DataChannel> &dc);

  std::map<std::pair<std::string, std::string>,
           std::unique_ptr<IBusinessHandler>>
      handlersMap_; // key是label type
  IBusinessHandler *getIBusinessHandler(
      const std::pair<std::string, std::string> &label_type);
  
  // // 声明和读取参数
  // template <typename NodeT>
  // bool getParams() {
  //   // todo 待补充
  //   return true;
  // }
  // struct Params params_;
};

} // namespace remote_control
