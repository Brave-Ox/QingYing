# F9 私有 IPC wire v1

本文件记录 F9-08 的 codec 边界及 F9-09 的本机管道传输。codec 入口位于 `src/ipc/automation_wire_codec.h`，不公开 JSON 类型；管道入口为 `PipeServer` 与 `PipeAutomationClient`。这是私有 IPC，不是 MCP stdio 协议。生产开关和应用接线留到 F9-11。

## 帧与错误

每帧为 **4 字节小端无符号 body 长度 + UTF-8 JSON body**。长度不包含前缀，必须为 1～65536；允许注入更小的上限。16 层容器深度包含根对象，允许收紧，不允许放宽。禁止 BOM、尾随第二个 JSON 值、重复字段（含解码后相同的转义键）、非法 UTF-8、浮点数、未知字段和未知枚举。

FrameDecoder 先收全长度前缀、验证上限，之后才预留 body；一次只缓冲一个帧。半包保留，粘包依次交付 Consumer，不累计消息列表。Consumer 返回 false 或抛异常后立即停止，后续帧不再处理。错误为粘滞状态；关闭连接并销毁 decoder，不跳过坏帧继续解析。EOF 调用 finish：空闲为成功，残留前缀/body 为 TruncatedFrame。Consumer 不得重入 decoder。

WireError 是本地解码错误，不等同于 ActionResult.error_code：InvalidConfiguration、InvalidFrameLength、TruncatedFrame、InvalidJson、DuplicateKey、DepthLimit、InvalidMessage、UnsupportedVersion、ResourceLimit、ConsumerRejected。异常在 codec 边界转换，分配失败返回 ResourceLimit，不把原始异常文本写入响应。

## hello

客户端：

```json
{"type":"hello","wire_version":1,"role":"client","capabilities":[]}
```

服务端使用 `role:"server"`，另须提供非零 uint64 `application_epoch`、`connection_generation`、完整 `limits` 和实际 `capabilities`；客户端 hello 不接受 epoch/generation/limits。F9-09 补齐服务端 generation 字段，客户端据此绑定服务端实际分配的连接；F9-08 的开发态 server hello 缺少此字段时会被拒绝。版本不为 1 返回 UnsupportedVersion。capabilities 不重复，只能使用 codec 已知动作/控制能力名称；当前传输默认空列表，实际 Tool 能力接线留到后续任务。

limits 覆盖 AutomationLimits 全部字段，整数保持原类型；chrono 字段改为 `result_ttl_ms`、`completed_operation_ttl_ms`、`default_longshot_timeout_ms`、`default_request_timeout_ms`、`max_request_timeout_ms`、`tombstone_ttl_ms`，接收值最多 24 小时并继续验证限额间关系。对端声明不能自动放宽本地 decoder 限额。

epoch、generation 和能力声明仅是协议元数据。F9-09 先通过操作系统完成身份校验，再建立可信连接；不得以 hello 或 JSON 中的进程号作为身份依据。codec 本身不实现握手时序与消息方向检查，Pipe 传输负责这些准入规则。

## 本机 Pipe 传输与线程边界（F9-09）

- 端点固定为 `\\\\.\\pipe\\QingYing.Automation.v1.<logon-sid>`；测试仅允许追加 `.test.<字母数字、下划线或连字符>`，不能注入远程路径。默认最多四个槽，等待 hello、等待 UI 创建 context 和等待 UI 清理都占用槽位。
- 所有实例在 worker 启动前创建。首实例带 `FILE_FLAG_FIRST_PIPE_INSTANCE`，全部实例使用受保护的显式登录 SID DACL、`PIPE_REJECT_REMOTE_CLIENTS` 和非继承句柄；原始实例句柄保持到所有 worker 回收，断连后复用实例，避免重新创建端点的空隙。抢占或身份检查失败直接关闭，不尝试其他名称或权限。
- 双向查询管道实际进程和会话，再读取 token 比较 user SID、logon SID 和 session。服务端读取 client hello 后还模拟客户端，校验有效 token；RAII 保证回到自身身份。客户端以 `SECURITY_IDENTIFICATION` 打开管道，只向对端提供身份查询所需模拟级别。
- `PipeServer::start/drain/stop` 在创建它的 UI owner 线程调用；I/O 不直接访问 Endpoint、ResultStore 或 Workflow。UI 周期调用 `drain()`；connect hook 绑定 `Endpoint::connectAuthenticated`，submit hook 绑定 `Scheduler::submit`，revoke hook 绑定线程安全的 `Scheduler::disconnect`，disconnect hook 绑定 UI 的 `Endpoint::disconnect`。hook 不得抛异常或重入 server。先撤销传输和 scheduler 准入，再清理 UI scope；首次队列受理时间保留至 scheduler。
- 每个槽一个连接/read worker，每条连接一个独立 write worker。连接、读、写均为 overlapped；短读写按实际字节数推进。握手默认 3 秒（包括等待 UI），已认证连接空闲读取可等待，部分帧必须在 3 秒内收完；每个输出帧默认 3 秒写期限。期限可注入为 1 毫秒至 1 分钟。
- 普通/控制请求分别遵守本地和全局容量，已交给 UI 但未完成的请求仍计数；drain 优先控制队列，每轮只处理有界快照。重复在途 request/RPC id、超容量、错误消息方向或畸形帧会关闭该连接。输出同时限制帧数和总字节数（含正在写的帧及四字节前缀），超限或写超时只撤销该连接。迟到或重复的 completion 不会消费新请求。
- `PipeAutomationClient` 实现 `IAutomationClient`。每个对象只连接一次；先完成 `connect()`，之后可并发 submit/close。请求跟踪在发送前建立；接收完成在 reader 线程，立即拒绝在调用线程。close 取消 I/O 并等待 reader 结算所有 pending 回调；回调内 close 会先结算其余回调，当前回调返回后 reader 继续回收。回调不得抛异常。连接失败、断连后不重放请求。
- stop 先撤销准入并唤醒所有 I/O；每个 pending 操作调用 `CancelIoEx` 后收取 `GetOverlappedResult`，再释放 OVERLAPPED、event 和 buffer。worker 不等 UI 消息、不调用 `FlushFileBuffers`；UI join 的只是已收到停止信号且不依赖 UI 的 I/O worker。重新启用构造新的 PipeServer，Endpoint 继续分配不复用的 generation。

Windows API 依据：[Named Pipe 安全与权限](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)、[获取真实客户端进程](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeclientprocessid)、[取消后收取 I/O 完成](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)。跨实际登录会话、远程拒绝和完整生产进程验证仍在 F9-24/F9-11；本地单测只创建测试专用管道。

## 请求

所有请求必填 `type`、`rpc_id`、非零 uint64 `request_id`、对象 `payload`；可选 `timeout_ms`，省略选择应用默认期限，null/零/负数/超限均拒绝。

```json
{"type":"execute_action","rpc_id":"call-1","request_id":1,"payload":{"action":"status"}}
```

`rpc_id` 为 int64、uint64 或 UTF-8 字符串，字符串最多 max_request_key_bytes（默认 128）字节，可为空；不接受 null/bool/float。整数绝不经 double，UINT64_MAX 和 INT64_MIN 均无损。非负 signed 整数在解码后可规范化为 uint64，但数字值保持不变；字符串 `"123"` 与数字 `123` 不混同。

白名单请求和 payload：

- `execute_action`：由 `action` 区分。status 无额外字段；capture_region 接收 region（x/y/width/height）；capture_window 接收 query；crop_center 接收 width/height；copy/pin 接收 result_id；save 接收 result_id/path。只有 copy/pin/save 可选 request_key。
- `begin_longshot`：空对象，期限放在外层 timeout_ms。
- `get_operation`：operation_id。
- `cancel_operation`：operation_id 或 request_id，恰好一个。内部 RequestId 目标不可等于本次调用自身 request_id。公共 cancel_operation Tool 仍只接受不透明 OperationHandle，内部 RequestId 不成为公共参数。
- `release_result`：result_id。

这些数值 ID 属于私有 DTO，应用仍须检查该 ID 的作用域所有权。F9-10 增加控制请求的句柄形式：get_operation/cancel_operation 的 payload 可改为仅包含 operation_handle，release_result 可改为仅包含 result_handle。句柄与数字目标必须互斥；MCP 只生成句柄形式，由 Endpoint 在可信 context 内调用 Registry 解析。codec 不创建 TrustedAutomationContext，不接受 scope/user/pid/generation、取消令牌或提交时刻。query 默认最多 1024 UTF-16 单元、路径最多 32767 单元、末尾文件名最多 255 单元、request_key 最多 128 字节；补充平面字符计两个 UTF-16 单元。SAX 在创建 DOM 前检查这些字符串上限，Windows 转换使用严格 Unicode 校验。

AutomationResponse.transport_available 是 F9-10 为离线 status 补充的客户端本地事实，不进入 wire。服务端 JSON 不能伪造此标记；客户端断连回调同时保留旧 connection 身份并设置 transport_available:false。

save.path 是现有内部 SaveRequest 的完整路径，不是公共 Tool 的 path/name schema；目录合成、覆盖策略、路径授权及句柄所有权属于后续 Tool/应用层。截图坐标保留负原点，但拒绝整数越界及 right/bottom 加法溢出。像素预算和实际桌面范围继续由执行者检查。

## 响应与通知

`response` 必填 rpc_id、result、control；可选 result_handle/operation_handle。result 包含 request_id、operation_id（未分配可为零）、ok、error_code、message、data、failure_stage、failure_frame、output。未知非负 error_code 数值保留；ok 必须与 error_code 是否为零一致。

output 通过 kind 标记区分 none/status/captured/saved/copied/pinned/window_candidates，对应中立 ActionOutput；不包含 Image、像素或平台窗口句柄。status 保留未知布尔字段的 null，包含实际能力及可选 limits/resources/queues；队列计数校验 queued+running 与 ordinary+control 相等。候选列表最多 64 项，每项只含 title/process_id/bounds/window_token，进程号只是候选元数据。

control 通过 kind 区分 none/operation/cancellation/released。operation 含 OperationSnapshot；cancellation 含互斥 target、operation_id、state、cancellation_requested；released 含 result_id、already_released。控制结果的 operation_id 与外层结果一致，release 的外层 operation_id 为零。查询失败操作时，外层查询仍可成功，原失败存在 snapshot.outcome 内。

通知 type 为 operation_progress 或 operation_completed，必填 operation，可选两个不透明句柄；进度通知只能包含非终态，完成通知必须包含终态、completed_ago_ms 与 outcome。终态及成功/失败/取消/超时结果须一致。

两个 handle 字段均为非空 UTF-8 字符串（默认最多 128 字节，禁止内嵌 NUL），不会被解析成数值或互换类型。公共句柄和私有数值 ID 的连接绑定映射仍由后续协议/应用适配层维护。AutomationResponse.connection 不序列化，解码值为空；认证 transport 在回调前绑定本连接身份。

## 相对时间与生命周期

CapturedResult.expires_at 编码为 expires_in_ms（无期限为 null，已过期为零）；OperationSnapshot 的 submitted_at/updated_at/completed_at 编码为 submitted_ago_ms/updated_ago_ms/completed_ago_ms。均为有界相对毫秒，最多 24 小时；接收方用自己的单调时钟重建显示元数据，绝不传输平台 steady_clock 原始 ticks。

相对期限只是状态提示，不能授予或延长服务端结果租约；服务端 ResultStore 的实际期限和 scope 校验始终生效。解码成功也不代表连接已认证或能力已开放。F9-08 的新 target 仅参与编译与测试，生产 Pipe 监听和 MCP Tool 接入留给后续任务。
