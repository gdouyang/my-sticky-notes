# AGENTS.md - 架构规范与开发约定

本文档为参与 `my-sticky-notes` 项目的开发者及 AI Coding Agent 提供架构设计规范、模块职责划分与编码约定，确保在长期迭代演进中保持代码清晰度与易维护性。

---

## 1. 设计哲学与核心原则

- **纯原生零依赖（Zero Fat）**：保持 Win32 API + C++20 + GDI/GDI+ 极轻量原生架构，禁止引入体积庞大、依赖复杂的第三方 GUI 框架或 Webview 容器。
- **高内聚低耦合（Modular Separation）**：所有职责明确解耦，禁止将数据持久化、图形自绘、富文本解析或度量逻辑随意堆积在 `main.cpp`。
- **高 DPI 优先（Per-Monitor V2 First）**：所有坐标、内外边距、字号和图标必须通过 `ScaleDpi` 进行动态 DPI 换算，严禁硬编码物理像素。
- **无 Mock / Fallback 保证真实性**：业务功能必须基于真实 Windows 系统 API 完整实现并进行真实编译和运行时验证。

---

## 2. 目录结构与模块划分

```text
my-sticky-notes/
├── src/
│   ├── app.manifest         # 应用程序清单（声明 PerMonitorV2 高 DPI 与通用控件 v6）
│   ├── resource.rc          # Windows 资源脚本（内嵌清单及未来图标）
│   ├── autostart.h          # 开机自启管理模块（Windows 注册表 Run 键探测与切换）
│   ├── data_store.h/.cpp    # 数据存储层（NoteCardData/AppData 多便签模型、JSON 序列化与 RTF 磁盘分流持久化）
│   ├── ui_theme.h           # 主题色板系统（6套现代微拟物与 Fluent 配色方案）
│   ├── dpi_layout.h         # DPI 缩放与几何布局引擎（动态 DPI 换算、尺寸度量与 Header/Format 按钮区域计算）
│   ├── rich_text_utils.h    # 富文本排版与交互层（RTF 流、粗斜体下划线、项目符号、图片与荧光笔高亮）
│   ├── ui_renderer.h        # 视图渲染层（GDI 双缓冲自绘卡片、Header 按钮自绘、进度条、格式图标与荧光笔）
│   ├── tray.h/.cpp          # 系统托盘管理（自绘托盘图标、托盘消息、右键菜单包含透明度/开机自启/多便签联动）
│   └── main.cpp             # 主程序入口与消息泵（窗口生命周期、多卡片 NoteWindowContext、待办内联编辑、超链接跳转、贴边收起）
├── build.bat                # 自动化编译构建脚本（windres + g++ 链接）
├── AGENTS.md                # 架构约定与 Agent 开发规范（本文件）
└── README.md                # 项目简介与使用文档
```

---

## 3. 核心模块详细职责与交互约定

### 3.1 DPI 缩放与度量模块 (`dpi_layout.h`)
- **职责**：
  - 动态获取当前窗口所在屏幕的实时 DPI（[`GetWindowDpi`](file:///D:/dev/git_rep1/my-sticky-notes/src/dpi_layout.h)）。
  - 基于基准 96 DPI 动态等比换算尺寸（[`ScaleDpi`](file:///D:/dev/git_rep1/my-sticky-notes/src/dpi_layout.h)）。
  - 输出结构化度量对象 [`LayoutMetrics`](file:///D:/dev/git_rep1/my-sticky-notes/src/dpi_layout.h)，统筹 Header 高度、Tab 栏、进度条、待办列表项高、格式工具栏高与所有内外边距。
  - 提供统一的顶部按钮与底部格式栏按钮矩形命中计算（[`GetHeaderBtnRect`](file:///D:/dev/git_rep1/my-sticky-notes/src/dpi_layout.h)、[`GetFormatBtnRect`](file:///D:/dev/git_rep1/my-sticky-notes/src/dpi_layout.h)）。
- **约定**：任何新增的自绘元素尺寸，必须在此统一定义度量基准，不得在绘制逻辑中随意散落绝对数值。

### 3.2 富文本与排版交互模块 (`rich_text_utils.h`)
- **职责**：
  - 处理 RichEdit 4.1+ (`msftedit.dll`) 的 RTF 内存数据双向流转换（`EM_STREAMIN` / `EM_STREAMOUT`）。
  - 字符格式切换（粗体、斜体、下划线、删除线）。
  - **项目符号规范（方案 1）**：开启项目符号时必须调用 [`ToggleBullet`](file:///D:/dev/git_rep1/my-sticky-notes/src/rich_text_utils.h)，配置原生段落缩进：
    - 起始缩进：`dxStartIndent = 360` twips（前置预留约 2~3 个半角字符的左侧缩进）；
    - 悬挂偏移：`dxOffset = 240` twips（保持圆点与文字内容的优雅间距）；
    - 取消项目符号时清零恢复顶格。
  - 图片插入：通过 GDI+ 载入磁盘图片并通过剪贴板无损贴入 RichEdit 对象。
- **约定**：富文本相关的所有系统命令均在此模块扩展，保持纯净并向外暴露无副作用的操作接口。

### 3.3 视图渲染层 (`ui_renderer.h`)
- **职责**：
  - 双缓冲内存画布绘制入口 [`RenderView`](file:///D:/dev/git_rep1/my-sticky-notes/src/ui_renderer.h)。
  - 负责毛玻璃/拟物卡片圆角矩形渲染、待办清单进度条槽位填充、列表项勾选划线状态绘制。
  - 1:1 像素级复刻微软便签格式图标矢量绘制（粗体 B、衬线斜体 I、底线 U、中划线 ab、三点三线列表符、相框山峰图像图标）。
- **约定**：渲染函数仅依赖纯状态输入（只读 `const AppData&`、鼠标 Hover 索引、DPI 度量与字体句柄），严禁在渲染流程中直接修改业务状态或直接调用磁盘保存。

### 3.4 数据模型与持久化 (`data_store.h` / `data_store.cpp`)
- **职责**：
  - 定义内存核心状态结构 `AppData`（窗口坐标、宽高、置顶状态、主题色编号、当前 Tab 索引、RTF 文本流、待办项数组）。
  - **双文件分流持久化架构**（位于 `%APPDATA%\MyStickyNotes\`）：
    - `notes_data.json`：仅保存轻量级窗口坐标尺寸、置顶状态、主题编号、当前 Tab 及待办项数组（JSON 保持小巧干净，避免体积膨胀）。
    - `memo_notes.rtf`：独立二进制存储富文本速记便签内容，免除 JSON 字符转义开销，提升 IO 效率且可独立被外部编辑器查看。
  - **平滑兼容机制**：`Load` 优先读取 `memo_notes.rtf`，如不存在则自动回退提取历史 `notes_data.json` 中的 `memoRtf` 并于保存时自动分流迁移。
- **约定**：主窗口通过防抖定时器（`ID_TIMER_SAVE`，300ms 延时）触发落盘，避免高频按键或窗口拖动造成的性能开销。

### 3.5 托盘管理模块 (`tray.h` / `tray.cpp`)
- **职责**：
  - 基于当前主题颜色动态生成 16x16 / 32x32 现代圆角便笺托盘图标。
  - 管理 Windows 托盘图标添加、动态换色更新与退出时安全移除。
  - 托盘右键上下文菜单事件处理。
- **约定**：托盘图标必须跟随便签当前所选主题色实时同步更新。

### 3.6 主程序入口与消息路由 (`main.cpp`)
- **职责**：
  - 进程级 Per-Monitor V2 DPI 上下文初始化。
  - 单例互斥体管理（避免多开并自动唤醒已有实例）。
  - 创建并维护三个核心窗口：所有者窗口 `MyStickyNotes_Owner`（隐藏任务栏图标）、主窗口 `MyStickyNotes_Main`（POPUP 拟态卡片窗口）、悬浮提示层 `StickyNotes_Tooltip`（无阻断穿透浮层）。
  - **置顶贴边自动收起引擎（Edge Dock & Auto-Hide）**：
    - 当窗口处于置顶（Pin）状态下被拖动靠紧屏幕左、右或顶部工作区边缘（<= 20px 容差）时，自动吸附；
    - 当鼠标离开窗口且无编辑焦点时，300ms 防误触延时后自动收起至屏幕边缘（露出 5px 主题色感应细边）；
    - 鼠标划过或点击露出边缘时，通过 60fps 平滑插值动画自动展开展示；
    - 拖离边缘或取消置顶时自动解除吸附还原。
  - 消息循环分发：响应 `WM_CREATE`、`WM_DPICHANGED`、`WM_SIZE`、`WM_MOVE`、`WM_ENTERSIZEMOVE`、`WM_EXITSIZEMOVE`、`WM_MOUSEMOVE`（Hover 与 Tooltip 触发）、`WM_LBUTTONDOWN`（按钮点击与拖动）、`WM_NCHITTEST`（无边框卡片 8 向自由拉伸）。
- **约定**：保持 `main.cpp` 专注作为调度控制器，行数应控制在合理规模，禁止在此堆积大量绘制与业务算法细节。

---

## 4. 后续开发与扩展规范

1. **新增交互按钮**：
   - 在 `dpi_layout.h` 中分配几何尺寸并加入 `GetFormatBtnRect` 或 `GetHeaderBtnRect`；
   - 在 `ui_renderer.h` 中实现该按钮的矢量绘制；
   - 在 `main.cpp` 的 `FMT_BTN_TITLES` 中登记悬浮提示文本，并在 `WM_LBUTTONDOWN` 分支中调用处理接口。
2. **构建与发布验证**：
   - 每次代码修改后，务必运行 `.\build.bat` 确保编译零错误、零警告。
   - 保持 `.gitignore` 过滤 `*.o` 与临时文件，若更新了二进制产物，需确保其在 100%、125%、150%、175% 等各级 DPI 缩放下均可原生清晰运行。
