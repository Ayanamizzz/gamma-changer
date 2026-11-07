# Gamma Changer 架构与重构说明

最后更新：2026-11-07（2.6.2 之后：应用配置交互简化与界面重做，见 §8）

本文档是项目的**架构基准**：说明系统应该长什么样、哪些不变量不能破坏、这一轮做了什么、
还欠了哪些债以及偿还顺序。历史审计见 `Gamma-Changer-Optimization-Audit.md`。

---

## 1. 评审结论

Gamma Changer 的业务内核很小：**把一组校准参数变成 LUT，写到正确的显示器上，并在正确的时机
写回去**。真正的复杂度不在算法，而在“谁在什么时候有权改屏幕”——手动预览、已提交校准、
按应用自动切换、拓扑变化恢复，这四股力量都在争同一块 LUT。

按这个视角审视 2.6.1，问题依次是：

| # | 问题 | 后果 | 本轮 |
|---|---|---|---|
| A1 | **LUT 有两个写入者**。`CalibrationController` 管预览/提交，但按应用自动切换的逻辑写在 UI 文件 `app_profiles.cpp` 里，直接调用 `ramp_backend.write/read` | 预览/提交/运行时三种状态的互斥只能靠调用顺序“碰巧正确”；自动切换无法脱离窗口单测 | ✅ 已修 |
| A2 | **领域逻辑住在表现层**。自动切换的决策（规则→回退→桌面）、桌面快照生命周期、去重都和 `set_status`/`HWND` 混在一起 | 测试只能 `#include "ui/app_profiles.cpp"` 来覆盖 | ✅ 引擎与对话框均已抽离，测试不再包含 .cpp（见 §8） |
| A3 | **显示器身份不一致**。全系统用 `display_storage_id()`（稳定路径）做键，唯独 controller 的预览用 `device_name`（`\\.\DISPLAYn`） | 拔掉另一块屏导致编号重排后，预览无法被取消（“belongs to a different display”） | ✅ 已修 + 回归测试 |
| A4 | **滑块量化用截断**：`static_cast<int>(v * 100.0)` | 1.15 → 114 → 读回 1.14；加载 profile 后下一次自动保存会把漂移值写盘 | ✅ 已修 + 回归测试 |
| A5 | **持久化样板复制 6 份**（临时文件 → flush → MoveFileEx → 清理），各自略有差异 | 改一处漏五处；`profiles.v1` “存在但打不开”被当成 missing，会触发迁移覆盖用户文件 | ✅ 抽成 `file_io`，并修正 missing/corrupt 判定 |
| A6 | **提交后的 UI 同步序列复制 10+ 处**（`committed = …; dirty = false; KillTimer; "Saved"; 禁用按钮…`） | 已出现分叉：托盘路径漏设按钮文字 | ✅ 收敛为 `mark_committed` / `show_save_failed` |
| A7 | **六个调节项在 6 处手写展开**（EN_CHANGE、EN_KILLFOCUS、方向键、Esc、双击、范围设置…） | 加一个参数要改 6 个地方 | ✅ 表驱动 `AdjustmentBinding` |
| A8 | **同一常量/规则多处定义**：`kBuiltinProfileId` 两份；损坏回退列表在 `WM_CREATE` 里重写了一遍 `ProfileManager` 的迁移规则（且命名不一致） | 领域规则漂移 | ✅ 收归 `ProfileManager` |
| A9 | **构建图冗余**：每个测试目标手工重复列源文件，GUI 测试从 GUI 源列表里 `REMOVE_ITEM` | 新模块要改 6 处；依赖边界不可见 | ✅ `gamma_core` / `gamma_ui` 静态库 |
| B1 | `GuiState` 是上帝对象（~100 个字段：HWND、GDI、领域状态、服务实例、定时器标志混在一起） | 状态转移不可推理、不可测 | ⏳ 路线图 R1 |
| B2 | **控件是模型**：`params_from_sliders()` 是“当前设置”的唯一来源 | 模型精度受控件约束；会话逻辑无法脱离窗口 | ⏳ R1 |
| B3 | `save_and_apply_current` / `preserve_pending_adjustment` 等用例函数在 UI 层，且对“profile 库可写/只读”各写一遍 | 事务语义分散 | ⏳ R2 |
| B4 | `app_profiles.cpp` 1000+ 行，三个对话框 + 运行时胶水 | 难以维护 | ⏳ R3 |
| B5 | 状态栏文案即事件（字符串散落各处，无 i18n） | 无法本地化、无法断言 | ⏳ R4 |

**不建议做的事**：换 UI 框架、引入 DI 容器/事件总线/线程池。对一个单线程、单窗口的常驻小工具，
这些带来的复杂度大于收益。架构改进应当是“把已有的隐式规则变成显式边界”，而不是加层。

---

## 2. 分层与依赖规则

```text
┌──────────────────────────────────────────────────────────────┐
│ gamma_changer_gui (exe)   src/main.cpp + 资源                 │
├──────────────────────────────────────────────────────────────┤
│ gamma_ui (static lib)  —  Win32 表现层                        │
│   main_window  消息 → 用例调用                                 │
│   application  手动校准用例（暂居此处，见 R2）                   │
│   app_profiles 侧栏/对话框 ↔ AppProfileRuntime 的胶水           │
│   app_dialogs  两个应用配置对话框（只操作草稿，可脱离窗口测试）     │
│   layout(ui_geometry) / painting / controls / profile_list    │
├──────────────────────────────────────────────────────────────┤
│ gamma_core (static lib)  —  领域 + 应用服务，无窗口/GDI         │
│   CalibrationController   LUT 唯一写入者                       │
│   AppProfileRuntime       按应用切换引擎（无 HWND）              │
│   app_profile_rules       规则匹配 + “应用→profile”视图（纯函数） │
│   ProfileManager          profile 集合与旧四槽迁移              │
│   ProfileStore → file_io  文件格式 / 原子写入                   │
│   gamma_lut               参数校验与 LUT 生成（纯函数）           │
│   DisplayManager / DisplayRampBackend   系统边界（可替换）       │
└──────────────────────────────────────────────────────────────┘
```

规则（由构建目标强制，而不是靠自觉）：

1. `gamma_core` 不依赖 `gamma_ui`；所有单元测试只链接 `gamma_core`。
   新增领域代码如果需要 `HWND`、`set_status` 或定时器，说明它放错了层。
2. 纯领域模块（`gamma_lut`、`app_profile_rules`、`profile_list_logic`、`display_selection`、
   `AppProfileRuntime` 的 `resolve_app_profile`）不包含 `<windows.h>`。
3. 表现层只做三件事：把窗口消息翻译成用例调用；把用例结果翻译成状态文案；把状态镜像到控件。

---

## 3. 系统不变量

这些规则一旦被破坏就会出现“屏幕颜色不对”这类最难排查的问题。修改相关代码前请先读这里。

**I1 — LUT 单一写入者。** 只有 `CalibrationController` 调用 `DisplayRampBackend::write`。
`AppProfileRuntime` 通过 `capture_ramp` / `apply_runtime` / `restore_ramp` 间接写入。
`grep -rn "ramp_backend\.\(write\|read\)" src/ui src/application.cpp` 必须为空。

**I2 — 预览独占。** 手动预览激活期间，`apply_runtime` 拒绝写入（运行时切换必须让路）。
预览按 `display_storage_id` 归属，与 `\\.\DISPLAYn` 编号无关。

**I3 — 显示器身份。** 所有持久化与内存映射一律以 `display_storage_id(display)` 为键。
`device_name` 只用于调用 GDI 和兼容读取旧文件（`legacy_storage_id`，读到后迁移）。

**I4 — 绝不向未配置的显示器写默认 LUT。** 恢复流程先 `has_saved_settings`，否则会覆盖驱动/
校色仪已有的校准且无法还原。

**I5 — 桌面快照只拍一次。** 运行时从桌面切到第一个应用时拍快照；应用→应用切换沿用同一快照；
回到桌面时原样写回。拍照失败则什么都不写。

**I6 — 持久化原子性。** 所有文件写入走 `file_io::write_atomically`：跨进程锁 → 写 `.tmp` →
`FlushFileBuffers` → `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)`；任何失败都清理 `.tmp`。
读取不加锁（原子替换保证读到完整文件）。

**I7 — 损坏 ≠ 缺失。** 文件存在但无法读取/解析时报告 `corrupt`，**绝不**走迁移路径覆盖它。
只有文件确实不存在时才是 `missing`。

**I8 — 参数量化。** 参数以 0.01 为步长；与整数步进互转只用 `setting_to_steps` /
`steps_to_setting`（四舍五入）。

---

## 4. 本轮重构内容

所有改动保持用户可见行为不变，除了明确列为“修复”的条目。

### 4.1 `AppProfileRuntime`（新，`include/app_profile_runtime.h`）

- `resolve_app_profile(config, profiles, exe, display_id)`：纯决策函数。规则（profile 存在且已保存）
  → 该显示器的未分配应用默认 → 桌面设置。
- `AppProfileRuntime::apply(...)` 返回 `std::vector<RuntimeEvent>`（`applied / restored /
  restore_failed / capture_failed / apply_failed`），不碰任何 UI。UI 的
  `apply_application_profiles` 只剩“调用 + 把事件翻译成状态文案”。
- `restore_all(displays)`：断开的显示器保留条目，等它回来再还原。
- `GuiState::automatic_display_profiles`（裸 map）被 `AppProfileRuntime app_runtime` 取代。

### 4.2 `CalibrationController`

- 新增 `capture_ramp` / `restore_ramp`，成为 LUT 唯一入口（I1）。
- “稳定 ID → 旧设备名”回退原先在 `load_settings`、`has_saved_settings`、`ensure_original_ramp`、
  `restore_original` 中各写一遍，收敛为 `legacy_storage_id` + `try_load_settings` + `load_base_ramp`。
- **修复**：预览归属改用 `display_storage_id`（A3）。

### 4.3 `file_io`（新，`include/file_io.h`）

- UTF-8 编解码、大小上限检查、整文件读取、原子写入、跨进程写锁从 `profile_store.cpp` 抽出。
  `profile_store.cpp` 由 761 行降到约 590 行，六个写函数各自只剩“序列化 + 一行写入”。
- **修复**：`load_profiles` 区分“不存在”与“存在但打不开”（I7）。此前被杀毒软件/备份工具锁住的
  `profiles.v1` 会被当成 missing，`ProfileManager::load` 随即迁移并覆盖用户 profile。

### 4.4 UI 层

- `AdjustmentBinding` 表：`{edit, slider, range, &CalibrationSettings::field}`。
  `params_from_sliders`、`set_params_to_controls`、EN_CHANGE/EN_KILLFOCUS、方向键、Esc、双击复位、
  滑块范围设置、`WM_CTLCOLORSTATIC`、消息循环的“正在编辑文本”判断全部改为遍历该表。
- **修复**：滑块量化四舍五入（A4，I8）。
- `mark_committed(gui, settings)` / `show_save_failed(gui, error)`：提交成功/失败后的会话与按钮状态
  只在一处定义；`session.clear_undo()` 取代散落的两行清理。
- `kBuiltinProfileId` 与 `builtin_default_profile()` 移到 `profile_manager.h`；
  损坏回退列表改由 `ProfileManager::read_only_legacy_profiles()` 提供。
- `wWinMain` 与消息循环移到 `src/main.cpp`；`main_window.cpp` 只保留窗口过程。

### 4.5 构建

- `gamma_core` / `gamma_ui` 静态库；测试用 `gamma_core_test(name [args])` 一行注册。
- `/W4 /WX /permissive-` 与静态 CRT 通过 `add_compile_options` / `CMAKE_MSVC_RUNTIME_LIBRARY`
  全局设置，不再逐目标重复。

### 4.6 测试

新增 `app_profile_runtime_check`（无窗口、假后端）：

- 决策：大小写/斜杠无关匹配、显示器作用域、未保存 profile 不生效、回退、已删除回退、禁用。
- 运行时：首次切换、应用间切换保留原快照、去重不重写 LUT、force 重写、未绑定应用精确还原、
  默认标签、断开显示器保留条目、拍照失败不写、预览期间让路。
- 回归：显示器重新编号后仍能取消预览；1.15/-0.29 量化；步进往返；原子写不残留 `.tmp`；
  被锁的 `profiles.v1` 报告 corrupt 且不会被迁移覆盖。

结果：Release / Debug 均 `/W4 /WX` 零警告，CTest 6/6 通过。

---

## 5. 路线图（按收益/风险排序）

### R1 — 会话模型取代“控件即模型”（高收益，中风险）

`CalibrationSession` 目前只是数据袋。把它升级为真正的状态机：

```text
working_settings ──edit──▶ dirty
      │ preview(80ms debounce)        commit ──▶ committed_settings (+ persisted)
      ▼                                 ▲
   screen ◀──────── CalibrationController
```

- `working_settings` 成为唯一真相，控件只是它的视图（`params_from_sliders` 退役）。
- 状态转移（编辑、切换 profile、撤销、恢复默认、切换显示器）成为 `CalibrationSession` 的方法，
  返回“需要提交/需要预览/需要刷新 UI”的意图，由 UI 执行。
- 收益：B1/B2 的大头；手动校准用例可以像 `AppProfileRuntime` 一样无窗口测试。

### R2 — `CalibrationService`：把用例从 `application.cpp` 移进 core（中收益，中风险）

`save_and_apply_current`、`preserve_pending_adjustment`、`apply_preferred_profile_to_display`、
`reapply_all_committed` 都是“控制器 + profile 库 + 偏好”的事务，不需要 HWND。
同时合并“profile 库可写 / 只读”两套分支：只读模式只是“提交不更新 profile 定义”。
`ProfileService` 作为 `profiles.v1` 与 `presets.profile` 的唯一写入者（旧审计计划项）。

### R3 — 拆分 `app_profiles.cpp` ✅ 已完成（见 §8）

对话框移入 `ui/app_dialogs.{h,cpp}`，`foreground_profile_check` 不再 `#include` .cpp，
`app_profiles.cpp` 回到 `gamma_ui`。

### R4 — 状态事件化（低收益，低风险，可随 R1/R2 顺带）

用例返回结构化结果（如 `RuntimeEvent`），文案集中到一个翻译函数。为 i18n 和断言铺路。

### R5 — `DisplayWatcher`（中收益，中风险）

把 `WM_DISPLAYCHANGE` / `WM_POWERBROADCAST` / 250/500 ms 定时器 / 3 次重试从窗口过程中抽出，
成为一个可注入时钟的小状态机。

### R6 — 清理兼容层（低风险，需要发布节奏）

`GammaParams` 别名、`PresetSlot` / `kPresetCount` / `presets.profile` 双写、`legacy_hashed_filename`
都是迁移期产物。设定一个最低升级版本后删除。

---

## 6. 构建与测试

```bat
build-release.bat            :: VS 2022 BuildTools，配置 + 构建 + CTest
```

或：

```bat
cmake --preset vs2022-x64
cmake --build --preset release-gui
cmake --build --preset release-checks
ctest --preset release
```

测试分层：

1. 纯逻辑单测（LUT、范围、量化、规则匹配、列表索引/滚动）—— `core_safety_check`、
   `app_profile_rules_check`、`app_profile_runtime_check`。
2. 持久化集成测试（临时目录）—— `core_safety_check`、`profile_migration_check`。
3. 事务测试（假 `DisplayRampBackend`）—— `calibration_transaction_check`、`app_profile_runtime_check`。
4. 消息级 GUI 冒烟（真实对话框，假后端）—— `foreground_profile_check`。不做像素断言。
5. 目视回归：`gamma_ui_snapshot <输出目录>` 在屏幕外渲染真实主窗口与两个对话框并保存 PNG
   （临时数据目录、假 LUT 后端、样例应用列表；不读写用户配置、不改显示器、不占单实例锁）。
   改界面后对比截图。

## 7. 发布工程

- `/W4 /WX` 与 CTest 为强制门禁；CI 在 Windows 上构建 Release/Debug 并运行全部测试。
- 发布流程见 `.github/workflows/release.yml` 与 `scripts/sign-release.ps1`。
- 待办：clang-format、ASan Debug 任务；exe 尚无 `VERSIONINFO` 资源（版本只在
  `app_version.h`），资源管理器“属性→详细信息”看不到版本号。

---

## 8. 应用配置交互与界面（2026-11-07）

### 8.1 问题

“某个软件在前台时用某个 profile”本是一句话的需求，2.6.2 却把它做成了 4 层嵌套对话框
（规则管理 → 运行程序选择 → 规则编辑 → 参数编辑），并把实现细节暴露给用户：
同一软件多条规则、上移/下移优先级、“Set as default”、逐条启用勾选、规则名、
“Edit settings” 背后复制出隐藏的 `app-filter-N` profile。侧栏另有 `Bind app…`、
`App rules…`、`Auto-switch` 三个入口，暂停状态的按钮文字还会变成 “Resume auto-switch”。

### 8.2 新模型

用户只需要理解三件事：

| 概念 | 界面 |
|---|---|
| 开关 | 侧栏 “Switch automatically” 开关：紫=开，灰=关，琥珀=手动操作后暂停（点开关恢复） |
| 每个显示器上：应用 → profile | “Manage apps…” 窗口里的一张两列表：App / Profile；Add / Change / Remove |
| 其他应用 | 同一窗口底部 “Other apps use”：桌面校准（不变）或任一已保存 profile |

- **两个对话框，最多一层嵌套**：`IDD_APP_PROFILES`（管理）和 `IDD_APP_ASSIGN`
  （选应用 + 选 profile，一步完成）。右键 profile 的 “Use for an app…” 直接打开后者并立即保存。
- **一个保存边界**：管理窗口里的所有修改只改草稿，Save 一次性持久化，Cancel 无副作用。
  保存失败时窗口保持打开并显示原因。
- **可见的副作用**：添加应用会勾上 “Switch profiles automatically”——用户在点 Save
  之前就能看到；保存时开关为开则清除暂停。除此之外保存不改变任何模式。
- **单显示器时隐藏 Display 选择器**；断开的显示器仍可选，配置不丢。
- 删除的概念：多规则优先级、规则名编辑、逐条启用、Set as default、按应用改参数
  （改 profile 本身即可）。对应的核心函数 `update_app_filter_settings` 已删除。

### 8.3 兼容旧配置

文件格式不变（`app_rules.v1` v2/v3）。核心新增纯函数：

- `app_assignments(config, display)`：每个应用一行，显示**实际生效**的那条规则
  （第一条启用的；若全部禁用则显示第一条并标 “(off)”）。
- `assign_app_profile(...)`：保留生效规则的 id 并改其 profile；同一应用/显示器的其余旧规则
  在用户**显式编辑该应用**时合并掉。未编辑的应用原样保留。从不修改 `config.enabled`。
- `unassign_app_profile(...)`、`app_profile_display_ids(...)`。

### 8.4 界面结构

- **几何单一来源**：`ui_geometry(gui)` 返回所有区域（profile 列表视口、卡片、曲线区、
  每个调节行的标签/滑块/输入框、页脚）。布局、绘制、滚轮命中、滚动到可见都读它；
  以前 profile 列表位置在 4 个文件里各算一遍（`scale(252) + scale(18) + …`）。
- **profile 列表高度自适应**：不再固定 4 行。
- **单一场景绘制**：`render_scene()` 同时服务 `WM_PAINT` 与子控件的“画我背后的东西”
  （`paint_parent_layer`），所以卡片能正确出现在滑块、按钮、下拉框背后。
- **抗锯齿**：圆角、曲线、滑块、开关、图标经 `ui::aa_*`（GDI+，只在 `ui_rendering.cpp`
  内引入）；文字仍用 GDI ClearType。
- **设计令牌**：`ui_theme.h` 的颜色/尺寸；一个强调色；Segoe UI Variable（可用时）。
- 侧栏显示器下拉框自绘为深色字段（原生白色下拉框与深色侧栏冲突）；数值输入框垂直居中。
