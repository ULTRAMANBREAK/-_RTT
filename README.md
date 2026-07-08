# RTT Box

多模块嵌入式项目集合仓库，涵盖 STM32 主控（RT-Thread）、ESP32 从机、上位机网页与 51 单片机夹子控制端。

## 项目结构

```
.
├── rtt_lazydog/   # STM32 主控固件（RT-Thread 系统）
├── ESP32/         # ESP32 端固件（小智 AI 助手 xiaozhi-esp32）
├── 夹子/          # STC8G 51 单片机夹子控制端
├── 网页/          # 上位机 / Web 前端
└── APP/           # 移动端（HBuilderX 工程，IDE 本体已在 .gitignore 中忽略）
```

### rtt_lazydog — STM32 主控

基于 RT-Thread 的 STM32 项目，使用 Keil MDK / CubeMX 配置。

- `applications/` — 应用层：GPS、传感器、显示、存储、UART 总线、ESP32 通讯等
- `board/` — 板级支持包 + CubeMX 生成的 HAL 驱动
- `packages/` — RT-Thread 组件包
- `libraries/` — 第三方库
- `project.uvprojx` — Keil 工程文件

### ESP32 — 小智 AI 助手

`xiaozhi-esp32-main/` 为 ESP32 端固件源码，适配多种开发板。

### 夹子 — STC8G 单片机

`STC8G/` 目录下的 51 系单片机固件，负责夹子机构的控制。

### 网页 — Web 前端

`index.html` 等页面文件，作为上位机 / 配置端界面。

### APP — 移动端

原 HBuilderX 工程目录。`APP/HBuilderX/` 是 IDE 本体（约 500MB），已通过 `.gitignore` 排除，不进入仓库。

## 构建与烧录

- **STM32**：Keil MDK 打开 `rtt_lazydog/project.uvprojx` 编译；或参考 `rtt_lazydog/README.md` 使用 SCons + GCC。
- **ESP32**：ESP-IDF 环境，进入 `ESP32/xiaozhi-esp32-main/` 后 `idf.py build flash`。
- **STC8G**：Keil C51 打开 `夹子/STC8G/` 内工程编译，STC-ISP 下载。

## .gitignore 策略

已忽略：`node_modules/`、`build/`、`Debug/`、`outputs/`、`__pycache__/`、Keil/IAR/GCC 中间产物、HBuilderX IDE 本体等。
