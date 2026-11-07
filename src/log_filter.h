// tnaimpact - log filtering helpers
//
// MoltenVK emits "Metal does not support disabling primitive restart" for
// every graphics pipeline created with primitiveRestartEnable=VK_FALSE. The
// flag is harmless on Metal (primitive restart is always available there),
// but the message arrives through the SDK's Vulkan debug messenger as a
// warning and floods the log (tens of thousands of identical lines per run,
// which is enough to rotate a 5 MB log away in under a minute). The SDK
// build we use has no cvar to drop it, so filter that one message on the
// "gpu" logger here. Everything else is passed through unchanged.

#pragma once

#include <memory>
#include <string_view>
#include <utility>

#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/sink.h>

#include <rex/logging.h>

namespace tnaimpact {

// Forwards all messages to |inner| except those whose payload contains
// |needle|.
class MessageFilterSink final : public spdlog::sinks::sink {
 public:
  MessageFilterSink(spdlog::sink_ptr inner, std::string_view needle)
      : inner_(std::move(inner)), needle_(needle) {}

  void log(const spdlog::details::log_msg& msg) override {
    const std::string_view payload(msg.payload.data(), msg.payload.size());
    if (payload.find(needle_) != std::string_view::npos) {
      return;
    }
    inner_->log(msg);
  }

  void flush() override { inner_->flush(); }

  void set_pattern(const std::string& pattern) override {
    inner_->set_pattern(pattern);
  }

  void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override {
    inner_->set_formatter(std::move(formatter));
  }

 private:
  spdlog::sink_ptr inner_;
  std::string_view needle_;
};

// Drops MoltenVK's per-pipeline "primitive restart" warning from the GPU log
// category.
inline void SilenceMoltenVkPrimitiveRestartWarning() {
  // Force the category to exist: it is registered lazily on first use, and
  // OnPostInitLogging runs before the GPU plugin starts logging. Registering
  // it here also gives it the current (file/console/capture) sinks.
  auto category = rex::log::gpu();
  auto logger = rex::GetLogger(category);
  if (!logger) {
    return;
  }
  for (auto& sink : logger->sinks()) {
    sink = std::make_shared<MessageFilterSink>(
        sink, "Metal does not support disabling primitive restart");
  }
  REXLOG_DEBUG(
      "tnaimpact: filtering MoltenVK primitive-restart warnings from the gpu log");
}

}  // namespace tnaimpact
