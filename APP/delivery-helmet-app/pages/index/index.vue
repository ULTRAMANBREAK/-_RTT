<template>
  <view class="page">
    <FallAlertModal />
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

      <!-- 天气条 -->
      <view class="weather-bar" v-if="weather.temp">
        <view class="weather-main">
          <text class="weather-emoji">{{ weather.emoji }}</text>
          <text class="weather-temp">{{ weather.temp }}°</text>
          <view class="weather-info">
            <text class="weather-cond">{{ weather.cond }}</text>
            <text class="weather-detail">{{ weather.wind }} · {{ weather.city }}</text>
          </view>
        </view>
        <text class="weather-humi">💧 {{ weather.humidity }}%</text>
      </view>
      <view class="weather-bar weather-loading" v-else>
        <text class="weather-loading-txt">🌡️ 获取天气中...</text>
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
import FallAlertModal from '@/components/FallAlertModal/FallAlertModal.vue'
export default {
  components: { FallAlertModal },
  data() {
    return {
      store,
      now: Date.now(),      // 响应式时间戳，替代 $forceUpdate()
      nowTime: '',
      timer: null,          // 合并成一个计时器
      alarmedOrders: {},
      weather: { temp: '', cond: '', emoji: '', wind: '', humidity: '', city: '' }
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

  },
  onLoad() {
    this.now = Date.now()
    this.nowTime = new Date().toLocaleTimeString()
    this.fetchWeather()
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
    goDevice() { uni.switchTab({ url: '/pages/device/device' }) },

    // ===== 天气 =====
    fetchWeather() {
      const KEY = '662e9fcefccaabf1d725fbf90a70b423'
      const applyWeather = (w, city) => {
        if (!w) return
        this.weather = {
          temp:     w.temperature,
          cond:     w.weather,
          emoji:    this.weatherEmoji(w.weather),
          wind:     w.winddirection + '风' + w.windpower + '级',
          humidity: w.humidity,
          city:     city || w.city || ''
        }
      }
      const fetchByAdcode = (adcode, city) => {
        uni.request({
          url: `https://restapi.amap.com/v3/weather/weatherInfo?key=${KEY}&city=${adcode}&extensions=base`,
          success: (r) => applyWeather(r.data?.lives?.[0], city)
        })
      }
      // 尝试 GPS 定位 → 逆地理编码 → 天气
      uni.getLocation({
        type: 'gcj02',
        success: (loc) => {
          uni.request({
            url: `https://restapi.amap.com/v3/geocode/regeo?key=${KEY}&location=${loc.longitude},${loc.latitude}&extensions=base`,
            success: (res) => {
              const comp   = res.data?.regeocode?.addressComponent
              const adcode = comp?.adcode
              const city   = comp?.city || comp?.province || ''
              if (adcode) fetchByAdcode(adcode, typeof city === 'string' ? city : city[0] || '')
              else        fetchByAdcode('421000', '荆州')  // 降级：荆州市
            },
            fail: () => fetchByAdcode('421000', '荆州')
          })
        },
        fail: () => fetchByAdcode('421000', '荆州')  // 定位失败降级
      })
    },
    weatherEmoji(cond) {
      if (!cond) return '🌡️'
      if (cond.includes('雷')) return '⛈️'
      if (cond.includes('雪')) return '❄️'
      if (cond.includes('雨')) return '🌧️'
      if (cond.includes('雾') || cond.includes('霾')) return '🌫️'
      if (cond.includes('多云')) return '⛅'
      if (cond.includes('阴')) return '🌥️'
      if (cond.includes('晴')) return '☀️'
      return '🌡️'
    }
  }
}
</script>

<style scoped>
.page { background:#eef1f6; min-height:100vh; padding-bottom:160rpx; }

/* ===== Header（深藏蓝，与网页导航栏一致）===== */
.header { background:linear-gradient(160deg,#1b2b4b 0%,#1d3461 100%); padding:56rpx 32rpx 32rpx; }
.header-top { display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:32rpx; }
.header-greeting { color:#fff; font-size:44rpx; font-weight:800; display:block; letter-spacing:2rpx; }
.header-time { color:rgba(255,255,255,0.6); font-size:24rpx; display:block; margin-top:6rpx; }

/* 连接状态徽章 */
.conn-badge { display:flex; align-items:center; gap:8rpx; padding:10rpx 20rpx; border-radius:30rpx; position:relative; }
.badge-on   { background:rgba(255,255,255,0.15); border:1rpx solid rgba(255,255,255,0.4); }
.badge-mock { background:rgba(255,255,255,0.10); border:1rpx solid rgba(255,255,255,0.25); }
.badge-off  { background:rgba(0,0,0,0.15); border:1rpx solid rgba(255,255,255,0.15); }
.badge-dot  { width:12rpx; height:12rpx; border-radius:50%; }
.badge-on .badge-dot   { background:#4ade80; }
.badge-mock .badge-dot { background:rgba(255,255,255,0.7); }
.badge-off .badge-dot  { background:rgba(255,255,255,0.3); }
.badge-pulse { position:absolute; left:20rpx; width:12rpx; height:12rpx; border-radius:50%; background:rgba(74,222,128,0.5); animation:pulse 1.5s infinite; }
.badge-txt { font-size:22rpx; font-weight:700; color:rgba(255,255,255,0.9); }

/* 天气条 */
.weather-bar { display:flex; align-items:center; justify-content:space-between; background:rgba(255,255,255,0.10); border:1rpx solid rgba(255,255,255,0.15); border-radius:20rpx; padding:20rpx 28rpx; margin-bottom:24rpx; }
.weather-main { display:flex; align-items:center; gap:18rpx; }
.weather-emoji { font-size:48rpx; }
.weather-temp  { font-size:52rpx; font-weight:900; color:#fff; line-height:1; }
.weather-info  { display:flex; flex-direction:column; gap:4rpx; }
.weather-cond  { font-size:26rpx; font-weight:700; color:rgba(255,255,255,0.95); }
.weather-detail { font-size:20rpx; color:rgba(255,255,255,0.55); }
.weather-humi  { font-size:22rpx; color:rgba(255,255,255,0.65); }
.weather-loading { justify-content:center; }
.weather-loading-txt { font-size:22rpx; color:rgba(255,255,255,0.5); }

/* 统计行 */
.stat-row { display:flex; background:rgba(255,255,255,0.12); border-radius:20rpx; padding:24rpx 0; border:1rpx solid rgba(255,255,255,0.1); }
.stat-item { flex:1; display:flex; flex-direction:column; align-items:center; gap:6rpx; }
.stat-sep  { width:1rpx; background:rgba(255,255,255,0.2); margin:8rpx 0; }
.stat-val  { font-size:36rpx; font-weight:800; color:#fff; }
.stat-key  { font-size:20rpx; color:rgba(255,255,255,0.6); }
.val-ok   { color:#fff !important; }
.val-warn { color:#fde68a !important; }

/* ===== 订单列表 ===== */
.list-wrap { padding:24rpx 24rpx 0; }
.list-header { display:flex; justify-content:space-between; align-items:center; margin-bottom:20rpx; }
.list-title  { color:#1a2942; font-size:30rpx; font-weight:800; }
.map-link    { display:flex; align-items:center; gap:4rpx; }
.map-link-txt   { color:#1565C0; font-size:24rpx; }
.map-link-arrow { color:#1565C0; font-size:28rpx; }

/* 订单卡片 */
.order-card { display:flex; background:#fff; border-radius:20rpx; margin-bottom:16rpx; overflow:hidden; border:1rpx solid #dde3ec; box-shadow:0 2rpx 12rpx rgba(0,0,0,0.06); transition:box-shadow 0.3s ease, transform 0.2s ease; }
.order-card:active { transform:scale(0.98); }
.card-arriving { box-shadow:0 0 0 3rpx rgba(198,40,40,0.25); animation:card-blink 1s infinite; }

.card-stripe { width:6rpx; flex-shrink:0; }
.stripe-pending_pickup { background:#1565C0; }
.stripe-delivering     { background:#e65100; }
.stripe-arriving       { background:#c62828; }
.stripe-completed      { background:#2e7d32; }

.card-body { flex:1; padding:24rpx 24rpx 20rpx; }

.card-row-top { display:flex; align-items:center; gap:16rpx; margin-bottom:14rpx; }
.order-num { width:48rpx; height:48rpx; border-radius:14rpx; display:flex; align-items:center; justify-content:center; flex-shrink:0; }
.num-pending_pickup { background:#eff6ff; }
.num-delivering     { background:#fff3e0; }
.num-arriving       { background:#ffebee; }
.num-completed      { background:#e8f5e9; }
.num-txt { font-size:26rpx; font-weight:800; color:#1565C0; }
.num-delivering .num-txt { color:#e65100; }
.num-arriving .num-txt   { color:#c62828; }
.num-completed .num-txt  { color:#2e7d32; }

.food-name { flex:1; font-size:30rpx; font-weight:700; color:#1a2942; }

.status-pill { padding:6rpx 16rpx; border-radius:20rpx; }
.pill-pending_pickup { background:#eff6ff; }
.pill-delivering     { background:#fff3e0; }
.pill-arriving       { background:#ffebee; }
.pill-completed      { background:#e8f5e9; }
.pill-txt { font-size:20rpx; font-weight:700; }
.pill-pending_pickup .pill-txt { color:#1565C0; }
.pill-delivering .pill-txt     { color:#e65100; }
.pill-arriving .pill-txt       { color:#c62828; }
.pill-completed .pill-txt      { color:#2e7d32; }

.dest-row { display:flex; align-items:center; gap:10rpx; margin-bottom:16rpx; }
.dest-icon { font-size:24rpx; }
.dest-txt  { flex:1; font-size:24rpx; color:#4a5a72; }
.dist-txt  { font-size:22rpx; color:#1565C0; font-weight:700; }

.card-row-bottom { display:flex; align-items:center; gap:12rpx; }
.clip-tag { background:#eff6ff; padding:6rpx 14rpx; border-radius:10rpx; }
.clip-txt { font-size:20rpx; color:#1565C0; font-weight:600; }
.temp-tag { background:#f5f5f5; padding:6rpx 14rpx; border-radius:10rpx; }
.temp-txt { font-size:20rpx; font-weight:600; }
.t-ok   { color:#2e7d32; }
.t-warn { color:#e65100; }
.t-cold { color:#1565C0; }
.spacer { flex:1; }

.countdown { display:flex; flex-direction:column; align-items:center; padding:8rpx 16rpx; border-radius:12rpx; min-width:96rpx; }
.cd-ok   { background:#e8f5e9; }
.cd-warn { background:#fff3e0; }
.cd-over { background:#ffebee; }
.cd-val  { font-size:28rpx; font-weight:800; }
.cd-ok .cd-val   { color:#2e7d32; }
.cd-warn .cd-val { color:#e65100; }
.cd-over .cd-val { color:#c62828; }
.cd-lbl  { font-size:16rpx; color:#bbb; }

/* 空状态 */
.empty-wrap { display:flex; flex-direction:column; align-items:center; padding:100rpx 0; }
.empty-icon { font-size:80rpx; margin-bottom:20rpx; }
.empty-txt  { font-size:30rpx; color:#bbb; font-weight:700; }
.empty-sub  { font-size:24rpx; color:#ccc; margin-top:8rpx; }

/* 底部操作栏 */
.action-bar { position:fixed; bottom:0; left:0; right:0; background:#fff; padding:20rpx 24rpx 50rpx; display:flex; gap:16rpx; border-top:1rpx solid #dde3ec; box-shadow:0 -2rpx 12rpx rgba(0,0,0,0.06); }
.action-btn { flex:1; display:flex; align-items:center; justify-content:center; gap:12rpx; padding:26rpx 0; border-radius:18rpx; transition:opacity 0.15s ease, transform 0.15s ease; }
.action-btn:active { opacity:0.85; transform:scale(0.97); }
.btn-nav    { background:linear-gradient(135deg,#1565C0,#1976D2); }
.btn-device { background:#eef1f6; border:1rpx solid #dde3ec; }
.action-icon { font-size:32rpx; }
.action-txt  { font-size:28rpx; font-weight:700; }
.btn-nav .action-txt    { color:#fff; }
.btn-device .action-txt { color:#4a5a72; }

@keyframes pulse { 0%{transform:scale(1);opacity:0.8} 70%{transform:scale(2.5);opacity:0} 100%{transform:scale(1);opacity:0} }
@keyframes card-blink { 0%,100%{box-shadow:0 0 0 3rpx rgba(198,40,40,0.2)} 50%{box-shadow:0 0 0 3rpx rgba(198,40,40,0.6)} }
</style>
