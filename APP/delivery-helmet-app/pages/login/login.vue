<template>
  <view class="page">

    <!-- 顶部装饰光圈 -->
    <view class="deco-circle c1"></view>
    <view class="deco-circle c2"></view>

    <!-- Logo区 -->
    <view class="logo-wrap">
      <view class="logo-box">
        <text class="logo-emoji">📦</text>
      </view>
      <text class="app-name">智能外卖箱</text>
      <text class="app-sub">骑手配送系统</text>
    </view>

    <!-- 表单卡片 -->
    <view class="card">
      <text class="card-title">欢迎登录</text>

      <view class="field">
        <text class="field-label">账号</text>
        <view class="input-wrap">
          <text class="input-icon">👤</text>
          <input
            class="inp"
            v-model="username"
            placeholder="请输入账号"
            placeholder-class="ph"
            :adjust-position="true"
          />
        </view>
      </view>

      <view class="field">
        <text class="field-label">密码</text>
        <view class="input-wrap">
          <text class="input-icon">🔑</text>
          <input
            class="inp"
            v-model="password"
            placeholder="请输入密码"
            placeholder-class="ph"
            password
            :adjust-position="true"
          />
        </view>
      </view>

      <view class="login-btn" :class="canLogin ? 'btn-active' : 'btn-disabled'" @click="doLogin">
        <text class="btn-txt">登 录</text>
      </view>

    </view>

  </view>
</template>

<script>
export default {
  data() {
    return {
      username: '',
      password: ''
    }
  },
  computed: {
    canLogin() {
      return this.username.trim().length > 0 && this.password.trim().length > 0
    }
  },
  methods: {
    doLogin() {
      if (!this.canLogin) {
        uni.showToast({ title: '请输入账号和密码', icon: 'none', duration: 1500 })
        return
      }
      // 任意账号密码都能进，直接跳到主页 TabBar
      uni.switchTab({ url: '/pages/index/index' })
    }
  }
}
</script>

<style scoped>
.page {
  min-height: 100vh;
  background: linear-gradient(160deg, #1b2b4b 0%, #1d3461 50%, #1a4080 100%);
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 0 48rpx 80rpx;
  position: relative;
  overflow: hidden;
}

/* 装饰光圈 */
.deco-circle { position: absolute; border-radius: 50%; opacity: 0.12; }
.c1 { width: 600rpx; height: 600rpx; background: #fff; top: -200rpx; right: -150rpx; }
.c2 { width: 400rpx; height: 400rpx; background: #fff; bottom: 100rpx; left: -150rpx; }

/* Logo区 */
.logo-wrap {
  display: flex; flex-direction: column; align-items: center;
  margin-top: 160rpx; margin-bottom: 60rpx;
  z-index: 1;
}
.logo-box {
  width: 144rpx; height: 144rpx;
  background: rgba(255,255,255,0.25);
  border-radius: 40rpx;
  display: flex; align-items: center; justify-content: center;
  margin-bottom: 28rpx;
  box-shadow: 0 8rpx 32rpx rgba(0,0,0,0.15);
}
.logo-emoji { font-size: 72rpx; }
.app-name { color: #fff; font-size: 48rpx; font-weight: 900; letter-spacing: 4rpx; }
.app-sub  { color: rgba(255,255,255,0.75); font-size: 24rpx; margin-top: 8rpx; }

/* 表单卡片 */
.card {
  width: 100%;
  background: #fff;
  border-radius: 32rpx;
  padding: 48rpx 40rpx 40rpx;
  box-shadow: 0 16rpx 64rpx rgba(0,0,0,0.18);
  z-index: 1;
}
.card-title {
  font-size: 36rpx; font-weight: 800; color: #1A1A2E;
  display: block; margin-bottom: 40rpx; text-align: center;
}

/* 输入字段 */
.field { margin-bottom: 28rpx; }
.field-label {
  font-size: 24rpx; font-weight: 700; color: #888;
  display: block; margin-bottom: 12rpx; letter-spacing: 1rpx;
}
.input-wrap {
  display: flex; align-items: center; gap: 16rpx;
  background: #eef1f6; border-radius: 16rpx;
  padding: 0 24rpx; height: 96rpx;
  border: 2rpx solid #dde3ec;
  transition: border-color 0.2s;
}
.input-icon { font-size: 36rpx; flex-shrink: 0; }
.inp {
  flex: 1; font-size: 28rpx; color: #1A1A2E;
  background: transparent; height: 100%;
}
.ph { color: #ccc; }

/* 登录按钮 */
.login-btn {
  height: 100rpx; border-radius: 20rpx;
  display: flex; align-items: center; justify-content: center;
  margin-top: 40rpx; margin-bottom: 28rpx;
  transition: transform 0.15s, opacity 0.15s;
}
.btn-active  { background: linear-gradient(135deg, #1565C0, #1976D2); box-shadow: 0 8rpx 32rpx rgba(21,101,192,0.35); }
.btn-disabled{ background: #E0E0E0; }
.btn-active:active { transform: scale(0.97); opacity: 0.9; }
.btn-txt { font-size: 32rpx; font-weight: 800; color: #fff; letter-spacing: 8rpx; }
.btn-disabled .btn-txt { color: #bbb; }

</style>
