#pragma once

#include "qingying/app/user_settings.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace qingying {

enum class SettingsStoreError : std::uint8_t
{
  None,
  InvalidSettings,
  RegistryReadFailed,
  RegistryWriteFailed,
  SnapshotVerificationFailed,
  ActiveSlotInvalid,
  SnapshotInvalid,
  UnknownSchemaVersion,
  RecoveredInvalidGroup,
  PreparedWriteInactive,
};

class UserSettingsStore;

class PreparedSettingsWrite final
{
 public:
  PreparedSettingsWrite() = delete;
  ~PreparedSettingsWrite();

  PreparedSettingsWrite(const PreparedSettingsWrite&) = delete;
  PreparedSettingsWrite& operator=(const PreparedSettingsWrite&) = delete;
  PreparedSettingsWrite(PreparedSettingsWrite&& other) noexcept;
  PreparedSettingsWrite& operator=(PreparedSettingsWrite&& other) noexcept;

  SettingsStoreError commit() noexcept;
  void cancel() noexcept;

 private:
  friend class UserSettingsStore;

  PreparedSettingsWrite(const UserSettingsStore* store,
                        std::uint32_t target_slot) noexcept;

  const UserSettingsStore* m_store{nullptr};
  std::uint32_t m_target_slot{0};
};

struct UserSettingsLoadResult
{
  UserSettings m_settings{defaultUserSettings()};
  SettingsStoreError m_error{SettingsStoreError::None};
};

struct SettingsPrepareResult
{
  SettingsStoreError m_error{SettingsStoreError::None};
  std::optional<PreparedSettingsWrite> m_write;
};

class UserSettingsStore final
{
 public:
  explicit UserSettingsStore(std::wstring test_namespace = {});

  UserSettingsLoadResult load() const noexcept;
  SettingsPrepareResult prepareSave(const UserSettings& settings) const noexcept;

  const std::wstring& key() const noexcept;

 private:
  friend class PreparedSettingsWrite;

  SettingsStoreError commitPreparedSlot(std::uint32_t target_slot) const noexcept;

  std::wstring m_key;
};

}  // namespace qingying
