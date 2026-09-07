# nlohmann_json 3.11.3

上游来源：[nlohmann/json v3.11.3](https://github.com/nlohmann/json/tree/v3.11.3)，MIT 许可证。

固定文件（保留上游原始字节，不转换 BOM 或行尾）：

- `include/nlohmann/json.hpp`：https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp
  SHA-256：`9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`
- `LICENSE.MIT`：https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT
  SHA-256：`86b998c792894ccb911a1cb7994f7a9652894e7a094c0b5e45be2f553f45cf14`

仓库携带依赖及许可证，离线克隆后即可构建。若缺失，在联网机器下载上述两个确切 URL，按相同目录复制到离线机器；CMake 每次配置都会校验 SHA-256，缺失或不符即失败，绝不自动下载或回退到系统版本。

`cmake/JsonDependency.cmake` 提供 `nlohmann_json::nlohmann_json`。IPC 实现 PRIVATE 链接；后续 MCP 实现也应 PRIVATE 链接，中立契约及公开接口禁止出现 JSON 类型。官方上游的 `.hpp` 文件名保留，项目新增头文件使用 `.h`。
