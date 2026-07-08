<template>
  <view class="page">
    <NotificationToast />

    <view class="header">
      <view class="header-top">
        <text class="header-title">设备监控</text>
        <view class="live-badge">
          <view class="live-dot"></view>
          <text class="live-txt">LIVE</text>
        </view>
      </view>
      <text class="header-sub">{{ store.device.lastUpdate ? '最后更新 ' + store.device.lastUpdate : '等待数据...' }}</text>
    </view>

    <!-- 温度仪表盘（箱体四角）-->
    <view class="section-title-row">
      <text class="section-title">箱体四角温度</text>
    </view>
    <!-- 2×2 示意图布局：前左/前右 上排，后左/后右 下排 -->
    <view class="temp-grid-2x2">
      <view v-for="(temp, id) in store.device.temperatures" :key="id"
        class="temp-gauge-card">
        <view class="gauge-top">
          <text class="gauge-id">{{ cornerName(id) }}</text>
          <view class="gauge-status-dot dot-idle"></view>
        </view>
        <view class="gauge-ring-wrap">
          <text class="gauge-val">{{ temp > 0 ? temp : '--' }}</text>
          <text class="gauge-unit">{{ temp > 0 ? '℃' : '' }}</text>
        </view>
        <view class="gauge-bar-bg">
          <view class="gauge-bar-fill bar-hot"
            :style="{ width: Math.min(100, temp/80*100) + '%' }"></view>
        </view>
      </view>
    </view>

    <!-- 传感器状态 -->
    <view class="section-title-row">
      <text class="section-title">传感器状态</text>
    </view>
    <view class="sensor-grid">
      <view class="sensor-card" :class="store.device.antiTheft==='alarm'?'sensor-alarm':''">
        <view class="sensor-icon-wrap" :class="store.device.antiTheft==='alarm'?'icon-alarm':'icon-normal'">
          <text class="sensor-emoji">🔒</text>
        </view>
        <text class="sensor-name">防盗锁</text>
        <text class="sensor-val" :class="store.device.antiTheft==='alarm'?'s-red':'s-green'">
          {{ store.device.antiTheft==='alarm' ? '报警!' : '安全' }}
        </text>
      </view>
      <view class="sensor-card" :class="store.device.fallDetect==='alarm'?'sensor-alarm':''">
        <view class="sensor-icon-wrap" :class="store.device.fallDetect==='alarm'?'icon-alarm':'icon-normal'">
          <text class="sensor-emoji">🏍️</text>
        </view>
        <text class="sensor-name">跌倒检测</text>
        <text class="sensor-val" :class="store.device.fallDetect==='alarm'?'s-red':'s-green'">
          {{ store.device.fallDetect==='alarm' ? '报警!' : '正常' }}
        </text>
      </view>
      <view class="sensor-card">
        <view class="sensor-icon-wrap" :class="store.device.connected?'icon-online':'icon-offline'">
          <text class="sensor-emoji">📡</text>
        </view>
        <text class="sensor-name">MQTT</text>
        <text class="sensor-val" :class="store.device.connected?'s-green':'s-orange'">
          {{ store.device.connected ? '已连接' : (store.device.mockMode?'模拟':'断开') }}
        </text>
      </view>
      <view class="sensor-card">
        <view class="sensor-icon-wrap icon-normal">
          <text class="sensor-emoji">🕐</text>
        </view>
        <text class="sensor-name">最后更新</text>
        <text class="sensor-val s-blue">{{ store.device.lastUpdate || '--' }}</text>
      </view>
    </view>

    <!-- 演示测试 -->
    <view class="section-title-row">
      <text class="section-title">演示测试</text>
    </view>
    <view class="test-grid">
      <view class="test-card test-red" @click="testAlarm('fall')">
        <text class="test-emoji">🏍️</text>
        <text class="test-name">跌倒报警</text>
      </view>
      <view class="test-card test-red" @click="testAlarm('theft')">
        <text class="test-emoji">🚨</text>
        <text class="test-name">防盗报警</text>
      </view>
      <view class="test-card test-blue" @click="testAlarm('temp')">
        <text class="test-emoji">🥶</text>
        <text class="test-name">低温报警</text>
      </view>
      <view class="test-card test-green" @click="resetAll()">
        <text class="test-emoji">✅</text>
        <text class="test-name">恢复正常</text>
      </view>
    </view>

  </view>
</template>

<script>
import store from '@/utils/store.js'
import { testAlarm, publishReset } from '@/utils/mqtt.js'
export default {
  data() { return { store } },
  methods: {
    testAlarm(type) { testAlarm(type) },
    resetAll() {
      store.device.antiTheft = 'normal'
      store.device.fallDetect = 'normal'
      store.device.temperatures = { 'FL': 0, 'FR': 0, 'BL': 0, 'BR': 0 }
      store.alarm.show = false
      // 同步告诉网页恢复正常
      publishReset()
    },
    // 四角名称：FL前左 FR前右 BL后左 BR后右
    cornerName(id) {
      const map = { 'FL': '前左', 'FR': '前右', 'BL': '后左', 'BR': '后右' }
      return map[id] || id
    },
  }
}
</script>

<style scoped>
.page { background:#F5F6FA; min-height:100vh; padding-bottom:60rpx; }

/* Header */
.header { background:linear-gradient(160deg,#FF6B35,#FF8C42); padding:56rpx 32rpx 32rpx; }
.header-top { display:flex; justify-content:space-between; align-items:center; margin-bottom:8rpx; }
.header-title { color:#fff; font-size:44rpx; font-weight:800; }
.header-sub   { color:rgba(255,255,255,0.75); font-size:24rpx; display:block; }
.live-badge { display:flex; align-items:center; gap:8rpx; background:rgba(255,255,255,0.2); border:1rpx solid rgba(255,255,255,0.4); padding:8rpx 16rpx; border-radius:20rpx; }
.live-dot { width:10rpx; height:10rpx; border-radius:50%; background:#fff; animation:pulse 1.2s infinite; }
.live-txt { font-size:20rpx; font-weight:800; color:#fff; letter-spacing:2rpx; }

/* 区块标题行 */
.section-title-row { display:flex; justify-content:space-between; align-items:center; padding:28rpx 32rpx 14rpx; }
.section-title { color:#333; font-size:26rpx; font-weight:700; }
.section-hint  { color:#bbb; font-size:22rpx; }

/* 温度卡片网格（2×2，对应箱体四角） */
.temp-grid-2x2 { display:grid; grid-template-columns:1fr 1fr; gap:16rpx; padding:0 24rpx; }
.temp-gauge-card { background:#fff; border-radius:20rpx; padding:20rpx 16rpx; box-shadow:0 2rpx 12rpx rgba(0,0,0,0.06); border:2rpx solid transparent; }
.card-hot  { border-color:rgba(76,175,80,0.3); }
.card-warm { border-color:rgba(255,152,0,0.3); }
.card-cold { border-color:rgba(25,118,210,0.3); background:#F3F8FF; }

.gauge-top { display:flex; justify-content:space-between; align-items:center; margin-bottom:16rpx; }
.gauge-id  { font-size:22rpx; color:#999; }
.gauge-status-dot { width:12rpx; height:12rpx; border-radius:50%; }
.dot-hot  { background:#4CAF50; box-shadow:0 0 8rpx rgba(76,175,80,0.5); }
.dot-warm { background:#FF9800; }
.dot-cold { background:#1976D2; animation:pulse 1s infinite; }
.dot-idle { background:#ddd; }

.gauge-ring-wrap { display:flex; align-items:baseline; justify-content:center; margin-bottom:16rpx; gap:4rpx; }
.gauge-val  { font-size:48rpx; font-weight:900; color:#1A1A2E; line-height:1; }
.gauge-unit { font-size:20rpx; color:#bbb; }

.gauge-bar-bg   { height:8rpx; background:#F0F0F0; border-radius:4rpx; overflow:hidden; margin-bottom:12rpx; }
.gauge-bar-fill { height:100%; border-radius:4rpx; transition:width 0.8s ease; }
.bar-hot  { background:linear-gradient(90deg,#81C784,#4CAF50); }
.bar-warm { background:linear-gradient(90deg,#FFB74D,#FF9800); }
.bar-cold { background:linear-gradient(90deg,#64B5F6,#1976D2); }

.gauge-label { font-size:20rpx; font-weight:700; display:block; text-align:center; }
.lbl-hot  { color:#4CAF50; }
.lbl-warm { color:#FF9800; }
.lbl-cold { color:#1976D2; }
.lbl-idle { color:#ccc; }

/* 传感器网格 */
.sensor-grid { display:grid; grid-template-columns:1fr 1fr; gap:16rpx; padding:0 24rpx; }
.sensor-card { background:#fff; border-radius:20rpx; padding:28rpx 20rpx; display:flex; flex-direction:column; align-items:center; gap:12rpx; box-shadow:0 2rpx 12rpx rgba(0,0,0,0.06); border:2rpx solid transparent; }
.sensor-alarm { border-color:rgba(244,67,54,0.4); background:#FFF5F5; }
.sensor-icon-wrap { width:72rpx; height:72rpx; border-radius:20rpx; display:flex; align-items:center; justify-content:center; }
.icon-normal  { background:#F5F6FA; }
.icon-alarm   { background:#FFEBEE; animation:shake 0.4s infinite; }
.icon-online  { background:#E8F5E9; }
.icon-offline { background:#F5F5F5; }
.sensor-emoji { font-size:36rpx; }
.sensor-name  { font-size:22rpx; color:#999; }
.sensor-val   { font-size:28rpx; font-weight:800; }
.s-green { color:#4CAF50; } .s-red { color:#F44336; } .s-orange { color:#FF9800; } .s-blue { color:#1976D2; }

/* 测试按钮 */
.test-grid { display:grid; grid-template-columns:1fr 1fr; gap:16rpx; padding:0 24rpx; }
.test-card { background:#fff; border-radius:20rpx; padding:32rpx 20rpx; display:flex; flex-direction:column; align-items:center; gap:14rpx; box-shadow:0 2rpx 12rpx rgba(0,0,0,0.06); border:2rpx solid transparent; }
.test-red  { border-color:rgba(244,67,54,0.2); }
.test-blue { border-color:rgba(25,118,210,0.2); }
.test-green{ border-color:rgba(76,175,80,0.2); }
.test-emoji { font-size:48rpx; }
.test-name  { font-size:26rpx; font-weight:700; }
.test-red .test-name   { color:#EF5350; }
.test-blue .test-name  { color:#1976D2; }
.test-green .test-name { color:#4CAF50; }

@keyframes pulse { 0%{opacity:1;transform:scale(1)} 50%{opacity:0.5;transform:scale(1.3)} 100%{opacity:1;transform:scale(1)} }
@keyframes shake  { 0%,100%{transform:rotate(0)} 25%{transform:rotate(-5deg)} 75%{transform:rotate(5deg)} }
</style>
