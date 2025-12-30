\# OpenGL Voxel Engine \& Shadow Mapping Demo



!\[Project Screenshot](screenshots/demo.png) 

\*(请在这里替换为你实际截图的路径)\*



A C++ based voxel engine built from scratch using OpenGL 3.3. This project demonstrates advanced graphics programming concepts including real-time shadow mapping, hierarchical character animation, and voxel terrain generation.



\## 🚀 Features



\* \*\*Render Engine\*\*: Core Profile OpenGL 3.3 renderer.

\* \*\*Shadow Mapping\*\*: Implemented directional shadow mapping with \*\*PCF (Percentage-Closer Filtering)\*\* for soft shadows and bias adjustment to prevent shadow acne.

\* \*\*Terrain System\*\*: Procedural generation of "Skyblock" style islands with voxel types (Grass, Dirt, Stone, Ores).

\* \*\*Physics \& Collision\*\*: AABB (Axis-Aligned Bounding Box) collision detection, gravity simulation, and ray-casting for block selection.

\* \*\*Character Animation\*\*: Hierarchical modeling for the robot character with procedural walking animation (limb swinging).

\* \*\*Camera System\*\*: Support for First-Person, Third-Person, and Free-Roaming camera modes.

\* \*\*Visual Effects\*\*: Distance-based linear fog and dynamic day/night cycle calculations.



\## 🛠️ Tech Stack



\* \*\*Language\*\*: C++

\* \*\*Graphics API\*\*: OpenGL 3.3

\* \*\*Libraries\*\*:

&nbsp;   \* `GLFW` (Windowing \& Input)

&nbsp;   \* `GLAD` (OpenGL Loader)

&nbsp;   \* `GLM` (Mathematics)

&nbsp;   \* `stb\_image` (Texture Loading)



\## 📂 Project Structure



\* `src/`: Main source code.

\* `shaders/`: GLSL vertex and fragment shaders.

\* `assets/`: Textures and sprites.



\## 🎮 Controls



\* \*\*W/A/S/D\*\*: Move Character / Camera

\* \*\*Space\*\*: Jump

\* \*\*Mouse\*\*: Look around

\* \*\*V\*\*: Switch Camera Mode (First/Third/Free)

\* \*\*I/K/J/L/U/O\*\*: Interact with the Chest object (Move/Rotate)

