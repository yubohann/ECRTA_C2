# OpenGL 兼容补丁

文件：src/MARSIM/local_sensing/include/opengl_sim.hpp

改动：请求的 OpenGL Core Profile 从 4.6 降为 3.3。

原因：WSLg 在 Ubuntu 20.04 下只暴露 OpenGL 3.3，不改会导致 GLFW 窗口创建失败。

影响：MARSIM 着色器为 GLSL 3.30，本补丁只影响渲染初始化，不改变任务分解、ACVRP 分配、轨迹规划、通信或 LKH 求解逻辑。

该补丁必须披露，不得视为算法贡献。
