# Scenario Studio User Manual([中文版](README_CN.md))

## 1. Introduction

Scenario Studio is an OpenSCENARIO integrated development environment based on the [Esmini](README_esmini.md) engine. It is used to create and edit OpenSCENARIO (`.xosc`) files and inspect scenario execution results, providing a smooth, convenient, and efficient user experience for designing, developing, and debugging traffic scenarios.

![UI Overview](docs/scenario_studio_ui.png)

### 1.1 Supported Operating Systems

Currently, only Windows is supported. For other platforms, please use [Wine](https://www.winehq.org/) or submit a request in the issue tracker.

### 1.2 Launch the Application

Scenario Studio can be launched by directly double-clicking `scenariostudio.exe`. The default file `resources/xodr/e6mini.xodr` will be loaded.

You may also launch Scenario Studio via command-line arguments, specifying an OpenDRIVE road network file, an OpenSCENARIO file, and configuring 3D model files and resource search paths:

```bash
scenariostudio [options]
```

| Option           | Description                                                                                   |
| :--------------- | :-------------------------------------------------------------------------------------------- |
| `--help`         | Show all available command-line options                                                       |
| `--odr <file>`   | Specify the OpenDRIVE (`.xodr`) road network file                                             |
| `--osc <file>`   | Specify the OpenSCENARIO (`.xosc`) file to load                                               |
| `--model <file>` | Specify a model file                                                                          |
| `--path <dir>`   | Specify a resource search path prefix for models and directories (may be used multiple times) |

**Example:**

```bash
F:\esmini-demo\bin>scenariostudio --odr ../resources/xodr/soderleden.xodr --osc ../resources/xosc/highway_merge.xosc
```

## 2. Interface Overview

The main interface consists of the following areas:

* **Top View**: Displays the road network, vehicles, and positions. Used to visualize scenario configuration, preview, and playback.
* **Menu Bar**: Provides file operations, editing tools, and settings.
* **XML Tree**: A hierarchical view of the OpenSCENARIO XML structure, offering editing, searching, and filtering.
* **Timeline**: At the bottom; used mainly in Inspector Mode to adjust simulation time.
* **Log Window**: Displays system messages, errors, and logs.

## 3. Working Modes

Scenario Studio has three different working modes (displayed in the window title bar):

### 3.1 Composer Mode

Used for editing scenario configuration.

* **Functions**: Create and edit XML structures, modify attributes, visually place or move objects.
* **Switch mode**: Press `Space` to enter Viewer Mode.
  * **Note**: If switching fails, the Log window will display the details. See [Troubleshooting](#Troubleshooting).

### 3.2 Viewer Mode

This mode runs the simulation using the `esmini` engine.

* **Functions**: Real-time playback with automatic camera tracking of the ego vehicle.
* **Switch mode**: Press `Esc` to return to Composer Mode. Press `Space` or wait for the simulation to end to enter Inspector Mode.

### 3.3 Inspector Mode

Used for analyzing and replaying simulation results.

* **Functions**: Drag the timeline slider to review the simulation results
* **Switch mode**: Press `Esc` to return to Composer Mode.

## 4. View Display and Controls

### 4.1 Camera Controls

* **Pan**: Right mouse button drag
* **Zoom**: Mouse wheel

### 4.2 Heads-Up Display (HUD)

Shows position information where the mouse pointer is pointing

* **World coordinates**: X, Y, Z
* **Lane coordinates**: Road ID, Lane ID, S, Lane Offset

![hud](docs/hud.png)

### 4.3 Visualization Markers

Scenario Studio uses different 3D shapes to represent various entities and positions:

* **OSGB models**: Entities are displayed using their OSGB model. If loading fails, they appear as cubes.
* **Positions in conditions or other nodes**: Displayed as yellow spheres; selectable and draggable.
* **Trajectory paths**: Connected by cyan polylines.

**Note**: You can still pan the camera while dragging objects.

![Positions and paths](docs/position_and_path.png)

## 5. Editing Scenarios (Composer Mode)

### 5.1 Schema-Aware Editing (Based on OpenSCENARIO Specification)

Scenario Studio actively uses `OpenSCENARIO.xsd` to guide editing:

* **Context-sensitive menus**: Only valid child nodes defined by the schema will be shown.
* **Auto-fill**: Required attributes and mandatory child elements are created with default values.
* **Type checking**: Inputs adapt to data types (dropdown for enums, checkbox for booleans).
* **Automatic creation of common content**: Such as `LanePosition`, `ego` entity, initial speed, etc.

### 5.2 XML Tree Operations

The XML tree is the central tool for editing the scenario structure.

* **Navigation**:
    * **Expand/Collapse**: Click arrows or use **Expand All** / **Collapse All** buttons
    * **Search**: Enter text and click **Find** to cycle through matching items (nodes and attributes)
    * **Filter**: Enter text and click **Filter** to show only matching nodes. Use **Isolate** to completely hide non-matching nodes
* **Context menu** (right-click a node):
    * **Delete Node**: Remove the node (if schema allows)
    * **Delete Attribute**: Remove a specific attribute
    * **Create Attribute**: Add a valid attribute for the selected node (selected from sub-menu)
    * **Create Element**: Add a valid child XML node (selected from sub-menu, and following `minOccurs`/`maxOccurs` rules)
    * **Move Node Up/Down**: Move clicked node up/down in its sibling nodes
    * **Duplicate Node**: Clone the node and its subtree (automatically renames to avoid duplicates by incrementing numbers at the end of the original name or adding numeric suffixes)

![XML context menu](docs/xml_context_menu.png)

* **Attribute Editing**:
    * **Value editing**: Click the value to edit
        * **Numbers**: Drag to adjust or click to input. Numpad input is supported
        * **Boolean**: Checkbox (True/False)
        * **Enum**: Dropdown list of valid enum values (excluding values defined as `deprecated`)
        * **Entity reference**: Dropdown list of defined `ScenarioObject` names
    * **Expressions**:
        * **Parameter expressions supported**: Attributes set as expressions are displayed in yellow (e.g., `${$EgoSpeed / 3.6}`)
        * **Conversion**: Right-click an attribute -> **Convert to Expression** to wrap it in `${}`, remove the `$` character inside the expression to restore the original type
        * **Preview calculated results**: Calculated results are displayed in green next to the field

![expression](docs/expression.png)

    * **Position Node Editing**:
        * **Direct attribute editing**: All types of Position nodes in OpenSCENARIO can be modified through attribute editing
        * **Find and drag**: The following types of Position nodes support finding positions in the view and dragging to edit via the `Find It` or `Move It` buttons in the XML tree:
            * **WorldPosition**: Absolute world coordinates (x, y, z, h, p, r)
            * **LanePosition**: Lane coordinates (roadId, laneId, s, offset)
            * **RelativeLanePosition**: Lane coordinates relative to another entity (dLane, ds, offset)

    * **Undo / Redo**:
        * **Undo**: Press `Ctrl+Z` or select menu `Edit > Undo` to undo the last operation
        * **Redo**: Press `Ctrl+Shift+Z` or select menu `Edit > Redo` to redo the last undone operation

### 5.3 Object Operations in the View

* **Select**: Left-click a vehicle/pedestrian to highlight its position node in the XML tree.
* **Move**: Right-click, then click the context menu `Move`, drag the mouse, left-click to confirm.
  Position attributes will update automatically.

## 6. Schema Validation (WIP)

Validate scenario correctness before simulation.

* **Menu**: `Edit > Validate Scenario`
* **Report window**:
    * **Comprehensive validation** based on `OpenSCENARIO.xsd`:
        * Required attributes and child elements
        * Data types (integer, float, boolean, string)
        * Enums (including deprecated values)
        * Entity references (checks if referenced objects exist)
        * Structural constraints (`minOccurs`, `maxOccurs`, `xsd:choice` exclusivity)
        * Deprecated elements/attributes
    * **Jump**: Locate errors in the XML tree
    * **Fix It**: Automatically fix simple errors:
        * Add missing required attributes with default values
        * Reset invalid attribute values to default values
    * **Note**: Parameter expressions (e.g., `$Speed`) are ignored during type validation to avoid false positives

## 7. Parameters and Expression Support

Scenario Studio supports the OpenSCENARIO parameter system:

* **Define**: Add `ParameterDeclaration` elements in the `ParameterDeclarations` section
* **Use**: In any attribute value, use the syntax `${ParameterName}`
* **Evaluation**: The editor will try to resolve these parameters based on defined default values or values
    * **Tree view**: Resolved values are displayed in green next to the attribute values in the XML tree

## 8. File Menu

* **Clear**: Reset current workspace (creates default scenario content)
* **Open an OpenSCENARIO File ...**: Open an OpenSCENARIO file
* **Save** (`Ctrl+S`): Save the current OpenSCENARIO file
* **Save As...**: Save as a new OpenSCENARIO file
* **Open an OpenDRIVE File ...**: Open an OpenDRIVE file
* **Exit**: Close the application

## 9. Settings Menu

* **Esmini Settings** to configure simulator parameters:
    * **Seed**: Random number seed used by the simulation to ensure reproducibility of random elements
    * **Timestep**: Simulation timestep (default 0.05 seconds)
    * **Resource Paths**: Search paths for 3D models and directories
* **Reset Windows**: Reset sub-window sizes
    * **Note**: Program window size or sub-window sizes are automatically saved to `config.json` and loaded automatically on next startup

## 10. Log Window

Show logs of system events and errors
* **Clear**: Remove all current log messages
* **Copy All**: Copy the entire log content to clipboard
* **Auto-scroll**: Toggle to automatically scroll to the latest message
* **Selection**: Click and drag to select text. Right-click to copy selected text

## 11. Troubleshooting

* Incorrect scenario configuration or missing critical nodes may prevent simulation startup, such as unresolved `CatalogReference` in Entities.
* Check command-line arguments and resource search paths in `Settings > Esmini Settings`
* Use **Edit > Validate** for validation
* Diagnose issues based on information in the **Log** window

