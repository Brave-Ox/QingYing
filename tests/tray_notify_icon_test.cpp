#include "qingying/app/tray_notify_icon.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace {

NOTIFYICONDATAW makeFilledNotifyIcon()
{
  NOTIFYICONDATAW nid = {};
  fillTrayNotifyIconData(nid, reinterpret_cast<HWND>(1), WM_APP + 1, nullptr);
  return nid;
}

}  // namespace

TEST(TrayNotifyIconTest, HoverTipTextIsQingYing)
{
  const NOTIFYICONDATAW nid = makeFilledNotifyIcon();
  EXPECT_STREQ(nid.szTip, L"轻映");
}

TEST(TrayNotifyIconTest, FlagsRequestStandardTooltipOnVersion4)
{
  const NOTIFYICONDATAW nid = makeFilledNotifyIcon();
  EXPECT_NE(nid.uFlags & NIF_TIP, 0u);
  EXPECT_NE(nid.uFlags & NIF_SHOWTIP, 0u);
}

}  // namespace qingying
