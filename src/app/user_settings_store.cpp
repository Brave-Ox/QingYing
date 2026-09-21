#include "qingying/app/user_settings_store.hpp"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace qingying {
namespace {

constexpr wchar_t SettingsKeyPath[] = L"Software\\QingYing\\Settings";
constexpr wchar_t TestKeySuffix[] = L"\\Tests\\";
constexpr wchar_t ActiveSlotValueName[] = L"ActiveSlot";
constexpr wchar_t Slot0ValueName[] = L"Slot0";
constexpr wchar_t Slot1ValueName[] = L"Slot1";
constexpr std::size_t MaximumTestNamespaceLength = 64;
constexpr std::size_t SnapshotFieldCount = 9;
constexpr std::size_t SnapshotSize = SnapshotFieldCount * sizeof(std::uint32_t);

class RegistryKey final
{
 public:
  explicit RegistryKey(HKEY key) noexcept : m_key(key)
  {
  }

  ~RegistryKey()
  {
    if (m_key != nullptr)
    {
      static_cast<void>(RegCloseKey(m_key));
    }
  }

  RegistryKey(const RegistryKey&) = delete;
  RegistryKey& operator=(const RegistryKey&) = delete;

  HKEY get() const noexcept
  {
    return m_key;
  }

 private:
  HKEY m_key{nullptr};
};

enum class SnapshotDecodeResult : std::uint8_t
{
  Valid,
  RecoveredInvalidGroup,
  Invalid,
  UnknownVersion,
};

bool validTestNamespace(const std::wstring& value) noexcept
{
  return value.size() <= MaximumTestNamespaceLength &&
         value.find_first_not_of(
             L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") ==
             std::wstring::npos;
}

const wchar_t* slotValueName(std::uint32_t slot) noexcept
{
  return slot == 0 ? Slot0ValueName : Slot1ValueName;
}

void writeLittleEndian(std::array<BYTE, SnapshotSize>& bytes,
                       std::size_t& offset, std::uint32_t value) noexcept
{
  for (std::size_t index = 0; index < sizeof(value); ++index)
  {
    bytes.at(offset + index) =
        static_cast<BYTE>((value >> (index * 8U)) & 0xFFU);
  }
  offset += sizeof(value);
}

std::uint32_t readLittleEndian(const std::array<BYTE, SnapshotSize>& bytes,
                               std::size_t& offset) noexcept
{
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index)
  {
    value |= static_cast<std::uint32_t>(bytes.at(offset + index)) <<
             (index * 8U);
  }
  offset += sizeof(value);
  return value;
}

std::array<BYTE, SnapshotSize> encodeSnapshot(
    const UserSettings& settings) noexcept
{
  std::array<BYTE, SnapshotSize> bytes{};
  std::size_t offset = 0;
  writeLittleEndian(bytes, offset, settings.m_schema_version);
  writeLittleEndian(bytes, offset, settings.m_capture_hotkey.m_modifiers);
  writeLittleEndian(bytes, offset, settings.m_capture_hotkey.m_virtual_key);
  writeLittleEndian(bytes, offset,
                    settings.m_selection_shortcuts.m_copy.m_modifiers);
  writeLittleEndian(bytes, offset,
                    settings.m_selection_shortcuts.m_copy.m_virtual_key);
  writeLittleEndian(bytes, offset,
                    settings.m_selection_shortcuts.m_toggle_longshot.m_modifiers);
  writeLittleEndian(
      bytes, offset,
      settings.m_selection_shortcuts.m_toggle_longshot.m_virtual_key);
  writeLittleEndian(bytes, offset,
                    static_cast<std::uint32_t>(settings.m_longshot_limits.max_frames));
  writeLittleEndian(
      bytes, offset,
      static_cast<std::uint32_t>(settings.m_longshot_limits.max_output_height));
  return bytes;
}

bool selectionGroupIsValid(const SelectionShortcutSettings& shortcuts) noexcept
{
  if (!selectionShortcutIsValid(shortcuts.m_copy) ||
      !selectionShortcutIsValid(shortcuts.m_toggle_longshot))
  {
    return false;
  }
  return shortcuts.m_copy.empty() || shortcuts.m_toggle_longshot.empty() ||
         shortcuts.m_copy != shortcuts.m_toggle_longshot;
}

SnapshotDecodeResult decodeSnapshot(
    const std::array<BYTE, SnapshotSize>& bytes,
    UserSettings& settings) noexcept
{
  std::size_t offset = 0;
  const std::uint32_t schema_version = readLittleEndian(bytes, offset);
  if (schema_version != UserSettingsSchemaVersion)
  {
    return SnapshotDecodeResult::UnknownVersion;
  }

  const std::uint32_t capture_modifiers = readLittleEndian(bytes, offset);
  const std::uint32_t capture_virtual_key = readLittleEndian(bytes, offset);
  const std::uint32_t copy_modifiers = readLittleEndian(bytes, offset);
  const std::uint32_t copy_virtual_key = readLittleEndian(bytes, offset);
  const std::uint32_t toggle_modifiers = readLittleEndian(bytes, offset);
  const std::uint32_t toggle_virtual_key = readLittleEndian(bytes, offset);
  const std::uint32_t max_frames = readLittleEndian(bytes, offset);
  const std::uint32_t max_output_height = readLittleEndian(bytes, offset);

  if (capture_modifiers > (std::numeric_limits<UINT>::max)() ||
      capture_virtual_key > (std::numeric_limits<UINT>::max)() ||
      copy_modifiers > (std::numeric_limits<UINT>::max)() ||
      copy_virtual_key > (std::numeric_limits<UINT>::max)() ||
      toggle_modifiers > (std::numeric_limits<UINT>::max)() ||
      toggle_virtual_key > (std::numeric_limits<UINT>::max)() ||
      max_frames >
          static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
      max_output_height >
          static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
  {
    return SnapshotDecodeResult::Invalid;
  }

  settings = UserSettings{};
  settings.m_schema_version = schema_version;
  settings.m_capture_hotkey = ShortcutBinding{capture_modifiers,
                                               capture_virtual_key};
  settings.m_selection_shortcuts =
      SelectionShortcutSettings{ShortcutBinding{copy_modifiers, copy_virtual_key},
                                ShortcutBinding{toggle_modifiers,
                                                toggle_virtual_key}};
  settings.m_longshot_limits =
      LongShotLimits{static_cast<int>(max_frames),
                     static_cast<int>(max_output_height)};

  const UserSettings defaults = defaultUserSettings();
  bool recovered_invalid_group = false;
  if (!captureHotkeyIsValid(settings.m_capture_hotkey))
  {
    settings.m_capture_hotkey = defaults.m_capture_hotkey;
    recovered_invalid_group = true;
  }
  if (!selectionGroupIsValid(settings.m_selection_shortcuts))
  {
    settings.m_selection_shortcuts = defaults.m_selection_shortcuts;
    recovered_invalid_group = true;
  }
  if (!longShotLimitsAreValid(settings.m_longshot_limits))
  {
    settings.m_longshot_limits = defaults.m_longshot_limits;
    recovered_invalid_group = true;
  }

  if (!validateUserSettings(settings).valid())
  {
    return SnapshotDecodeResult::Invalid;
  }
  return recovered_invalid_group ? SnapshotDecodeResult::RecoveredInvalidGroup
                                 : SnapshotDecodeResult::Valid;
}

bool readActiveSlot(HKEY key, std::uint32_t& slot) noexcept
{
  DWORD type = 0;
  DWORD bytes = sizeof(slot);
  const LONG result = RegQueryValueExW(
      key, ActiveSlotValueName, nullptr, &type,
      reinterpret_cast<BYTE*>(&slot), &bytes);
  return result == ERROR_SUCCESS && type == REG_DWORD && bytes == sizeof(slot) &&
         slot <= 1;
}

}  // namespace

PreparedSettingsWrite::PreparedSettingsWrite(const UserSettingsStore* store,
                                             std::uint32_t target_slot) noexcept
    : m_store(store), m_target_slot(target_slot)
{
}

PreparedSettingsWrite::~PreparedSettingsWrite()
{
  cancel();
}

PreparedSettingsWrite::PreparedSettingsWrite(
    PreparedSettingsWrite&& other) noexcept
    : m_store(other.m_store), m_target_slot(other.m_target_slot)
{
  other.cancel();
}

PreparedSettingsWrite& PreparedSettingsWrite::operator=(
    PreparedSettingsWrite&& other) noexcept
{
  if (this != &other)
  {
    cancel();
    m_store = other.m_store;
    m_target_slot = other.m_target_slot;
    other.cancel();
  }
  return *this;
}

SettingsStoreError PreparedSettingsWrite::commit() noexcept
{
  if (m_store == nullptr)
  {
    return SettingsStoreError::PreparedWriteInactive;
  }

  const SettingsStoreError result = m_store->commitPreparedSlot(m_target_slot);
  if (result == SettingsStoreError::None)
  {
    cancel();
  }
  return result;
}

void PreparedSettingsWrite::cancel() noexcept
{
  m_store = nullptr;
}

UserSettingsStore::UserSettingsStore(std::wstring test_namespace)
    : m_key(SettingsKeyPath)
{
  if (!validTestNamespace(test_namespace))
  {
    throw std::invalid_argument("user settings test namespace");
  }
  if (!test_namespace.empty())
  {
    m_key += TestKeySuffix;
    m_key += std::move(test_namespace);
  }
}

UserSettingsLoadResult UserSettingsStore::load() const noexcept
{
  HKEY raw_key = nullptr;
  const LONG open_result =
      RegOpenKeyExW(HKEY_CURRENT_USER, m_key.c_str(), 0, KEY_QUERY_VALUE,
                    &raw_key);
  if (open_result == ERROR_FILE_NOT_FOUND)
  {
    return UserSettingsLoadResult{};
  }
  if (open_result != ERROR_SUCCESS)
  {
    return UserSettingsLoadResult{defaultUserSettings(),
                                  SettingsStoreError::RegistryReadFailed};
  }
  const RegistryKey key(raw_key);

  std::uint32_t active_slot = 0;
  if (!readActiveSlot(key.get(), active_slot))
  {
    return UserSettingsLoadResult{defaultUserSettings(),
                                  SettingsStoreError::ActiveSlotInvalid};
  }

  DWORD type = 0;
  DWORD bytes = SnapshotSize;
  std::array<BYTE, SnapshotSize> snapshot{};
  const LONG read_result = RegQueryValueExW(
      key.get(), slotValueName(active_slot), nullptr, &type, snapshot.data(),
      &bytes);
  if (read_result != ERROR_SUCCESS || type != REG_BINARY ||
      bytes != SnapshotSize)
  {
    return UserSettingsLoadResult{defaultUserSettings(),
                                  SettingsStoreError::SnapshotInvalid};
  }

  UserSettings settings;
  switch (decodeSnapshot(snapshot, settings))
  {
    case SnapshotDecodeResult::Valid:
      return UserSettingsLoadResult{settings, SettingsStoreError::None};
    case SnapshotDecodeResult::RecoveredInvalidGroup:
      return UserSettingsLoadResult{settings,
                                    SettingsStoreError::RecoveredInvalidGroup};
    case SnapshotDecodeResult::UnknownVersion:
      return UserSettingsLoadResult{defaultUserSettings(),
                                    SettingsStoreError::UnknownSchemaVersion};
    case SnapshotDecodeResult::Invalid:
      return UserSettingsLoadResult{defaultUserSettings(),
                                    SettingsStoreError::SnapshotInvalid};
  }
  return UserSettingsLoadResult{defaultUserSettings(),
                                SettingsStoreError::SnapshotInvalid};
}

SettingsPrepareResult UserSettingsStore::prepareSave(
    const UserSettings& settings) const noexcept
{
  if (!validateUserSettings(settings).valid() ||
      settings.m_schema_version != UserSettingsSchemaVersion)
  {
    return SettingsPrepareResult{SettingsStoreError::InvalidSettings, std::nullopt};
  }

  HKEY raw_key = nullptr;
  const LONG create_result = RegCreateKeyExW(
      HKEY_CURRENT_USER, m_key.c_str(), 0, nullptr, 0,
      KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &raw_key, nullptr);
  if (create_result != ERROR_SUCCESS)
  {
    return SettingsPrepareResult{SettingsStoreError::RegistryWriteFailed,
                                 std::nullopt};
  }
  const RegistryKey key(raw_key);

  std::uint32_t active_slot = 0;
  const std::uint32_t target_slot =
      readActiveSlot(key.get(), active_slot) ? 1U - active_slot : 0U;
  const std::array<BYTE, SnapshotSize> snapshot = encodeSnapshot(settings);
  const LONG write_result = RegSetValueExW(
      key.get(), slotValueName(target_slot), 0, REG_BINARY, snapshot.data(),
      static_cast<DWORD>(snapshot.size()));
  if (write_result != ERROR_SUCCESS)
  {
    return SettingsPrepareResult{SettingsStoreError::RegistryWriteFailed,
                                 std::nullopt};
  }

  DWORD type = 0;
  DWORD bytes = SnapshotSize;
  std::array<BYTE, SnapshotSize> persisted{};
  const LONG read_result = RegQueryValueExW(
      key.get(), slotValueName(target_slot), nullptr, &type, persisted.data(),
      &bytes);
  UserSettings decoded;
  if (read_result != ERROR_SUCCESS || type != REG_BINARY ||
      bytes != SnapshotSize || persisted != snapshot ||
      decodeSnapshot(persisted, decoded) != SnapshotDecodeResult::Valid)
  {
    return SettingsPrepareResult{SettingsStoreError::SnapshotVerificationFailed,
                                 std::nullopt};
  }
  PreparedSettingsWrite prepared_write(this, target_slot);
  return SettingsPrepareResult{
      SettingsStoreError::None,
      std::optional<PreparedSettingsWrite>(std::move(prepared_write))};
}

const std::wstring& UserSettingsStore::key() const noexcept
{
  return m_key;
}

SettingsStoreError UserSettingsStore::commitPreparedSlot(
    std::uint32_t target_slot) const noexcept
{
  if (target_slot > 1)
  {
    return SettingsStoreError::PreparedWriteInactive;
  }

  HKEY raw_key = nullptr;
  const LONG open_result = RegOpenKeyExW(
      HKEY_CURRENT_USER, m_key.c_str(), 0, KEY_SET_VALUE, &raw_key);
  if (open_result != ERROR_SUCCESS)
  {
    return SettingsStoreError::RegistryWriteFailed;
  }
  const RegistryKey key(raw_key);
  const DWORD active_slot = target_slot;
  return RegSetValueExW(
             key.get(), ActiveSlotValueName, 0, REG_DWORD,
             reinterpret_cast<const BYTE*>(&active_slot),
             sizeof(active_slot)) == ERROR_SUCCESS
             ? SettingsStoreError::None
             : SettingsStoreError::RegistryWriteFailed;
}

}  // namespace qingying
