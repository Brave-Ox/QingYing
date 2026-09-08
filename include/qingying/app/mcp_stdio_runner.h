#pragma once
#include <string>
#include <vector>
namespace qingying {
enum class LaunchMode { Tray, McpStdio, Invalid };
LaunchMode parseLaunchMode(const std::vector<std::wstring>& arguments);
int runMcpStdio(const std::wstring& test_namespace = {});
}  // namespace qingying
