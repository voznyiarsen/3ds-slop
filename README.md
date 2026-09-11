# Nintendo 3DS Cube Manipulator

A small homebrew application for Nintendo 3DS that provides a Blender-style 3D cube editor. It lets you move and rotate the cube as a whole, and select and move individual vertices.

## Requirements

- Nintendo 3DS homebrew-capable system or emulator
- devkitPro with devkitARM
- libctru
- citro3d
- citro2d
- Picasso shader compiler
- 3dsxtool

The project was built and verified with devkitARM 16.1.0.

## Build

From this directory:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
make
```

The build creates:

```text
build/cube_manipulator.elf
build/cube_manipulator.3dsx
```

Copy `build/cube_manipulator.3dsx` to your 3DS homebrew launcher or emulator.

To rebuild from a clean state:

```sh
make clean
make
```

## Controls

| Control | Action |
| --- | --- |
| Circle Pad | Rotate the cube |
| D-pad | Move the cube horizontally and vertically |
| L / R | Move the cube toward or away from the camera |
| Touch screen | Select and drag the nearest vertex |
| X | Reset cube position and rotation |
| Y | Assign random colors to vertices |
| Start | Deselect the current vertex and exit the application |

## Interaction

The top screen displays the cube. The cube is rendered with vertex colors, a wireframe-style edge overlay, and highlighted vertex markers.

The bottom screen displays:

- Cube position
- Cube rotation
- The selected vertex
- The current coordinates of all eight vertices
- A control reference

Touching near a projected vertex selects it. Dragging the touch point moves that vertex in screen space. The selected vertex is highlighted in the vertex list.

## Implementation

The application uses:

- `libctru` for system services and input
- `citro3d` for 3D rendering and matrix math
- `citro2d` for the bottom-screen interface
- Picasso-generated PICA200 vertex shaders
- C++11 with no RTTI or exceptions

The cube has eight editable vertices and twelve edges. Vertex positions are stored in local cube coordinates and transformed with translation and rotation matrices before rendering.

## License

This project is provided as a homebrew example. The devkitPro libraries retain their respective licenses.
