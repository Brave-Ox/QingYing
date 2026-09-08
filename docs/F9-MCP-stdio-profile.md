# F9-10 / F9-11 MCP stdio profile

实现范围固定为 **2025-11-25 / stdio**。`McpBridge` 持有一个依赖 `IAutomationClient` 的 `McpProtocolSession`；`StdioTransport` 使用继承的标准句柄运行该 bridge。公开头文件不暴露 JSON 类型。F9-11 已接通生产 `--mcp-stdio` 启动分支、Pipe 连接创建及托盘开关。

## 启动与本机开关

无参数运行 `qingying.exe` 进入原有单例托盘；右键勾选“允许本机 Agent 接口”后，客户端可以通过同一个 EXE 的 `--mcp-stdio` 参数启动 bridge。该分支在任何 GUI、热键、单例互斥体和插件构造之前分流，不创建控制台。未知参数退出码为 2，stdio 传输失败为 4，正常 EOF 为 0。

开关默认关闭，以 DWORD `Enabled` 保存在 `HKCU\Software\QingYing\Automation`。关闭开关会撤销并清理已有连接；重新启用后客户端需要重新启动 bridge，旧连接和句柄不能复用。GUI 未运行或接口关闭时，bridge 仍支持 initialize、tools/list；status 返回 `reachable:false` 和 `pipe_not_found` 等原因，不能推断的 GUI 状态为 null。不会自动启动 GUI、重连或重放请求。

## 握手与支持范围

1. `initialize` 校验 protocolVersion、capabilities 与 clientInfo.name/version，返回确定的 2025-11-25、QingYing serverInfo，以及 tools 能力（listChanged:false）。
2. 请求其他版本时，返回本端唯一支持的 2025-11-25；由客户端决定接受或断开。收到 `notifications/initialized` 后才允许 tools/list/call；重复 initialize 是协议错误。
3. ping 返回空对象；未知方法（包括 server/discover）返回 -32601。不声明 resources、prompts、tasks、logging 或动态目录能力。

目标产品客户端尚未指定，本次不宣称通过任何特定产品或 2026 profile 的集成验收。运行时 `clientInfo()` 记录对端声明的名称、产品版本、请求协议和协商协议；这些仅为协议元数据，不能代替身份认证。本地测试使用 `fixture / 1.2.3` 和 `stdio-fixture / 1`。

依据：[2025 生命周期及版本协商](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle)。

## 静态工具与句柄

同一个 `Tool` 登记项提供名称、参数字段、描述、schema、decoder 和 encoder；目录只列四项：

- status：空对象参数，返回实际状态；传输不可达时返回 reachable:false、app_running:null 和 connection_reason，未知应用字段为 null。
- get_operation：operation_id 为非空不透明字符串，返回操作状态、进度、相对时间和结果可用性。操作本身失败不会使成功的查询变成 isError:true。
- cancel_operation：operation_id 为不透明字符串，返回是否请求取消及当前状态。公共 schema 不接受 request_id。
- release_result：result_id 为不透明字符串，返回释放结果及 already_released。

所有参数对象均禁止额外字段；句柄使用 ASCII 字母、数字、下划线或连字符，长度上限来自 AutomationLimits（默认 128）。不把数字字符串解析为内部 ID。控制 DTO 可携带 operation_handle/result_handle，通过私有 wire 到 Endpoint；Endpoint 使用当前可信 context 调用 Registry 解析并验证归属，同时传数字和句柄会被拒绝。释放路径允许解析有界失效记录以保持重复释放语义。

输出同时提供 structuredContent 和对应的 text JSON，业务失败使用 isError:true；公共响应中的操作/结果 ID 保留不透明字符串，不输出内部 request/operation 数字 ID。首批 get_operation 的 outcome 保留成功/错误诊断，不公开内部 ActionOutput 和 data；后续捕获工具在 F9-15 完成结果句柄输出映射。

依据：[Tools、结构化输出及错误分层](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)。

## 请求相关性与取消

- 数字 ID 保留 int64/uint64 精度，字符串与数字分开，允许空字符串；拒绝 null、bool、浮点 ID。完整 JSON 文法由固定版本 nlohmann_json 解析，SAX 预检重复字段、深度和字符串大小。
- parse/invalid request/method/params 错误分别使用 -32700/-32600/-32601/-32602；已识别工具的业务错误放在 result.isError。有效 notification 从不应答，tools/call notification 不执行动作。
- 发送前登记原 RPC id 到内部单调 RequestId 的映射，兼容同步完成。相同在途 RPC id 再次出现时关闭 session 并清空排队输出，避免产生同 ID 的歧义响应；已完成 ID 可重用，迟到 completion 必须同时匹配内部 RequestId。
- `notifications/cancelled` 立即抑制原调用的后续响应，并提交新的 CancelOperationRequest，其 RequestCancellation 指向原内部 RequestId。取消不等待 OperationId，取消请求自身也占有有限跟踪容量；重复/未知取消不创建新请求。业务取消仍由下游判定是否受理，协议停止应答不等于操作已经停止。
- 普通工具调用使用 max_control_requests_per_connection 在途上限；协议取消另有同样大小的跟踪上限。原调用的容量保留到真实 completion 或 close，防止反复取消绕过容量。

依据：[2025 取消语义](https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/cancellation)。

## stdio、背压和退出

stdio 为逐行 UTF-8 JSON-RPC，可分段、可连续多行，也接受 CRLF；EOF 中的未结束行是截断错误，不执行残帧。默认每行最多 64 KiB、深度最多 16；session 输出按帧数和字节数限制（默认 16 帧、1 MiB），超限关闭准入，transport 负责 close 回收客户端。writer 最多另外持有一帧正在写的有界缓冲。

使用借入并复制的同步 Windows 标准句柄，适配 MCP 常用的匿名管道。读取和写入分别在 worker 中执行；write 默认 3 秒期限，watchdog 用 CancelSynchronousIo 解除阻塞，并覆盖停止信号与开始 I/O 之间的竞态。worker 实际返回后再 join 和释放缓冲。stdout 只输出协议；错误诊断走 stderr，stderr 写入同样有期限。

EOF 使本 bridge 关闭所绑定的 IAutomationClient，抑制在途请求响应，已排队响应在写期限内排空。不会关闭托盘、重连或重放操作。`AutomationResponse.transport_available` 是客户端本地传输事实，不序列化进 wire：它允许已断连客户端保留旧 connection 身份，同时让 status 正确表达不可达。

依据：[2025 stdio 传输](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)、[CancelSynchronousIo 取消及完成语义](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio)。F9-11 已通过 GUI + 双 stdio 子进程及慢 stdout 退出测试，M1 本地 status 链路已接通；实际目标客户端验收留在 F9-24。
