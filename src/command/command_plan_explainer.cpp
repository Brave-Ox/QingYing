#include "qingying/command/command_plan_explainer.hpp"

#include <type_traits>
#include <variant>

namespace qingying {
namespace {

std::wstring explainAction(const ActionRequest& request) {
  return std::visit([](const auto& payload) -> std::wstring {
    using Payload = std::decay_t<decltype(payload)>;
    if constexpr (std::is_same_v<Payload, CaptureWindowRequest>) {
      return L"截取窗口：" + payload.window_query;
    } else if constexpr (std::is_same_v<Payload, CropCenterRequest>) {
      return L"中心裁剪：" + std::to_wstring(payload.width) + L"×" +
             std::to_wstring(payload.height);
    } else if constexpr (std::is_same_v<Payload, CopyRequest>) {
      return L"复制当前截图";
    } else if constexpr (std::is_same_v<Payload, SaveRequest>) {
      return L"保存到：" + payload.path;
    } else if constexpr (std::is_same_v<Payload, PinRequest>) {
      return L"钉图";
    } else {
      return L"查看状态";
    }
  }, request.payload);
}

}  // namespace

std::wstring explainCommandPlan(const CommandPlan& plan) {
  std::wstring result;
  for (std::size_t index = 0; index < plan.actions.size(); ++index) {
    if (index != 0) result += L"；然后";
    result += explainAction(plan.actions[index]);
  }
  return result;
}

}  // namespace qingying
