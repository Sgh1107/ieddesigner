# IED²esigner

IEC 61850 IED 模型设计工具（Qt 5 / Qt 6 / C++）：树形建模 + 数据集与控制块 + 通信段 + 在线读取 + XSD 结构校验 + 点表导出。

基于原 newfert IED 设计器（Qt 4.8.7）的重构增强版：在原有"在线读取 + 点表导出 + 模型加载校验"能力之上，补齐了图形化建模的核心能力——DOI/DAI 值编辑、DO 管理、数据集/FCDA、五类控制块、通信段完整建模，并支持界面多语言。

> 命名：IED² + esigner = IED²esigner（IED 平方设计器）

> 📖 面向使用者的图文操作说明见 **[doc/Usage.md](doc/Usage.md)**；本 README 侧重功能范围、环境依赖、构建运行与版本演进。

---

## 一、分支与版本

| 分支 | Qt 版本 | 定位 | 源码编码 |
|---|---|---|---|
| `main` | Qt 5.15.2 | **稳定线**（与 `Qt5` 同源同内容） | UTF-8 |
| `Qt5` | Qt 5.15.2 | 稳定线开发分支，验证后合入 `main` | UTF-8 |
| `Qt6` | Qt 6.6.3 | 新功能主线（二期/三期能力在此开发） | UTF-8 |
| `Qt4` | Qt 4.8.7 x64 | 老环境兼容分支（C++03，无 Qt5/6 专有 API） | GBK |

- `Qt5` 与 `main` 内容一致（`main` 只做快进合并），本文档同时适用于两者。
- 二期/三期功能均已在 `Qt6` 完成，并已回移到 `Qt5`/`main`（2026-10-20）与 `Qt4`；四个分支**功能对齐**，差异仅在 API 与构建配置。
- 移植约定：Qt5 与 Qt6 的源代码保持**逐字节一致**，仅 `ieddesigner.pro`、`.gitignore`、`README.md` 随分支不同；Qt4 因语言/编码限制单独适配。

---

## 二、功能特性

### 模型编辑
- 树形编辑：IED → AccessPoint → Server → LDevice → LN0/LN → DO → DAI 全层级浏览与编辑
- IED 属性（名称/描述/厂家/类型/通信参数），修订历史(History)管理（手动增删 + 保存/导出时自动追加或更新当天条目）
- Server 服务声明：`timeout`、`Authentication`、`<Services>` 能力项（按 Ed1 schema 输出）
- 多接入点（AccessPoint / ServerAt）、逻辑设备与逻辑节点的添加/删除/复制/粘贴
- DO 添加（CDC 模板选择 + 枚举自动绑定）/重命名/删除
- DAI 值编辑：Struct DA 自动展开（如 `mag.f`），按 bType 校验（布尔/整型/浮点/枚举/字符串）
- LD/LN 注释（XML comment）、命名规范与重名校验（IED/AP/LD/LN prefix+inst）
- 获取引用（Get references）：右键任意节点显示其 IEC 61850 引用路径与 MMS 引用路径，可一键复制

### 数据集与控制块
- **数据集(DataSet)/FCDA**：LD→LN→DO 勾选树生成成员，顺序即树序；支持 DO 级与 **DA 级** FCDA（`daName` 点分路径、`fc` 取 DAI 精确 FC），勾选为增量追加/移除，成员可上移/下移/删除
- **报告控制块(ReportControl)**：BRCB/URCB、RptID、TrgOps 触发条件、OptFields 可选字段、RptEnabled、ConfRev、intgPd/bufTime
- **GOOSE 控制块(GSEControl)**、**采样值控制块(SampledValueControl)**、**日志控制块(LogControl)**、**定值控制块(SettingControl)**
- 添加 GOOSE/SV 控制块时自动创建对应通信地址（MAC/APPID/VLAN 顺序分配）

### 外部引用与通信段
- **ExtRef 外部信号绑定**：LN0/LN 下添加/编辑/删除 `<Inputs><ExtRef .../>`（iedName/ldInst/prefix/lnClass/lnInst/doName/daName/desc/intAddr），左侧模型树点选 DO/DAI 自动填充，`iedName` 可手填实现跨装置引用；LN 下生成"外部引用"分组节点，属性页支持逐字段编辑与校验
- 多子网(SubNetwork)、多接入点(ConnectedAP)，每接入点 9 种 P 参数（OSI-AP-Title / OSI-AE-Qualifier / OSI-PSEL / OSI-SSEL / OSI-TSEL、IP / IP-SUBNET / IP-GATEWAY / MMS-Port）
- GSE / SMV 通信地址管理（MAC-Address、APPID、VLAN-ID、VLAN-PRIORITY、MinTime/MaxTime）

### 模板库
- 内置 13 类 CDC 公共数据类模板（SPS/DPS/MV/SPC/DPC/APC/ASG/SPG/ING/ENC/ENS/LPL/DPL）+ 自定义模板（JSON 持久化，支持搜索/增删改）
- 8 类内置 LNClass 逻辑节点模板（LLN0/LPHD/MMXU/CSWI/XCBR/XSWI/GGIO/SGCB）
- 枚举类型自动绑定：添加 DO 时按 CDC 模板自动挂接枚举类型

### 在线与校验（本工具独有）
- **在线 MMS 浏览读取 IED 模型**（libiec61850）
- **模型加载器校验**：导出前用 libprot61850 试载，统计 DI/AI/DO/AO/SG 点数；并做数据集/控制块引用一致性校验（控制块 `datSet` 指向本 LN 存在的 DataSet、`FCDA` 可解析到本 IED 的 LN/DO/DA、通信段 `GSE`/`SMV` 地址可对应到控制块），违规以 `Semantic warning` 记入日志
- **XSD 结构校验**：用 IEC 61850-6 官方 schema（SCL2003 v1.7）校验生成的 ICD/CID，错误列表含行号；XML 预览内可手动触发，CLI 提供 `--xsd`
- **云行 JSON 导入**：与在线系统数据链路打通

### 界面
- 23 个自绘 SVG 线性图标、工具栏、全树按节点类型挂图标
- 独立主题样式 `src/ui/theme.qss`（运行时加载，改样式免重编译，缺失时回退 qrc 内置）
- 树内子结构展开：数据集/FCDA 成员、报告控制块子结构、通信接入点 GSE/SMV 地址、LN 分类分组标签、ExtRef 外部引用
- 按天滚动日志文件（exe 目录 `log/yyyy-MM-dd.log`）
- 最近打开文件（10 条）、XML 源码预览
- 界面国际化：源文英文，中文经 Qt 翻译包加载（见"界面翻译"）；工具菜单 → Language 可切换
- 布局记忆：关闭时保存窗口几何与停靠窗格状态（QSettings），启动恢复

---

## 三、环境依赖

| 组件 | 说明 |
|---|---|
| Qt | 稳定线 **Qt 5.15.2**、主线 **Qt 6.6.3**；模块：widgets / xml / concurrent / svg。已验证套件：MSVC2019 64 位（VS2019 14.29 + Windows SDK 10.0.19041）、MinGW 8.1 64 位 |
| libprot61850 | IEC 61850 模型加载器，源码位于 `../protocol/libprot61850`（随本工程一起编译）；点表类型 `SMMSPointTable` 含 `sgRefs` 字段，加载器已实现 `sgRefs` 填充与数据集/控制块引用一致性校验 |
| libiec61850 1.5.3 | 头文件位于 `../include/libiec61850`，链接库见下 |
| libxml2 2.12.9 | XSD 校验（第三期任务 4）静态链接库；MinGW / MSVC-Release / MSVC-Debug 三套，见下 |
| Windows SDK | MSVC 构建需要（10.0.19041.0 或 10.0.26100.0 均可） |

### 链接库（与本工程同级目录）
- **MinGW**：`../libiec61850-mingw/src/libiec61850.a`（CMake + MinGW 从源码构建，hal 为 OBJECT 库已并入）、`../libxml2-2.12.9-install-mingw/lib/libxml2.a`
- **MSVC**：`../libiec61850-msvc/{Debug,Release}/iec61850.lib hal.lib`（CMake + VS 构建），`../libxml2-2.12.9-install-msvc{,-debug}/lib/libxml2s{d}.lib`

> 参考：本机 61850 源码副本位于 `C:\Users\sungh\Desktop\libiec61850-1.5.3`。其 CMakeLists 原本强制 VS2013 且仅 Debug 配置，本地构建需先放开这两处限制。

> 静态库的 CRT 必须与工程一致：Debug 用 `/MDd` + `libxml2sd.lib`，Release 用 `/MD` + `libxml2s.lib`，`.pro` 已按配置分目录链接，混用会出现 LNK4098 等告警。

### .pro 关键配置
```pro
QT += core gui widgets xml concurrent svg
RESOURCES += src/resources/icons.qrc schemas/schemas.qrc src/ui/theme.qrc
RC_ICONS = src/resources/app.ico                 # exe 图标
TRANSLATIONS += translate/ieddesigner_zh_CN.ts   # .qm 一并打进 qrc，可被 exe 目录外部翻译覆盖
COPIES += theme                                  # theme.qss 同步到 exe 目录（bin/），运行时优先加载
INCLUDEPATH += src src/core src/ui validate $$PWD/../protocol/libprot61850 $$PWD/../include/libiec61850
win32-g++ { LIBS += -liec61850 -lws2_32 -lxml2 }
else:win32 { CONFIG(debug, debug|release) 分目录链接 iec61850 与 libxml2（Debug 用 libxml2sd）MSVC 库 }
win32-msvc: QMAKE_CXXFLAGS += /utf-8             # 源码统一 UTF-8
win32 { DESTDIR = bin；Debug/Release 的 obj/moc/ui/rcc 中间目录分开 }  # 两套配置可共存互不覆盖
```

### 编码约定
- **Qt5 / Qt6 分支**源码统一 **UTF-8**：MSVC 需显式 `/utf-8`（`.pro` 已加），MinGW g++ 默认按 UTF-8 处理。
- **Qt4 分支**源码为 **GBK**（Qt4 + 老编译器下窄字面量按 ANSI 解析），程序内部 `std::string` 仍统一存 UTF-8。
- 资源与文档（`.qss` / `.qrc` / `.ts` / `.md`）为 UTF-8 / ASCII。

---

## 四、构建与运行

### Qt Creator（推荐）
1. 打开 `ieddesigner.pro`，选择套件（MSVC 或 MinGW）
2. 修改 .pro 后先执行一次 **Run qmake**（构建菜单）
3. 构建运行

### 命令行（MSVC2019 64 位）
```bat
set PATH=D:\programming\Qt\5.15.2\5.15.2\msvc2019_64\bin;<MSVC+SDK 环境>;%PATH%
qmake ieddesigner.pro -spec win32-msvc
nmake /f Makefile.Release
```
> 注意：中文 Windows 上用 **Qt6** 的 qmake 时，若 cl 输出为中文会导致 qmake 解析默认
> include 路径失败，需先 `set VSLANG=1033`（Qt Creator 的 MSVC 套件默认会设置）。
> Qt5 的 qmake 无此问题。

### 命令行（MinGW 8.1 64 位示例）
```bat
set PATH=D:\programming\Qt\5.15.2\mingw81_64\bin;D:\programming\Qt\Tools\mingw810_64\bin;%PATH%
qmake ieddesigner.pro -spec win32-g++
mingw32-make -f Makefile.Release
```

### 构建产物与部署运行
- 产物统一在 `bin/`（`DESTDIR = bin`）：`ieddesigner.exe`、`theme.qss`（qmake 自动同步）、`log/`；首次执行 XSD 校验时会在 exe 目录释放 `schemas/SCL2003/`
- 运行依赖 Qt 动态库：把 `<Qt>/bin` 加入 PATH，或执行 `windeployqt bin\ieddesigner.exe` 部署
- **平台插件**：若 exe 目录存在 `qt.conf`（例如部署残留 `Plugins = .`），Qt 只在该目录下找 `platforms/`；缺少 `qwindows.dll` 会直接以 `0xC0000409` 退出（表现为"启动即崩"）。此时设置 `QT_PLUGIN_PATH=<Qt>/plugins`，或把 `plugins/platforms` 部署到 exe 目录
- 主题：`bin/theme.qss` 优先于 qrc 内置样式，改样式无需重编译（删除后自动回退内置）
- 翻译：`.qm` 已打包进 exe；可在 `bin/translate/`（或 exe 上级 `translate/`）放外部 `.qm` 覆盖
- 偏好：`QSettings("newfert","ieddesigner")` 保存窗口几何、停靠面板、语言与最近文件
- 日志：`bin/log/yyyy-MM-dd.log`，按天滚动

### 命令行自检模式
| 参数 | 用途 |
|---|---|
| `--xsd <file>` | 对 SCL 文件执行 XSD 校验（退出码 0=通过 / 1=流程错误 / 2=有违规） |
| `--selftest <icd> <csv>` | 演示模型写出 + 加载器校验（含 SG 定值点）+ CSV 导出回归 |
| `--roundtrip <file>` | 打开→写出→回读 一致性校验（含点表 SG 与数据集/控制块语义计数比对） |
| `--jsonimport <json>` | 云行在线 JSON 导入回归 |
| `--skeleton <cid>` | 新建模型骨架（含数据集/控制块/GSE 地址/DAI/SGCB 定值点）写盘→加载校验（含引用一致性）→回读比对 |
| `--opentest <file>` | 复刻 GUI 打开路径的异步加载与树构建自检 |
| `--autopen <file>` | 创建真实窗口并定时触发打开流程（配合 cdb 抓崩溃栈） |
| `--online <ip> <port> [ied]` | 在线读取 IED 模型 |

> 注意：`--selftest` 的第一个参数是**输出**路径（会覆盖同名文件），回归测试请指向临时目录。

---

## 五、界面翻译

界面文案源文为**英文**，中文经 Qt 翻译包加载（源码不含中文字面量，CSV 点表表头除外——属下游导入契约）。

```
translate/
  ieddesigner_zh_CN.ts   # 翻译源（lupdate 生成，人工/脚本回填中文）
  ieddesigner_zh_CN.qm   # 发布翻译（lrelease 生成，经 qrc 打包进 exe 资源，免拷贝）
```

更新翻译的流程：
```bat
lupdate ieddesigner.pro                                  REM 提取新增文案到 .ts
（编辑 translate/ieddesigner_zh_CN.ts 填写 <translation>）
lrelease translate\ieddesigner_zh_CN.ts                  REM 生成 .qm
```
`.qm` 必须用**与运行期同版本**的 `lrelease` 生成；Qt5 安装包若不含 `lrelease`
（部分在线安装的 MSVC 套件如此），可用同版本 MinGW 套件的 `lrelease.exe`，两者产出的 `.qm` 格式一致。

运行时加载顺序：`exe目录/translate` → `exe目录/../translate` → 工作目录 `translate` → qrc 内置（`:/i18n`，可被外部 `.qm` 覆盖）。全部缺失时界面回退英文。

---

## 六、目录结构

```
ieddesigner/
├── ieddesigner.pro            # qmake 工程文件
├── src/
│   ├── main.cpp               # 入口：CLI 自检模式 + 主题/翻译加载
│   ├── core/                  # 核心逻辑（与 UI 解耦，纯 C++）
│   │   ├── sclmodel.*         # SCL 数据模型（IED/LD/LN/DO/DAI/数据集/控制块/ExtRef/通信段）
│   │   ├── sclreader.*        # SCL/CID 解析（QDomDocument）
│   │   ├── sclwriter.*        # ICD/CID 生成
│   │   ├── scltemplates.*     # CDC/LNClass 模板表 + DAI 规格推导 + 枚举表
│   │   ├── jsonmodel.*        # 云行在线 JSON 导入
│   │   ├── onlineclient.*     # 在线 MMS 浏览（libiec61850，QThread）
│   │   ├── csvwriter.*        # GBK 点表 CSV 导出
│   │   ├── modelvalidate.*    # libprot61850 加载校验封装
│   │   ├── xsdvalidator.*     # IEC 61850-6 XSD 校验（libxml2）
│   │   └── browsemodel.*      # 在线浏览结果 → 模型
│   ├── ui/                    # 主窗口（树/属性页/菜单/对话框）+ theme.qss / theme.qrc
│   └── resources/             # 应用图标(app.ico/app_256.png) + 23 个 SVG 界面图标 + icons.qrc
├── schemas/                   # XSD 套件（SCL2003 v1.7）+ schemas.qrc
├── translate/                 # 界面翻译（zh_CN，.ts 源 + .qm 发布）
├── doc/                       # Usage.md 使用说明 + img/ 截图
├── CIDs/                      # 测试用 SCL 样本
├── validate/                  # 校验相关头文件
└── docs/                      # UI 原型与功能对比资料（本地文件，未纳入版本库）
```

---

## 七、版本演进与后续计划

### 里程碑
| 阶段 | 内容 |
|---|---|
| 一期 | 继承原始能力：在线 MMS 读取、点表 CSV 导出、模型加载器校验 |
| 二期 | 图形化建模：DOI/DAI 值编辑、DO 管理、数据集/FCDA、五类控制块、通信段、模板库、界面多语言、布局记忆 |
| 三期 | ExtRef 外部信号绑定、FCDA 成员微调与 DA 级 FCDA、加载器 `sgRefs` + 语义校验、XSD 结构校验、工程易用性 |

### 三期任务与验收
| 任务 | 状态 | 验收要点 |
|---|---|---|
| 1. ExtRef 外部信号绑定 | ✅ | `--skeleton` 注入 DA 级 + DO 级 ExtRef，回读字段零差异；`--roundtrip` 比对 extRefs 计数 |
| 2. FCDA 成员微调 / DA 级 FCDA | ✅ | 列表顺序即 CID 中 FCDA 顺序；DA 级 FCDA 经 `--roundtrip` 保留 |
| 3. 加载器 `sgRefs` 与语义校验 | ✅ | `--selftest` SG=3；`--skeleton` 语义计数 ds/report/gse/sv/log/setting/gseAddr=1、errors=0 |
| 4. XSD 校验 | ✅ | 骨架产物经 `--xsd` 无违规；人为破坏的模型报出带行号的违规 |
| 5. sAddr/ix 信号地址分配 | ⏸ 未开工 | 按需启动；建议按本项目点表 `iInternalIndex` 体系设计，不照抄 yx 的五段式"云行 ID" |
| 6. 工程易用性 | ✅ | 保存/导出自动维护 Hitem；工具菜单语言切换；关闭保存布局 |

#### XSD 校验实现说明
- **libxml2 2.12.9 静态链接**（MinGW / MSVC-Debug / MSVC-Release 三套），进程内 `xmlSchemaValidateDoc`；schema 套件（SCL2003 v1.7，来自 com-pas/compas-scl-xsd 的官方 IEC 61850-6 XSD）打包进 qrc，运行时释放到 exe 目录 `schemas/SCL2003/`。
- 校验反向暴露并修复了 writer 与 Ed1 schema 的多处差异：MinTime/MaxTime 补 unit/multiplier、Services 修正元素集合与顺序（去 ClientServices/nameLength）、FCDA 去除 iedName 与空 daName、ReportControl 的 confRev 改为必填、TrgOps/OptFields 按 Ed1 属性集裁剪（去 gi/bufOvfl/config）、SMV 补必备 SmvOpts 并去除 smpMod、SettingControl 改用 numOfSGs、LN0/LN 子元素顺序对齐 schema（DataSet→ReportControl→LogControl→DOI→Inputs→GSE/SMV/Setting）。

### 分支同步记录
- 2026-10-20：`Qt5` / `main` 同步 `Qt6` 二期/三期全部成果——源码逐字节一致，仅构建配置差异；Qt 5.15.2 下 MSVC2019 与 MinGW 8.1 双套件零错误零告警，自检与 GUI 实机验证通过。
- `Qt4`：同源成果已回移（GBK 源码 / C++03 / libxml2 静态链接，libxml2 缺失时自动降级为 stub）。

### 后续待办
- 任务 5（sAddr/ix 分配、回收与批量导入导出）按需启动。
- 双击就地编辑、DO 下 DA/BDA 层级化展示：受渲染期崩溃影响暂缓（见"已知问题"）。

---

## 八、已知限制与已知问题

### 已知限制
- CSV 点表表头为 GBK 中文固定格式（下游导入契约），不随界面语言切换。
- 加载器语义校验为**单 IED 内**的引用一致性校验：跨 IED 的 `FCDA`（`ldInst` 不在本 IED）跳过、不做跨装置 `ExtRef` 解析。
- `SG`（定值）点按 `FC ∈ {SG, SE, SP}` 归类；`SPC/DPC/APC` 控制类 CDC 不重复计入 `SG`。
- Ed2 专属属性（TrgOps 的 gi、OptFields 的 bufOvfl/config、SMV 的 smpMod）在 Ed1 schema 下不输出；非 LN0 节点上的 GSE/SV/定值控制块不符合 Ed1 schema（XSD 校验会提示）。
- 本机 61850 库与 libxml2 由源码重建（`../libiec61850-mingw`、`../libiec61850-msvc`、`../libxml2-2.12.9-*`），第三方库有更新时需同步重建。

### 已知问题
- **双击就地编辑暂禁用**：树"详细"列的双击就地编辑与展开渲染存在交互崩溃，编辑触发器暂未启用（`mainwindow.cpp` 中 `setEditTriggers` 被注释）；单击选中后在属性面板编辑不受影响。
- **DO 下 DA/BDA 层级化展示暂缓**：DAI 目前以点分路径单层展示（如 `mag.f`），更深的层级化树节点因渲染期堆损坏未启用。
