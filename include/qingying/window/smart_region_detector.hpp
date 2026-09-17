#pragma once

#include "qingying/window/smart_region_types.hpp"

namespace qingying {

class SmartRegionDiagnosticTrace;
class SmartRegionVisualResultCache;

// Providers are borrowed and must outlive the detector. A supplied snapshot
// bypasses all native window/context queries, enabling tests without UI windows.
struct SmartRegionProviders {
  void* context{nullptr};
  bool (*window_snapshot)(int, int, SmartRegionWindowSnapshot&, void*) noexcept{nullptr};
  std::size_t (*accessibility_query)(const SmartRegionWindowSnapshot&, POINT,
      SmartRegionCandidate*, std::size_t, void*) noexcept{nullptr};
  bool (*visual_locator)(const SmartRegionVisualContext&, const SmartRegionWindowSnapshot&,
      POINT, SmartRegionCandidate&, void*) noexcept{nullptr};
};

// 根据屏幕坐标选择已知内容区、应用客户区或窗口边框。
class SmartRegionDetector {
 public:
  explicit SmartRegionDetector(SmartRegionProviders providers = {}) noexcept
      : m_providers(providers) {}
  bool detectAt(int screen_x, int screen_y,
                SmartRegionCandidate& out,
                SmartRegionDiagnosticTrace* diagnostics = nullptr,
                const SmartRegionVisualContext* visual_context = nullptr,
                SmartRegionDetectionPolicy policy =
                    SmartRegionDetectionPolicy::Complete,
                SmartRegionCandidateCollection* collection = nullptr,
                SmartRegionWindowSnapshot* window_snapshot = nullptr,
                SmartRegionVisualResultCache* visual_cache = nullptr)
      const noexcept;

 private:
  SmartRegionProviders m_providers;
};

}  // namespace qingying
