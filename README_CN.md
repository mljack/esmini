# Scenario Studio 用户手册

## 1. 简介
Scenario Studio 是基于[Esmini](README_esmini.md)引擎的OpenSCENARIO集成开发环境，用于创建、编辑OpenSCENARIO 文件 (`.xosc`)和检视案例运行结果，旨在为设计、开发和调试交通场景提供了流畅、便捷、高效的使用体验

![界面概览](docs/scenario_studio_ui.png)

### 1.1. 支持的操作系统
目前仅支持Windows平台，其他平台请使用[Wine](https://www.winehq.org/)或在issue区提交需求

### 1.2. 启动程序
可以直接点击 `scenariostudio.exe` 文件启动 Scenario Studio，将默认加载`resources/xodr/e6mini.xodr`。也可以通过带有参数命令行启动 Scenario Studio，加载指定OpenDRIVE路网文件和OpenSCENARIO文件，以及配置3D模型文件和资源搜索路径：

```bash
scenariostudio [选项]
```

| 选项 | 描述 |
| :--- | :--- |
| `--help` | 显示所有可用的命令行参数 |
| `--odr <file>` | 指定 OpenDRIVE (`.xodr`) 路网文件 |
| `--osc <file>` | 指定要打开的 OpenSCENARIO (`.xosc`) 文件 |
| `--model <file>` | 指定model文件 |
| `--path <dir>` | 指定资源（模型、目录）的搜索路径前缀（支持使用多次） |

**示例：**
```bash
F:\esmini-demo\bin>scenariostudio --odr ../resources/xodr/soderleden.xodr --osc ../resources/xosc/highway_merge.xosc
```

## 2. 界面概览
主界面分为如下几个区域：

*   **俯视视图 (Top View)**：路网和车辆与其他位置的显示区域，用于可视化场景设置、预览和回放场景运行结果
*   **菜单栏 (Menu Bar)**：位于窗口顶部，提供文件操作、编辑工具和设置选项
*   **XML 树 (XML Tree)**：右侧面板，显示 OpenSCENARIO XML 的层级结构，提供编辑、查找、筛选等功能
*   **时间轴 (Timeline)**：底部时间条，主要用于在检视模式下拖拽改变仿真时间
*   **日志窗口 (Log Window)**：底部面板，显示系统消息、错误和日志等信息

## 3. 工作模式
Scenario Studio 有三种不同的工作模式：（在窗口标题栏上显示）

### 3.1. 编辑模式 (Composer Mode)
此模式用于编辑仿真场景设置。
*   **功能**：创建和编辑 XML 树形结构，修改属性，直观地放置或移动对象
*   **切换**：按 `空格键 (Space)` 切换到预览模式。
    *   **注意**：若切换失败，在Log窗口中会显示具体过程和原因，具体方法参见[常见问题处理](#常见问题处理)

### 3.2. 预览模式 (Viewer Mode)
此模式使用 `esmini` 引擎实时运行仿真
*   **功能**：场景的实时回放，摄像机自动跟随 "ego" 车辆
*   **切换**：按 `Esc` 键返回编辑模式。按空格键 (Space) 或仿真结束时切换到检视模式

### 3.3. 检视模式 (Inspector Mode)
此模式允许对仿真结果进行分析和回放
*   **功能**：拖动时间轴查看仿真结果
*   **退出**：按 `Esc` 键返回编辑模式

## 4. 视图中的显示与控制

### 4.1. 摄像机控制
*   **平移**：右键拖动
*   **缩放**：鼠标滚轮

### 4.2. 抬头显示 (HUD)
显示鼠标指针处的位置信息：
*   **世界坐标**：X, Y, Z。
*   **车道坐标**：Road ID, Lane ID, S, Lane Offset

    ![hud](docs/hud.png)

### 4.3. 可视化显示 (Visualization Markers)
Scenario Studio 使用不同的 3D 形状来表示各种实体类型和位置：
*   **加载osgb模型**：默认按照实体对应的osgb模型进行可视化显示，加载失败时显示为方块
*   **条件或其他节点中的位置**：用黄色小球显示，也支持选择和移动
*   **轨迹路径**：使用青色折线连接轨迹中的位置点

**注意**：即使在拖动对象进行放置时，您也可以平移摄像机（右键拖动）

![视图中的各种位置](docs/position_and_path.png)

## 5. 编辑场景 (编辑模式)

### 5.1. 在OpenSCENARIO规范指导下的编辑 (Schema-Aware Editing)
Scenario Studio 主动使用 `OpenSCENARIO.xsd` 来指导您的编辑：
*   **上下文相关菜单**：创建元素时，仅显示规范定义的有效子节点
*   **自动填充**：添加新节点时，会自动创建必需属性和强制子元素，并赋予默认值
*   **类型检查**：属性输入字段适应数据类型（例如，枚举的下拉列表，布尔值的复选框）
*   **自动添加常见内容**：自动创建非强制要求但却非常常用节点和属性，如LanePosition节点、ego实体、初始速度等

### 5.2. XML 树操作
XML 树面板是编辑场景结构的核心枢纽。
*   **导航**：
    *   **展开/折叠**：点击箭头或使用 **Expand All (全部展开)** / **Collapse All (全部折叠)** 按钮
    *   **搜索**：输入文本并点击 **Find (查找)** 以循环查看匹配项（节点和属性）
    *   **筛选**：输入文本并点击 **Filter (筛选)** 仅显示匹配的节点。使用 **Isolate (隔离)** 可完全隐藏不匹配的节点
*   **上下文菜单** (右键点击节点)：
    *   **Delete Node (删除节点)**：移除节点（如果架构允许）
    *   **Delete Attribute (删除属性)**：移除特定属性
    *   **Create Attribute (创建属性)**：为选定节点添加有效属性
    *   **Create Element (创建元素)**：添加有效子元素（遵循 `minOccurs`/`maxOccurs` 规则）
    *   **Move Node Up/Down (上移/下移节点)**：重新排序同级节点
    *   **Duplicate Node (复制节点)**：克隆节点及其子树（如复制会引起重名将会通过自增原名末尾数字或添加数字后缀来避免重名）

    ![XML 树上下文菜单](docs/xml_context_menu.png)

*   **属性编辑**：
    *   **值**：点击值进行编辑。
        *   **数字**：拖动调整或点击输入。支持小键盘输入
        *   **布尔值**：勾选框 (True/False)
        *   **枚举**：有效枚举值的下拉列表（不包含已经被定义为 `deprecated` 的枚举值）
        *   **实体引用**：已定义 `ScenarioObject` 名称的下拉列表
    *   **表达式**：
        *   **支持参数表达式**：被设置成表达式的属性会用黄色显示（例如 `${$EgoSpeed / 3.6}`）
        *   **转换**：右键点击属性 -> **Convert to Expression (转换为表达式)** 将其包裹在 `${}` 中，删除表达式字符中的 `$`字符即可恢复为其原始类型
        *   **预览计算结果**：计算结果以绿色显示在字段旁边

        ![表达式](docs/expression.png)   

    *   **Position节点编辑**：
        *   **直接属性编辑**：OpenSCENARIO 中所有类型的位置节点均可通过属性编辑进行修改
        *   **查找与拖拽**：以下类型的位置节点，支持通过XML树中 `Find It` 或 `Move It` 按钮在视图中查找位置并拖拽编辑位置节点
            *   **WorldPosition (世界位置)**：绝对世界坐标 (x, y, z, h, p, r)
            *   **LanePosition (车道位置)**：车道坐标 (roadId, laneId, s, offset)
            *   **RelativeLanePosition (相对车道位置)**：相对于另一个实体的车道坐标 (dLane, ds, offset)
    *   撤销与重做
        *   **撤销**：按 `Ctrl+Z` 或选择菜单 `Edit > Undo` 撤销上一步操作
        *   **重做**：按 `Ctrl+Shift+Z` 或选择菜单 `Edit > Redo` 重做上一步撤销的操作

### 5.3. 视图中对象操作
*   **选择**：在 3D 视图中左键点击车辆或行人，在 XML 树中会展开并高亮相应位置节点
*   **移动**：在 3D 视图中右键点击该对象，选择 `Move`，移动鼠标来修改位置，左键点击以确认修改完成
    *   **注意**：在移动对象时，Scenario Studio 会自动更新相应的位置节点的属性

## 6. Schema检查 （WIP）
在运行仿真前验证场景正确性
*  **菜单**：`Edit > Validate Scenario`
*  **报告窗口**：
    *   **全面检查**：根据 `OpenSCENARIO.xsd` 验证：
        *   必需的属性和子元素
        *   数据类型（整数、浮点数、布尔值、字符串）
        *   枚举（包括已弃用的值）
        *   实体引用（检查引用的对象是否存在）
        *   结构约束（`minOccurs`、`maxOccurs`、`xsd:choice` 互斥性）
        *   已弃用的元素/属性。
    *   **Jump**：在 XML 树中定位错误
    *   **Fix It**：自动修复简单错误：
        *   添加带有默认值的缺失必需属性
        *   将无效属性值重置为默认值
    *   **注意**：参数表达式（例如 `$Speed`）在类型验证期间会被忽略，以防止误报

## 7. 参数与表达式支持
Scenario Studio 支持 OpenSCENARIO 参数系统：
*  **定义**：在 `ParameterDeclarations` 部分添加 `ParameterDeclaration` 元素
*  **使用**：在任何属性值中，使用语法 `${ParameterName}`
*  **评估**：编辑器将尝试根据定义的默认值或值来解析这些参数
    *   **树视图**：解析后的值会以绿色显示在 XML 树中的属性值旁边

## 8. 文件菜单（File）
*   **Clear**：重置当前工作区（将创建默认的场景内容）
*   **Open an OpenSCENARIO File ...**：打开 OpenSCENARIO 文件
*   **Save** (`Ctrl+S`)：保存当前 OpenSCENARIO 文件
*   **Save As...**：保存为新的 OpenSCENARIO 文件
*   **Open an OpenDRIVE File ...**：打开 OpenDRIVE 文件
*   **Exit**：关闭应用程序

## 9. 设置菜单（Setting）
*   **Esmini Settings** 配置仿真器参数：
    *   **Seed**：仿真使用的随机数种子，用于确保随机元素在仿真中的可重复性
    *   **Timestep**：仿真时间步长（默认 0.05秒）
    *   **Resource Paths**：3D 模型和目录的搜索路径
*   **Reset Windows**：重置子窗口大小
    *   **注意**：程序窗口尺寸或子窗口尺寸会自动保存到 `config.json`，下次启动时会自动加载

## 10. 日志窗口（Log）
显示系统事件和错误的实时日志
*   **Clear (清除)**：移除所有当前日志消息
*   **Copy All (全部复制)**：将整个日志内容复制到剪贴板
*   **Auto-scroll (自动滚动)**：切换以自动滚动到最新消息
*   **选择**：点击并拖动以选择文本。右键点击以复制选定文本

## 11 常见问题处理
*   场景文件中的错误设置或缺少关键节点导致仿真无法启动，如Entities中的CatalogReference未能正确设置等。
*   检查命令行参数和 Settings > Esmini Settings 中的资源搜索路径是否正确设置
*   可以通过点击菜单中的 Edit > Validate 进行验证
*   根据 `Log` 窗口中的信息进行排查
