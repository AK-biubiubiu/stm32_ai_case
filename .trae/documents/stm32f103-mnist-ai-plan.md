# 野火 STM32F103「指南者」手写数字识别 AI 案例 —— 实施方案

## 1. 目标与核心结论

在野火指南者（STM32F103VET6，Cortex-M3，72MHz，512KB Flash / 64KB SRAM，3.2 寸 320×240 TFT + 电阻触摸）上实现一个可演示的 AI 案例：

> 在触摸屏上用手指/触摸笔写 0~9，抬手（或书写过程中）实时识别，屏幕右侧显示识别结果与 10 类置信度条形图。

**技术选型结论：不使用 TensorFlow Lite Micro，改为手写 INT8 定点 MLP 推理引擎。**

理由（已核对硬件约束）：

| 方案 | 结论 |
|---|---|
| TFLite Micro | 解释器 + 算子注册 + arena 通常需 80~150KB RAM，F103 仅 64KB，不可行 |
| CMSIS-NN | 可在 M3 上跑，全连接 int8 只有通用 C 回退路径，收益有限，且引入额外构建复杂度 |
| **手写 INT8 MLP（选定）** | 单文件 C99，零依赖，RAM 占用 < 2KB，算力 ~5 万次 MAC，72MHz 下约 7~11ms/次，可边写边识别 |

工作区当前为空（仅有一个空 [README.md](file:///workspace/README.md)），无任何 BSP/工程。所有代码需从零生成，且必须拿到野火 BSP 源码。

---

## 2. 当前状态分析

### 2.1 沙箱环境（已实测）

| 工具 | 状态 |
|---|---|
| gcc / cmake / ninja / make | 可用 |
| python3 + numpy 2.5.3 | 可用 |
| arm-none-eabi-gcc | **不可用**（无法在此交叉编译 ARM） |
| openocd | **不可用**（无法烧录） |
| 网络（MNIST 下载） | 可用（`mnist.npz` 返回 200） |

→ 结论：**推理引擎与预处理可在沙箱内用 x86 gcc 完成端到端验证**；ARM 交叉编译与烧录需在你的本机（Windows）执行。

### 2.2 你的环境

- 开发环境：VS Code + STM32CubeIDE 插件
- 烧录器：DAP-Link（OpenOCD 用 `interface/cmsis-dap.cfg`）
- 野火资料在 `E:\F\A盘（资料盘）`，例程为 Keil 工程，**不使用 Keil**（版权原因）
- 你同意复制整套标准库模板到 `/workspace/board_bsp/`

---

## 3. 前置条件：需要你提供的 BSP

在你开始实现前，把野火模板复制到工作区（若我未能读到该目录，第 1 阶段会先等待）：

```
/workspace/board_bsp/
├── User/                 # main.c, stm32f10x_it.c, stm32f10x_conf.h
├── Libraries/            # CMSIS + STM32F10x_StdPeriph_Driver
├── BSP/ 或 bsp/          # bsp_*.c / bsp_*.h（LCD、触摸、延时、串口、LED、按键、fonts.h）
└── Project/              # 可保留，我不会使用 .uvprojx/.uvoptx
```

查找方法：进入 `A盘（资料盘）`，找「STM32F103 指南者」相关目录 → 「标准库」/「固件库例程」/「程序源码」→ 名称含「模板」的工程。若模板里没有 LCD/触摸，再把「LCD 显示」「触摸屏」两个例程目录一起复制过来。

直接复制文件夹即可（压缩包我也能解压）。**不要**复制 Keil 安装目录或 Crack 文件。

---

## 4. 方案设计

### 4.1 数据流

```
触摸中断/轮询 → 采集 (x,y) → 映射到 96×96 画布 → 笔刷画粗线 + Bresenham 补点
                                      ↓ 同步 2× 放大绘制到 LCD（192×192）
                              [每 150ms 或抬手]
                                      ↓
             裁剪外接框 → 等比缩放至 20×20 → 质心居中对齐到 28×28
                                      ↓
                    uint8 灰度 [0,255] → 量化 int8 [0,127]
                                      ↓
                    MLP 784→64→10（INT8 权重 + INT32 累加）
                                      ↓
                  argmax + softmax → LCD 显示结果 / 置信度条
```

### 4.2 模型与量化（Python 端与 C 端必须完全一致）

- 结构：`784 → 64 (ReLU) → 10`，浮点基线预期 **97.5%~98.2%**（MNIST）
- 训练：`tools/train_mnist.py`，纯 NumPy 实现（Adam，batch 128，12 epoch，lr 1e-3），不依赖 TensorFlow
- 数据增强（缩小"屏幕手写"与 MNIST 的域差距）：随机旋转 ±10°、缩放 0.85~1.15、平移 ±2px、笔画粗细 2~4px 的形态学膨胀/腐蚀
- 量化格式（**对称、零点为 0**，两端同一算法，避免 zero-point 修正）：

```
输入:  x_q = round(x * 127)                x∈[0,1] → x_q∈[0,127], x_scale = 1/127
权重:  w_q = clamp(round(w / w_scale))     w_scale = max|w| / 127
偏置:  保持 float 或 int32（acc_scale 换算）
第一层: acc1 = Σ w1_q * x_q                real1 = acc1 * w1_scale * x_scale
激活:  h = max(0, real1);  h_q = clamp(round(h / h_scale), 0, 127)   # h_scale 用校准集 99.9 分位
第二层: acc2 = Σ w2_q * h_q                logit = acc2 * w2_scale * h_scale + b2
取整约定: 两端统一用「四舍五入远离零」: (int)(v + (v>=0 ? 0.5f : -0.5f))
```

- 产物：`Model/model_data.h`（int8 权重数组、各层 scale、形状宏），由训练脚本自动生成

**资源预算（已核算）**

| 项目 | 占用 |
|---|---|
| int8 权重 784×64 + 64×10 = 50,816 B | Flash |
| 代码 + 野火 BSP + 字库 | Flash ≈ 80~120 KB |
| Flash 合计 | ≈ 170 KB / 512 KB ✅ |
| 画布 96×96 uint8 | 9,216 B |
| 激活 784 + 64 + 10 int8 + int32 累加 | ≈ 1.2 KB |
| 28×28 输入 + 栈/堆 | ≈ 12 KB |
| **RAM 合计** | **≈ 23 KB / 64 KB ✅** |

**算力**：50,816 MAC × 约 10~15 cycle ≈ 0.5~0.8 M cycle ≈ **7~11 ms @72MHz**，支持 150ms 节流的"边写边识别"。

### 4.3 屏幕布局（320×240，横屏）

```
┌──────────────────────────────────────────────┐
│  Draw a digit 0-9            [KEY1: Clear]   │  顶栏 24px
├───────────────────────┬──────────────────────┤
│                       │  RESULT              │
│   96×96 画布 → 2×     │    ██ 7              │  右侧 112px
│   192×192 绘图区      │  ────────────        │  10 条置信度条
│   (黑底白笔)          │  0 ▏                  │
│                       │  1 ▎                  │
│                       │  ...                 │
├───────────────────────┴──────────────────────┤
│  int8 98.1%  |  9 ms                         │  状态栏
└──────────────────────────────────────────────┘
```

- UI 文本**全部使用 ASCII**：野火 `fonts.h` 的中文是 GB2312 编码，GCC 按 UTF-8 编译会乱码，改用英文可彻底规避
- 绘制用 `LCD_DrawPoint`/`LCD_DrawLine` 直接写屏（无帧缓存，F103 也放不下 320×240×2=150KB 帧缓存）
- 左侧绘图区每个画布像素画成 2×2 实心块（`LCD_Fill` 或 2×2 点）

### 4.4 触摸与笔刷

- 复用野火 `TOUCH_Init` / `TOUCH_Scan` / `TOUCH_GetPoint` 系列接口（第 1 阶段读头文件确认真实符号名）
- 触摸坐标 → 画布坐标：按绘图区矩形做线性映射 + 边界钳位
- 两次采样之间用 Bresenham 连线补点，避免电阻屏采样稀疏导致笔迹断裂
- 笔刷半径 3 画布像素（对应屏幕上 6px），用圆形笔刷保证不同方向笔画粗细一致
- 抬手判定：连续 N 次（约 40ms）无触摸点 → 结束一笔

### 4.5 预处理细节（关键，决定实测准确率）

1. 计算画布上非零像素的外接框；若面积过小（<40 px）判为无效，忽略
2. 等比缩放到 20×20 以内（保长宽比），用面积平均降采样
3. 按**质心**而非外接框中心平移，对齐到 28×28 的画布中心（与 MNIST 官方预处理一致）
4. 按当前笔画最大灰度归一化到 [0,255]，再量化到 [0,127]

### 4.6 工程结构

```
/workspace/stm32-mnist-ai/
├── CMakeLists.txt                 # 顶层，目标 stm32_mnist_ai(.elf/.bin/.hex)
├── CMakePresets.json              # debug / release 两个 preset
├── cmake/arm-none-eabi.cmake      # 交叉工具链文件（Windows 下自动补 .exe）
├── ld/STM32F103VETx_FLASH.ld      # 512K Flash @0x08000000, 64K RAM @0x20000000
├── startup/startup_stm32f10x_hd.S # GCC 语法启动文件（替换 Keil 的 .s）
├── Libraries/                     # ← 从 board_bsp 复制（CMSIS + StdPeriph）
├── BSP/                           # ← 从 board_bsp 复制（LCD/触摸/延时/串口/按键）
├── User/
│   ├── main.c                     # 初始化 + 主循环
│   ├── stm32f10x_conf.h
│   ├── stm32f10x_it.c
│   └── keil_compat.h              # __align/__packed/__weak 等 Keil 宏 → GCC 属性
├── App/
│   ├── draw_canvas.c/.h           # 96×96 画布、笔刷、Bresenham、LCD 同步绘制
│   ├── ai_preprocess.c/.h         # 裁剪/缩放/质心居中/量化（纯 C，可在 PC 编译）
│   ├── ai_mlp.c/.h                # INT8 推理（纯 C99，无 STM32 依赖，可在 PC 编译）
│   └── app_ui.c/.h                # 布局、结果数字、置信度条、串口耗时
├── Model/model_data.h             # 训练脚本生成
├── tools/
│   ├── train_mnist.py             # 训练 + 量化 + 导出 model_data.h + 导出测试向量
│   ├── host_test.c                # 宿主端验证：预处理 + int8 推理 + 准确率对比
│   └── run_host_test.sh
└── .vscode/
    ├── settings.json
    ├── c_cpp_properties.json
    ├── tasks.json
    └── launch.json                # Cortex-Debug + OpenOCD + cmsis-dap
```

`App/ai_mlp.c` 与 `App/ai_preprocess.c` 只依赖 `<stdint.h>`/`<math.h>`，因此能在 PC 上原样编译验证——这是本方案能在无硬件条件下保证正确性的关键。

### 4.7 Keil → GCC 移植清单（已预判的坑）

| 问题 | 处理 |
|---|---|
| Keil ARM 语法启动文件 | 用 ST 官方 GCC 版 `startup_stm32f10x_hd.S`（提供） |
| `__align(n)` / `__packed` / `__weak` / `__inline` | `keil_compat.h` 中定义成 `__attribute__((...))` |
| `__attribute__((at(0x...)))` 固定地址变量 | 改为放独立 section，在 .ld 中固定 |
| 中文字符串 GB2312 | UI 全 ASCII（见 4.3） |
| MicroLIB 的 `printf` 重定向 | 用 `-specs=nano.specs` + 自写 `_write`/`_sbrk` |
| `#pragma arm section` / `--c99` | 无需，统一 `-std=gnu11` |
| 编译选项 | `-mcpu=cortex-m3 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections -Wl,--gc-sections` |

---

## 5. 实施步骤

### 阶段 0：接收并核对 BSP
- 读取 `/workspace/board_bsp/` 下 `bsp_*.h`，列出 LCD、触摸、延时、按键的真实函数名与宏（不假设野火 API 名字）
- 检查是否存在 `__attribute__((at(`、`__align(`、GB2312 中文字符串
- **验收**：产出一份 BSP 符号清单，后续代码只调用清单内的接口

### 阶段 1：GCC/CMake 工程骨架跑通显示
- 生成 `CMakeLists.txt`、`CMakePresets.json`、`cmake/arm-none-eabi.cmake`、`ld/STM32F103VETx_FLASH.ld`、`startup/startup_stm32f10x_hd.S`、`User/keil_compat.h`
- `main.c` 只做：时钟 → `LCD_Init` → 清屏 → 写 "Hello F103" → 闪 LED
- **验收**：你本机 `cmake --preset debug && cmake --build` 零错误；烧录后屏幕出字、LED 闪烁

### 阶段 2：触摸画布 UI
- 实现 `draw_canvas.c`、`app_ui.c`：96×96 画布、圆形笔刷、Bresenham 补点、2× 显示、KEY1 清屏、顶栏/侧栏/状态栏
- **验收**：能在屏幕上流畅写数字，轨迹跟手、不断线，清屏正常

### 阶段 3：训练与量化脚本
- `tools/train_mnist.py`：下载/读取 MNIST → 增强 → NumPy Adam 训练 → 量化校准 → 生成 `Model/model_data.h` → 导出 `tools/test_vectors.h`（2000 张测试样本）
- **验收**：脚本在沙箱跑通，打印 float 准确率与 int8 准确率，两者差值 < 1%

### 阶段 4：推理与预处理（宿主端闭环验证）
- 实现 `ai_mlp.c`、`ai_preprocess.c`，以及 `tools/host_test.c`
- 验证项：
  1. MNIST 原图直接输入：int8 准确率 ≥ 97%
  2. int8 与 float 参考实现的预测一致率 ≥ 99.5%
  3. 模拟"画布手写"（放大到 96×96 + 3px 笔刷重绘 + 走完整预处理链路）：准确率 ≥ 95%
  4. 单次推理耗时（x86，作为量级参考）
- **验收**：以上 4 项全部达标，脚本一键可复跑

### 阶段 5：板端联调
- `main.c` 接入：触摸 → 画布 → 150ms 节流推理 → UI 刷新；串口打印单次耗时
- **验收**：板上手写 0~9 各 10 次，识别正确率 ≥ 85%；单次推理 ≤ 20ms；连续书写不卡顿、不花屏

### 阶段 6：VS Code 配置与交付
- `.vscode/`：CMake Tools 配置、`c_cpp_properties.json`（`compilerPath` 指向 arm-none-eabi-gcc）、`tasks.json`（build / flash）、`launch.json`（Cortex-Debug：`servertype=openocd`，`configFiles: interface/cmsis-dap.cfg + target/stm32f1x.cfg`）
- 本机需装：CMake、Ninja、arm-none-eabi-gcc（STM32CubeIDE 自带或 xpack 版）、OpenOCD、VS Code 扩展 **CMake Tools + Cortex-Debug**
- **验收**：在 VS Code 里 F5 能断点调试，`tasks.json` 的 flash 任务能烧录

---

## 6. 假设与决策

1. **不使用 Keil 工程文件**，只用 `.c/.h` 源码与标准外设库；构建体系为 CMake + arm-none-eabi-gcc + Ninja
2. **不用 STM32CubeMX/HAL**：野火 BSP 基于标准外设库，混用 HAL 会导致中断向量与初始化冲突
3. **推理引擎自研**，不引入 TFLite Micro / CMSIS-NN，避免 64KB RAM 超限
4. **UI 使用英文**，规避 GB2312 字库编码问题
5. **模型 784-64-10**；若 Flash 紧张可降到 784-32-10（准确率约 -0.7%），改脚本一行参数即可
6. 屏幕默认横屏 320×240；若你的屏是 240×320 或 480×272，只改 `app_ui.c` 里的布局常量
7. 假设触摸为野火配套电阻屏；若实际是电容屏（GT811/GT911），第 0 阶段确认后改用对应 `bsp_gtxxx` 接口，画布与 AI 部分完全不受影响

---

## 7. 验证方法汇总

| 层级 | 手段 | 通过标准 |
|---|---|---|
| 模型 | `tools/train_mnist.py` | float ≥ 97.5%，int8 掉点 < 1% |
| 推理正确性 | x86 `host_test`（沙箱内可跑） | int8 准确率 ≥ 97%，与 float 一致率 ≥ 99.5% |
| 预处理链路 | 模拟画布重绘测试 | ≥ 95% |
| 编译 | 你本机 `cmake --build`（或沙箱若可装 arm-gcc） | 0 error，输出 `.elf/.bin/.map`，size 报告 Flash < 200KB、RAM < 30KB |
| 板端 | 实机手写 100 次 | 准确率 ≥ 85%，单次推理 ≤ 20ms，无花屏/卡死 |

---

## 8. 风险与回退

| 风险 | 回退方案 |
|---|---|
| 触摸校准差、笔迹漂移 | 提供一个隐藏的 4 点校准入口，或直接读野火默认校准参数 |
| 板上实测准确率偏低（域差距大） | 加大训练增强幅度；增加"笔画细化"预处理；改用 784-96-10 |
| Keil BSP 里有无法直接编过的专有代码 | 用 `keil_compat.h` 适配；实在不行只替换 LCD 驱动为最小实现（FSMC 初始化 + 写点/填矩形即可） |
| 沙箱始终无法交叉编译 | 交付前用 `arm-none-eabi-gcc -fsyntax-only` 不可行时，改为你本机首次构建并把报错贴回来，我按报错修 |
| 触摸屏不可用 | 退化为"上/下键切换内置测试图"演示模式（保留完整 AI 链路） |
