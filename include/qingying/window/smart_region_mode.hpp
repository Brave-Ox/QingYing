#pragma once

#include <cstdint>
#include <string>

namespace qingying {

enum class SmartRegionMode : std::uint8_t
{
  DetectElements = 0,
  WindowOnly = 1,
  Disabled = 2,
};

SmartRegionMode smartRegionModeFromPersistedValue(
    std::uint32_t value) noexcept;

// 注册表仅在 Overlay 首帧完成后读取，并仅在用户切换模式时写入。
class SmartRegionModeSettings final
{
 public:
  explicit SmartRegionModeSettings(std::wstring test_namespace = {});

  SmartRegionMode load() const noexcept;
  bool save(SmartRegionMode mode) const noexcept;
  const std::wstring& key() const noexcept;

 private:
  std::wstring m_key;
};

}  // namespace qingying
