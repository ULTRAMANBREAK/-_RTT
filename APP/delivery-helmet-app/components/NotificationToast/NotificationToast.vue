<template>
  <view class="nt-wrap">
    <view
      v-for="t in store.toasts"
      :key="t.id"
      class="nt"
      :class="['nt-' + t.level, t.hiding ? 'nt-out' : 'nt-in']"
      @click="dismiss(t.id)"
    >
      <!-- 左侧彩色竖条 -->
      <view class="nt-stripe" :class="'nts-' + t.level"></view>

      <!-- 大号 emoji 图标 -->
      <view class="nt-icon-wrap">
        <text class="nt-emoji">{{ t.emoji }}</text>
      </view>

      <!-- 标题 + 正文 -->
      <view class="nt-body">
        <text class="nt-title">{{ t.titleText }}</text>
        <text class="nt-msg" v-if="t.message">{{ t.message }}</text>
      </view>

      <!-- 关闭按钮 -->
      <view class="nt-close" @click.stop="dismiss(t.id)">
        <text class="nt-close-txt">✕</text>
      </view>

      <!-- 底部进度条：随倒计时耗尽 -->
      <view
        class="nt-prog"
        :class="'ntp-' + t.level"
        :style="{ animationDuration: t.duration + 'ms' }"
      ></view>
    </view>
  </view>
</template>

<script>
import store from '@/utils/store.js'
export default {
  data() { return { store } },
  methods: {
    dismiss(id) {
      const t = store.toasts.find(t => t.id === id)
      if (!t || t.hiding) return
      t.hiding = true
      setTimeout(() => {
        const idx = store.toasts.findIndex(t => t.id === id)
        if (idx !== -1) store.toasts.splice(idx, 1)
      }, 350)
    }
  }
}
</script>

<style scoped>
/* ===== 容器：固定在顶部，不拦截底部点击 ===== */
.nt-wrap {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  z-index: 9999;
  padding: calc(var(--status-bar-height) + 20rpx) 24rpx 0;
  pointer-events: none;
}

/* ===== 单条通知卡片 ===== */
.nt {
  display: flex;
  align-items: stretch;
  background: rgba(20, 5, 50, 0.92);
  border: 1rpx solid rgba(255,255,255,0.14);
  border-radius: 24rpx;
  margin-bottom: 16rpx;
  box-shadow:
    0 8rpx 40rpx rgba(0, 0, 0, 0.50),
    0 2rpx 6rpx  rgba(0, 0, 0, 0.30);
  overflow: hidden;
  pointer-events: all;
  position: relative;
}

/* ===== 进入/离开动画 ===== */
.nt-in  { animation: nt-in  0.45s cubic-bezier(0.34, 1.56, 0.64, 1) both; }
.nt-out { animation: nt-out 0.30s cubic-bezier(0.4,  0,    1,    1) both; }

@keyframes nt-in {
  0%   { transform: translateY(-120%); opacity: 0; }
  100% { transform: translateY(0);     opacity: 1; }
}
@keyframes nt-out {
  0%   { transform: translateY(0);     opacity: 1; max-height: 260rpx; margin-bottom: 16rpx; }
  100% { transform: translateY(-120%); opacity: 0; max-height: 0;      margin-bottom: 0; }
}

/* ===== 左侧彩色竖条 ===== */
.nt-stripe { width: 8rpx; flex-shrink: 0; }
.nts-danger  { background: linear-gradient(180deg, #FF5252 0%, #C62828 100%); }
.nts-warning { background: linear-gradient(180deg, #FFB300 0%, #E65100 100%); }
.nts-info    { background: linear-gradient(180deg, #42A5F5 0%, #1565C0 100%); }

/* ===== 卡片按等级的极淡背景着色 ===== */
.nt-danger  { background: rgba(20,5,50,0.92); border-color: rgba(248,113,113,0.30); }
.nt-warning { background: rgba(20,5,50,0.92); border-color: rgba(251,191,36,0.30); }
.nt-info    { background: rgba(20,5,50,0.92); border-color: rgba(167,139,250,0.30); }

/* ===== Emoji 图标区 ===== */
.nt-icon-wrap {
  width: 90rpx;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  padding: 28rpx 0;
}
.nt-emoji { font-size: 54rpx; line-height: 1; }

/* ===== 文字内容区 ===== */
.nt-body {
  flex: 1;
  padding: 26rpx 12rpx 26rpx 4rpx;
  min-width: 0;
  justify-content: center;
  display: flex;
  flex-direction: column;
}
.nt-title {
  font-size: 32rpx;
  font-weight: 800;
  color: rgba(255,255,255,0.95);
  display: block;
  line-height: 1.4;
}
.nt-msg {
  font-size: 26rpx;
  color: rgba(255,255,255,0.55);
  display: block;
  margin-top: 8rpx;
  line-height: 1.6;
  overflow: hidden;
}

/* ===== 关闭按钮 ===== */
.nt-close {
  width: 68rpx;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  padding-right: 4rpx;
}
.nt-close-txt { font-size: 32rpx; color: rgba(255,255,255,0.30); }

/* ===== 底部进度条 ===== */
.nt-prog {
  position: absolute;
  bottom: 0;
  left: 0;
  height: 5rpx;
  width: 100%;
  transform-origin: left center;
  animation: nt-prog linear both;
}
@keyframes nt-prog {
  from { transform: scaleX(1); }
  to   { transform: scaleX(0); }
}
.ntp-danger  { background: rgba(198, 40, 40, 0.35); }
.ntp-warning { background: rgba(230, 81, 0, 0.35); }
.ntp-info    { background: rgba(21, 101, 192, 0.35); }
</style>
