#pragma once
#include <string>

/**
 * SensorUploader
 *
 * 功能：
 *   1. 通过 UART 接收 STM32 上报的传感器/NFC 数据
 *   2. 将数据发布到 MQTT broker（broker.emqx.io）
 *   3. 将来自 APP/网页的到达指令通过 UART 转发给 STM32
 *   4. 在特定事件发生时，通过小智AI云端 TTS 主动播报语音提示
 *
 * 调用方式：
 *   在板子初始化时调用一次 SensorUploader::Start() 即可
 *   需要主动播报时调用 SensorUploader::SayText("要说的内容")
 */
class SensorUploader {
public:
    // 启动传感器上报和MQTT通信
    static void Start();

    // 主动播报语音（通过小智AI云端TTS），说完自动关闭聆听
    // 线程安全，可从任意 FreeRTOS 任务调用
    static void SayText(const std::string& text);

    // 播报并保持聆听（跌倒确认专用）
    // 说完后不关闭聆听通道，等待用户回答，由MCP工具或超时定时器决定后续
    static void SayAndListen(const std::string& text);
};
