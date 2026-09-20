#include "qingying/app/user_settings_store.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qingying {
namespace {

constexpr wchar_t SettingsKeyPrefix[] =
    L"Software\\QingYing\\Settings\\Tests\\";
constexpr wchar_t ActiveSlotValueName[] = L"ActiveSlot";
constexpr wchar_t Slot0ValueName[] = L"Slot0";
constexpr wchar_t Slot1ValueName[] = L"Slot1";
constexpr std::size_t SchemaVersionOffset = 0;
constexpr std::size_t CaptureModifiersOffset = 4;
constexpr std::size_t CopyModifiersOffset = 12;
constexpr std::size_t CopyVirtualKeyOffset = 16;
constexpr std::size_t MaximumFramesOffset = 28;

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

std::wstring makeTestNamespace()
{
  return L"user_settings_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
         std::to_wstring(GetTickCount64());
}

std::wstring testKey(const std::wstring& test_namespace)
{
  return std::wstring(SettingsKeyPrefix) + test_namespace;
}

void expectShortcutEqual(const ShortcutBinding& actual,
                         const ShortcutBinding& expected)
{
  EXPECT_EQ(actual.m_modifiers, expected.m_modifiers);
  EXPECT_EQ(actual.m_virtual_key, expected.m_virtual_key);
}

void expectSettingsEqual(const UserSettings& actual,
                         const UserSettings& expected)
{
  EXPECT_EQ(actual.m_schema_version, expected.m_schema_version);
  expectShortcutEqual(actual.m_capture_hotkey, expected.m_capture_hotkey);
  expectShortcutEqual(actual.m_selection_shortcuts.m_copy,
                      expected.m_selection_shortcuts.m_copy);
  expectShortcutEqual(actual.m_selection_shortcuts.m_toggle_longshot,
                      expected.m_selection_shortcuts.m_toggle_longshot);
  EXPECT_EQ(actual.m_longshot_limits.max_frames,
            expected.m_longshot_limits.max_frames);
  EXPECT_EQ(actual.m_longshot_limits.max_output_height,
            expected.m_longshot_limits.max_output_height);
}

UserSettings alternateSettings()
{
  UserSettings settings = defaultUserSettings();
  settings.m_capture_hotkey =
      ShortcutBinding{MOD_CONTROL | MOD_ALT, static_cast<UINT>('R')};
  settings.m_selection_shortcuts.m_copy =
      ShortcutBinding{MOD_CONTROL, static_cast<UINT>('V')};
  settings.m_selection_shortcuts.m_toggle_longshot =
      ShortcutBinding{0, static_cast<UINT>('K')};
  settings.m_longshot_limits = LongShotLimits{45, 40000};
  return settings;
}

void writeLittleEndian(std::vector<BYTE>& bytes, std::size_t offset,
                       std::uint32_t value)
{
  ASSERT_LE(offset + sizeof(value), bytes.size());
  for (std::size_t index = 0; index < sizeof(value); ++index)
  {
    bytes.at(offset + index) =
        static_cast<BYTE>((value >> (index * 8U)) & 0xFFU);
  }
}

std::vector<BYTE> readBinaryValue(const std::wstring& key_path,
                                  const wchar_t* value_name)
{
  DWORD bytes = 0;
  const LONG size_result =
      RegGetValueW(HKEY_CURRENT_USER, key_path.c_str(), value_name,
                   RRF_RT_REG_BINARY, nullptr, nullptr, &bytes);
  EXPECT_EQ(size_result, ERROR_SUCCESS);
  if (size_result != ERROR_SUCCESS)
  {
    return {};
  }
  std::vector<BYTE> value(bytes);
  const LONG read_result =
      RegGetValueW(HKEY_CURRENT_USER, key_path.c_str(), value_name,
                   RRF_RT_REG_BINARY, nullptr, value.data(), &bytes);
  EXPECT_EQ(read_result, ERROR_SUCCESS);
  if (read_result != ERROR_SUCCESS)
  {
    return {};
  }
  value.resize(bytes);
  return value;
}

void writeBinaryValue(const std::wstring& key_path, const wchar_t* value_name,
                      const std::vector<BYTE>& value)
{
  HKEY raw_key = nullptr;
  ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, key_path.c_str(), 0, nullptr,
                            0, KEY_SET_VALUE, nullptr, &raw_key, nullptr),
            ERROR_SUCCESS);
  const RegistryKey key(raw_key);
  ASSERT_EQ(RegSetValueExW(key.get(), value_name, 0, REG_BINARY, value.data(),
                           static_cast<DWORD>(value.size())),
            ERROR_SUCCESS);
}

void writeDwordValue(const std::wstring& key_path, const wchar_t* value_name,
                     DWORD value)
{
  HKEY raw_key = nullptr;
  ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, key_path.c_str(), 0, nullptr,
                            0, KEY_SET_VALUE, nullptr, &raw_key, nullptr),
            ERROR_SUCCESS);
  const RegistryKey key(raw_key);
  ASSERT_EQ(RegSetValueExW(key.get(), value_name, 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&value),
                           sizeof(value)),
            ERROR_SUCCESS);
}

DWORD readDwordValue(const std::wstring& key_path, const wchar_t* value_name)
{
  DWORD value = 0;
  DWORD bytes = sizeof(value);
  EXPECT_EQ(RegGetValueW(HKEY_CURRENT_USER, key_path.c_str(), value_name,
                         RRF_RT_REG_DWORD, nullptr, &value, &bytes),
            ERROR_SUCCESS);
  return value;
}

class UserSettingsStoreTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    m_test_namespace = makeTestNamespace();
    m_key_path = testKey(m_test_namespace);
    static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, m_key_path.c_str()));
  }

  void TearDown() override
  {
    const LONG result = RegDeleteKeyW(HKEY_CURRENT_USER, m_key_path.c_str());
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
  }

  void save(const UserSettings& settings)
  {
    UserSettingsStore store(m_test_namespace);
    SettingsPrepareResult prepared = store.prepareSave(settings);
    ASSERT_EQ(prepared.m_error, SettingsStoreError::None);
    ASSERT_TRUE(prepared.m_write.has_value());
    EXPECT_EQ(prepared.m_write->commit(), SettingsStoreError::None);
  }

  std::wstring m_test_namespace;
  std::wstring m_key_path;
};

TEST_F(UserSettingsStoreTest, EmptyRegistryReturnsDefaults)
{
  const UserSettingsLoadResult result =
      UserSettingsStore(m_test_namespace).load();

  EXPECT_EQ(result.m_error, SettingsStoreError::None);
  expectSettingsEqual(result.m_settings, defaultUserSettings());
}

TEST_F(UserSettingsStoreTest, SavesAndReadsACompleteSnapshot)
{
  const UserSettings expected = alternateSettings();
  save(expected);

  const UserSettingsLoadResult result =
      UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::None);
  expectSettingsEqual(result.m_settings, expected);
}

TEST_F(UserSettingsStoreTest, UncommittedPreparedWriteLeavesPreviousSlotActive)
{
  const UserSettings original = defaultUserSettings();
  save(original);

  {
    UserSettingsStore store(m_test_namespace);
    SettingsPrepareResult prepared = store.prepareSave(alternateSettings());
    ASSERT_EQ(prepared.m_error, SettingsStoreError::None);
    ASSERT_TRUE(prepared.m_write.has_value());
  }

  const UserSettingsLoadResult result =
      UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::None);
  expectSettingsEqual(result.m_settings, original);
}

TEST_F(UserSettingsStoreTest, CancelledPreparedWriteLeavesPreviousSlotActive)
{
  const UserSettings original = defaultUserSettings();
  save(original);

  UserSettingsStore store(m_test_namespace);
  SettingsPrepareResult prepared = store.prepareSave(alternateSettings());
  ASSERT_EQ(prepared.m_error, SettingsStoreError::None);
  ASSERT_TRUE(prepared.m_write.has_value());
  prepared.m_write->cancel();

  const UserSettingsLoadResult result =
      UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::None);
  expectSettingsEqual(result.m_settings, original);
}

TEST_F(UserSettingsStoreTest, CommitChangesTheActiveSlotAndRotatesSlots)
{
  save(defaultUserSettings());
  EXPECT_EQ(readDwordValue(m_key_path, ActiveSlotValueName), 0U);

  const UserSettings second = alternateSettings();
  save(second);
  EXPECT_EQ(readDwordValue(m_key_path, ActiveSlotValueName), 1U);

  UserSettings third = second;
  third.m_longshot_limits = LongShotLimits{50, 45000};
  save(third);
  EXPECT_EQ(readDwordValue(m_key_path, ActiveSlotValueName), 0U);

  expectSettingsEqual(UserSettingsStore(m_test_namespace).load().m_settings,
                      third);
}

TEST_F(UserSettingsStoreTest, KnownVersionRecoversOnlyTheInvalidSelectionGroup)
{
  const UserSettings expected = alternateSettings();
  save(expected);

  std::vector<BYTE> snapshot = readBinaryValue(m_key_path, Slot0ValueName);
  writeLittleEndian(snapshot, CopyModifiersOffset, MOD_CONTROL);
  writeLittleEndian(snapshot, CopyVirtualKeyOffset, 0);
  writeBinaryValue(m_key_path, Slot0ValueName, snapshot);

  const UserSettingsLoadResult result =
      UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::RecoveredInvalidGroup);
  expectShortcutEqual(result.m_settings.m_capture_hotkey,
                      expected.m_capture_hotkey);
  expectShortcutEqual(result.m_settings.m_selection_shortcuts.m_copy,
                      defaultUserSettings().m_selection_shortcuts.m_copy);
  expectShortcutEqual(
      result.m_settings.m_selection_shortcuts.m_toggle_longshot,
      defaultUserSettings().m_selection_shortcuts.m_toggle_longshot);
  EXPECT_EQ(result.m_settings.m_longshot_limits.max_frames,
            expected.m_longshot_limits.max_frames);
}

TEST_F(UserSettingsStoreTest,
       KnownVersionRecoversInvalidCaptureAndLongShotGroups)
{
  const UserSettings expected = alternateSettings();
  save(expected);

  std::vector<BYTE> snapshot = readBinaryValue(m_key_path, Slot0ValueName);
  writeLittleEndian(snapshot, CaptureModifiersOffset, 0);
  writeLittleEndian(snapshot, MaximumFramesOffset, 1);
  writeBinaryValue(m_key_path, Slot0ValueName, snapshot);

  const UserSettingsLoadResult result =
      UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::RecoveredInvalidGroup);
  expectShortcutEqual(result.m_settings.m_capture_hotkey,
                      defaultUserSettings().m_capture_hotkey);
  expectShortcutEqual(result.m_settings.m_selection_shortcuts.m_copy,
                      expected.m_selection_shortcuts.m_copy);
  expectShortcutEqual(
      result.m_settings.m_selection_shortcuts.m_toggle_longshot,
      expected.m_selection_shortcuts.m_toggle_longshot);
  EXPECT_EQ(result.m_settings.m_longshot_limits.max_frames,
            defaultUserSettings().m_longshot_limits.max_frames);
  EXPECT_EQ(result.m_settings.m_longshot_limits.max_output_height,
            defaultUserSettings().m_longshot_limits.max_output_height);
}

TEST_F(UserSettingsStoreTest, FinalDuplicateShortcutFallsBackToAllDefaults)
{
  save(alternateSettings());

  std::vector<BYTE> snapshot = readBinaryValue(m_key_path, Slot0ValueName);
  writeLittleEndian(snapshot, CopyModifiersOffset, MOD_CONTROL | MOD_ALT);
  writeLittleEndian(snapshot, CopyVirtualKeyOffset,
                    static_cast<std::uint32_t>('R'));
  writeBinaryValue(m_key_path, Slot0ValueName, snapshot);

  const UserSettingsLoadResult result =
      UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::SnapshotInvalid);
  expectSettingsEqual(result.m_settings, defaultUserSettings());
}

TEST_F(UserSettingsStoreTest, UnknownVersionAndDamagedActiveSlotFallBackToDefaults)
{
  save(alternateSettings());
  std::vector<BYTE> snapshot = readBinaryValue(m_key_path, Slot0ValueName);
  writeLittleEndian(snapshot, SchemaVersionOffset, UserSettingsSchemaVersion + 1);
  writeBinaryValue(m_key_path, Slot0ValueName, snapshot);

  UserSettingsLoadResult result = UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::UnknownSchemaVersion);
  expectSettingsEqual(result.m_settings, defaultUserSettings());

  writeDwordValue(m_key_path, ActiveSlotValueName, 2);
  result = UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::ActiveSlotInvalid);
  expectSettingsEqual(result.m_settings, defaultUserSettings());
}

TEST_F(UserSettingsStoreTest, TwoDamagedSlotsFallBackToDefaults)
{
  save(defaultUserSettings());
  save(alternateSettings());
  writeBinaryValue(m_key_path, Slot0ValueName, std::vector<BYTE>{0x01});
  writeBinaryValue(m_key_path, Slot1ValueName, std::vector<BYTE>{0x02});

  const UserSettingsLoadResult result =
      UserSettingsStore(m_test_namespace).load();
  EXPECT_EQ(result.m_error, SettingsStoreError::SnapshotInvalid);
  expectSettingsEqual(result.m_settings, defaultUserSettings());
}

TEST_F(UserSettingsStoreTest, InvalidSettingsAreRejectedWithoutWriting)
{
  UserSettings invalid = defaultUserSettings();
  invalid.m_capture_hotkey = ShortcutBinding{};

  const SettingsPrepareResult result =
      UserSettingsStore(m_test_namespace).prepareSave(invalid);
  EXPECT_EQ(result.m_error, SettingsStoreError::InvalidSettings);
  EXPECT_FALSE(result.m_write.has_value());
  EXPECT_EQ(UserSettingsStore(m_test_namespace).load().m_error,
            SettingsStoreError::None);
}

TEST(UserSettingsStoreConstructionTest, RejectsInvalidTestNamespace)
{
  EXPECT_THROW(UserSettingsStore(L"invalid\\namespace"), std::invalid_argument);
  EXPECT_THROW(UserSettingsStore(L"invalid namespace"), std::invalid_argument);
}

}  // namespace
}  // namespace qingying
