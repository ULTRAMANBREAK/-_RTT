<template>
  <view class="page">
    <FallAlertModal />

    <map
      id="mainMap"
      class="map"
      :latitude="mapCenter.lat"
      :longitude="mapCenter.lng"
      :markers="markers"
      :polyline="polyline"
      :scale="15"
      show-location
      enable-zoom
      enable-scroll
      @markertap="onMarkerTap"
      @regionchange="onRegionChange"
    ></map>

    <!-- 重新居中按钮：用户手动移动地图后显示 -->
    <view class="locate-btn" v-if="mapDragged" @click="reCenter">
      <text class="locate-icon">📍</text>
    </view>

    <!-- 顶部信息胶囊：显示「下一站」信息 -->
    <view class="top-capsule" v-if="routeInfo && !loading">
      <text class="cap-item cap-next">下一站</text>
      <view class="cap-line"></view>
      <text class="cap-item">🚴 {{ routeInfo.dist }}</text>
      <view class="cap-line"></view>
      <text class="cap-item">⏱ {{ routeInfo.time }}</text>
      <view class="cap-line"></view>
      <text class="cap-item cap-orange">共{{ routeInfo.stops }}站</text>
    </view>
    <view class="top-capsule top-loading" v-if="loading">
      <text class="cap-item">⚙️ 规划下一站路线...</text>
    </view>

    <!-- 底部步骤面板 -->
    <view class="bottom-panel">
      <view class="drag-handle"></view>
      <view class="panel-header">
        <text class="panel-title">最优配送路线</text>
        <text class="panel-sub" v-if="routeInfo">共 {{ routeInfo.stops }} 站</text>
      </view>

      <!-- 一键高德导航按钮：路线规划完成后才显示 -->
      <view class="nav-btn" v-if="routeSteps.length > 0 && !loading" @click="openAmap">
        <text class="nav-btn-icon">🗺️</text>
        <view class="nav-btn-body">
          <text class="nav-btn-title">高德电动车导航</text>
          <text class="nav-btn-dest">前往：{{ routeSteps[0].place }}</text>
        </view>
        <text class="nav-btn-arrow">›</text>
      </view>
      <!-- 路线规划中时显示 loading 状态 -->
      <view class="nav-btn nav-btn-loading" v-if="loading">
        <text class="nav-btn-icon">⚙️</text>
        <text class="nav-btn-title" style="color:#fff;">正在规划路线，请稍候...</text>
      </view>

      <view v-for="(step, idx) in routeSteps" :key="idx"
        class="step-item" @click="focusStep(step)">
        <view class="step-line-col">
          <view class="step-dot" :class="step.type==='pickup'?'dot-pickup':'dot-deliver'">
            {{ idx + 1 }}
          </view>
          <view class="step-line" v-if="idx < routeSteps.length - 1"></view>
        </view>
        <view class="step-content">
          <view class="step-header">
            <text class="step-tag" :class="step.type==='pickup'?'tag-pickup':'tag-deliver'">
              {{ step.type==='pickup' ? '取餐' : '送达' }}
            </text>
            <text class="step-food">{{ step.foodName }}</text>
            <text class="step-dist" :class="step.type==='deliver'?'step-dist-blue':''">{{ step.segDist }}</text>
          </view>
          <text class="step-place">{{ step.place }}</text>
          <text class="step-time" v-if="step.segTime">预计骑行 {{ step.segTime }}</text>
        </view>
      </view>

      <view v-if="routeSteps.length===0 && !loading" class="empty">暂无配送任务</view>
    </view>

  </view>
</template>

<script>
import store from '@/utils/store.js'
import FallAlertModal from '@/components/FallAlertModal/FallAlertModal.vue'
const KEY = '662e9fcefccaabf1d725fbf90a70b423'

export default {
  components: { FallAlertModal },
  data() {
    return {
      store,
      mapCenter:  { lat: store.riderLocation.lat, lng: store.riderLocation.lng },
      markers:    [],
      polyline:   [],
      routeInfo:  null,
      routeSteps: [],
      loading:    false,
      mapDragged: false,   // 用户手动拖动地图后为true，显示"重新居中"按钮
      // ===== 平滑移动动画所需状态 =====
      _dispLat: store.riderLocation.lat,  // 当前屏幕上显示的纬度（插值中间值）
      _dispLng: store.riderLocation.lng,  // 当前屏幕上显示的经度（插值中间值）
      _animTimer: null,                   // 插值定时器句柄
    }
  },
  onLoad() { this.planRoute() },
  onShow() { this.planRoute() },
  onUnload() {
    // 离开页面时清理插值定时器
    if (this._animTimer) { clearInterval(this._animTimer); this._animTimer = null }
  },
  watch: {
    // ===== 骑手位置实时变化 → 启动平滑插值动画，不重新规划路线 =====
    'store.riderLocation': {
      deep: true,
      handler(newLoc) {
        this._smoothMoveTo(newLoc.lat, newLoc.lng)
      }
    },
    // ===== 订单变化 → 防抖800ms重新规划路线 =====
    'store.orders': {
      deep: true,
      handler() {
        if (this._debounceTimer) clearTimeout(this._debounceTimer)
        this._debounceTimer = setTimeout(() => {
          if (!this.loading) this.planRoute()
        }, 800)
      }
    }
  },
  methods: {

    // ===== 骑手标记平滑移动（在两个GPS点之间插值，约50帧/秒补画中间过程）=====
    _smoothMoveTo(targetLat, targetLng) {
      // 如果有正在执行的动画，先中止它（以当前插值位置为新起点）
      if (this._animTimer) { clearInterval(this._animTimer); this._animTimer = null }

      const fromLat  = this._dispLat
      const fromLng  = this._dispLng
      const diffLat  = targetLat - fromLat
      const diffLng  = targetLng - fromLng

      // 距离极小（<0.5米）时直接跳过，不必浪费帧
      if (Math.abs(diffLat) < 0.000005 && Math.abs(diffLng) < 0.000005) return

      // 动画时长 750ms（略短于GPS刷新间隔，确保下次更新时上次动画已结束）
      const DURATION = 750
      const INTERVAL = 20      // 20ms ≈ 50fps，手机上流畅且省电
      const STEPS    = Math.ceil(DURATION / INTERVAL)
      let   step     = 0

      this._animTimer = setInterval(() => {
        step++
        // ease-out cubic：开始快，接近目标时减速，自然感最强
        const t    = Math.min(step / STEPS, 1)
        const ease = 1 - Math.pow(1 - t, 3)

        const lat = fromLat + diffLat * ease
        const lng = fromLng + diffLng * ease
        this._dispLat = lat
        this._dispLng = lng

        // 更新骑手标记（markers[0]）
        if (this.markers.length > 0) {
          const m = this.markers[0]
          this.markers.splice(0, 1, { ...m, latitude: lat, longitude: lng })
        }
        // 地图视野自动跟随（用户没有手动拖动时）
        if (!this.mapDragged) {
          this.mapCenter = { lat, lng }
        }

        // 动画结束：精确对齐目标坐标
        if (step >= STEPS) {
          clearInterval(this._animTimer)
          this._animTimer = null
          this._dispLat = targetLat
          this._dispLng = targetLng
        }
      }, INTERVAL)
    },

    // ===== 主入口：只规划「下一个」停靠点的路线 =====
    async planRoute() {
      const active = store.orders.filter(o => o.status !== 'completed')
      if (active.length === 0) {
        this.routeSteps = []; this.polyline = []; this.routeInfo = null; return
      }
      this.loading = true

      // 1. 生成最优完整顺序（用于底部步骤列表展示全貌）
      const bestOrder = await this._findBestOrder(active)

      // 2. 只请求「第一步」的骑行路线并画到地图上
      const origin  = { lat: store.riderLocation.lat, lng: store.riderLocation.lng }
      const nextStop = bestOrder[0]

      let seg = await this._bicycling(origin.lat, origin.lng, nextStop.lat, nextStop.lng)
      if (!seg) seg = await this._walking(origin.lat, origin.lng, nextStop.lat, nextStop.lng)

      if (seg) {
        nextStop.segDist = seg.dist < 1000
          ? Math.round(seg.dist) + 'm'
          : (seg.dist / 1000).toFixed(1) + 'km'
        nextStop.segTime = seg.time + '分钟'
        this.polyline = [{
          points:      seg.coords,
          color:       nextStop.type === 'deliver' ? '#2e7d32' : '#1565C0',
          width:       10,
          arrowLine:   true,
          borderColor: '#ffffff',
          borderWidth: 3
        }]
        this.routeInfo = {
          dist:  nextStop.segDist,
          time:  nextStop.segTime,
          stops: bestOrder.length   // 剩余总停靠数
        }
        // 存入 store，供 App.vue 的 GPS 回调做导航语音播报
        store.navSteps     = seg.navSteps || []
        store.navStepIndex = 0
        store.navTarget    = { place: nextStop.place, type: nextStop.type }
      } else {
        // 两个API都失败，画虚线直线兜底
        nextStop.segDist = '路线获取失败'
        nextStop.segTime = null
        this.polyline = [{
          points: [
            { latitude: origin.lat, longitude: origin.lng },
            { latitude: nextStop.lat, longitude: nextStop.lng }
          ],
          color:      '#AAAAAA',
          width:      6,
          dottedLine: true,
          arrowLine:  true
        }]
        this.routeInfo = { dist: '--', time: '--', stops: bestOrder.length }
      }

      this.routeSteps = bestOrder
      this._buildMarkers(bestOrder)
      this.loading = false
    },

    // ===== 找最优停靠顺序（支持穿插送餐 + 截止时间罚分）=====
    async _findBestOrder(orders) {
      // Step1：取餐节点 —— 只为 pending_pickup 订单建，同一家店合并
      const shopMap = {}
      orders.filter(o => o.status === 'pending_pickup').forEach(o => {
        if (!shopMap[o.shopName]) {
          shopMap[o.shopName] = {
            type: 'pickup', shopName: o.shopName,
            lat: o.shopLat, lng: o.shopLng, place: o.shopName, foods: []
          }
        }
        shopMap[o.shopName].foods.push(o.foodName)
      })
      const pickupNodes = Object.values(shopMap)

      // Step2：送达节点 —— 所有订单都需要送
      const deliverNodes = orders.map(o => ({
        type: 'deliver', shopName: o.shopName,
        lat: o.lat, lng: o.lng, place: o.address,
        foodName: o.foodName, deadline: o.deadline
      }))

      // Step3：DFS 枚举合法顺序
      // 规则：deliver 节点只能排在该店 pickup 节点之后
      //   —— delivering/arriving 的订单没有 pickup 节点，所以视为"已取餐"，可随时送
      const doneShops  = new Set(
        orders.filter(o => o.status === 'delivering' || o.status === 'arriving').map(o => o.shopName)
      )
      const allNodes   = [...pickupNodes, ...deliverNodes]
      const visited    = new Array(allNodes.length).fill(false)
      // pickedShops：DFS 过程中已访问过取餐点的店（预置已在配送中的店）
      const pickedShops = new Set([...doneShops])
      const validSeqs  = []

      const dfs = (seq) => {
        if (seq.length === allNodes.length) { validSeqs.push([...seq]); return }
        for (let i = 0; i < allNodes.length; i++) {
          if (visited[i]) continue
          const node = allNodes[i]
          // deliver 节点必须在对应店铺已取餐后才能出现
          if (node.type === 'deliver' && !pickedShops.has(node.shopName)) continue
          visited[i] = true
          if (node.type === 'pickup') pickedShops.add(node.shopName)
          seq.push(node)
          dfs(seq)
          seq.pop()
          visited[i] = false
          // 回溯时只删除本次 DFS 新增的店，不删预置的 doneShops
          if (node.type === 'pickup' && !doneShops.has(node.shopName)) {
            pickedShops.delete(node.shopName)
          }
        }
      }
      dfs([])

      if (validSeqs.length === 0) return [...pickupNodes, ...deliverNodes]

      // Step4：评分 = 总直线距离 + 超时罚分
      const SPEED        = 250    // 米/分钟（15km/h）
      const STOP_MIN     = 2
      const LATE_PENALTY = 1000
      const now          = Date.now()
      let bestScore = Infinity, bestSeq = null

      for (const seq of validSeqs) {
        let dist = 0, timeMins = 0, penalty = 0
        let prev = { lat: store.riderLocation.lat, lng: store.riderLocation.lng }
        for (const node of seq) {
          const d = this._dist(prev.lat, prev.lng, node.lat, node.lng)
          dist += d; timeMins += d / SPEED + STOP_MIN
          if (node.type === 'deliver' && node.deadline) {
            const eta = now + timeMins * 60 * 1000
            if (eta > node.deadline) penalty += ((eta - node.deadline) / 60000) * LATE_PENALTY
          }
          prev = node
        }
        const score = dist + penalty
        if (score < bestScore) { bestScore = score; bestSeq = seq }
      }

      // Step5：格式化返回
      return bestSeq.map(node => ({
        type:     node.type,
        lat:      node.lat,
        lng:      node.lng,
        place:    node.place,
        foodName: node.type === 'pickup' ? node.foods.join('、') : node.foodName
      }))
    },

    // ===== 高德骑行路线API（v4）=====
    _bicycling(fromLat, fromLng, toLat, toLng) {
      return new Promise(resolve => {
        uni.request({
          url: `https://restapi.amap.com/v4/direction/bicycling?origin=${fromLng},${fromLat}&destination=${toLng},${toLat}&key=${KEY}`,
          success: res => {
            try {
              const path = res.data.data.paths[0]
              const coords = []
              // 提取导航步骤（instruction + 步骤终点坐标），供 App.vue 语音播报使用
              const navSteps = []
              path.steps.forEach(step => {
                const pts = step.polyline ? step.polyline.split(';') : []
                pts.forEach(pt => {
                  const [lng, lat] = pt.split(',').map(Number)
                  if (lng && lat) coords.push({ latitude: lat, longitude: lng })
                })
                // 取每步最后一个点作为"步骤终点"（骑手到达此处说下一步）
                if (pts.length > 0) {
                  const last = pts[pts.length - 1].split(',').map(Number)
                  if (last[0] && last[1]) {
                    navSteps.push({
                      instruction: step.instruction || step.assistant_action || '继续前行',
                      distance:    parseInt(step.distance) || 0,
                      endLng:      last[0],
                      endLat:      last[1]
                    })
                  }
                }
              })
              if (coords.length < 2) { resolve(null); return }
              resolve({ coords, dist: path.distance, time: Math.ceil(path.duration / 60), navSteps })
            } catch(e) { resolve(null) }
          },
          fail: () => resolve(null)
        })
      })
    },

    // ===== 高德步行路线API（v3）骑行失败时降级使用 =====
    _walking(fromLat, fromLng, toLat, toLng) {
      return new Promise(resolve => {
        uni.request({
          url: `https://restapi.amap.com/v3/direction/walking?origin=${fromLng},${fromLat}&destination=${toLng},${toLat}&key=${KEY}`,
          success: res => {
            try {
              const path = res.data.route.paths[0]
              const coords = []
              const navSteps = []
              path.steps.forEach(step => {
                const pts = step.polyline ? step.polyline.split(';') : []
                pts.forEach(pt => {
                  const [lng, lat] = pt.split(',').map(Number)
                  if (lng && lat) coords.push({ latitude: lat, longitude: lng })
                })
                if (pts.length > 0) {
                  const last = pts[pts.length - 1].split(',').map(Number)
                  if (last[0] && last[1]) {
                    navSteps.push({
                      instruction: step.instruction || step.action || '继续前行',
                      distance:    parseInt(step.distance) || 0,
                      endLng:      last[0],
                      endLat:      last[1]
                    })
                  }
                }
              })
              if (coords.length < 2) { resolve(null); return }
              resolve({ coords, dist: parseFloat(path.distance), time: Math.ceil(parseFloat(path.duration) / 60), navSteps })
            } catch(e) { resolve(null) }
          },
          fail: () => resolve(null)
        })
      })
    },

    // ===== 地图标记（同坐标点用黄金角度错开，防止重叠）=====
    _buildMarkers(steps) {
      // 记录每个坐标已摆了几个标记
      const posMap = {}
      const spread = (lat, lng) => {
        const key = lat.toFixed(4) + ',' + lng.toFixed(4)
        const cnt = posMap[key] || 0
        posMap[key] = cnt + 1
        if (cnt === 0) return { lat, lng }
        // 黄金角度 137.5°，半径约 30 米（0.00027°≈30m）
        const R     = 0.00027
        const angle = cnt * 137.5 * Math.PI / 180
        return { lat: lat + R * Math.cos(angle), lng: lng + R * Math.sin(angle) }
      }

      const ms = [{
        id: 0, latitude: store.riderLocation.lat, longitude: store.riderLocation.lng,
        title: '我的位置', iconPath: '/static/logo.png', width: 32, height: 32
      }]
      steps.forEach((s, i) => {
        const pos = spread(s.lat, s.lng)
        ms.push({
          id: i + 1, latitude: pos.lat, longitude: pos.lng, title: s.place,
          label: {
            content:      String(i + 1),
            color:        '#fff',
            bgColor:      s.type === 'pickup' ? '#1565C0' : '#2e7d32',
            borderRadius: 10, padding: 5, fontSize: 13
          }
        })
      })
      this.markers = ms
    },

    // ===== 打开高德地图 APP 电动车导航 =====
    openAmap() {
      if (!this.routeSteps || this.routeSteps.length === 0) return
      const next  = this.routeSteps[0]
      const dlat  = next.lat
      const dlng  = next.lng
      const dname = encodeURIComponent(next.place)
      const slat  = store.riderLocation.lat
      const slng  = store.riderLocation.lng
      const sname = encodeURIComponent('我的位置')

      // t=3 骑行/电动车（0驾车 1公交 2步行 3骑行）
      // Android 路径必须是 route/plan/，iOS 是 path
      const androidUrl = `androidamap://route/plan/?sourceApplication=delivery&sid=&slat=${slat}&slon=${slng}&sname=${sname}&did=&dlat=${dlat}&dlon=${dlng}&dname=${dname}&dev=0&t=3`
      const iosUrl     = `iosamap://path?sourceApplication=delivery&sid=&slat=${slat}&slon=${slng}&sname=${sname}&did=&dlat=${dlat}&dlon=${dlng}&dname=${dname}&dev=0&t=3`

      // #ifdef APP-PLUS
      const platform = uni.getSystemInfoSync().platform
      if (platform === 'ios') {
        // iOS 直接用 URL Scheme
        plus.runtime.openURL(iosUrl)
      } else {
        // Android：用原生 Intent 调起高德，绕过 Android 11 包可见性限制
        try {
          const main    = plus.android.runtimeMainActivity()
          const Intent  = plus.android.importClass('android.content.Intent')
          const Uri     = plus.android.importClass('android.net.Uri')
          const intent  = new Intent(Intent.ACTION_VIEW, Uri.parse(androidUrl))
          main.startActivity(intent)
        } catch(e) {
          uni.showToast({ title: '请安装高德地图APP', icon: 'none', duration: 2000 })
        }
      }
      // #endif

      // #ifndef APP-PLUS
      uni.showToast({ title: '请在真机上使用导航', icon: 'none' })
      // #endif
    },

    // 用户手动拖动地图时触发
    onRegionChange(e) {
      if (e.type === 'begin') this.mapDragged = true
    },
    // 点击"重新居中"按钮，回到骑手位置并恢复自动跟随
    reCenter() {
      this.mapDragged = false
      this.mapCenter  = { lat: store.riderLocation.lat, lng: store.riderLocation.lng }
    },

    onMarkerTap(e) {
      const m = this.markers.find(x => x.id === e.detail.markerId)
      if (m) {
        this.mapDragged = true   // 点击标记也视为手动操作，暂停自动跟随
        this.mapCenter = { lat: m.latitude, lng: m.longitude }
      }
    },
    focusStep(step) {
      this.mapDragged = true
      this.mapCenter = { lat: step.lat, lng: step.lng }
    },

    _dist(lat1, lng1, lat2, lng2) {
      const R = 6371000
      const dLat = (lat2 - lat1) * Math.PI / 180
      const dLng = (lng2 - lng1) * Math.PI / 180
      const a = Math.sin(dLat/2)**2 + Math.cos(lat1*Math.PI/180)*Math.cos(lat2*Math.PI/180)*Math.sin(dLng/2)**2
      return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a))
    }
  }
}
</script>

<style scoped>
.page { display:flex; flex-direction:column; height:100vh; background:#eef1f6; }
.map  { width:100%; height:55vh; }

/* 重新居中按钮 */
.locate-btn {
  position: absolute;
  right: 24rpx;
  top: calc(55vh - 100rpx);
  width: 80rpx; height: 80rpx;
  background: #fff;
  border-radius: 50%;
  display: flex; align-items: center; justify-content: center;
  box-shadow: 0 4rpx 20rpx rgba(0,0,0,0.12);
  border: 1rpx solid #dde3ec;
  z-index: 10;
}
.locate-btn:active { opacity: 0.7; transform: scale(0.92); }
.locate-icon { font-size: 40rpx; }

/* 顶部信息胶囊 */
.top-capsule { position:absolute; top:24rpx; left:50%; transform:translateX(-50%); background:rgba(255,255,255,0.95); border-radius:50rpx; padding:16rpx 36rpx; display:flex; align-items:center; gap:24rpx; white-space:nowrap; box-shadow:0 4rpx 20rpx rgba(0,0,0,0.10); border:1rpx solid #dde3ec; }
.top-loading { background:rgba(21,101,192,0.92); box-shadow:0 4rpx 20rpx rgba(21,101,192,0.3); border:none; }
.cap-item   { color:#1a2942; font-size:24rpx; font-weight:700; }
.top-loading .cap-item { color:#fff; }
.cap-orange { color:#1565C0; }
.cap-next   { color:#1565C0; font-size:22rpx; font-weight:900; letter-spacing:2rpx; }
.cap-line   { width:1rpx; height:24rpx; background:#dde3ec; }
.top-loading .cap-line { background:rgba(255,255,255,0.3); }

/* 高德导航按钮（面板内，不被地图遮挡）*/
.nav-btn {
  display: flex;
  align-items: center;
  gap: 16rpx;
  background: linear-gradient(135deg, #1565C0, #1976D2);
  border-radius: 20rpx;
  padding: 24rpx 28rpx;
  margin-bottom: 24rpx;
  box-shadow: 0 6rpx 24rpx rgba(21,101,192,0.30);
}
.nav-btn:active      { opacity: 0.88; transform: scale(0.98); }
.nav-btn-loading     { background: linear-gradient(135deg,#78909C,#546E7A); box-shadow:none; }
.nav-btn-icon        { font-size: 44rpx; flex-shrink: 0; }
.nav-btn-body        { flex: 1; min-width: 0; }
.nav-btn-title       { font-size: 28rpx; font-weight: 800; color: #fff; display: block; }
.nav-btn-dest        { font-size: 22rpx; color: rgba(255,255,255,0.8); display: block;
                       margin-top: 4rpx; overflow: hidden; white-space: nowrap; text-overflow: ellipsis; }
.nav-btn-arrow       { font-size: 44rpx; color: rgba(255,255,255,0.6); flex-shrink: 0; }

/* 底部面板 */
.bottom-panel { flex:1; background:#fff; border-radius:28rpx 28rpx 0 0; padding:0 24rpx 40rpx; overflow-y:auto; box-shadow:0 -4rpx 24rpx rgba(0,0,0,0.06); }
.drag-handle  { width:56rpx; height:6rpx; background:#dde3ec; border-radius:3rpx; margin:16rpx auto 0; }
.panel-header { display:flex; justify-content:space-between; align-items:center; padding:20rpx 0; border-bottom:1rpx solid #dde3ec; margin-bottom:20rpx; }
.panel-title  { font-size:30rpx; font-weight:800; color:#1a2942; }
.panel-sub    { font-size:22rpx; color:#bbb; }

/* 步骤列表 */
.step-item     { display:flex; gap:16rpx; padding-bottom:24rpx; }
.step-line-col { display:flex; flex-direction:column; align-items:center; flex-shrink:0; }
.step-dot  { width:48rpx; height:48rpx; border-radius:14rpx; color:#fff; font-size:22rpx; font-weight:800; display:flex; align-items:center; justify-content:center; }
.dot-pickup  { background:linear-gradient(135deg,#1565C0,#1976D2); }
.dot-deliver { background:linear-gradient(135deg,#2e7d32,#43a047); }
.step-line { flex:1; width:2rpx; background:#dde3ec; margin:6rpx 0; min-height:20rpx; }

.step-content { flex:1; padding-top:4rpx; }
.step-header  { display:flex; align-items:center; gap:10rpx; margin-bottom:8rpx; }
.step-tag  { font-size:20rpx; padding:4rpx 14rpx; border-radius:8rpx; flex-shrink:0; font-weight:700; }
.tag-pickup  { background:#eff6ff; color:#1565C0; }
.tag-deliver { background:#e8f5e9; color:#2e7d32; }
.step-food  { font-size:28rpx; font-weight:700; color:#1a2942; flex:1; }
.step-dist       { font-size:22rpx; color:#1565C0; font-weight:800; flex-shrink:0; }
.step-dist-blue  { color:#2e7d32; }
.step-place { font-size:24rpx; color:#4a5a72; display:block; margin-bottom:6rpx; }
.step-time  { font-size:22rpx; color:#bbb; display:block; }

.empty { text-align:center; color:#ccc; padding:80rpx; font-size:28rpx; }
</style>
