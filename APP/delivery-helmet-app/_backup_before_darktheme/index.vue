<template>
  <view class="page">
    <NotificationToast />

    <!-- 深色顶部 Header -->
    <view class="header">
      <view class="header-top">
        <view class="header-left">
          <text class="header-greeting">配送中心</text>
          <text class="header-time">{{ nowTime }}</text>
        </view>
        <view class="conn-badge" :class="store.device.connected?'badge-on':store.device.mockMode?'badge-mock':'badge-off'">
          <view class="badge-pulse" v-if="store.device.connected"></view>
          <view class="badge-dot"></view>
          <text class="badge-txt">{{ store.device.connected?'在线':store.device.mockMode?'模拟':'离线' }}</text>
        </view>
      </view>
      <!-- 统计数据行 -->
      <view class="stat-row">
        <view class="stat-item">
          <text class="stat-val">{{ store.orders.filter(o => o.status !== 'completed').length }}</text>
          <text class="stat-key">待配送</text>
        </view>
        <view class="stat-sep"></view>
        <view class="stat-item">
          <text class="stat-val" :class="urgentCount>0?'val-warn':'val-ok'">{{ urgentCount }}</text>
          <text class="stat-key">即将超时</text>
        </view>
        <view class="stat-sep"></view>
        <view class="stat-item">
          <text class="stat-val" :class="tempStatus.cls">{{ tempStatus.txt }}</text>
          <text class="stat-key">保温状态</text>
        </view>
      </view>
    </view>

    <!-- 报警弹窗 -->
    <!-- 订单卡片列表 -->
    <view class="list-wrap">
      <view class="list-header">
        <text class="list-title">配送任务</text>
        <view class="map-link" @click="goMap">
          <text class="map-link-txt">查看地图</text>
          <text class="map-link-arrow">›</text>
        </view>
      </view>

      <view v-for="o in store.orders.filter(o => o.status !== 'completed')" :key="o.id" class="order-card" :class="'card-'+o.status">
        <!-- 左侧彩色竖条 -->
        <view class="card-stripe" :class="'stripe-'+o.status"></view>

        <!-- 卡片内容 -->
        <view class="card-body">
          <!-- 顶行：序号 + 餐品 + 状态 -->
          <view class="card-row-top">
            <view class="order-num" :class="'num-'+o.status">
              <text class="num-txt">{{ o.clipId || '?' }}</text>
            </view>
            <text class="food-name">{{ o.foodName }}</text>
            <view class="status-pill" :class="'pill-'+o.status">
              <text class="pill-txt">{{ statusText(o.status) }}</text>
            </view>
          </view>

          <!-- 目标地址 -->
          <view class="dest-row">
            <text class="dest-icon">{{ o.status==='pending_pickup'?'🏪':'🏠' }}</text>
            <text class="dest-txt">{{ o.status==='pending_pickup'?o.shopName:o.address }}</text>
            <text class="dist-txt" v-if="o.distance">{{ fmtDist(o.distance) }}</text>
          </view>

          <!-- 底行：夹子 + 温度 + 倒计时 -->
          <view class="card-row-bottom">
            <view class="clip-tag" v-if="o.clipId_bound">
              <text class="clip-txt">📎 {{ o.clipId_bound }}号夹</text>
            </view>
            <view class="spacer"></view>
            <view class="countdown" :class="getTimerClass(o.deadline)">
              <text class="cd-val">{{ fmtCountdown(o.deadline) }}</text>
              <text class="cd-lbl">{{ getRemain(o.deadline)<=0?'超时':'剩余' }}</text>
            </view>
          </view>
        </view>
      </view>

      <view v-if="store.orders.length===0" class="empty-wrap">
        <text class="empty-icon">📭</text>
        <text class="empty-txt">暂无配送任务</text>
        <text class="empty-sub">等待管理员派单</text>
      </view>
    </view>

    <!-- 底部操作栏 -->
    <view class="action-bar">
      <view class="action-btn btn-nav" @click="goMap">
        <text class="action-icon">🗺️</text>
        <text class="action-txt">开始导航</text>
      </view>
      <view class="action-btn btn-device" @click="goDevice">
        <text class="action-icon">📊</text>
        <text class="action-txt">设备详情</text>
      </view>
    </view>

  </view>
</template>

<script>
import store from '@/utils/store.js'
export default {
  data() {
    return {
      store,
      now: Date.now(),      // 响应式时间戳，替代 $forceUpdate()
      nowTime: '',
      timer: null,          // 合并成一个计时器
      alarmedOrders: {}
    }
  },
  computed: {
    urgentCount() {
      // 依赖 this.now，每秒自动更新，不需要强制刷新
      return store.orders.filter(o => {
        const s = Math.floor((o.deadline - this.now) / 1000)
        return s > 0 && s <= 300
      }).length
    },
    tempStatus() {
      const temps = Object.values(store.device.temperatures).filter(t => t > 0)
      if (temps.length === 0) return { txt: '--', cls: 'val-gray' }
      const min = Math.min(...temps)
      if (min < 40) return { txt: '已凉透', cls: 'val-cold' }
      if (min < 60) return { txt: '偏低', cls: 'val-warn' }
      return { txt: '正常', cls: 'val-ok' }
    }
  },
  onLoad() {
    this.now = Date.now()
    this.nowTime = new Date().toLocaleTimeString()
    // 合并为一个计时器，减少性能开销
    this.timer = setInterval(() => {
      this.now = Date.now()
      this.nowTime = new Date().toLocaleTimeString()
      store.orders.forEach(o => {
        if (this.getRemain(o.deadline) <= 0 && !this.alarmedOrders[o.id]) {
          this.alarmedOrders[o.id] = true
          store.triggerAlarm('⏰ 订单超时', o.shopName + ' 已超时！请加快配送！', 'warning')
        }
      })
    }, 1000)
  },
  onUnload() {
    clearInterval(this.timer)
  },
  methods: {
    // 用 this.now 而不是 Date.now()，让 Vue 知道该更新哪些组件
    getRemain(d) { return Math.floor((d - this.now) / 1000) },
    fmtCountdown(d) {
      const s = this.getRemain(d), abs = Math.abs(s)
      return (s<0?'+':'') + Math.floor(abs/60) + ':' + String(abs%60).padStart(2,'0')
    },
    getTimerClass(d) { const s=this.getRemain(d); return s<=0?'cd-over':s<=300?'cd-warn':'cd-ok' },
    fmtDist(m) { return !m?'':m<1000?m+'m':(m/1000).toFixed(1)+'km' },
    statusText(s) {
      return { pending_pickup:'待取餐', delivering:'配送中', arriving:'即将到达', completed:'已送达' }[s] || s
    },
    goMap()    { uni.switchTab({ url: '/pages/map/map' }) },
    goDevice() { uni.switchTab({ url: '/pages/device/device' }) }
  }
}
</script>

<style scoped>
.page { background:#F5F6FA; min-height:100vh; padding-bottom:160rpx; }

/* ===== Header ===== */
.header { background:linear-gradient(160deg,#FF6B35 0%,#FF8C42 100%); padding:56rpx 32rpx 32rpx; }
.header-top { display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:32rpx; }
.header-greeting { color:#fff; font-size:44rpx; font-weight:800; display:block; letter-spacing:2rpx; }
.header-time { color:rgba(255,255,255,0.7); font-size:24rpx; display:block; margin-top:6rpx; }

/* 连接状态徽章 */
.conn-badge { display:flex; align-items:center; gap:8rpx; padding:10rpx 20rpx; border-radius:30rpx; position:relative; }
.badge-on   { background:rgba(255,255,255,0.25); border:1rpx solid rgba(255,255,255,0.5); }
.badge-mock { background:rgba(255,255,255,0.15); border:1rpx solid rgba(255,255,255,0.3); }
.badge-off  { background:rgba(0,0,0,0.1); border:1rpx solid rgba(255,255,255,0.2); }
.badge-dot  { width:12rpx; height:12rpx; border-radius:50%; }
.badge-on .badge-dot   { background:#fff; }
.badge-mock .badge-dot { background:rgba(255,255,255,0.7); }
.badge-off .badge-dot  { background:rgba(255,255,255,0.4); }
.badge-pulse { position:absolute; left:20rpx; width:12rpx; height:12rpx; border-radius:50%; background:rgba(255,255,255,0.5); animation:pulse 1.5s infinite; }
.badge-txt { font-size:22rpx; font-weight:700; color:#fff; }

/* 统计行 */
.stat-row { display:flex; background:rgba(255,255,255,0.2); border-radius:20rpx; padding:24rpx 0; }
.stat-item { flex:1; display:flex; flex-direction:column; align-items:center; gap:6rpx; }
.stat-sep  { width:1rpx; background:rgba(255,255,255,0.3); margin:8rpx 0; }
.stat-val  { font-size:36rpx; font-weight:800; color:#fff; }
.stat-key  { font-size:20rpx; color:rgba(255,255,255,0.7); }
.val-ok   { color:#fff !important; }
.val-warn { color:#FFF3CD !important; }
.val-cold { color:#B3E5FC !important; }
.val-gray { color:rgba(255,255,255,0.5) !important; }

/* ===== 订单列表 ===== */
.list-wrap { padding:24rpx 24rpx 0; }
.list-header { display:flex; justify-content:space-between; align-items:center; margin-bottom:20rpx; }
.list-title  { color:#1A1A2E; font-size:30rpx; font-weight:800; }
.map-link    { display:flex; align-items:center; gap:4rpx; }
.map-link-txt   { color:#FF6B35; font-size:24rpx; }
.map-link-arrow { color:#FF6B35; font-size:28rpx; }

/* 订单卡片 */
.order-card { display:flex; background:#fff; border-radius:20rpx; margin-bottom:16rpx; overflow:hidden; box-shadow:0 2rpx 16rpx rgba(0,0,0,0.06); transition:box-shadow 0.3s ease, transform 0.2s ease; }
.order-card:active { transform:scale(0.98); }
.card-arriving { box-shadow:0 0 0 3rpx rgba(233,30,99,0.3); animation:card-blink 1s infinite; }

.card-stripe { width:6rpx; flex-shrink:0; }
.stripe-pending_pickup { background:#FF6B35; }
.stripe-delivering     { background:#FF9800; }
.stripe-arriving       { background:#E91E63; }
.stripe-completed      { background:#4CAF50; }

.card-body { flex:1; padding:24rpx 24rpx 20rpx; }

.card-row-top { display:flex; align-items:center; gap:16rpx; margin-bottom:14rpx; }
.order-num { width:48rpx; height:48rpx; border-radius:14rpx; display:flex; align-items:center; justify-content:center; flex-shrink:0; }
.num-pending_pickup { background:#FFF3EE; }
.num-delivering     { background:#FFF8E1; }
.num-arriving       { background:#FCE4EC; }
.num-completed      { background:#E8F5E9; }
.num-txt { font-size:26rpx; font-weight:800; color:#FF6B35; }
.num-delivering .num-txt { color:#FF9800; }
.num-arriving .num-txt   { color:#E91E63; }
.num-completed .num-txt  { color:#4CAF50; }

.food-name { flex:1; font-size:30rpx; font-weight:700; color:#1A1A2E; }

.status-pill { padding:6rpx 16rpx; border-radius:20rpx; }
.pill-pending_pickup { background:#FFF3EE; }
.pill-delivering     { background:#FFF8E1; }
.pill-arriving       { background:#FCE4EC; }
.pill-completed      { background:#E8F5E9; }
.pill-txt { font-size:20rpx; font-weight:700; }
.pill-pending_pickup .pill-txt { color:#FF6B35; }
.pill-delivering .pill-txt     { color:#FF9800; }
.pill-arriving .pill-txt       { color:#E91E63; }
.pill-completed .pill-txt      { color:#4CAF50; }

.dest-row { display:flex; align-items:center; gap:10rpx; margin-bottom:16rpx; }
.dest-icon { font-size:24rpx; }
.dest-txt  { flex:1; font-size:24rpx; color:#888; }
.dist-txt  { font-size:22rpx; color:#FF6B35; font-weight:700; }

.card-row-bottom { display:flex; align-items:center; gap:12rpx; }
.clip-tag { background:#E3F2FD; padding:6rpx 14rpx; border-radius:10rpx; }
.clip-txt { font-size:20rpx; color:#1976D2; font-weight:600; }
.temp-tag { background:#F5F5F5; padding:6rpx 14rpx; border-radius:10rpx; }
.temp-txt { font-size:20rpx; font-weight:600; }
.t-ok   { color:#4CAF50; }
.t-warn { color:#FF9800; }
.t-cold { color:#1976D2; }
.spacer { flex:1; }

.countdown { display:flex; flex-direction:column; align-items:center; padding:8rpx 16rpx; border-radius:12rpx; min-width:96rpx; }
.cd-ok   { background:#E8F5E9; }
.cd-warn { background:#FFF8E1; }
.cd-over { background:#FFEBEE; }
.cd-val  { font-size:28rpx; font-weight:800; }
.cd-ok .cd-val   { color:#4CAF50; }
.cd-warn .cd-val { color:#FF9800; }
.cd-over .cd-val { color:#F44336; }
.cd-lbl  { font-size:16rpx; color:#bbb; }

/* 空状态 */
.empty-wrap { display:flex; flex-direction:column; align-items:center; padding:100rpx 0; }
.empty-icon { font-size:80rpx; margin-bottom:20rpx; }
.empty-txt  { font-size:30rpx; color:#ccc; font-weight:700; }
.empty-sub  { font-size:24rpx; color:#ddd; margin-top:8rpx; }

/* 底部操作栏 */
.action-bar { position:fixed; bottom:0; left:0; right:0; background:#fff; padding:20rpx 24rpx 50rpx; display:flex; gap:16rpx; border-top:1rpx solid #F0F0F0; box-shadow:0 -4rpx 20rpx rgba(0,0,0,0.06); }
.action-btn { flex:1; display:flex; align-items:center; justify-content:center; gap:12rpx; padding:26rpx 0; border-radius:18rpx; transition:opacity 0.15s ease, transform 0.15s ease; }
.action-btn:active { opacity:0.85; transform:scale(0.97); }
.btn-nav    { background:linear-gradient(135deg,#FF6B35,#FF8C42); }
.btn-device { background:#F5F6FA; border:1rpx solid #EBEBEB; }
.action-icon { font-size:32rpx; }
.action-txt  { font-size:28rpx; font-weight:700; }
.btn-nav .action-txt    { color:#fff; }
.btn-device .action-txt { color:#555; }

@keyframes pulse { 0%{transform:scale(1);opacity:0.8} 70%{transform:scale(2.5);opacity:0} 100%{transform:scale(1);opacity:0} }
@keyframes card-blink { 0%,100%{border-color:rgba(233,30,99,0.4)} 50%{border-color:rgba(233,30,99,0.9)} }
</style>
