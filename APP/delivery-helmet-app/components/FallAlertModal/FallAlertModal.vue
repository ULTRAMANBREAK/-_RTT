<template>
  <view class="fall-overlay" v-if="store.fallAlert.visible">
    <!-- 背景脉冲圆圈 -->
    <view class="pulse-ring r1"></view>
    <view class="pulse-ring r2"></view>
    <view class="pulse-ring r3"></view>

    <!-- 警报主体 -->
    <view class="fall-box">
      <!-- 顶部闪烁红条 -->
      <view class="flash-bar"></view>

      <view class="fall-inner">
        <!-- 大警告图标 -->
        <view class="icon-wrap">
          <text class="icon-emoji">⚠️</text>
        </view>

        <!-- 标题 -->
        <text class="fall-title">跌倒警报！</text>
        <text class="fall-sub">已检测到骑手跌倒</text>
        <text class="fall-sub2">请立即确认骑手安全状况！</text>

        <!-- 分割线 -->
        <view class="divider"></view>

        <!-- 附加信息 -->
        <view class="info-row">
          <text class="info-dot">●</text>
          <text class="info-txt">设备已自动记录事件时间</text>
        </view>
        <view class="info-row">
          <text class="info-dot">●</text>
          <text class="info-txt">管理员端已同步收到警报</text>
        </view>

        <!-- 确认按钮 -->
        <view class="confirm-btn" @click="confirm">
          <text class="confirm-txt">✓ 骑手安全，解除警报</text>
        </view>
      </view>

      <!-- 底部闪烁红条 -->
      <view class="flash-bar"></view>
    </view>
  </view>
</template>

<script>
import store from '@/utils/store.js'
export default {
  data() { return { store } },
  methods: {
    confirm() { store.closeFallAlert() }
  }
}
</script>

<style scoped>
/* ===== 全屏遮罩 ===== */
.fall-overlay {
  position: fixed; top: 0; left: 0; right: 0; bottom: 0;
  z-index: 99999;
  background: rgba(100, 0, 0, 0.88);
  display: flex; align-items: center; justify-content: center;
  animation: overlay-in 0.3s ease both;
}
@keyframes overlay-in {
  from { opacity: 0; } to { opacity: 1; }
}

/* ===== 脉冲背景圆圈 ===== */
.pulse-ring {
  position: absolute;
  border-radius: 50%;
  background: rgba(255, 50, 50, 0.15);
  animation: ring-pulse 2s ease-out infinite;
}
.r1 { width: 300rpx; height: 300rpx; animation-delay: 0s; }
.r2 { width: 500rpx; height: 500rpx; animation-delay: 0.4s; }
.r3 { width: 700rpx; height: 700rpx; animation-delay: 0.8s; }
@keyframes ring-pulse {
  0%   { transform: scale(0.8); opacity: 0.8; }
  100% { transform: scale(1.6); opacity: 0; }
}

/* ===== 警报主体卡片 ===== */
.fall-box {
  width: 660rpx;
  background: #1a0000;
  border-radius: 32rpx;
  border: 3rpx solid #ff3333;
  overflow: hidden;
  box-shadow: 0 0 60rpx rgba(255, 0, 0, 0.5), 0 0 120rpx rgba(255, 0, 0, 0.25);
  animation: box-shake 0.5s ease both, border-pulse 1.5s ease-in-out infinite 0.5s;
}
@keyframes box-shake {
  0%,100% { transform: translateX(0); }
  20%     { transform: translateX(-12rpx); }
  40%     { transform: translateX(12rpx); }
  60%     { transform: translateX(-8rpx); }
  80%     { transform: translateX(8rpx); }
}
@keyframes border-pulse {
  0%,100% { box-shadow: 0 0 40rpx rgba(255,0,0,0.4), 0 0 80rpx rgba(255,0,0,0.2); }
  50%     { box-shadow: 0 0 80rpx rgba(255,0,0,0.8), 0 0 160rpx rgba(255,0,0,0.4); }
}

/* 顶/底闪烁红条 */
.flash-bar {
  height: 8rpx;
  background: linear-gradient(90deg, #ff0000, #ff6b6b, #ff0000);
  background-size: 200% 100%;
  animation: bar-flow 1s linear infinite;
}
@keyframes bar-flow {
  from { background-position: 0% 0%; }
  to   { background-position: 200% 0%; }
}

/* ===== 内容区 ===== */
.fall-inner {
  padding: 48rpx 48rpx 40rpx;
  display: flex; flex-direction: column; align-items: center;
}

/* 图标 */
.icon-wrap {
  width: 160rpx; height: 160rpx;
  background: rgba(255, 50, 50, 0.2);
  border-radius: 50%;
  display: flex; align-items: center; justify-content: center;
  margin-bottom: 32rpx;
  animation: icon-pulse 1s ease-in-out infinite;
}
@keyframes icon-pulse {
  0%,100% { transform: scale(1); background: rgba(255,50,50,0.2); }
  50%     { transform: scale(1.1); background: rgba(255,50,50,0.4); }
}
.icon-emoji { font-size: 88rpx; line-height: 1; }

/* 标题文字 */
.fall-title {
  font-size: 72rpx; font-weight: 900;
  color: #ff3333; letter-spacing: 4rpx;
  text-align: center; display: block;
  animation: text-blink 0.8s ease-in-out infinite;
}
@keyframes text-blink {
  0%,100% { opacity: 1; } 50% { opacity: 0.6; }
}
.fall-sub {
  font-size: 36rpx; font-weight: 700;
  color: rgba(255,255,255,0.9);
  text-align: center; display: block;
  margin-top: 16rpx;
}
.fall-sub2 {
  font-size: 26rpx; color: rgba(255,255,255,0.6);
  text-align: center; display: block;
  margin-top: 10rpx;
}

/* 分割线 */
.divider { width: 100%; height: 1rpx; background: rgba(255,50,50,0.3); margin: 36rpx 0 28rpx; }

/* 附加信息 */
.info-row { display: flex; align-items: center; gap: 12rpx; margin-bottom: 14rpx; align-self: flex-start; }
.info-dot  { font-size: 16rpx; color: #ff3333; }
.info-txt  { font-size: 24rpx; color: rgba(255,255,255,0.5); }

/* 确认按钮 */
.confirm-btn {
  margin-top: 40rpx; width: 100%;
  background: linear-gradient(135deg, #c62828, #ef5350);
  border-radius: 20rpx; padding: 30rpx 0;
  display: flex; align-items: center; justify-content: center;
  box-shadow: 0 4rpx 20rpx rgba(198,40,40,0.5);
}
.confirm-btn:active { opacity: 0.8; transform: scale(0.98); }
.confirm-txt {
  font-size: 32rpx; font-weight: 800;
  color: #fff; letter-spacing: 2rpx;
}
</style>
