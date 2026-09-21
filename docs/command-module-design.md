# Command 模块设计

## 目标

`command` 将本地口令转换为既有的 `ActionRequest` / `CommandPlan`。模块只负责
文本解析、计划校验和诊断说明；不创建窗口、不调用 `app`、不执行截图或导出。

## 分层

```text
输入文本
  -> CommandText（裁剪、引号与标点处理）
  -> CommandDimensionParser（中心裁剪规格）
  -> CommandRules（动作规则匹配）
  -> CommandParser（组装 CommandPlan）
  -> CommandPlanValidator（只校验既有计划约束）
  -> CommandPlanExplainer / CommandParseDiagnostic（测试与演示）
```

## 兼容性约束

- `tryParse` 与两参数 `tryParsePlan` 保持原有调用方式。
- 现有支持和拒绝的口令由黄金测试固定，重构不得扩展或收缩语法。
- 三参数 `tryParsePlan` 仅增加可选诊断，不参与 app 执行路径。
- 计划校验仅接受现有的“窗口截图后复制/钉图”两步计划。

## 测试

`qingying_command_tests` 是独立测试目标，可使用以下命令运行：

```powershell
ctest --test-dir build -C Debug -L command --output-on-failure
```

测试包含黄金语料、文本边界、尺寸解析、计划校验、诊断输出和畸形输入稳定性。
