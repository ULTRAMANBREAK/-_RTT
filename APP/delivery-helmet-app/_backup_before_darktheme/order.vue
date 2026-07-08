<template>
  <view class="page">
    <NotificationToast />

    <view class="header">
      <text class="header-title">配送订单</text>
      <text class="header-sub">{{ store.orders.filter(o=>o.status!=='completed').length }} 单 · 共 {{ totalDist }} 路程</text>
    </view>

    <view v-for="o in store.orders.filter(o => o.status !== 'completed')" :key="o.id" class="order-card">
      <!-- 左侧竖条 -->
      <view class="card-stripe" :class="'stripe-'+o.status"></view>

      <view class="card-body">
        <!-- 顶行 -->
        <view class="top-row">
          <view class="order-num" :class="'num-'+o.status">
            <text class="num-txt">{{ o.clipId || '?' }}</text>
          </view>
          <view class="title-col">
            <text class="food-name">{{ o.foodName }}</text>
            <text class="shop-name">{{ o.shopName }}</text>
          </view>
          <view class="status-tag" :class="'tag-'+o.status">
            <text class="tag-txt">{{ statusText(o.status) }}</text>
          </view>
        </view>

        <!-- 路线可视化 -->
        <view class="route-block">
          <view class="route-node">
            <view class="node-icon pickup-icon"><text class="node-icon-txt">取</text></view>
            <view class="node-detail">
              <text class="node-type">取餐地点</text>
              <text class="node-name">{{ o.shopName }}</text>
            </view>
          </view>
          <view class="route-connector">
            <view class="connector-line"></view>
            <view class="connector-dot"></view>
            <view class="connector-line"></view>
          </view>
          <view class="route-node">
            <view class="node-icon deliver-icon"><text class="node-icon-txt">送</text></view>
            <view class="node-detail">
              <text class="node-type">送达地点</text>
              <text class="node-name">{{ o.address }}</text>
            </view>
          </view>
        </view>

        <!-- 底部信息 -->
        <view class="footer-row">
          <view class="info-chip chip-time" :class="getTimerClass(o.deadline)">
            <text class="chip-icon">⏰</text>
            <text class="chip-val">{{ fmtDeadline(o.deadline) }}</text>
            <view class="chip-cd">
              <text class="chip-cd-txt">{{ fmtCountdown(o.deadline) }}</text>
            </view>
          </view>
          <view class="info-chip chip-clip" v-if="o.clipId_bound">
            <text class="chip-icon">📎</text>
            <text class="chip-val">{{ o.clipId_bound }}号夹</text>
          </view>
        </view>
      </view>
    </view>

    <view v-if="store.orders.length===0" class="empty-wrap">
      <text class="empty-icon">📭</text>
      <text class="empty-txt">暂无配送订单</text>
    </view>

  </view>
</template>

<script>
import store from '@/utils/store.js'
export default {
  data() { return { store, timer: null } },
  computed: {
    totalDist() {
      const total = store.orders.reduce((s, o) => s + (o.distance || 0), 0)
      return total < 1000 ? total + 'm' : (total/1000).toFixed(1) + 'km'
    }
  },
  onLoad() { this.timer = setInterval(() => this.$forceUpdate(), 1000) },
  onUnload() { clearInterval(this.timer) },
  methods: {
    getRemain(d) { return Math.floor((d - Date.now()) / 1000) },
    fmtCountdown(d) {
      const s = this.getRemain(d), abs = Math.abs(s)
      return (s<0?'+':'') + Math.floor(abs/60) + ':' + String(abs%60).padStart(2,'0')
    },
    fmtDeadline(d) {
      const t = new Date(d)
      return t.getHours() + ':' + String(t.getMinutes()).padStart(2,'0')
    },
    getTimerClass(d) { const s=this.getRemain(d); return s<=0?'chip-over':s<=300?'chip-warn':'chip-ok' },
    statusText(s) {
      return { pending_pickup:'待取餐', delivering:'配送中', arriving:'即将到达', completed:'已送达' }[s] || s
    }
  }
}
</script>

<style scoped>
.page { background:#F5F6FA; min-height:100vh; padding-bottom:40rpx; }

.header { background:linear-gradient(160deg,#FF6B35,#FF8C42); padding:56rpx 32rpx 32rpx; }
.header-title { color:#fff; font-size:44rpx; font-weight:800; display:block; }
.header-sub   { color:rgba(255,255,255,0.75); font-size:24rpx; display:block; margin-top:8rpx; }

/* 订单卡片 */
.order-card { display:flex; background:#fff; margin:20rpx 24rpx 0; border-radius:20rpx; overflow:hidden; box-shadow:0 2rpx 16rpx rgba(0,0,0,0.06); }
.card-stripe { width:6rpx; flex-shrink:0; }
.stripe-pending_pickup { background:#FF6B35; }
.stripe-delivering     { background:#FF9800; }
.stripe-arriving       { background:#E91E63; }
.stripe-completed      { background:#4CAF50; }
.card-body { flex:1; padding:24rpx; }

/* 顶行 */
.top-row { display:flex; align-items:center; gap:16rpx; margin-bottom:20rpx; }
.order-num { width:52rpx; height:52rpx; border-radius:14rpx; display:flex; align-items:center; justify-content:center; flex-shrink:0; }
.num-pending_pickup { background:#FFF3EE; }
.num-delivering     { background:#FFF8E1; }
.num-arriving       { background:#FCE4EC; }
.num-completed      { background:#E8F5E9; }
.num-txt { font-size:26rpx; font-weight:800; color:#FF6B35; }
.num-delivering .num-txt { color:#FF9800; }
.num-arriving .num-txt   { color:#E91E63; }
.num-completed .num-txt  { color:#4CAF50; }
.title-col { flex:1; }
.food-name { font-size:30rpx; font-weight:800; color:#1A1A2E; display:block; }
.shop-name { font-size:22rpx; color:#bbb; display:block; margin-top:4rpx; }
.status-tag { padding:6rpx 16rpx; border-radius:20rpx; }
.tag-pending_pickup { background:#FFF3EE; }
.tag-delivering     { background:#FFF8E1; }
.tag-arriving       { background:#FCE4EC; animation:blink 0.8s infinite; }
.tag-completed      { background:#E8F5E9; }
.tag-txt { font-size:20rpx; font-weight:700; }
.tag-pending_pickup .tag-txt { color:#FF6B35; }
.tag-delivering .tag-txt     { color:#FF9800; }
.tag-arriving .tag-txt       { color:#E91E63; }
.tag-completed .tag-txt      { color:#4CAF50; }

/* 路线可视化 */
.route-block { background:#F8F9FC; border-radius:16rpx; padding:20rpx; margin-bottom:20rpx; }
.route-node  { display:flex; align-items:center; gap:16rpx; }
.route-connector { display:flex; align-items:center; padding:6rpx 0 6rpx 20rpx; }
.connector-line { flex:1; height:1rpx; background:#E8E8E8; }
.connector-dot  { width:8rpx; height:8rpx; border-radius:50%; background:#ddd; margin:0 8rpx; }
.node-icon { width:36rpx; height:36rpx; border-radius:10rpx; display:flex; align-items:center; justify-content:center; flex-shrink:0; }
.pickup-icon  { background:#FFF3EE; }
.deliver-icon { background:#E3F2FD; }
.node-icon-txt { font-size:18rpx; font-weight:800; }
.pickup-icon .node-icon-txt  { color:#FF6B35; }
.deliver-icon .node-icon-txt { color:#1976D2; }
.node-detail { flex:1; }
.node-type { font-size:20rpx; color:#bbb; display:block; }
.node-name { font-size:26rpx; color:#1A1A2E; font-weight:600; display:block; }

/* 底部信息chips */
.footer-row { display:flex; gap:12rpx; flex-wrap:wrap; }
.info-chip { display:flex; align-items:center; gap:8rpx; padding:8rpx 16rpx; border-radius:12rpx; }
.chip-icon { font-size:22rpx; }
.chip-val  { font-size:22rpx; font-weight:700; }
.chip-ok   { background:#E8F5E9; }
.chip-warn { background:#FFF8E1; }
.chip-over { background:#FFEBEE; }
.chip-ok .chip-val   { color:#4CAF50; }
.chip-warn .chip-val { color:#FF9800; }
.chip-over .chip-val { color:#F44336; }
.chip-cd { background:#F0F0F0; padding:2rpx 10rpx; border-radius:8rpx; margin-left:4rpx; }
.chip-cd-txt { font-size:20rpx; color:#999; }
.chip-temp { background:#F5F5F5; }
.chip-clip { background:#E3F2FD; }
.chip-clip .chip-val { color:#1976D2; }
.t-ok   { color:#4CAF50; }
.t-warn { color:#FF9800; }
.t-cold { color:#1976D2; }

.empty-wrap { display:flex; flex-direction:column; align-items:center; padding:120rpx 0; }
.empty-icon { font-size:80rpx; margin-bottom:20rpx; }
.empty-txt  { font-size:28rpx; color:rgba(255,255,255,0.2); }

@keyframes blink { 0%,100%{opacity:1} 50%{opacity:0.3} }
</style>
