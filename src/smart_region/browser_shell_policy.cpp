#include "browser_shell_policy.hpp"

namespace qingying {
namespace {

bool isInteractiveBrowserShellRole(BrowserShellRole role) noexcept
{
  switch (role)
  {
    case BrowserShellRole::Button:
    case BrowserShellRole::BookmarkFolder:
    case BrowserShellRole::ExtensionAction:
    case BrowserShellRole::ToolbarIcon:
      return true;
    case BrowserShellRole::Unknown:
    case BrowserShellRole::Pane:
      return false;
  }
  return false;
}

}  // namespace

bool shouldUseBrowserShellPolicy(
    const SmartRegionWindowSnapshot& snapshot) noexcept
{
  return snapshot.is_chromium_browser_chrome;
}

bool acceptsBrowserShellCandidate(
    const SmartRegionCandidate& candidate, BrowserShellRole role) noexcept
{
  return candidate.valid() &&
         candidate.source == SmartRegionDiagnosticSource::Uia &&
         candidate.semantic == SmartRegionSemantic::ActionableControl &&
         candidate.uia_metadata.available &&
         candidate.uia_metadata.is_control_element &&
         candidate.uia_metadata.is_enabled &&
         isInteractiveBrowserShellRole(role);
}

}  // namespace qingying
