# IED²esigner

IEC 61850 IED 模型设计工具（Qt 5 / Qt 6 / C++）。

基于原 newfert IED 设计器（Qt 4.8.7）的 Qt5/Qt6 重构增强版：在原有"在线读取 + 点表导出 + 模型加载校验"能力之上，补齐了图形化建模的核心能力——DOI/DAI 值编辑、DO 管理、数据集/FCDA、五类控制块、通信段完整建模，并支持界面多语言。

> 同一份代码同时支持 Qt 5.15 与 Qt 6.6（Qt5 分支已完成 5.15.2 全量验证：MSVC2019 与 MinGW 双套件编译 + 全部自检模式 + GUI 实机验证）。

> 命名：IED² + esigner = IED²esigner（IED 平方设计器）

---

## 一、功能特性

### 模型编辑
- 树形编辑：IED → AccessPoint → Server → LDevice → LN0/LN → DO → DAI 全层级浏览与编辑
- IED 属性（名称/描述/厂家/类型/通信参数），修订历史(History)管理
- 多接入点（AccessPoint / ServerAt）、逻辑设备与逻辑节点的添加/删除/复制/粘贴
- DO 添加（CDC 模板选择 + 枚举自动绑定）/重命名/删除
- DAI 值编辑：Struct DA 自动展开（如 `mag.f`），按 bType 校验（布尔/整型/浮点/枚举/字符串）
- LD/LN 注释（XML comment）、命名规范与重名校验（IED/AP/LD/LN prefix+inst）

### 数据集与控制块
- **数据集(DataSet)/FCDA**：LD→LN→DO 勾选树生成成员，顺序即树序
- **报告控制块(ReportControl)**：BRCB/URCB、RptID、TrgOps 触发条件、OptFields 可选字段、RptEnabled、ConfRev、intgPd/bufTime
- **GOOSE 控制块(GSEControl)**、**采样值控制块(SampledValueControl)**、**日志控制块(LogControl)**、**定值控制块(SettingControl)**
- 添加 GOOSE/SV 控制块时自动创建对应通信地址（MAC/APPID/VLAN 顺序分配）

### 通信段建模
- 多子网(SubNetwork)、多接入点(ConnectedAP)，每接入点 9 种 P 参数（OSI-AP-Title/PSEL/SSEL/TSEL/IP/…）
- GSE / SMV 通信地址管理（MAC-Address、APPID、VLAN-ID、VLAN-PRIORITY、MinTime/MaxTime）

### 模板库
- 内置 13 类 CDC 公共数据类模板 + 自定义模板（JSON 持久化，支持搜索/增删改）
- 8 类内置 LNClass 逻辑节点模板（添加逻辑节点时选择）
- 枚举类型自动绑定：添加 DO 时按 CDC 模板自动挂接枚举类型

### 在线与校验（本工具独有）
- **在线 MMS 浏览读取 IED 模型**（libiec61850）
- **模型加载器校验**：导出前用 libprot61850 试载，统计 DI/AI/DO/AO/SG 点数
- **云行 JSON 导入**：与在线系统数据链路打通

### 界面
- 22 个自绘 SVG 线性图标、工具栏、全树按节点类型挂图标
- 按天滚动日志文件（exe 目录 `log/yyyy-MM-dd.log`）
- 最近打开文件（10 条）、XML 源码预览
- 界面国际化：源文英文，中文经 Qt 翻译包加载（见"界面翻译"）

---

## 二、环境依赖

| 组件 | 说明 |
|---|---|
| Qt 5.15.2 或 Qt 6.6.3 | 模块：widgets / xml / concurrent / svg（MSVC2019 64 位或 MinGW 81_64 套件） |
| libprot61850 | IEC 61850 模型加载器，位于 `../protocol/libprot61850`（相对仓库），**需带 sgRefs 的新版** |
| libiec61850 1.5.3 | 头文件位于 `../include/libiec61850`，链接库见下 |
| Windows SDK | MSVC 构建需要（10.0.19041.0 或 10.0.26100.0 均可） |

### 61850 链接库（两种套件分别构建，位于仓库同级目录）
- **MinGW**：`../libiec61850-mingw/src/libiec61850.a`（CMake + MinGW 从源码构建，hal 为 OBJECT 库已并入）
- **MSVC**：`../libiec61850-msvc/{Debug,Release}/iec61850.lib hal.lib`（CMake + VS2022 构建，Debug/Release 双配置）

> 参考：本机源码副本位于 `C:\Users\sungh\Desktop\libiec61850-1.5.3`。注意其 CMakeLists 原本强制 VS2013 且仅 Debug 配置，本地构建需先放开这两处限制。

### .pro 关键配置
```pro
QT += core gui widgets xml concurrent svg
RC_ICONS = src/resources/app.ico          # exe 图标
TRANSLATIONS += translate/ieddesigner_zh_CN.ts
win32-g++ { LIBPATH += $$PWD/../libiec61850-mingw/src; LIBS += -liec61850 -lws2_32 }
else:win32 { CONFIG(debug, debug|release) 分目录链接 MSVC 库 }
win32-msvc: QMAKE_CXXFLAGS += /utf-8      # 源码统一 UTF-8
win32 { Debug/Release 的 obj/moc/ui/rcc 中间目录分开 }  # 两套配置可共存互不覆盖
```

### Qt5 构建（MSVC2019 64 位示例）
```bat
set PATH=D:\programming\Qt\5.15.2\5.15.2\msvc2019_64\bin;<MSVC+SDK 环境>;%PATH%
qmake ieddesigner.pro -spec win32-msvc
nmake /f Makefile.Release
```
> 注意：中文 Windows 上用 **Qt6** 的 qmake 时，若 cl 输出为中文会导致 qmake 解析默认
> include 路径失败，需先 `set VSLANG=1033`（Qt Creator 的 MSVC 套件默认会设置）。
> Qt5 的 qmake 无此问题。

---

## 三、构建与运行

### Qt Creator（推荐）
1. 打开 `ieddesigner.pro`，选择套件（MinGW 或 MSVC）
2. 修改 .pro 后先执行一次 **Run qmake**（构建菜单）
3. 构建运行

### 命令行（MinGW 示例）
```bat
call vcvars64.bat 或配置 MinGW 环境
qmake ieddesigner.pro
mingw32-make -f Makefile.Debug
```

### 命令行自检模式
| 参数 | 用途 |
|---|---|
| `--selftest <icd> <csv>` | 演示模型写出 + 加载器校验 + CSV 导出回归 |
| `--roundtrip <file>` | 打开→写出→回读 一致性校验 |
| `--jsonimport <json>` | 云行在线 JSON 导入回归 |
| `--skeleton <cid>` | 新建模型骨架（含数据集/控制块/GSE 地址/DAI）写盘→校验→回读比对 |
| `--opentest <file>` | 复刻 GUI 打开路径的异步加载与树构建自检 |
| `--autopen <file>` | 创建真实窗口并定时触发打开流程（配合 cdb 抓崩溃栈） |
| `--online <ip> <port> [ied]` | 在线读取 IED 模型 |

---

## 四、界面翻译

界面文案源文为**英文**，中文经 Qt 翻译包加载（源码不含中文字面量，CSV 点表表头除外——属下游导入契约）。

```
translate/
  ieddesigner_zh_CN.ts   # 翻译源（lupdate 生成，人工/脚本回填中文）
  ieddesigner_zh_CN.qm   # 发布翻译（lrelease 生成，构建后自动复制到 exe 目录 translate/）
```

更新翻译的流程：
```bat
lupdate ieddesigner.pro                                  REM 提取新增文案到 .ts
（编辑 translate/ieddesigner_zh_CN.ts 填写 <translation>）
lrelease translate\ieddesigner_zh_CN.ts                  REM 生成 .qm
```
运行时加载顺序：`exe目录/translate` → `exe目录/../translate` → 工作目录 `translate`。`.qm` 缺失时界面回退英文。

---

## 五、目录结构

```
ieddesigner/
├── ieddesigner.pro            # qmake 工程文件
├── src/
│   ├── main.cpp               # 入口：CLI 自检模式 + 翻译加载
│   ├── core/                  # 核心逻辑（与 UI 解耦，纯 C++）
│   │   ├── sclmodel.*         # SCL 数据模型（IED/LD/LN/DO/DAI/数据集/控制块/通信段）
│   │   ├── sclreader.*        # SCL/CID 解析（QDomDocument）
│   │   ├── sclwriter.*        # ICD/CID 生成
│   │   ├── scltemplates.*     # CDC/LNClass 模板表 + DAI 规格推导 + 枚举表
│   │   ├── jsonmodel.*        # 云行在线 JSON 导入
│   │   ├── onlineclient.*     # 在线 MMS 浏览（libiec61850，QThread）
│   │   ├── csvwriter.*        # GBK 点表 CSV 导出
│   │   ├── modelvalidate.*    # libprot61850 加载校验封装
│   │   └── browsemodel.*      # 在线浏览结果 → 模型
│   ├── ui/mainwindow.*        # 主窗口（树/属性页/菜单/对话框）
│   └── resources/             # 应用图标(app.ico/app_256.png) + 22 个 SVG 界面图标 + icons.qrc
├── translate/                 # 界面翻译（zh_CN）
├── docs/                      # 功能对比与规划文档
├── CIDs/                      # 测试用 SCL 样本
└── validate/                  # 校验相关头文件
```

---

## 六、第三期工作计划

> 以下任务按优先级排列，均为第二期之后的增量工作。1/2 在现有框架内可独立完成；3 需与 protocol 侧联动；4/5 为工具链增强。

### 任务 1：ExtRef 外部信号绑定 【核心，建议首先开工】
- **内容**：LN0 下添加/编辑/删除 `<Inputs><ExtRef iedName ldInst prefix lnClass lnInst doName daName/></Inputs>`，实现跨 IED 信号引用（GOOSE 订阅方声明）。
- **界面**：ExtRef 编辑对话框（目标 IED/接入点/LD/LN/DO 级联选择，复用数据集编辑器的勾选树），ExtRef 列表挂 LN0"外部引用"分组节点。
- **模型/读写**：`SclLn.extRefs` + reader/writer 对应支持（Outputs 元素按需）。
- **验收**：添加 ExtRef 后保存 CID，引用字段与手写 SCL 一致；回读零差异。
- **依赖**：无（第二期的树/属性页框架直接复用）。

### 任务 2：FCDA 成员微调与 DA 级 FCDA 【增强】
- **内容**：数据集编辑器内 FCDA 条目的上移/下移/插删（当前只能按树序整体重建）；FCDA 支持到 DA 级（`daName` 勾选，DO 下的 DAI 勾选树）。
- **验收**：调整顺序后保存的 CID 中 FCDA 顺序与编辑器一致；DA 级 FCDA 被 libiec61850 静态模型正确解析。

### 任务 3：模型加载器升级联动 【需 protocol 侧配合】
- **内容**：libprot61850 加载器当前只解析到 LN/DO 点表，需升级以识别数据集/控制块（用于导出前的语义校验），并支持 SG 引用（`sgRefs`）完整落地。
- **说明**：加载器源码在 `../protocol/libprot61850`（protocol 侧仓库），工具侧已预留：`--skeleton` 自检可随时验证升级后的加载器对本工具输出的兼容性。
- **验收**：带数据集/控制块的模型通过加载器校验且点表统计正确。

### 任务 4：XSD 校验（IEC 61850-6 schema）【工具链增强】
- **内容**：保存/导出时用官方 SCL.xsd 套件校验生成文件，错误列表窗口展示。
- **方案选型**：yx-ied-designer 依赖 JRE 8+（xsd-schema-validator）；本工具建议用 **libxml2（可静态链接，带 XML Schema 校验）** 避免引入 Java 运行时依赖。
- **验收**：故意构造非法 SCL（缺必填属性/枚举越界）能被检出并列出行号。

### 任务 5：sAddr/ix 信号地址分配 【按需】
- **内容**：DAI 级短地址（sAddr）与索引（ix）的分配/回收/CSV 批量导入导出。
- **说明**：yx 采用五段式"云行 ID"与其系统耦合；本工具建议按自家点表 `iInternalIndex` 体系设计等价物，**不建议照抄其格式**。
- **验收**：分配的地址在 DataTypeTemplates 中按选项保留/清除（对齐 yx 的 keepSAddr 语义）。

### 任务 6：工程易用性 【顺手项】
- 修订历史一键生成（保存时自动追加 Hitem）
- 多语言切换菜单（重启生效提示）
- 主窗口布局记忆（QSettings 保存 dock 位置/大小）

---

## 七、已知限制

- CSV 点表表头为 GBK 中文固定格式（下游导入契约），不随界面语言切换。
- libprot61850 加载器（protocol 侧）尚未支持数据集/控制块的语义校验，导出校验目前依赖点表统计 + 第三方工具。
- 本机 61850 库由源码重建（`../libiec61850-mingw`、`../libiec61850-msvc`），若 protocol 侧库有更新需同步重建。
