# Gamma Changer Optimization Audit

审计日期：2026-08-13  
范围：当前 C++17 / Win32 GUI、CLI、Gamma/LUT、显示器枚举、Profile、Wallpaper、DWM/GDI、构建与测试。  
原则：本轮只审计，不修改产品源码。

## 结论摘要

当前项目已经具备一个可继续演进的轻量架构，不需要整体重写。核心优点是：

- `CalibrationSettings -> LutGenerator -> GammaRamp -> DisplayManager` 的核心路径已经形成；
- LUT 使用固定大小 `std::array`，没有逐次 heap allocation；
- Live Preview 已使用同一个 80 ms Win32 Timer 做 debounce；
- Wallpaper 只解码一次，Profile v1 已使用临时文件和替换写入；
- Display HDC 已有局部 RAII；
- Release x64 使用 `/O2 /W4 /GS /permissive-`，GUI、CLI 和迁移检查均可构建；
- 当前机器通过只读枚举识别到两台显示器：`S65` 和 `25G3Z`。

当前最大问题不是算法速度，而是产品状态语义：Preview、Committed、Saved Profile
还没有被明确分开。其次是显示器稳定身份、配置损坏保护和事件驱动的显示器生命周期。

## 1. Current architecture

### 模块

| 模块 | 当前职责 | 评价 |
|---|---|---|
| `gui_main.cpp` | Win32 控件、布局、状态、Profile 交互、Tray、消息循环、绘制 | 功能完整，但 1500+ 行且承担过多产品状态 |
| `calibration_controller.*` | 校验、捕获原始 Ramp、Preview、Apply、Reset | 边界清楚，但缺少 session/committed 状态 |
| `gamma_lut.*` | 参数校验和 3×256 LUT 生成 | 小、确定性强、无动态分配，适合单测 |
| `display_manager.*` | 枚举显示器、友好名称、读写 Gamma Ramp | HDC 有 RAII；稳定 ID 与拓扑事件不足 |
| `profile_store.*` | 每屏参数、旧四槽、Profile v1、基础 Ramp 文件 | Profile v1 原子替换良好，其余文件仍直接覆盖 |
| `profile_manager.*` | 新 Profile 模型和旧四槽兼容桥 | 迁移思路正确，但 UI 仍依赖固定四槽 |
| `ui_rendering.*` | WIC 图片解码、背景与半透明面板绘制 | 图片只解码一次；缩放结果没有缓存 |

### 当前 Live Preview 调用链

```text
Slider / valid numeric change
  -> controls become the effective settings source
  -> dirty = true
  -> restart the same 80 ms timer
  -> immediate UI repaint / graph generation
  -> timer fires
  -> CalibrationController::preview
  -> validate
  -> load cached base ramp file from disk
  -> generate fixed-size LUT
  -> CreateDC + SetDeviceGammaRamp
```

结论：已经有合理 debounce，不应再盲目加入第二套 Timer。真正可优化的是磁盘读取、
重绘范围和背景缩放，而不是 LUT 本身。

### 当前保存调用链

```text
Save profile
  -> update legacy slot in memory
  -> write profiles.v1 atomically
  -> write presets.profile directly
  -> apply Gamma Ramp
  -> write per-display .profile directly
```

该流程缺少统一的成功/失败事务语义，部分步骤失败时 UI 可能仍显示最终成功。

## 2. Findings by severity

这里的 `Critical / High / Medium / Low` 表示影响程度；`P0-P3` 表示实施优先级。

### Critical

#### C1 — Live Preview 会在切换显示器时被隐式保存和提交（P0）

- 证据：Live Preview 下只要 `dirty`，显示器切换就调用 `flush_active_display()`；该函数执行
  `apply_and_save()`。正常从 Tray Exit 退出时也没有恢复 Preview 前状态。
- 结果：用户只是试调参数，也可能在未点击 `Save profile` 的情况下把参数永久写入每屏配置；
  UI 中“live, reversible feedback”的含义与实际行为不一致。
- 预期收益：建立可靠且可解释的 Preview / Committed / Saved Profile 三态。
- 风险：中。需要先确定产品语义，不能简单地在所有退出路径无条件 Reset。
- 涉及：`src/gui_main.cpp`、`src/calibration_controller.cpp/.h`、可能新增轻量 session state。

#### C2 — 不完整的基础 Ramp 文件会被当成有效文件（P0）

- 证据：`load_base_ramp()` 在固定长度读取后返回 `input.good() || input.eof()`；截断文件通常会
  同时出现 EOF，因此可能返回成功。
- 结果：Reset 可能把部分有效、部分零值的 Ramp 写入显示器，造成严重的异常画面。
- 预期收益：直接消除最危险的配置损坏恢复路径。
- 风险：低。严格检查实际读取字节数和文件长度即可。
- 涉及：`src/profile_store.cpp`，并增加 Ramp 文件测试。

#### C3 — `\\.\DISPLAY1` / `DISPLAY2` 被当作长期身份（P0）

- 证据：每屏参数与基础 Ramp 均以 `device_name` 命名；刷新仅按同一名称重新选择。
- 结果：重插、Dock、显卡/拓扑变化后编号可能改变，旧 Ramp 或配置可能对应到另一台物理屏幕。
- 预期收益：避免在错误显示器上加载配置或执行 Reset。
- 风险：中。需要增加 stable monitor identifier，并兼容迁移现有文件。
- 涉及：`gamma_types.h`、`display_manager.*`、`profile_store.*`、`calibration_controller.*`。

### High

#### H1 — 核心参数校验没有统一安全范围（P0）

- UI Slider 有范围，但 `LutGenerator::validate()` 只检查正数和 finite；CLI 和配置文件可传入
  极端但 finite 的 Gamma/Contrast/Gain。
- GUI 对 `NaN/inf` 不会崩溃，但失焦时会把非法文本留在输入框中。
- 建议把 UI 当前范围作为单一 domain policy，并提供 `sanitize/validate`，让 GUI、CLI、Profile、
  Controller 共用。
- 收益：防止极端 Ramp，消除多处范围常量漂移。
- 风险：低；但要先决定旧配置越界时是 clamp 还是拒绝。
- 涉及：`gamma_types.h`、`gamma_lut.*`、`gui_main.cpp`、`main.cpp`。

#### H2 — 保存失败可能被后续“Applied successfully”覆盖（P0）

- `save_active_preset()` 返回 `void`；Profile 持久化失败后，调用方仍继续 `apply_selected()`，
  最终可能覆盖错误状态。
- 删除 Profile 时，持久化返回值被忽略，随后仍显示 cleared success。
- 收益：用户不会误以为数据已保存。
- 风险：低。
- 涉及：`gui_main.cpp`、`profile_manager.cpp`。

#### H3 — 部分配置仍直接截断覆盖（P0）

- `profiles.v1` 已安全替换，但 `.profile`、`presets.profile` 和 `.ramp` 使用 `trunc` 直接写目标。
- Crash、断电或写入失败会留下半文件；Ramp 文件后果最严重。
- 建议复用一个 `write temp -> close/flush -> MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` helper。
- 收益：显著提高配置可靠性。
- 风险：低到中，需处理首次创建和遗留 `.tmp`。
- 涉及：`profile_store.cpp`。

#### H4 — 损坏的 `profiles.v1` 与“不存在”无法区分（P0）

- `load_profiles()` 仅返回 bool；解析损坏会触发 legacy migration，并可能覆盖原文件。
- 建议返回 `missing / loaded / corrupt / unsupported_version`，损坏文件先备份，再回退默认值。
- 收益：升级或损坏时不静默丢失用户 Profile。
- 风险：低到中。
- 涉及：`profile_store.*`、`profile_manager.*`。

#### H5 — Dirty state 只是布尔值，不是设置比较（P1）

- 选择任意 Profile 后无条件 `dirty = true`；切换 Profile 会直接覆盖当前未保存编辑；没有
  committed snapshot 或 selected-profile snapshot。
- Save 是否可用不能真实表达“当前值是否和已保存值不同”。
- 收益：Save、Discard、Reset、Profile 切换都变得可预测。
- 风险：中，需要先完成 C1 的状态模型。
- 涉及：`gui_main.cpp`，可抽取 `CalibrationSession`。

#### H6 — Reset 同时混合了两种含义（P1）

- 当前 Reset 恢复最初捕获的系统 Ramp，同时把每屏参数保存为 defaults。
- 它不是“撤销当前编辑”，也不是单纯“恢复 Profile 保存值”。
- 建议明确分成 `Reset changes` 与 `Reset to defaults/original ramp`，危险操作使用更清楚文案。
- 收益：避免用户意外清除已提交状态。
- 风险：中，依赖 Preview/Committed 模型。
- 涉及：`gui_main.cpp`、`calibration_controller.*`。

#### H7 — 无显示器热插拔与电源恢复处理（P3，可靠性优先于 Tray 新功能）

- 没有 `WM_DISPLAYCHANGE`、`WM_DEVICECHANGE`、`WM_POWERBROADCAST`。
- 当前只在用户手动 Refresh 时重枚举；sleep/wake、分辨率变化、Dock 重连后不会自动验证目标。
- 收益：多屏日常常驻更稳定。
- 风险：中。必须做事件合并，避免消息风暴和重复 Apply。
- 涉及：`gui_main.cpp`、`display_manager.*`、session state。

#### H8 — 缺少结构化日志（P0/P1）

- 关键 API 有错误字符串，但没有启动、显示器、配置、Apply、拓扑变化的持久日志。
- 建议轻量滚动文本日志，Release 仅 INFO/WARN/ERROR，避免引入框架。
- 收益：显示器/驱动问题可诊断。
- 风险：低；需避免记录敏感路径或高频 Slider 事件。
- 涉及：新增小型 `logger.*`，接入 controller/store/display lifecycle。

#### H9 — 普通顶层窗口缺少完整键盘导航调度（P1）

- 控件虽有 `WS_TABSTOP`，消息循环只有 `TranslateMessage/DispatchMessage`，没有 Dialog manager
  或等价 focus navigation；Escape cancel edit 也未实现。
- 收益：Tab/Shift+Tab/Enter/Escape 行为可靠，改善可访问性。
- 风险：低到中，需避免破坏自绘控件按键。
- 涉及：`gui_main.cpp`。

### Medium

#### M1 — 多处全窗重绘，且一次状态变化可重复 Invalidate（P2）

- `set_status()` 全窗 invalidation，`mark_changed()` 随后再次全窗 invalidation；Slider、numeric hover、
  Profile 刷新也常触发整个窗口。
- 建议先记录 paint 次数，再为 graph、footer、numeric、profile 建立局部 invalidation rect。
- 收益：减少 Wallpaper 和所有面板的重复绘制。
- 风险：中；透明父背景要求正确扩展脏区。
- 涉及：`gui_main.cpp`。

#### M2 — Wallpaper 解码已缓存，但缩放结果每次 repaint 重做（P2）

- WIC 只在启动解码一次，这是正确的；但 `BackgroundRenderer::draw()` 每次都 `StretchBlt/AlphaBlend`
  整张约 5.8 MB 图片。
- 自绘 Slider 为恢复透明父背景也会调用完整 Background draw。
- 建议按 client size + DPI 缓存合成后的背景 bitmap，仅在 resize/DPI/theme/wallpaper 变化时重建。
- 收益：拖动 Slider 时减少 GDI 缩放工作。
- 风险：中；只保留一份合成 cache，避免 4K 多副本。
- 涉及：`ui_rendering.*`、`gui_main.cpp`。

#### M3 — 每次 Preview 都从磁盘读取基础 Ramp（P2）

- `ensure_original_ramp()` 每次先调用 `load_base_ramp()`；80ms Preview 不写配置，但会重复小文件读取。
- 建议 Controller 在进程内记录已成功捕获的 stable display id。
- 收益：消除 Slider 拖动期间磁盘 IO。
- 风险：低；拓扑变化时必须清除缓存。
- 涉及：`calibration_controller.*`。

#### M4 — 绘制热路径频繁创建临时 GDI 对象（P2）

- 当前未发现明显未释放的常规 GDI 对象，但 brush、pen、1×1 bitmap、compatible DC、region
  在每次 paint 中频繁创建/销毁。
- 建议在证明有收益后缓存稳定 Theme 对象；先做 M1/M2，再决定是否值得封装。
- 收益：降低 GDI churn。
- 风险：中；缓存对象反而更容易产生生命周期错误。
- 涉及：`ui_rendering.*`、`gui_main.cpp`。

#### M5 — DPI 变化只重排控件，没有重建字体（P3）

- 字体在 `WM_CREATE` 按初始 DPI 创建；`WM_DPICHANGED` 只 SetWindowPos + layout。
- 在 100% 与 200% 显示器之间移动时，文字尺寸可能不随目标 DPI 正确更新。
- 收益：跨屏清晰且比例一致。
- 风险：中；重建字体前后必须正确替换并删除旧 HFONT。
- 涉及：`gui_main.cpp`。

#### M6 — Display enumeration 重复查询整个 topology（P2/P3）

- 每枚举一台 monitor，`friendly_name_for_device()` 都重新调用 QueryDisplayConfig。
- 建议一次 QueryDisplayConfig，建立 source name -> target metadata map。
- 收益：Refresh 与热插拔处理更轻，且便于生成 stable id。
- 风险：低到中。
- 涉及：`display_manager.*`。

#### M7 — GUI 的 Settings Single Source of Truth 尚未完成（P1）

- `CalibrationSettings` 已存在，但 GUI 的实际状态仍由六个 Slider 反向读取；Profile、dirty、graph、
  controller 通过多个 helper 间接同步。
- 建议渐进引入一个 `currentSettings`，控件只是 view，不重写整个 Win32 UI。
- 收益：减少不同步和重复读取。
- 风险：中。
- 涉及：`gui_main.cpp`，可能新增 `calibration_session.*`。

#### M8 — Profile 模型与 UI 能力不一致（P1）

- Core 已有 vector Profile 和版本头，但 UI 仍固定四个 legacy slot，不支持 Rename/Duplicate，且同步写两种格式。
- 建议先稳定状态/持久化，再逐步让 UI 使用 Profile ID；保留 v1 migration。
- 收益：真正的 Profile 管理和每屏 preferred profile。
- 风险：中到高，不应进入第一批。
- 涉及：`profile_manager.*`、`profile_store.*`、`gui_main.cpp`。

#### M9 — Tray 生命周期不够健壮（P3）

- Shell_NotifyIcon 返回值未检查，没有 `NIM_SETVERSION`，Explorer 重启后不恢复图标；Close 默认隐藏到
  Tray，但没有用户可见的行为设置。
- 收益：常驻行为更可信。
- 风险：低到中。
- 涉及：`gui_main.cpp`。

#### M10 — HDR 状态未检测（P3）

- 应用不能保证 SDR Gamma Ramp 在 HDR 管线中的实际效果，但 UI 当前没有提示。
- 建议只检测并提示，不在本阶段重写 Color Management。
- 收益：避免功能有效性的误导。
- 风险：低到中。
- 涉及：`display_manager.*`、`gui_main.cpp`。

#### M11 — 启动前同步解码 5.8 MB Wallpaper（P2）

- 图片在 `WM_CREATE`、窗口 Show 前同步解码。当前没有实际启动基准，不能直接认定为严重瓶颈。
- 建议先加 Debug-only timing；若窗口出现时间已足够快，不引入线程。
- 收益：用数据决定是否优化。
- 风险：低。
- 涉及：`gui_main.cpp`、`ui_rendering.*`。

#### M12 — 缺少核心单元测试与 CTest（P0/P1）

- 目前只有 Profile migration check，可运行但不做断言；没有 LUT、范围、序列化、截断 Ramp、dirty
  comparison 测试。
- 收益：后续修可靠性时防止回归。
- 风险：低。
- 涉及：`CMakeLists.txt`、新增小型 tests；不测试复杂 Win32 绘制。

### Low

#### L1 — Theme 仍有少量散落常量（P2）

- Wallpaper overlay 数值与 RGB curve 色仍在 `gui_main.cpp`；大部分 Theme 已集中。
- 收益：维护一致性。
- 风险：低。

#### L2 — DWM、控件创建、Tray 等部分 Win32 返回值未检查（P0/P3）

- DWM 失败可以优雅降级，严重度低；控件创建和 Tray 注册失败则应至少记录日志。
- 收益：更好诊断，不必向普通用户显示原始错误码。
- 风险：低。

#### L3 — `app.rc/app.manifest` 与实际 GUI target 集成不清晰（P3）

- GUI target 当前列入 `wallpaper.rc`，而不是 `app.rc`；运行时 DPI API 提供了保护，但构建资产容易产生
  重复或漏嵌入 manifest 的维护问题。
- 收益：打包一致、减少曾出现过的重复 manifest 错误。
- 风险：低，需先检查最终 EXE manifest。

#### L4 — 版本、图标和 build artifact 管理未统一（P3）

- 窗口标题仍为 `Gamma Changer C++`，使用系统默认图标，没有单一版本源；多个 build 目录位于源码树。
- 收益：Release 更整洁。
- 风险：低。

#### L5 — `/MD` 依赖 VC Runtime（P3）

- Release 优化正确，但分发机器需要兼容的 Visual C++ Runtime；应在发行说明或 installer 中明确，
  或评估 `/MT` 的体积/更新权衡。
- 收益：减少“开发机能运行、用户机缺 DLL”。
- 风险：低到中。

## 3. Reliability semantics recommendation

建议建立但不做大型框架：

```text
savedProfileSettings
        |
        v
committedDisplaySettings  <--- explicit Save/Apply updates this
        |
        v
previewSettings           <--- slider edits update this
```

- Graph 总是读取 `previewSettings`；
- Live Preview ON 时，80ms debounce 后把 preview 临时写到目标显示器；
- Save Profile 成功后再更新 committed 与 saved profile；
- 切换显示器/Profile、关闭或 Tray Exit 时，对 dirty preview 给出轻量明确策略；
- 正常退出可恢复 committed ramp；Task Manager 强制终止无法依赖进程清理，应明确这是 Windows Gamma
  API 的限制。若未来必须覆盖强杀场景，应评估独立 watchdog，而不是假装 `WM_DESTROY` 能处理。

## 4. Recommended order

### Batch 1 — Safety boundary（最高收益、最低风险）

1. 严格检查 Ramp 文件固定长度；损坏时禁止 Reset。
2. 为 Ramp、per-display params、legacy presets 统一原子写入。
3. 建立集中参数范围并为 GUI/CLI/Profile 共用验证。
4. 让 save/delete/apply 的错误可传播，禁止失败后显示 success。
5. 增加最小核心测试：范围、NaN/inf、Ramp 截断、原子保存失败、Profile round-trip。

预计文件：`gamma_lut.*`、`gamma_types.h`、`profile_store.*`、`gui_main.cpp`、`main.cpp`、
`CMakeLists.txt` 和少量 test 文件。不会改 UI 布局。

### Batch 2 — Preview / committed session model

1. 引入 `CalibrationSession` 或等价小状态对象。
2. 修复显示器/Profile 切换时的隐式 commit。
3. 明确 Reset changes 与 Reset to defaults/original。
4. 使用 settings comparison 驱动 dirty 和 Save enabled。

### Batch 3 — Display identity and lifecycle

1. 一次性查询 DisplayConfig，并形成 stable identifier。
2. 兼容迁移 `DISPLAY1.profile/.ramp`。
3. 接入合并后的 `WM_DISPLAYCHANGE` / device change。
4. 再处理 sleep/resume 和必要的 committed profile re-apply。

### Batch 4 — Measured performance

1. 加 Debug-only paint/apply/startup timing。
2. 局部 invalidation，移除重复全窗 invalidation。
3. 缓存窗口尺寸对应的合成背景。
4. 在 Controller 内缓存“本进程已捕获 base ramp”的状态。
5. 只有数据仍显示 GDI churn 时才缓存 brush/pen。

### Batch 5 — UX and Windows integration

1. Numeric Enter/Escape/Arrow/Tab 完整语义。
2. Before/After press-and-hold。
3. Profile Rename/Duplicate 与 preferred profile per display。
4. HDR 提示、Tray profile menu、Startup（全部 opt-in）。

## 5. Verification performed

- Release GUI 已在当前源码状态构建通过。
- Release CLI 构建通过。
- Profile migration check 构建并在隔离临时目录运行通过，生成 1 个 builtin profile 与 4 个
  legacy compatibility profile。
- CLI 只读 display enumeration 成功，返回两台显示器：`S65`、`25G3Z`。
- Release 编译命令确认启用 `/O2 /W4 /GS /NDEBUG`，目标为 x64。
- 本轮未调用 `SetDeviceGammaRamp`，未改变任何显示器 Gamma；未修改产品源码。

## 6. Go / No-Go recommendation

可以继续在现有 C++/Win32 架构上迭代，不建议更换 UI framework，也不建议为轻量 Gamma 工具加入
线程池、数据库、WebView 或大型依赖。

下一步建议执行 **Batch 1 — Safety boundary**。它不会改变现有 UI 外观，能先把最危险的文件损坏、
非法参数和错误状态问题消除，为后续 Preview/Committed 重构提供安全基线。

## 7. Follow-up remediation status（2026-08-16）

后续迭代已修复本报告中的主要问题，并补充回归测试：

- 未配置显示器不再被写入默认 LUT（reapply 前检查已保存配置）。
- CLI reset 支持 legacy device-name base-ramp 回退与迁移。
- profiles.v1 保存拒绝空集合与重复 ID；presets.profile 采用严格逐行解析。
- 显示器刷新失败会中止流程；undo 绑定显示器身份；Reset 不再改写 profile 定义。
- ProfileStore 写操作增加跨进程命名互斥；解析文件增加规模上限。
- 日志轮转失败会截断，跨进程日志写入加锁。
- 修复 rename 编辑框隐藏创建导致的同级 Z 序遮挡问题。
- 清理死代码与死枚举，补齐 SAL/空指针/整数溢出防护与关键 Win32 返回值检查。
- Release/Debug 构建零 `/W4` 警告，CTest 3/3 通过，`/analyze` 仅剩测试程序栈大小提示。

第二轮修复（同日）：

- 刷新流程先恢复 preview、再保存 pending；恢复失败即中止，避免“UI 已提交、屏幕仍预览”。
- profiles.v1 损坏时回退为只读展示 legacy presets，不再显示空 profile 列表。
- 长显示器 ID 文件名改用稳定的 FNV-1a 哈希，替代 `std::hash`。
- `save_presets` 写入前校验 occupied 槽参数，保持 save/load 契约一致。
- 显示器枚举对 `QueryDisplayConfig` 缓冲区竞争重试一次，并把 adapter 元数据改为一次枚举、O(1) 查询。
- CLI 拒绝未知选项，`apply` 要求至少一个调整项。
- `LutGenerator::generate` 对非法/非有限参数进行防御性 sanitize。
- `LOCALAPPDATA` 缺失时回退到 temp 目录且不再抛出；测试临时目录错误改为显式异常。
- 自绘 profile 文本缓冲扩大到 128 并省略号显示。
- 构建脚本以脚本所在目录为项目根目录；CI 开启 `/WX`。

第三轮修复（同日）：

- 统一 `local_app_data()` 返回基础目录，修复 fallback 分支 `GammaChangerCpp` 双重拼接。
- 日志 temp/cwd fallback 现在会创建 `GammaChangerCpp` 目录。
- profiles.v1 损坏时仍允许保存 per-display 校准（profile 集合保持只读）。
- 长名 FNV 文件增加旧 `std::hash` 名称回退读取，避免开发期数据孤儿化。
- `StoreWriteLock`/`LogWriteLock` 正确处理 `WaitForSingleObject` 返回值，避免失败时错误释放 mutex。
- `save_presets` 拒绝超过 64 字符的 profile 名。
- 多流 adapter 不再使用不确定的第一个物理显示器名作为 fallback。
- README 补充 CLI `apply` 至少需要一个调整项、未知选项会被拒绝。

第四轮（按产品决定）：

- 移除 CLI 目标与 `src/main.cpp`，不再生成 `gamma_changer.exe`。
- 使用桌面提供的 PNG 生成多尺寸 `assets/app.ico`（16/24/32/48/64/128/256），并嵌入 GUI 资源，窗口类与托盘图标改用应用图标。
- README、完成报告同步移除 CLI 描述。
- rename 改为“隐藏目标按钮、编辑框独占其矩形”的内联编辑方案，彻底消除自绘按钮/双缓冲父窗口造成的同级遮挡与渲染错位。
- rename 的 Enter/Escape/失焦现在同步提交，并在主消息循环中先于 `IsDialogMessage` 处理 Enter/Escape，确保用户按 Enter 或点击其他区域都会保存并退出编辑模式。
- rename 编辑框与 profile 按钮几何完全重合：同样尺寸、同样的 6px 圆角，左/右 14px 文本缩进与按钮文字对齐。
- Profile 列表升级为真正的动态集合：New 创建带默认参数的全新 profile，Delete 从 `profiles.v1` 中永久删除，不再受 4 槽限制；侧栏支持滚轮滚动，存储上限提高到 1024 个 profile。
- 修复删除 profile 后的 active 索引语义；移除 F2/双击重命名入口（仅保留右键菜单 Rename）；内置 Default 禁止重命名；滚轮命中范围限定为 profile 列表区域并绘制可视滚动条。
