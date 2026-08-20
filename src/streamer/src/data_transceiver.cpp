#include "data_transceiver.hpp"
#include <rclcpp/node.hpp>

namespace remote_control {
namespace {
template <class T> std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) {
  return ptr;
}

} // namespace
DataTransceiver *DataTransceiver::p_instance = nullptr;
DataTransceiver *DataTransceiver::instance() {
  assert(p_instance);
  return p_instance;
}
std::shared_ptr<rclcpp::Node> DataTransceiver::get_node_instance()
{
  return p_instance->get_node();
}
rclcpp::Logger DataTransceiver::get_logger() {
  return p_instance->get_node()->get_logger();
}
void DataTransceiver::saveDataChannel(const std::string &id,
                                     std::shared_ptr<rtc::DataChannel> dc) {
  RCLCPP_INFO(node_->get_logger(), "id:%s label:%s", id.c_str(),
              dc->label().c_str());
  std::lock_guard<std::mutex> Lock(this->dataChannelMapMutex_);

  dataChannelMap_.emplace(dc->label(),
                          std::make_shared<DataChannelCtx>(id, dc));

  dc->onOpen([wdc = make_weak_ptr(dc)]() {
    if (auto dc = wdc.lock()) {
      DataChannelCtx *p = DataTransceiver::instance()->getDataChannelCtx(dc);
      if (p) {
        p->is_open_ = true;
      }
    }
  });

  dc->onClosed([id]() {
    DataChannelCtx *p = DataTransceiver::instance()->getDataChannelCtxById(id);
    if (p) {
      p->is_open_ = false;
    }
  });
  dc->onMessage([id](auto data) {
    DataChannelCtx *pdctx =
        DataTransceiver::instance()->getDataChannelCtxById(id);
    if (!std::holds_alternative<std::string>(data)) {
      return;
    }
    RCLCPP_INFO(DataTransceiver::get_logger(), "message:%s",
                std::get<std::string>(data).c_str());
    nlohmann::json message = nlohmann::json::parse(std::get<std::string>(data));
    std::string type = message["type"];
    if (pdctx) {
      IBusinessHandler *phandler =
          DataTransceiver::instance()->getIBusinessHandler(
              {pdctx->dc_->label(), type});
      if (phandler) {
        phandler->handleMessage(message);
      }
    }
  });
}

void DataTransceiver::registerBussinessHandler(std::unique_ptr<IBusinessHandler> handler)
{
  handlersMap_[{handler->get_label(),handler->get_type()}] =std::move(handler);
}

//   bool DataTransceiver::sendMessage(const std::string& label,const
//   std::string& message)
//   {
//     auto dc=getDataChannel(label);
//     if(dc)
//     {
//         return dc->send(message);
//     }
//   }

DataChannelCtx *DataTransceiver::getDataChannelCtxById(std::string id) {
  for (const std::pair<std::string, std::shared_ptr<DataChannelCtx>> &m :
       dataChannelMap_) {
    if (m.second->id_ == id) {
      return m.second.get();
    }
  }
  return nullptr;
}
DataChannelCtx *DataTransceiver::getDataChannelCtxByLabel(std::string label) {
  auto it = dataChannelMap_.find(label);
  if (it != dataChannelMap_.end()) {
    return it->second.get();
  }
  return nullptr;
}
DataChannelCtx *
DataTransceiver::getDataChannelCtx(std::shared_ptr<rtc::DataChannel> &dc) {
  for (const std::pair<std::string, std::shared_ptr<DataChannelCtx>> &m :
       dataChannelMap_) {
    if (m.second->dc_ == dc) {
      return m.second.get();
    }
  }
  return nullptr;
}

IBusinessHandler *
DataTransceiver::getIBusinessHandler(
    const std::pair<std::string, std::string> &label_type) {
  const auto handler = handlersMap_.find(label_type);
  return handler != handlersMap_.end() ? handler->second.get() : nullptr;
}

} // namespace remote_control
