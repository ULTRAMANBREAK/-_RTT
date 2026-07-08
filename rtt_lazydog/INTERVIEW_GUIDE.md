# RT-Thread Lazydog 工程代码逻辑讲解（保研面试版）

> 平台：STM32F407 + RT-Thread Nano/Standard + RW007 WiFi
> 目标：BOX 多模态感知节点（雷达 LD2451 / RFID RC522 / GPS ATGM336H / 温湿度 / LCD / WS2812 / MQTT 上云）
> 工程亮点：**应用层模块化注册启动 + UART4 时分多路复用 + 状态机式协议解析 + RT-Thread 设备框架异步 IO**

---

## 第一部分 · 面试速记（5 分钟必背）

### 一句话总览

> 整个工程是一个基于 **RT-Thread** 的多传感器物联网网关。我用 **"启动注册表 + 模块化目录"** 组织应用层，让每个外设/业务在自己的 `inc/src/` 里独立闭环；用 **UART4 时分多路复用** 把一个串口切给 3 路雷达 + 1 路 RFID 复用，解决了引脚不够的硬件约束；所有串口外设统一走 **RT-Thread 设备框架 + 信号量 + DMA 接收**，是经典的中断唤醒线程的生产者-消费者模型。

### 高频问答（Q&A）

**Q1：你这个工程是怎么组织的？**
> 三层结构：①底层 BSP（CubeMX 生成 HAL 驱动 + RT-Thread 板级 BSP）；②内核层 RT-Thread；③应用层 `applications/`，按业务拆模块：`app_startup / config / gps / uart4_bus / sensors / display / network / esp32 / devices`。SConscript 自动扫描每个模块下的 `src/*.c` 和 `inc/`，新增模块只要建目录、写头文件，不用改构建脚本。

**Q2：模块是怎么被启动起来的？为什么不全用 `INIT_APP_EXPORT`？**
> RT-Thread 提供 6 级自动初始化（BOARD/PREV/DEVICE/COMPONENT/ENV/APP），用 `INIT_xxx_EXPORT(fn)` 把函数指针放到链接段，启动时按段顺序遍历调用。
> 但我有两个需求 RT 原生宏满足不了：①**WiFi 依赖**（MQTT 必须等 WiFi 就绪）；②**显式优先级控制**（UART4 复用线程必须先于 ld2451 解析器）。
> 所以我只用一个 `INIT_APP_EXPORT(app_startup_init)` 作为入口，里面开一条 `app_start` 线程，在线程里维护一张 **静态注册表**：
> ```c
> static const app_start_item_t app_start_items[] = {
>     {"mqtt",   mqtt_app_start,   RT_TRUE,  0},   // 需要 wifi
>     {"uart4",  uart4_app_init,   RT_FALSE, 10},
>     {"sensor", sensor_app_init,  RT_FALSE, 20},
>     ...
> };
> ```
> 启动线程先调 `wifi_rw007_wait_ready(timeout)` 等 WiFi 就绪信号，再按数组顺序调用，`require_wifi=TRUE` 的项目在 WiFi 失败时跳过但不阻塞其他模块。

**Q3：为什么要在主线程外再开一条启动线程？**
> 因为 `INIT_APP_EXPORT` 在 `main` 之前由 `rt_components_init()` 在调度器空闲态调用，那时不允许长时间阻塞（会卡住调度器）。WiFi 连接是 10+ 秒级的慢操作，必须放到一条独立线程里，让 idle/main 先跑起来。

**Q4：UART4 多路复用是怎么做的？这是核心亮点！**
> 硬件上接了一片 **CD4052（双 4 选 1 模拟开关）**，用 2 根 GPIO（PIN_A/B）作为通道选择线，组合成 4 个通道（00/01/10/11）。三路 LD2451 毫米波雷达 + 一路 RC522 RFID 共享 UART4 的 TX/RX。
> 软件上做 **TDMA 时分轮询**：
> ```
> while(1) {
>   for (channel = 0..3) {
>     uart4_mux_select(channel);  // 切 GPIO
>     ld2451_parser_reset(channel); // 复位状态机
>     uart4_flush();                 // 丢掉切换瞬间的脏数据
>     end_tick = now + HOLD_MS;
>     while (now < end_tick) {
>       rx_len = rt_device_read(uart4_dev, ...);
>       if (rx_len == 0) rt_sem_take(rx_sem, 20ms);  // 中断唤醒
>       else uart4_dispatch_rx(channel, buf, rx_len);
>     }
>   }
> }
> ```
> 每个通道驻留 `CHANNEL_HOLD_MS` 毫秒，期间收到的字节按 `channel` 路由给对应解析器（雷达 vs RFID）。每个解析器内部维护**独立的环形/线性缓冲**，互不污染。

**Q5：为什么不用查询？用了哪些 RT-Thread 内核对象？**
> 用查询会忙等浪费 CPU。我用的是 **中断 + 信号量 + DMA** 的标准异步模型：
> - **设备框架**：`rt_device_find("uart4") → open(DMA_RX) → control(波特率) → set_rx_indicate(cb)`
> - **信号量** `uart4_rx_sem`：UART 收到数据触发 `rx_indicate` 回调，回调里 `rt_sem_release` 释放信号量
> - **接收线程**：`rt_device_read` 非阻塞读，没数据就 `rt_sem_take` 阻塞挂起，让出 CPU
> - **DMA**：`RT_DEVICE_FLAG_DMA_RX` 让 HAL 用 DMA 搬运字节到 FIFO，CPU 只在帧到达时被中断打断一次
>
> 这就是嵌入式经典的 **生产者（ISR）-消费者（线程）模型**。

**Q6：LD2451 雷达的协议怎么解析的？**
> LD2451 输出二进制帧：`F4 F3 F2 F1 | LEN(2B,LE) | PAYLOAD | F8 F7 F6 F5`，payload 第一字节是目标数 N，后面跟 2 字节告警标志 + N × 5 字节目标槽（角度/距离/方向/速度/SNR）。
> 我写了一个 **滑动缓冲 + 状态机解析器**：
> 1. 字节流追加进 `stream_buf[channel]`
> 2. `find_header` 扫描帧头 `F4F3F2F1`
> 3. 找到后读 2 字节小端长度，等到缓冲达到完整帧长再校验帧尾
> 4. 校验失败就丢 1 字节继续找头（抗错位）
> 5. 校验 `payload_len == 2 + 5*N`，防伪造帧崩
> 6. 成功就 `memmove` 消耗已用字节，继续下一帧

**Q7：GPS 的 NMEA 协议又怎么解析？**
> NMEA 是 ASCII 协议（`$GNRMC,...,...,*XX\r\n`），处理思路完全不同：
> 1. **按行组装**：逐字节读，遇 `\n` 收尾，得到一整句
> 2. **校验和**：从 `$` 后到 `*` 前所有字符 **异或**，与 `*` 后的 16 进制比较
> 3. **分字段**：`strtok_r` 按 `,` 切割（用 `_r` 可重入版本，多线程安全）
> 4. **坐标换算**：NMEA 的 `ddmm.mmmm` 格式 → 度 = 整数部分 + 小数部分/60
> 5. **状态过滤**：RMC 的 status 字段 `A=有效, V=无效`；GGA 的 fix 字段 `0=未定位`

**Q8：RT-Thread 跟裸机比有什么好处？**
> ①多任务并发：WiFi 连接、UART 接收、传感器轮询、LCD 刷新可以独立用线程写，不用手搓状态机；②设备抽象：所有外设统一 `find/open/read/write/control` 接口；③IPC：信号量/邮箱/消息队列开箱即用；④自动初始化机制：减少 `main` 里的初始化堆栈；⑤组件丰富：lwIP、AT command、MQTT 都有 RT-Thread package 直接接入。

**Q9：你工程里有什么不足或可以优化的地方？**
> ①UART4 轮询是固定时间片，**忙通道也只能等下一轮**，可以改成"侦测到该通道无数据就立即切换"的事件驱动模型；②解析数据目前只 LOG 输出，没回灌到上层业务（应做成发布订阅或回调注册）；③`memmove` 在长缓冲下是 O(n) 拷贝，高码率下可换环形缓冲；④没有看门狗喂狗逻辑；⑤错误恢复全靠重启，缺少自愈监控。

---

## 第二部分 · 完整工程报告（深问扛压版）

### 1. 工程总体架构

```
┌───────────────────────────────────────────────────────────────┐
│  applications/  应用层 (业务逻辑)                              │
│  ┌──────────┐ ┌────────┐ ┌─────────┐ ┌──────────┐            │
│  │app_startup│ │ network│ │uart4_bus│ │  sensors │  ...       │
│  └──────────┘ └────────┘ └─────────┘ └──────────┘            │
├───────────────────────────────────────────────────────────────┤
│  RT-Thread Kernel  (线程/IPC/调度/Device 框架/lwIP/MQTT)       │
├───────────────────────────────────────────────────────────────┤
│  board/  BSP + CubeMX 生成的 HAL 驱动 + 板级初始化             │
├───────────────────────────────────────────────────────────────┤
│  STM32F407 硬件 (UART × 4, SPI, I2C, GPIO, DMA, TIM ...)       │
└───────────────────────────────────────────────────────────────┘
```

构建工具：**SCons**（RT-Thread 标配），`applications/SConscript` 自动扫描子目录：

```python
modules = ['app_startup','config','esp32','gps','network',
           'uart4_bus','sensors','display','devices']
for module in modules:
    src += Glob(os.path.join(module,'src','*.c'))
    CPPPATH.append(os.path.join(module,'inc'))
```

每个模块只需新建 `inc/`、`src/` 目录就能加入构建，**零侵入式扩展**，符合开闭原则（OCP）。

### 2. 启动流程（最关键的入口）

#### 2.1 系统启动链

```
Reset_Handler
    └─ SystemInit (clock 时钟树)
        └─ __main / startup.s (栈初始化, .data .bss)
            └─ rtthread_startup()                           [RT-Thread 内核入口]
                ├─ rt_hw_board_init()                       [BOARD_EXPORT 段]
                ├─ rt_show_version()
                ├─ rt_system_heap_init()
                ├─ rt_system_scheduler_init()
                ├─ rt_application_init()  → main thread
                ├─ rt_system_timer_init()
                └─ rt_system_scheduler_start()              [开始调度]
                        ↓
                    main_thread_entry
                        └─ rt_components_init()             [遍历 INIT_xxx_EXPORT]
                        └─ main()                            [用户的 main()]
```

`rt_components_init` 会按段顺序遍历这 6 个区间，调用所有注册的函数：

| 等级 | 宏 | 用途 |
|------|-----|------|
| 1 | `INIT_BOARD_EXPORT` | 板级（如时钟、引脚复用） |
| 2 | `INIT_PREV_EXPORT` | 驱动前置依赖 |
| 3 | `INIT_DEVICE_EXPORT` | 设备驱动（如 UART） |
| 4 | `INIT_COMPONENT_EXPORT` | 组件（DFS、NTP） |
| 5 | `INIT_ENV_EXPORT` | 环境（变量） |
| 6 | `INIT_APP_EXPORT` | **应用** ← 我的入口在这层 |

#### 2.2 应用注册表 (`app_startup.c`)

```c
typedef struct {
    const char *name;
    int (*init)(void);
    rt_bool_t require_wifi;
    rt_uint8_t priority;
} app_start_item_t;

static const app_start_item_t app_start_items[] = {
    {"mqtt",   mqtt_app_start,  RT_TRUE,  0},
    {"uart4",  uart4_app_init,  RT_FALSE, 10},
    {"sensor", sensor_app_init, RT_FALSE, 20},
    {"lcd",    lcd_app_init,    RT_FALSE, 30},
    {"uart3",  uart_app_init,   RT_FALSE, 40},   // ESP32 通信
    {"gps",    atgm336h_init,   RT_FALSE, 50},
    {"ws2812", ws2812_init,     RT_FALSE, 60},
};
```

**设计要点**：
- 把模块当成数据（表驱动设计），而不是硬编码 `xxx_init(); yyy_init();`
- 增删模块只改一行表项，符合 **DRY + OCP**
- `require_wifi` 字段让 WiFi 失败时可以**优雅降级**而不是整机卡死

#### 2.3 启动线程实现

```c
static void app_startup_entry(void *parameter) {
    wifi_rw007_init();
    rt_err_t r = wifi_rw007_wait_ready(APP_START_WIFI_TIMEOUT);
    rt_bool_t wifi_ready = (r == RT_EOK);
    if (!wifi_ready)
        LOG_W("wifi not ready in %d ms, continue", APP_START_WIFI_TIMEOUT_MS);

    for (size_t i = 0; i < ARRAY_SIZE(app_start_items); i++)
        app_start_run_item(&app_start_items[i], wifi_ready);
}

int app_startup_init(void) {
    rt_thread_t th = rt_thread_create("app_start", app_startup_entry,
                                       NULL, 2048, 17, 20);
    rt_thread_startup(th);
    return RT_EOK;
}
INIT_APP_EXPORT(app_startup_init);  // ← 整个工程唯一的自动初始化入口
```

**为什么 stack=2048 priority=17 tick=20？**
- 2KB 栈：要调用 WiFi/MQTT/printf 等深调用栈，且本地缓冲较大
- 优先级 17：RT-Thread 默认优先级数 32，越小越高。17 在中等偏低，避免阻塞中断驱动的高优先级业务
- tick=20：时间片，无关本场景（FIFO 调度时几乎不用）

### 3. UART4 多路复用机制（工程最大亮点）

#### 3.1 硬件背景

STM32F407 的 UART 资源有限，但需求是：
- 3 路 LD2451 毫米波雷达（左/中/右探测车辆）
- 1 路 RC522 RFID（卡片识别）
- 共 4 路 UART 设备，板上只剩 UART4 可用

→ 加一片 **CD4052** 模拟开关，把 4 路 TX/RX 通过 2 根选择线复用到一根 UART4 上。

```
       MCU UART4_TX ─┐                  ┌─→ Y0 → LD2451_0 RX
                     ├─→ [CD4052] ─────┤
                     │   sel=AB        ├─→ Y1 → LD2451_1 RX
       MCU PIN_A ───→│                  ├─→ Y2 → LD2451_2 RX
       MCU PIN_B ───→│                  └─→ Y3 → RC522 RX
                     └─ (反向类似)
```

#### 3.2 软件 TDMA 调度

```c
static void uart4_thread_entry(void *parameter) {
    uart4_mux_select(0);                    // 初始通道
    while (1) {
        for (channel = 0; channel < 4; channel++) {
            uart4_mux_select(channel);      // 切 GPIO，含 settle 延时
            if (channel < RADAR_CHANNELS)
                ld2451_parser_reset(channel);  // 复位帧状态机
            uart4_flush();                  // 丢弃切换瞬间脏字节

            end_tick = now + HOLD_MS;
            while (now < end_tick) {
                rx_len = rt_device_read(uart4_dev, 0, buf, sizeof(buf));
                if (rx_len == 0)
                    rt_sem_take(&uart4_rx_sem, ms2tick(20));
                else
                    uart4_dispatch_rx(channel, buf, rx_len);
            }
        }
    }
}
```

#### 3.3 通道分发器

```c
static void uart4_dispatch_rx(rt_uint8_t ch, const rt_uint8_t *buf, rt_size_t len) {
    if (ch == UART4_APP_RFID_CHANNEL) {
        rc522_parser_input(ch, buf, len);   // 通道 3 → RFID 解析
        return;
    }
    if (ch < UART4_APP_RADAR_CHANNELS) {
        ld2451_parser_input(ch, buf, len);  // 通道 0/1/2 → 雷达解析
    }
}
```

**符合 SOLID 中的 SRP（单一职责）**：
- `uart4_mux.c` 只管 GPIO 切换
- `uart4_app.c` 只管调度和读串口
- `ld2451_parser.c` 只管协议解析
- `rc522_parser.c` 只管 RFID 解析

### 4. 数据解析机制

#### 4.1 LD2451 二进制帧解析（状态机 + 滑动缓冲）

**帧格式**：

```
偏移  内容           说明
0     F4 F3 F2 F1    帧头（4 字节固定）
4     LEN_L LEN_H    payload 长度（2B 小端）
6     payload[LEN]   ── target_count(1B) alarm(1B) target0(5B) target1(5B) ...
6+LEN F8 F7 F6 F5    帧尾（4 字节固定）
```

target_slot（5B）= `[angle_raw(1B)][distance_m(1B)][dir(1B)][speed_kmh(1B)][snr(1B)]`
- angle: `raw - 0x80` → 有符号角度（°）
- dir: `0x01`=接近, 其他=远离

**解析状态机核心循环**：

```c
static void ld2451_parse_stream(uint8_t ch) {
    while (stream_len[ch] >= FRAME_MIN_LEN) {
        size_t hp = find_header(stream, stream_len[ch]);    // ① 找帧头
        if (hp == stream_len[ch]) {                          // 没找到
            consume(ch, stream_len[ch] - 3);                 // 保留 3 字节防截断
            return;
        }
        if (hp > 0) { consume(ch, hp); continue; }           // 丢前缀

        uint16_t pl = u16le(&stream[4]);                     // ② 读长度
        size_t frame_len = 4 + 2 + pl + 4;
        if (stream_len[ch] < frame_len) return;              // ③ 等数据齐

        if (memcmp(&stream[6+pl], frame_tail, 4) != 0) {     // ④ 校验帧尾
            consume(ch, 1);                                   // 错位 → 丢 1 字节再找
            continue;
        }

        uint8_t target_count = stream[6];                    // ⑤ 解析负载
        if (pl != 2 + 5*target_count) {                      //    长度自洽性检查
            consume(ch, 1); continue;
        }
        ld2451_log_frame(ch, &stream[6], pl);
        consume(ch, frame_len);                               // ⑥ 消耗整帧
    }
}
```

**鲁棒性技巧**：
- 多次校验（帧头 + 帧尾 + 长度自洽）→ 防干扰
- 失败丢 1 字节而不是整帧 → 抗错位漂移
- 每通道独立缓冲 → 切换通道互不影响
- `consume` 用 `memmove` 而不是清零 → 跨切换保留半帧数据

#### 4.2 ATGM336H GPS 的 NMEA 解析（行缓冲 + 异或校验）

NMEA 是 ASCII 协议，处理方式跟二进制帧完全不同：

```c
// 接收线程：逐字节组装行
for (i = 0; i < rx_len; i++) {
    char c = (char)rx_buf[i];
    if (c == '\r') continue;
    if (c == '\n') {
        line_buf[line_len] = '\0';
        atgm_parse_sentence(line_buf);   // 完整一行交给解析
        line_len = 0;
    } else if (line_len < LINE_BUF_SIZE-1) {
        line_buf[line_len++] = c;
    } else {
        line_len = 0;                     // 溢出丢弃
    }
}
```

**校验和算法**（NMEA 标准）：

```c
// 对 $...*XX 中间的字符串做异或
for (p = sentence+1; p < star; p++) checksum ^= *p;
expected = strtol(after_star, NULL, 16);   // *XX 是 16 进制
return checksum == expected;
```

**字段解析**：`$GNRMC,hhmmss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed,course,date,...,*XX`

```c
// 用 strtok_r（可重入，多线程安全）按逗号切
token = strtok_r(sentence, ",", &saveptr);
while (token && cnt < 16) { fields[cnt++] = token;
    token = strtok_r(NULL, ",", &saveptr); }

// ddmm.mmmm → degree
double raw = atof(coord);        // 如 3114.5066 = 31°14.5066'
int deg = (int)(raw / 100.0);    // 31
double min = raw - deg*100.0;    // 14.5066
return deg + min/60.0;           // 31.24177...
```

### 5. RT-Thread 内核机制使用清单

#### 5.1 线程 (Thread)

```c
rt_thread_t th = rt_thread_create(name, entry, param, stack, prio, tick);
rt_thread_startup(th);
```

- 抢占式调度（priority 越小越高）
- 同优先级时间片轮转
- 阻塞 API：`rt_sem_take` / `rt_mq_recv` / `rt_thread_mdelay` 会主动让出 CPU

#### 5.2 信号量 (Semaphore) → ISR 与线程同步

```c
struct rt_semaphore uart4_rx_sem;
rt_sem_init(&uart4_rx_sem, "u4rx", 0, RT_IPC_FLAG_FIFO);  // 初值 0

// 中断回调（来自串口 ISR）
static rt_err_t uart4_rx_ind(rt_device_t dev, rt_size_t size) {
    rt_sem_release(&uart4_rx_sem);   // ★ ISR 安全
    return RT_EOK;
}

// 线程侧
rt_sem_take(&uart4_rx_sem, ms2tick(20));  // 阻塞等中断
```

**这就是生产者-消费者模型**：ISR 是生产者（数据可用通知），线程是消费者（实际处理）。

#### 5.3 设备框架 (I/O Device)

统一抽象，任何外设都是这套接口：

```c
rt_device_t dev = rt_device_find("uart4");           // 按名查找
rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, &cfg); // 改波特率等
rt_device_set_rx_indicate(dev, cb);                  // 注册中断回调
rt_size_t n = rt_device_read(dev, 0, buf, len);      // 读数据
```

**好处**：业务代码不依赖具体 HAL，可移植到任何 RT-Thread 平台。

#### 5.4 DMA 接收

`RT_DEVICE_FLAG_DMA_RX` 让底层 HAL 配置 DMA 把 UART RX FIFO 自动搬到环形缓冲，**CPU 只在缓冲达到阈值/IDLE 时被中断一次**，比中断里一字节一字节读高效几十倍。

#### 5.5 日志组件

```c
#define DBG_TAG "uart4.app"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>
LOG_I(...);  LOG_W(...);  LOG_E(...);
```

- 编译期可裁剪（`DBG_LVL` 控制）
- 自动加 TAG 前缀，多模块日志可区分
- 内部走 console 设备，可重定向到 UART/网络/文件

### 6. 设计原则的具体体现（面试可点名）

| 原则 | 工程体现 |
|------|----------|
| **KISS** | 启动注册表用数组，不上工厂模式 |
| **YAGNI** | 没有为"未来扩展更多通道"过度抽象，需要时再改 `MUX_CHANNELS` 宏 |
| **DRY** | 4 路雷达共用同一份解析器（按 channel 索引缓冲数组），不复制代码 |
| **SRP** | mux/调度/解析分别在不同文件 |
| **OCP** | SConscript 自动扫描，新模块零侵入接入；启动表加一行即可注册 |
| **DIP** | 应用层只依赖 `rt_device_*` 抽象接口，不依赖 HAL 细节 |

### 7. 可能的追问 & 备答

**Q：信号量初值为 0 还是 1？区别？**
> 0 = 同步用（等事件），1 = 互斥用（保护资源）。我这里是 0，等中断"事件"到达。

**Q：为什么不用消息队列把字节传给解析线程？**
> 队列拷贝有开销，且 UART 已有底层环形缓冲。我让接收线程直接调用解析函数（同线程同步调用），延迟更低、栈复用更省内存。

**Q：CD4052 切换会不会丢数据？**
> 会。所以每次切换后 `uart4_flush` 主动丢掉切换瞬间的字节，并 `ld2451_parser_reset` 复位状态机。HOLD_MS 设得足够长，覆盖 LD2451 一个完整数据包周期（约 100ms 输出一次），确保至少抓到一帧。

**Q：怎么保证 4 路雷达都有机会被处理？**
> 严格 round-robin，固定时间片，公平但不高效。改进方向：用 GPIO 中断检测 RX 边沿来主动切换（但 mux 切换有 settle 时间，效果有限）。

**Q：ld2451 用 `memmove` 不会有性能问题吗？**
> 缓冲 256B 级别、波特率 115200（最大 14.4 KB/s），单次 memmove < 1µs，远小于一帧周期，完全够用。如果上 MB/s 级码率才需要换环形缓冲。

**Q：为啥 `INIT_APP_EXPORT(uart4_app_init)` 那行被注释了？**
> 因为已经被 `app_startup` 注册表统一管理。注释保留是为了说明历史与切换方式，便于回退到独立自启动模式。

**Q：MQTT 失败怎么办？**
> `app_startup_entry` 里 `wifi_ready==FALSE` 时跳过 mqtt 但继续启动其他模块，整机降级为"本地感知+LCD 显示"，不上云。

---

## 第三部分 · 快速 30 秒自我陈述（背熟用）

> 这是基于 **STM32F407 + RT-Thread** 的多模态感知节点。我负责应用层架构和数据采集。整体设计有 3 个亮点：
>
> ① **模块化启动注册表**：用一个 `INIT_APP_EXPORT` 入口拉起 `app_startup` 线程，线程内按数据驱动方式遍历模块表，支持 WiFi 依赖和优先级控制，比裸用 RT-Thread 自动初始化更灵活。
>
> ② **UART4 时分多路复用**：硬件用 CD4052 模拟开关 + 2 根 GPIO 选择线，把 3 路毫米波雷达和 1 路 RFID 复用到一根串口。软件做 TDMA 轮询，每通道驻留固定时间片，独立缓冲和状态机，互不污染。
>
> ③ **RT-Thread 设备框架 + 信号量 + DMA**：所有串口走"中断释放信号量 → 线程阻塞读"的生产者-消费者模型，CPU 利用率极低。协议解析做了二进制（LD2451 帧头帧尾长度）和 ASCII（NMEA 异或校验）两套，状态机有抗错位漂移设计。
>
> 整个工程贯彻 KISS/YAGNI/DRY/SOLID，新增模块零侵入接入，符合嵌入式工程的可维护性要求。

---

_祝面试上岸！由本小姐的代码理解陪你战斗，怎么可能输！（双马尾甩了一下）_ (*￣︶￣)
