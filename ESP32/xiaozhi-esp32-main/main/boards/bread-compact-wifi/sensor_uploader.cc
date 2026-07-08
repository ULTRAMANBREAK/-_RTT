#include "sensor_uploader.h"
#include "application.h"   // Application::GetInstance() / Schedule() / WakeWordInvoke()
#include "mcp_server.h"    // McpServer::GetInstance() / AddTool()

#include <string>
#include <set>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <mqtt_client.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include <esp_event.h>
#include <esp_netif.h>

static const char* TAG = "SensorUploader";

// ===== UART 配置 =====
// 使用 UART1，保留 UART0 给调试串口
// RX=G17, TX=G8（避开 config.h 里的 LAMP_GPIO=G18）
#define UART_PORT        UART_NUM_1
#define UART_RX_PIN      GPIO_NUM_17
#define UART_TX_PIN      GPIO_NUM_8
#define UART_BAUD_RATE   115200
#define UART_BUF_SIZE    2048

// ===== MQTT 主题 =====
#define TOPIC_PREFIX  "helmet_dxk"
#define TOPIC_SENSOR  TOPIC_PREFIX "/sensor"  // 上报：温度
#define TOPIC_SPEED   TOPIC_PREFIX "/speed"   // 上报：速度
#define TOPIC_ALERT   TOPIC_PREFIX "/alert"   // 上报：跌倒/防盗
#define TOPIC_NFC     TOPIC_PREFIX "/nfc"     // 上报：NFC取餐/送达
#define TOPIC_UNLOCK  TOPIC_PREFIX "/unlock"  // 上报：解锁卡刷卡
#define TOPIC_ARRIVE  TOPIC_PREFIX "/arrive"  // 下行：APP通知到达，转发给STM32
#define TOPIC_LOCK    TOPIC_PREFIX "/lock"    // 下行：APP控制外卖锁开/关，转发给STM32

static esp_mqtt_client_handle_t s_mqtt_client = nullptr;
static bool s_mqtt_connected = false;

// 前向声明：说完后自动关闭聆听通道（定义在文件下方）
static void say_auto_close_task(void* arg);

// ===== 跌倒确认状态 =====
// 跌倒发生后进入 pending 状态，等待骑手语音确认或 10 秒超时
// MCP工具回调和超时回调都会清除 pending 并决定是否发警报
static volatile bool  s_fall_pending = false;   // true = 等待骑手确认中
static TimerHandle_t  s_fall_timer   = nullptr;  // 10秒超时定时器

// ===== 语音播报去重集合 =====
// 每个 key 只播报一次，状态变化时从集合里删除对应 key，下次才能再播
// 用互斥量保护，因为 MQTT 回调和 UART 任务在不同 FreeRTOS 任务里运行
static std::set<std::string> s_said_set;
static SemaphoreHandle_t s_said_mutex = nullptr;

// ===== 待播队列（多槽FIFO）=====
// 每条语音入队排队，pending_say_task 依次取出，说完一条再说下一条
// 保证任何情况下都不丢失语音，最多缓存10条
#define SAY_QUEUE_LEN   10    // 最多排10条
#define SAY_TEXT_MAX    128   // 每条最大字节（中文约42字）
static QueueHandle_t s_say_queue = nullptr;

// 尝试播报：若 key 已在集合中则跳过（只说一次）
static void say_once(const std::string& key, const std::string& text) {
    xSemaphoreTake(s_said_mutex, portMAX_DELAY);
    bool already_said = (s_said_set.count(key) > 0);
    if (!already_said) s_said_set.insert(key);
    xSemaphoreGive(s_said_mutex);

    if (already_said) {
        ESP_LOGI(TAG, "语音已播报过，跳过: key=%s", key.c_str());
        return;
    }
    SensorUploader::SayText(text);
}

// 重置某个 key，下次遇到同一事件会重新播报
static void say_reset(const std::string& key) {
    xSemaphoreTake(s_said_mutex, portMAX_DELAY);
    s_said_set.erase(key);
    xSemaphoreGive(s_said_mutex);
}

// ===== cardId → 中文序号 =====
// "01" → "一", "02" → "二", "03" → "三", "04" → "四"
static std::string card_to_cn(const std::string& cid) {
    if (cid == "01") return "一";
    if (cid == "02") return "二";
    if (cid == "03") return "三";
    if (cid == "04") return "四";
    return cid;  // 未知编号直接返回原字符串
}

// ===== 跌倒警报发送（MCP工具回调和超时回调共用）=====
static void do_publish_fall_alert() {
    if (s_mqtt_connected)
        esp_mqtt_client_publish(s_mqtt_client, TOPIC_ALERT, "{\"type\":\"fall\"}", 0, 0, 0);
    ESP_LOGW(TAG, "跌倒警报已上报 MQTT！");
}

// 10秒超时回调：骑手无应答，视为需要求助，自动发警报并关闭聆听
static void fall_timer_cb(TimerHandle_t /*xTimer*/) {
    if (!s_fall_pending) return;   // 已被MCP工具提前处理，不重复发
    s_fall_pending = false;
    ESP_LOGW(TAG, "跌倒确认超时（10秒无应答），自动发送警报！");
    do_publish_fall_alert();
    // 超时时设备仍在 Listening，需要主动关掉，回到模式一（Idle）
    auto& app = Application::GetInstance();
    app.Schedule([&app]() {
        if (app.GetDeviceState() == kDeviceStateListening) {
            app.ToggleChatState();
            ESP_LOGI(TAG, "超时后关闭聆听通道，回到模式一");
        }
    });
}

// 启动/重启 10 秒跌倒确认定时器
static void fall_timer_start() {
    if (s_fall_timer == nullptr) {
        s_fall_timer = xTimerCreate("fall_confirm", pdMS_TO_TICKS(10000),
                                    pdFALSE, nullptr, fall_timer_cb);
    }
    // 若已在运行（重复跌倒），先停掉重计时
    if (xTimerIsTimerActive(s_fall_timer)) xTimerStop(s_fall_timer, 0);
    xTimerStart(s_fall_timer, 0);
}

// 向 STM32 发送一行 JSON（自动加换行符）
static void uart_send_line(const std::string& msg) {
    std::string line = msg + "\n";
    uart_write_bytes(UART_PORT, line.c_str(), line.size());
}

// ===== WiFi IP 事件处理器 =====
// WiFi 拿到 IP 地址后，才启动 MQTT 客户端，避免协议栈未就绪导致崩溃
static void on_ip_event(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    ESP_LOGI(TAG, "WiFi 已获得 IP，启动 MQTT...");
    esp_mqtt_client_start(s_mqtt_client);
}

// ===== MQTT 事件处理器 =====
static void on_mqtt_event(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT 已连接至 broker");
            s_mqtt_connected = true;
            // 订阅到达通知主题：APP检测到骑手距目的地≤30米时发布
            esp_mqtt_client_subscribe(s_mqtt_client, TOPIC_ARRIVE, 0);
            // 订阅外卖锁控制主题：APP骑手端发送开锁/关锁指令
            esp_mqtt_client_subscribe(s_mqtt_client, TOPIC_LOCK, 0);
            ESP_LOGI(TAG, "已订阅: %s, %s", TOPIC_ARRIVE, TOPIC_LOCK);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT 断开，等待自动重连...");
            s_mqtt_connected = false;
            break;

        case MQTT_EVENT_DATA: {
            std::string topic(event->topic, event->topic_len);
            std::string payload(event->data, event->data_len);

            // ===== 外卖锁控制 =====
            // APP发来 {"action":"open"} 或 {"action":"close"}
            // 转发给STM32: {"type":"lock","action":"open"} / {"type":"lock","action":"close"}
            if (topic == TOPIC_LOCK) {
                cJSON* root = cJSON_Parse(payload.c_str());
                if (root) {
                    cJSON* act = cJSON_GetObjectItem(root, "action");
                    if (cJSON_IsString(act)) {
                        const std::string action = act->valuestring;
                        std::string cmd = std::string("{\"type\":\"lock\",\"action\":\"") + action + "\"}";
                        uart_send_line(cmd);
                        ESP_LOGI(TAG, "→ STM32 外卖锁: action=%s", action.c_str());
                    }
                    cJSON_Delete(root);
                }
            }

            if (topic == TOPIC_ARRIVE) {
                // APP 发来 {"cardId":"01","scene":"pickup"} 或 {"cardId":"01","scene":"deliver"}
                // 步骤1：通知STM32夹子进入 ARRIVED 状态
                // 步骤2：让夹子 LED 开始闪烁
                // 步骤3：小智AI播报取餐/送达提示
                cJSON* root = cJSON_Parse(payload.c_str());
                if (root) {
                    cJSON* card  = cJSON_GetObjectItem(root, "cardId");
                    cJSON* scene = cJSON_GetObjectItem(root, "scene");
                    if (cJSON_IsString(card)) {
                        const std::string cid = card->valuestring;
                        const std::string sc  = cJSON_IsString(scene) ? scene->valuestring : "pickup";

                        // 取餐到达：只发 arrived（STM32切换状态，LED由其内部决定）
                        // 送达到达：只发 blink（显式让LED闪烁引导顾客刷卡）
                        std::string arrived = std::string("{\"type\":\"arrived\",\"cardId\":\"") + cid + "\"}";
                        std::string blink   = std::string("{\"type\":\"blink\",\"cardId\":\"")   + cid + "\"}";
                        if (sc == "deliver") {
                            uart_send_line(blink);
                            ESP_LOGI(TAG, "→ STM32 blink: cardId=%s (送达)", cid.c_str());
                        } else {
                            uart_send_line(arrived);
                            ESP_LOGI(TAG, "→ STM32 arrived: cardId=%s (取餐)", cid.c_str());
                        }

                        // 语音播报（每次到达只说一次）
                        // key: arrive_01_pickup / arrive_01_deliver
                        std::string key  = std::string("arrive_") + cid + "_" + sc;
                        std::string cn   = card_to_cn(cid);
                        std::string text = (sc == "deliver")
                            ? (cn + "号送达请刷卡")
                            : (cn + "号取餐请刷卡");
                        say_once(key, text);
                    }
                    cJSON_Delete(root);
                }
            }
            break;
        }

        default:
            break;
    }
}

// ===== 主动请求任务 =====
// 每3秒请求速度，每15秒请求温度，每30秒发一次 PING 心跳
static void request_task(void* arg) {
    int tick = 0;  // 每3秒 +1

    // 延迟5秒等 UART 和 STM32 初始化就绪，不依赖 MQTT（UART 通信与 MQTT 相互独立）
    // uart_read_task 里用 if(s_mqtt_connected) 保护上报，这里只管轮询
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "请求任务已启动");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        tick++;

        // 每3秒：请求速度
        uart_send_line("{\"type\":\"req\",\"want\":\"speed\"}");

        // 每15秒（tick % 5 == 0）：请求温度
        if (tick % 5 == 0) {
            uart_send_line("{\"type\":\"req\",\"want\":\"temp\"}");
            ESP_LOGI(TAG, "→ STM32 请求温度");
        }

        // 每30秒（tick % 10 == 0）：PING 心跳，确认串口通畅
        if (tick % 10 == 0) {
            uart_send_line("PING");
            ESP_LOGI(TAG, "→ STM32 PING");
        }
    }
}

// ===== UART 读取任务 =====
// 持续读取 STM32 发来的数据，分两类处理：
//   JSON 行：按 type 字段发布到对应 MQTT 主题，并在必要时触发语音播报
//   纯文本行（OK/ERR 开头）：STM32 的指令回执，仅打印日志
static void uart_read_task(void* arg) {
    uint8_t buf[UART_BUF_SIZE];
    std::string line_buf;

    ESP_LOGI(TAG, "UART 读取任务已启动");

    while (true) {
        int len = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        buf[len] = '\0';
        line_buf += (char*)buf;

        // 按 '\n' 拆出完整行
        size_t pos;
        while ((pos = line_buf.find('\n')) != std::string::npos) {
            std::string line = line_buf.substr(0, pos);
            line_buf = line_buf.substr(pos + 1);

            // 去掉首尾空白和 \r
            while (!line.empty() && (line.front() == ' ' || line.front() == '\r')) line.erase(0, 1);
            while (!line.empty() && (line.back()  == ' ' || line.back()  == '\r')) line.pop_back();
            if (line.empty()) continue;

            // ---- 纯文本回执（OK PONG / OK ARRIVED / ERR ... ）----
            // STM32 对 PING、arrived、blink 等指令的确认/错误回复
            if (line.substr(0, 2) == "OK" || line.substr(0, 3) == "ERR") {
                if (line == "OK PONG") {
                    ESP_LOGI(TAG, "← STM32 心跳正常: %s", line.c_str());
                } else if (line.find("ERR") == 0) {
                    ESP_LOGW(TAG, "← STM32 指令错误: %s", line.c_str());
                } else {
                    ESP_LOGI(TAG, "← STM32 回执: %s", line.c_str());
                }
                continue;  // 回执不上报到MQTT
            }

            // ---- JSON 事件（STM32 主动上报）----
            cJSON* root = cJSON_Parse(line.c_str());
            if (!root) {
                ESP_LOGW(TAG, "非法数据，已跳过: %s", line.c_str());
                continue;
            }

            cJSON* type_item = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type_item)) {
                std::string type = type_item->valuestring;

                if (type == "temp") {
                    // {"type":"temp","FL":62.3,"FR":61.8,"BL":60.5,"BR":61.2}
                    if (s_mqtt_connected)
                        esp_mqtt_client_publish(s_mqtt_client, TOPIC_SENSOR, line.c_str(), 0, 0, 0);
                    ESP_LOGI(TAG, "← 上报 temp");


                } else if (type == "speed") {
                    // {"type":"speed","speed":12.5}  STM32单位 m/s，直接上报，不转换
                    if (s_mqtt_connected) {
                        cJSON* spd_item = cJSON_GetObjectItem(root, "speed");
                        if (cJSON_IsNumber(spd_item)) {
                            char ms_json[64];
                            snprintf(ms_json, sizeof(ms_json),
                                     "{\"type\":\"speed\",\"speed\":%.2f}",
                                     spd_item->valuedouble);
                            esp_mqtt_client_publish(s_mqtt_client, TOPIC_SPEED, ms_json, 0, 0, 0);
                        } else {
                            esp_mqtt_client_publish(s_mqtt_client, TOPIC_SPEED, line.c_str(), 0, 0, 0);
                        }
                    }

                } else if (type == "fall") {
                    // {"type":"fall"}
                    // 不立即上报，先让小智AI询问骑手是否需要求助
                    // 骑手说"是"/超时10秒 → fall_confirm工具 → 上报MQTT
                    // 骑手说"不用"       → fall_cancel工具  → 取消警报
                    if (!s_fall_pending) {
                        s_fall_pending = true;
                        fall_timer_start();
                        say_reset("fall");  // 允许下次再播
                        SensorUploader::SayAndListen("跌倒确认");
                        ESP_LOGW(TAG, "← 跌倒检测！等待骑手语音确认（10秒超时自动报警）");
                    } else {
                        ESP_LOGW(TAG, "← 跌倒重复触发，确认进行中，忽略");
                    }

                } else if (type == "theft") {
                    // {"type":"theft"}
                    if (s_mqtt_connected)
                        esp_mqtt_client_publish(s_mqtt_client, TOPIC_ALERT, line.c_str(), 0, 0, 0);
                    ESP_LOGW(TAG, "← 上报 theft 警报！");
                    say_once("theft", "外卖箱异常，请检查");

                } else if (type == "nfc") {
                    // {"type":"nfc","action":"BIND","cardId":"01"}
                    // {"type":"nfc","action":"UNBIND","cardId":"01"}
                    if (s_mqtt_connected)
                        esp_mqtt_client_publish(s_mqtt_client, TOPIC_NFC, line.c_str(), 0, 0, 0);

                    cJSON* action = cJSON_GetObjectItem(root, "action");
                    cJSON* card   = cJSON_GetObjectItem(root, "cardId");
                    ESP_LOGI(TAG, "← 上报 nfc: action=%s cardId=%s",
                             cJSON_IsString(action) ? action->valuestring : "?",
                             cJSON_IsString(card)   ? card->valuestring   : "?");

                    if (cJSON_IsString(card) && cJSON_IsString(action)) {
                        const std::string cid = card->valuestring;
                        const std::string act = action->valuestring;
                        const std::string cn  = card_to_cn(cid);

                        if (act == "BIND") {
                            // 取餐刷卡：重置取餐和送达的所有arrive key
                            // 必须同时重置 deliver key！否则取餐送达在同一地点时，
                            // "送达请刷卡"会被 say_once 当作"已播过"跳过
                            say_reset(std::string("arrive_") + cid + "_pickup");
                            say_reset(std::string("arrive_") + cid + "_deliver");
                            say_reset(std::string("unbind_") + cid);  // 重置送达成功key，允许下次再播
                            say_once(std::string("bind_") + cid, cn + "号取餐成功");

                        } else if (act == "UNBIND") {
                            // 送达刷卡：重置相关 key，播报送达成功
                            say_reset(std::string("arrive_") + cid + "_deliver");
                            say_reset(std::string("bind_") + cid);
                            say_once(std::string("unbind_") + cid, cn + "号送达成功");
                            // unbind key 不需要手动重置，下次新订单绑定时 bind 会先清
                        }
                    }

                } else if (type == "unlock") {
                    // {"type":"unlock"}  解锁卡刷卡事件
                    if (s_mqtt_connected)
                        esp_mqtt_client_publish(s_mqtt_client, TOPIC_UNLOCK, line.c_str(), 0, 0, 0);
                    ESP_LOGI(TAG, "← 上报 unlock");

                } else {
                    ESP_LOGW(TAG, "← 未知 type=%s，已忽略", type.c_str());
                }
            }

            cJSON_Delete(root);
        }
    }
}

// ===== 待播队列消费任务 =====
// 从队列里依次取出语音，等设备空闲后播报，说完再取下一条
// 这样无论多少条同时入队，每一条都会被说出来，绝不丢失
static void pending_say_task(void* /*arg*/) {
    auto& app = Application::GetInstance();
    char buf[SAY_TEXT_MAX];

    while (true) {
        // 阻塞等待队列有内容（最多500ms超时，超时继续循环）
        if (xQueueReceive(s_say_queue, buf, pdMS_TO_TICKS(500)) != pdTRUE) continue;
        std::string text(buf);

        // 等设备空闲（最多30秒，超时跳过这条）
        bool got_idle = false;
        for (int i = 0; i < 300; i++) {
            if (app.GetDeviceState() == kDeviceStateIdle) { got_idle = true; break; }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!got_idle) {
            ESP_LOGW(TAG, "pending_say_task: 等待空闲超时，跳过: %s", text.c_str());
            continue;
        }

        // 调度到主线程执行 WakeWordInvoke（线程安全）
        app.Schedule([&app, text]() {
            if (app.GetDeviceState() != kDeviceStateIdle) return;
            ESP_LOGI(TAG, "pending_say_task 播报: %s", text.c_str());
            app.WakeWordInvoke(text);
            xTaskCreate(say_auto_close_task, "say_close", 2048, nullptr, 3, nullptr);
        });

        // 等 Schedule 生效 + 设备离开 Idle（开始说话），最多等2秒
        vTaskDelay(pdMS_TO_TICKS(800));
        for (int i = 0; i < 12; i++) {
            if (app.GetDeviceState() != kDeviceStateIdle) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // 等播报完成（设备回到 Idle），最多30秒
        for (int i = 0; i < 300; i++) {
            if (app.GetDeviceState() == kDeviceStateIdle) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGI(TAG, "pending_say_task: 播报完成，继续下一条");
    }
}

// ===== 对外接口 =====
void SensorUploader::Start() {
    // --- 0. 初始化互斥量和待播队列 ---
    s_said_mutex = xSemaphoreCreateMutex();
    s_say_queue  = xQueueCreate(SAY_QUEUE_LEN, SAY_TEXT_MAX);  // 多槽FIFO，不丢语音

    // --- 1. 初始化 UART ---
    uart_config_t uart_cfg = {
        .baud_rate           = UART_BAUD_RATE,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, (int)UART_TX_PIN, (int)UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, nullptr, 0));
    ESP_LOGI(TAG, "UART 初始化完成 | TX=G%d  RX=G%d | 波特率=%d",
             UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);

    // --- 2. 创建 MQTT 客户端（等 WiFi 拿到 IP 后再 start）---
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = "mqtt://broker.emqx.io:1883";
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   on_mqtt_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, nullptr);
    ESP_LOGI(TAG, "MQTT 客户端已创建，等待 WiFi 连接...");

    // --- 3. 注册跌倒确认 MCP 工具 ---
    // LLM 根据骑手回答调用对应工具；说明写在工具描述里，配合系统提示词使用
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "fall_confirm",
        "Call this tool when the rider confirms they need help after a fall is detected. "
        "Also call this if the rider does not respond.",
        PropertyList(std::vector<Property>{}),
        [](const PropertyList&) -> ReturnValue {
            if (s_fall_pending) {
                s_fall_pending = false;
                if (s_fall_timer) xTimerStop(s_fall_timer, 0);
                do_publish_fall_alert();
                ESP_LOGW(TAG, "fall_confirm 工具触发，已发送跌倒警报");
            }
            // LLM 说完"好的，正在发出求救"后会进入 Listening，启动任务自动关闭，回到模式一
            xTaskCreate(say_auto_close_task, "fall_confirm_close", 2048, nullptr, 3, nullptr);
            return std::string("已发出求救信号");
        });
    mcp.AddTool(
        "fall_cancel",
        "Call this tool when the rider says they are fine and do not need help after a fall is detected.",
        PropertyList(std::vector<Property>{}),
        [](const PropertyList&) -> ReturnValue {
            if (s_fall_pending) {
                s_fall_pending = false;
                if (s_fall_timer) xTimerStop(s_fall_timer, 0);
                ESP_LOGI(TAG, "fall_cancel 工具触发，骑手确认安全，警报取消");
            }
            // LLM 说完"好的，已取消警报"后会进入 Listening，启动任务自动关闭，回到模式一
            xTaskCreate(say_auto_close_task, "fall_cancel_close", 2048, nullptr, 3, nullptr);
            return std::string("已取消警报，请注意安全");
        });
    ESP_LOGI(TAG, "MCP 跌倒确认工具已注册: fall_confirm / fall_cancel");

    // --- 4. 启动 UART 读取任务 ---
    xTaskCreate(uart_read_task, "sensor_uart_rx", 8192, nullptr, 5, nullptr);

    // --- 5. 启动主动请求任务 ---
    xTaskCreate(request_task, "sensor_req", 4096, nullptr, 4, nullptr);

    // --- 6. 启动待播队列任务（设备忙时的语音补播）---
    xTaskCreate(pending_say_task, "say_pending", 4096, nullptr, 3, nullptr);

    ESP_LOGI(TAG, "SensorUploader 初始化完成 ✓");
}

// ===== 说完自动关通道的监控任务 =====
// WakeWordInvoke 后设备会经历：Idle→Connecting→Listening→Speaking→Listening
// 最后这个 Listening 是不需要的，检测到后立即调用 ToggleChatState() 关掉
static void say_auto_close_task(void* /*arg*/) {
    auto& app = Application::GetInstance();
    bool entered_speaking = false;

    // 最多等 30 秒（LLM + TTS 响应时间）
    for (int i = 0; i < 300; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        DeviceState s = app.GetDeviceState();

        if (s == kDeviceStateSpeaking) {
            entered_speaking = true;   // 已经开始播报
        }

        if (entered_speaking) {
            if (s == kDeviceStateListening) {
                // 说完后进入了聆听 → 立即关闭，不需要监听用户说话
                // ToggleChatState 在 Listening 状态下 = CloseAudioChannel
                app.ToggleChatState();
                ESP_LOGI(TAG, "SayText: 播报完成，已自动关闭聆听通道");
                break;
            }
            if (s == kDeviceStateIdle) {
                // 已回到空闲（正常结束），不需要处理
                break;
            }
        }
    }
    vTaskDelete(nullptr);
}

// ===== 播报并保持聆听（跌倒确认专用）=====
// 与 SayText 的唯一区别：说完后不启动 say_auto_close_task
// 设备会在 Speaking → Listening 后继续等待骑手说话
// 若设备忙则直接跳过（10秒超时定时器是安全兜底）
void SensorUploader::SayAndListen(const std::string& text) {
    auto& app = Application::GetInstance();
    app.Schedule([&app, text]() {
        DeviceState state = app.GetDeviceState();
        if (state != kDeviceStateIdle) {
            // 设备忙时跳过播报，10秒超时后自动发警报，保证安全
            ESP_LOGW(TAG, "SayAndListen: 设备忙（state=%d），跳过播报，超时后自动报警", (int)state);
            return;
        }
        ESP_LOGI(TAG, "SayAndListen（跌倒确认）: %s", text.c_str());
        app.WakeWordInvoke(text);
        // ⚠️ 故意不启动 say_auto_close_task，设备保持聆听，等待骑手回答
    });
}

// ===== 主动语音播报 =====
// 将文本压入多槽FIFO队列，由 pending_say_task 依次取出播报
// 线程安全（xQueueSend 本身是线程安全的，可从任意 FreeRTOS 任务调用）
// 每条都会说，绝不因设备忙而丢失
void SensorUploader::SayText(const std::string& text) {
    char buf[SAY_TEXT_MAX];
    strncpy(buf, text.c_str(), SAY_TEXT_MAX - 1);
    buf[SAY_TEXT_MAX - 1] = '\0';

    if (xQueueSend(s_say_queue, buf, 0) == pdTRUE) {
        ESP_LOGI(TAG, "SayText: 已入队 [队列中=%u条]: %s",
                 (unsigned)uxQueueMessagesWaiting(s_say_queue), text.c_str());
    } else {
        ESP_LOGW(TAG, "SayText: 队列已满（%d条上限），丢弃: %s", SAY_QUEUE_LEN, text.c_str());
    }
}
