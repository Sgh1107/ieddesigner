# IED²esigner

IEC 61850 IED 模型设计工具（Qt 4.8.7 / C++，VS2013 x64）。

本分支是 **Qt4 分支**：把 Qt6 分支的第二期/第三期成果（图形化建模、数据集与控制块、模板库、XSD 校验、主题美化、界面多语言等）**回移到 Qt 4.8.7**，保持原有"在线读取 + 点表导出 + 模型加载校验"能力不变。

> 命名：IED² + esigner = IED²esigner（IED 平方设计器）
>
> 📖 面向使用者的完整图文操作说明见 **[doc/Usage.md](doc/Usage.md)**（截图为 Qt6 版本，界面与本分支一致）；本 README 侧重工程结构、构建与编码约定。

---

## 一、功能特性

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
- **模型加载器校验**：导出前用 libprot61850 试载，统计 DI/AI/DO/AO/SG 点数；并做数据集/控制块引用一致性校验（控制块 `datSet` 指向本 LN 存在的 DataSet、`FCDA` 可解析到本 IED 的 LN/DO/DA、通信段 `GSE`/`SMV` 地址可对应到控制块），违规以 `Semantic warning` 记入日志
- **XSD 结构校验**：用 IEC 61850-6 官方 schema（SCL2003 v1.7）校验生成的 ICD/CID，错误列表含行号；XML 预览内可手动触发，CLI 提供 `--xsd`（本分支需 libxml2，缺失时自动退化为"未编译"提示，见下）
- **云行 JSON 导入**：与在线系统数据链路打通

### 界面
- 23 个自绘 SVG 线性图标、工具栏、全树按节点类型挂图标
- 独立主题样式 `src/ui/theme.qss`（运行时优先加载 exe 旁的同名文件，改样式免重编译；缺失时回退 qrc 内置 `:/ui/theme.qss`）
- 树内子结构展开：数据集/FCDA 成员、报告控制块子结构、通信接入点 GSE/SMV 地址、LN 分类分组标签
- 按天滚动日志文件（exe 目录 `log/yyyy-MM-dd.log`）
- 最近打开文件（10 条）、XML 源码预览
- 界面国际化：源文英文，中文经 Qt 翻译包加载（见"界面翻译"）；工具菜单 → Language 可切换
- 布局记忆：关闭时保存窗口几何与停靠窗格状态（QSettings），启动恢复

---

## 二、环境依赖

| 组件 | 说明 |
|---|---|
| Qt 4.8.7（VS2013 x64） | 模块：`core gui xml svg`。**Qt4 没有 `widgets` 子模块**（控件都在 QtGui 内），**也没有 `concurrent` 子模块**（`QtConcurrent` 属于 QtCore），.pro 中不要加这两项 |
| libprot61850 | IEC 61850 模型加载器，源码位于 `../protocol/libprot61850`（随本工程一起编译）；点表类型 `SMMSPointTable` 含 `sgRefs` 字段，加载器已实现 `sgRefs` 填充与数据集/控制块引用一致性校验 |
| libiec61850 1.5.3 | 头文件位于 `../include/libiec61850`，链接库见下 |
| libxml2 2.12.9（可选） | XSD 校验（第三期任务 4）。需要**与 Qt4 工具链一致**构建的静态库；找不到时 .pro 自动改编译 `xsdvalidator_stub.cpp`（工程仍可构建，运行时给出"未编译"提示） |
| Windows SDK | 提供 `rc.exe`（编译 `src/resources/app.rc`，把 `app.ico` 嵌进 exe） |

### 61850 链接库
- 本分支沿用原有构建机路径（见 .pro 的 `win32:LIBPATH`，原为 `F:/WLMQ-Line2/NK8500_Wlmq/lib` 与 `F:/libiec61850-1.5.3/build_static_x64/hal/Release`）
- 若在本机（仓库同级有 `libiec61850-msvc/{Debug,Release}/iec61850.lib hal.lib`）构建，在 .pro 里加一行 `win32:LIBPATH += $$PWD/../libiec61850-msvc/Release` 即可；注意库必须与 Qt4 工具链（VS2013 / `/MD` 与 `/MDd`）一致，否则会出现 CRT 不匹配（LNK4098 等）

### .pro 关键配置（Qt4 版）
```pro
QT += core gui xml svg                     # 无 widgets / concurrent 子模块
RESOURCES += src/resources/icons.qrc schemas/schemas.qrc src/ui/theme.qrc
win32:RC_FILE = src/resources/app.rc       # exe 图标：Qt4.8 的 qmake 不支持 RC_ICONS，用 RC_FILE
TRANSLATIONS += translate/iedesigner_zh_CN.ts   # .qm 一并打进 qrc，可被 exe 目录外部翻译覆盖
QMAKE_POST_LINK += copy theme.qss 到 $$OUT_PWD/bin   # Qt4.8 没有 COPIES，用 post-link 复制
INCLUDEPATH += src src/core src/ui validate $$PWD/../protocol/libprot61850 $$PWD/../include/libiec61850
win32:LIBPATH += F:/WLMQ-Line2/NK8500_Wlmq/lib F:/libiec61850-1.5.3/build_static_x64/hal/Release
win32:LIBS += iec61850.lib hal.lib ws2_32.lib
# libxml2：exists() 自动探测；命中则 DEFINES += LIBXML_STATIC 并编译 xsdvalidator.cpp，否则编译 stub
```
> 注意：**不要**加 `win32-msvc:QMAKE_CXXFLAGS += /utf-8`——本分支源码是 GBK，加了会全乱码。

---

## 三、构建与运行

### 命令行（VS2013 x64）
```bat
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" amd64
qmake ieddesigner.pro
nmake -f Makefile.Release      REM 或 Makefile.Debug
```
产物：`bin\ieddesigner.exe`（同时会把 `src/ui/theme.qss` 复制到 `bin\`）。

### Qt Creator（推荐）
1. 打开 `ieddesigner.pro`，选择 Qt 4.8.7 VS2013 x64 套件
2. 修改 .pro 后先执行一次 **执行 qmake**（构建菜单）
3. 构建运行

> 首次构建若报 `libxml2s.lib`/`libxml2sd.lib` 找不到，说明 libxml2 路径与本机不符：改 .pro 里的 `LIBXML2_ROOT / LIBXML2_LIBDIR / LIBXML2_LIB`，或直接让工程走 stub（删掉这些变量所在的条件判断外的探测即可自动退化）。

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

---

## 四、源码编码约定（本分支重点）

| 类别 | 编码 | 说明 |
|---|---|---|
| 源码 `.cpp/.h/.pro/.rc` | **GBK（CP936）** | 与 VS2013 默认 ANSI 代码页一致，**不能**加 `/utf-8` |
| 资源 `.qrc/.ts/.xsd/.svg/.qss` | UTF-8 / ASCII | 由 qmake/rcc/lupdate/lrelease/Qt 自身解析 |
| 文档 `.md` | UTF-8 | |

运行时编码（`src/main.cpp` 的 `SetupCodecs()`）：`QTextCodec::setCodecForLocale / setCodecForTr / setCodecForCStrings` 全部设为 GBK，因此：

- 界面里 `tr("中文")` 的字面量是 GBK，`.ts` 源串是英文 → 翻译包按 GBK 解码源串再查表，正常；
- 核心层 `std::string` 内部**统一 UTF-8**（模型文本一律 `QString::fromUtf8 / toUtf8`）；
- 文件路径**走本地 8 位编码**（`fromLocal8Bit / toLocal8Bit`），Qt4 下 `argv`/文件系统均为 ANSI(GBK)；
- GBK 源码里的中文字面量若要拼进 UTF-8 的 `std::string`，经 `Utf8FromGbk()`（`QString::fromLocal8Bit(gbk).toUtf8()`）转换；
- GBK 无法表示的字符（如上标 `²`，U+00B2）不能直接写在字面量里，用 `QString::fromLatin1("IED\xB2" "esigner")` 之类构造。

---

## 五、界面翻译

界面文案源文为**英文**，中文经 Qt 翻译包加载（源码不含中文字面量，CSV 点表表头除外——属下游导入契约）。

```
translate/
  ieddesigner_zh_CN.ts   # 翻译源（lupdate 生成，人工/脚本回填中文）
  ieddesigner_zh_CN.qm   # 发布翻译（lrelease 生成，经 icons.qrc 打包进 exe 资源，免拷贝）
```

更新翻译的流程：
```bat
lupdate ieddesigner.pro                                  REM 提取新增文案到 .ts
（编辑 translate/ieddesigner_zh_CN.ts 填写 <translation>）
lrelease translate\ieddesigner_zh_CN.ts                  REM 生成 .qm
```
运行时加载顺序：`exe目录/translate` → `exe目录/../translate` → 工作目录 `translate` → qrc 内置（`:/i18n`，可被外部 `.qm` 覆盖）。全部缺失时界面回退英文。

---

## 六、目录结构

```
ieddesigner/
├── ieddesigner.pro            # qmake 工程文件（Qt4）
├── src/
│   ├── main.cpp               # 入口：CLI 自检模式 + 主题/翻译/编码设置
│   ├── core/                  # 核心逻辑（与 UI 解耦，纯 C++）
│   │   ├── sclmodel.*         # SCL 数据模型（IED/LD/LN/DO/DAI/数据集/控制块/通信段）
│   │   ├── sclreader.*        # SCL/CID 解析（QDomDocument）
│   │   ├── sclwriter.*        # ICD/CID 生成
│   │   ├── scltemplates.*     # CDC/LNClass 模板表 + DAI 规格推导 + 枚举表
│   │   ├── jsonmodel.*        # 云行在线 JSON 导入
│   │   ├── onlineclient.*     # 在线 MMS 浏览（libiec61850，QThread）
│   │   ├── csvwriter.*        # GBK 点表 CSV 导出
│   │   ├── modelvalidate.*    # libprot61850 加载校验封装
│   │   ├── xsdvalidator.*     # IEC 61850-6 XSD 校验（libxml2）
│   │   ├── xsdvalidator_stub.cpp  # 找不到 libxml2 时的退化实现（工程仍可编译）
│   │   └── browsemodel.*      # 在线浏览结果 → 模型
│   ├── ui/                    # 主窗口（树/属性页/菜单/对话框）+ theme.qss / theme.qrc
│   └── resources/             # 应用图标(app.ico/app_256.png + app.rc) + 23 个 SVG 界面图标 + icons.qrc
├── schemas/                   # XSD 套件（SCL2003 v1.7）+ schemas.qrc
├── translate/                 # 界面翻译（zh_CN，.ts 源 + .qm 发布）
├── doc/                       # Usage.md 使用说明 + img/ 截图
├── docs/                      # 功能对比与规划文档、UI 原型（ui-prototype-v1.html）
├── CIDs/                      # 测试用 SCL 样本
└── validate/                  # 校验相关头文件
```

---

## 七、与 Qt6 分支的差异（移植时的取舍）

| 项 | Qt6 分支 | 本分支（Qt4） |
|---|---|---|
| Qt 模块 | `core gui widgets xml concurrent svg` | `core gui xml svg`（Qt4 无 widgets/concurrent 子模块） |
| 源码编码 | UTF-8 + `QMAKE_CXXFLAGS += /utf-8` | GBK，**不加** `/utf-8`；另设 `setCodecForTr/setCodecForCStrings` |
| exe 图标 | `RC_ICONS = app.ico` | `RC_FILE = src/resources/app.rc`（Qt4.8 的 qmake 无 RC_ICONS） |
| 主题复制 | `COPIES += theme` | `QMAKE_POST_LINK += copy`（Qt4.8 的 qmake 无 COPIES；cmd 的 copy 不认正斜杠，用 `$$replace` 转反斜杠） |
| 异步加载 | C++17 lambda 捕获 | 自由函数 + `QtConcurrent::run` + `QFutureWatcher<T>`，`connect` 用旧式 `SIGNAL/SLOT`；原 lambda 体已提为成员槽 |
| JSON | QtJson（QJsonDocument） | 手写轻量 `JsonValue/JsonParser/JsonQuote`（仅用于自定义模板库读写） |
| XSD 校验 | 无条件 libxml2 | libxml2 自动探测，缺失时退化为 stub（可编译，运行时提示） |
| 语言标准 | C++17 | C++03（无 lambda/auto/nullptr/range-for/`std::to_string`/聚合花括号初始化等） |

---

## 八、第三期工作计划

> 以下任务按优先级排列，均为第二期之后的增量工作。1/2 在现有框架内可独立完成；3 需与 protocol 侧联动；4/5 为工具链增强。
>
> **进度（2026-10-19）**：任务 1、2、3、4、6 已完成并通过自检回归（任务 3 的**工具侧与 protocol 侧 loader 均已落地**）；任务 5 按需，暂未开工。**本分支已同步上述全部成果。**

### 任务 1：ExtRef 外部信号绑定 【核心，建议首先开工】✅ 已完成
- **内容**：LN0/LN 下添加/编辑/删除 `<Inputs><ExtRef iedName ldInst prefix lnClass lnInst doName daName/>`（另支持 desc/intAddr），实现跨 IED 信号引用（GOOSE 订阅方声明）。
- **界面**：ExtRef 编辑对话框（左侧当前模型浏览树点击 DO/DAI 自动填充，右侧 9 字段表单，iedName 可手填实现跨装置引用）；LN 下"外部引用"分组节点；LN0/LN 右键可直接添加；属性页支持逐字段编辑与校验。
- **模型/读写**：`SclLn.extRefs` + reader/writer 对应支持（空列表不输出 Inputs 段）。
- **验收**：✅ `--skeleton` 注入 DA 级 + DO 级两条 ExtRef，写盘→回读字段级零差异；`--roundtrip` 增加 extRefs 计数比对。

### 任务 2：FCDA 成员微调与 DA 级 FCDA 【增强】✅ 已完成
- **内容**：数据集编辑器改造——勾选树 DO 下挂 DA/DAI 子节点，勾 DO 生成 DO 级 FCDA、勾 DAI 生成 DA 级 FCDA（daName=点分路径，fc 取 DAI 精确 FC）；勾选/取消改为**增量**追加/移除（不再整体重建）；FCDA 列表支持**上移/下移/删除**。
- **验收**：✅ 列表顺序即保存的 CID 中 FCDA 顺序；DA 级 FCDA 由 `--roundtrip` 回读验证保留。

### 任务 3：模型加载器升级联动 【需 protocol 侧配合】✅ 已完成
- **工具侧**：`SMMSPointTable` 的 `sgRefs` 字段全链路消费——CSV 导出 `SG` 段（`csvwriter.cpp`）、在线 JSON（`jsonmodel.cpp`）、加载校验的 `sgQuirk` 判定（`modelvalidate.cpp`）、CLI 与界面计数（`main.cpp`、`mainwindow.cpp`）。
- **loader 侧**：`../protocol/libprot61850/iec61850_model_loader.*` 已升级：
  1. **`sgRefs` 实际填充**——`AppendSclDataRefsForDo` 将 `FC ∈ {SG, SE, SP}` 的 DA 归为定值点（控制类 CDC `SPC/DPC/APC` 除外，避免与 `AO` 的 `setMag` 重复计数）；Struct DA（如 `ASG.setMag`）展开成员叶子，标量 DA（如 `SPG/ING.setVal`）生成单点；`sgRefs` 已贯通 SCL/JSON 两条装载路径、`SMMSPointTable` 重载与 `ValidateRefs("SG")` 校验。
  2. **数据集/控制块引用一致性校验**（新增 `CollectSclSemantics`）：控制块 `datSet` 必须指向本 LN 内声明的 DataSet；`FCDA` 必须可解析到本 IED 的 LN/DO/DA；通信段 `<GSE>/<SMV>` 的 `ldInst/cbName` 必须对应到 `GSEControl/SampledValueControl`。结果经 `LoadResult.sgCount`/`LoadResult.semantics`（各类计数 + 逐条错误文本）返回，工具以 `Semantic check` 行与 `Semantic warning` 逐条记入日志。
- **说明**：加载器源码在 `../protocol/libprot61850`，随本工程一起编译（见 .pro 的 `SOURCES`）。
- **验收**：✅ `--selftest` 演示模型 `SG=3`；`--skeleton` 输出 `SG=3`、`ds=1 report=1 gse=1 sv=1 log=1 setting=1 gseAddr=1 errors=0`，且产物通过 `--xsd`（Ed1 schema）；`--roundtrip` 对人为破坏 `datSet`/`FCDA` 的文件报出 5 条 `Semantic warning`，对正常模型 0 错误且点表回环一致。

### 任务 4：XSD 校验（IEC 61850-6 schema）【工具链增强】✅ 已完成
- **内容**：保存/导出 CID/ICD 时用官方 SCL schema 校验生成文件，错误列表窗口展示（行号+信息）；XML 源码预览内可直接触发；CLI 提供 `--xsd <file>` 自检（退出码 0=通过/1=流程错误/2=有违规）。
- **实现**：**libxml2 2.12.9 静态链接**，进程内 `xmlSchemaValidateDoc`，schema 套件（SCL2003 v1.7，来自 com-pas/compas-scl-xsd 的官方 IEC 61850-6 XSD）打包进 qrc，运行时释放到 exe 目录 `schemas/SCL2003/`。
  - 为什么不用 Qt4 自带的 QtXmlPatterns：SCL2003 schema 大量使用 `xs:key / xs:unique / xs:keyref` 身份约束（本套 schema 共 148 处），QtXmlPatterns 不支持这些约束，校验结果不可靠。
- **附带产出**：XSD 校验反向暴露并修复了 writer 与 Ed1 schema 的多处差异——MinTime/MaxTime 补 unit/multiplier、Services 修正元素集合与顺序（去 ClientServices/nameLength）、FCDA 去除 iedName 与空 daName、ReportControl 的 confRev 改为必填属性、TrgOps/OptFields 按 Ed1 属性集裁剪（去 gi/bufOvfl/config）、SMV 补必备 SmvOpts 并去除 smpMod、SettingControl 改用 numOfSGs、LN0/LN 子元素顺序对齐 schema（DataSet→ReportControl→LogControl→DOI→Inputs→GSE/SMV/Setting）。
- **已知限制**：TrgOps 的 gi、OptFields 的 bufOvfl/config、SMV 的 smpMod 为 Ed2 特性，Ed1 schema 不允许，暂不输出；非 LN0 节点上的 GSE/SV/定值控制块不符合 Ed1 schema（校验会提示）。

### 任务 5：sAddr/ix 信号地址分配 【按需】⏸ 未开工
- **内容**：DAI 级短地址（sAddr）与索引（ix）的分配/回收/CSV 批量导入导出。
- **说明**：yx 采用五段式"云行 ID"与其系统耦合；本工具建议按自家点表 `iInternalIndex` 体系设计等价物，**不建议照抄其格式**。
- **验收**：分配的地址在 DataTypeTemplates 中按选项保留/清除（对齐 yx 的 keepSAddr 语义）。

### 任务 6：工程易用性 【顺手项】✅ 已完成
- ~~修订历史一键生成~~：保存/导出 CID/ICD 时自动追加（或更新当天）Hitem 条目
- ~~多语言切换菜单~~：工具菜单 → Language（中文/English），写入偏好，重启生效
- ~~主窗口布局记忆~~：关闭时保存 geometry/停靠窗格状态（QSettings），启动恢复

---

## 九、已知限制与已知问题

### 已知限制
- CSV 点表表头为 GBK 中文固定格式（下游导入契约），不随界面语言切换。
- 加载器语义校验为**单 IED 内**的引用一致性校验：跨 IED 的 `FCDA`（`ldInst` 不在本 IED）跳过、不做跨装置 `ExtRef` 解析。
- `SG`（定值）点按 `FC ∈ {SG, SE, SP}` 归类；`SPC/DPC/APC` 控制类 CDC 不重复计入 `SG`。
- Ed2 专属属性（TrgOps 的 gi、OptFields 的 bufOvfl/config、SMV 的 smpMod）在 Ed1 schema 下不输出；非 LN0 节点上的 GSE/SV/定值控制块不符合 Ed1 schema（XSD 校验会提示）。
- 本分支 XSD 校验依赖 libxml2 静态库；未配置时自动退化（`xsdvalidator_stub.cpp`），此时 `--xsd` 与预览里的"XSD 校验"会明确提示未编译。
- 61850 库与 libxml2 需与 Qt4 工具链（VS2013 x64、`/MD`/`/MDd`）一致，否则会出现 CRT 不匹配（LNK4098 等）。

### 已知问题
- **双击就地编辑暂禁用**：树"详细"列的双击就地编辑与展开渲染存在交互崩溃，编辑触发器暂未启用（`mainwindow.cpp` 中 `setEditTriggers` 被注释）；单击选中后在属性面板编辑不受影响。（Qt6 分支同此现象，属既有问题，移植时原样保留。）
- **DO 下 DA/BDA 层级化展示暂缓**：DAI 目前以点分路径单层展示（如 `mag.f`），更深的层级化树节点因渲染期堆损坏未启用。
- **数据集编辑器勾选联动存在既有缺陷**：勾选树节点的 `UserRole` 存的是数字序号字符串，而 `itemChanged` 处理器按 `"do"/"dai"` 比较，导致该分支逻辑实际不生效（Qt6 分支同样存在，移植时原样保留未修）。
