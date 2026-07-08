<script>
import { mqttConnect, publishLocation, publishStatus, publishArrive, publishSay } from '@/utils/mqtt.js'
import store from '@/utils/store.js'

// 记录已触发过到达提醒的key，防止重复弹窗
const _notified = new Set()
// GPS上报节流：记录上次上报时间，1秒内不重复上报
let _lastPublishTime = 0
// 是否已注册过 onLocationChange，防止重复叠加监听
let _gpsStarted = false

// ===== GPS 平滑滤波 =====
// EMA（指数移动平均）：smoothed = alpha * raw + (1-alpha) * prev
// alpha 调大 = 更跟手（响应快）；alpha 调小 = 更平滑（抖动少）
// 死区：位移 < DEADBAND_M 不更新地图，消除微小抖动
let _emaLat = null
let _emaLng = null
const DEADBAND_M = 3  // 3米以内的抖动忽略（比原来5米更灵敏）

// ===== 导航语音播报状态 =====
// _navAnnounced：记录当前路线步骤中已播报过的档位（key = stepIdx_300 / stepIdx_100）
// _lastNavSayTime：上次导航播报时间（毫秒），防止连续两条播报太近
const _navAnnounced   = {}
let   _lastNavSayTime = 0

export default {
  onLaunch() {
    console.log('APP启动 - 智能外卖箱')
    mqttConnect()
    // 监听路线更新：map.vue 规划好路线后 store.navSteps 会变化，
    // 延迟 3 秒播报第一步（避免和初始"前往XX取餐"重叠）
    this._unwatchNav = this.$watch
      ? null  // App.vue 里 $watch 不可用，用下面的 setTimeout 轮询代替
      : null
    // 用变量记录上次 navSteps 的长度，检测到变化就播报第一步
    let _lastNavLen = 0
    setInterval(() => {
      const steps = store.navSteps
      if (steps && steps.length > 0 && steps.length !== _lastNavLen) {
        _lastNavLen = steps.length
        // 延迟 3 秒，等初始播报（"前往XX取餐"）说完
        setTimeout(() => {
          const first    = steps[0]
          if (!first) return
          const distStr  = first.distance >= 1000
            ? (first.distance / 1000).toFixed(1) + '公里'
            : Math.round(first.distance) + '米'
          publishSay(first.instruction + '，行驶' + distStr)
          _lastNavSayTime = Date.now()
        }, 3000)
      }
      if (!steps || steps.length === 0) _lastNavLen = 0
    }, 2000)
  },
  onHide()  { uni.stopLocationUpdate(); _gpsStarted = false },
  onShow()  { this._startGps() },

  methods: {
    // 开启持续定位（防止重复注册）
    _startGps() {
      if (_gpsStarted) return   // 已在运行，不重复注册
      // 先清掉旧监听，确保只有一个回调
      try { uni.offLocationChange() } catch(e) {}

      uni.startLocationUpdate({
        type: 'gcj02',
        isHighAccuracy: true,      // 强制使用GPS芯片，刷新更快更准
        highAccuracyExpireTime: 3000, // 3s内必须给出高精度结果
        success: () => {
          _gpsStarted = true
          // 绑定位置变化回调（每次 GPS 刷新都触发，频率由系统决定，通常 1~2s）
          uni.onLocationChange(e => {
            const lat = e.latitude, lng = e.longitude
            const speed = e.speed || 0  // 单位 m/s

            // ===== 自适应 EMA 平滑 =====
            // 速度越快，alpha越大（位置越跟手）；静止时alpha小（最大程度除噪）
            const alpha = speed > 3.0 ? 0.90   // 快速骑行 >10km/h：90% 新值（几乎直接用原始坐标）
                        : speed > 1.0 ? 0.75   // 慢速移动  3-10km/h：75% 新值
                        :               0.60   // 静止/极慢 <3km/h：60% 新值（轻度平滑，快速收敛）
            if (_emaLat === null) {
              _emaLat = lat; _emaLng = lng      // 首次直接初始化
            } else {
              _emaLat = alpha * lat + (1 - alpha) * _emaLat
              _emaLng = alpha * lng + (1 - alpha) * _emaLng
            }

            // ===== 死区过滤（静止时完全消除抖动）=====
            const moveDist = store._calcDist(
              _emaLat, _emaLng,
              store.riderLocation.lat, store.riderLocation.lng
            )
            const isMoving = speed >= 1.0 || moveDist >= DEADBAND_M

            // 到达检测 & 导航播报：始终用原始坐标（精度最高，不受平滑影响）
            this._checkArrival(lat, lng)
            this._checkNavAnnounce(lat, lng)

            const now = Date.now()

            // 无论移动还是静止，都实时更新地图和上报MQTT（每秒一次）
            if (isMoving) {
              // 移动时：更新手机地图（触发 map.vue 的 watch 重绘路线）
              store.riderLocation = { lat: _emaLat, lng: _emaLng }
            }

            // 每秒上报一次给管理员网页（静止和移动都上报）
            if (now - _lastPublishTime >= 1000) {
              publishLocation(_emaLat, _emaLng, speed)
              _lastPublishTime = now
            }
          })
        },
        fail: () => console.warn('[GPS] 定位权限未授权或设备不支持')
      })
    },

    // ===== 导航语音播报检测（每次GPS更新调用）=====
    // 工作方式：
    //   · 300米前：播报 "前方X米，XXX"（下一步骤的指令）
    //   · 100米前：播报 "即将XXX"
    //   · 到达步骤终点（15米内）：切换到下一步，播报新步骤指令+距离
    // 两次播报之间至少间隔 8 秒，防止密集触发
    _checkNavAnnounce(lat, lng) {
      if (!store.navSteps || store.navSteps.length === 0) return
      const idx  = store.navStepIndex || 0
      if (idx >= store.navSteps.length) return

      const step = store.navSteps[idx]
      if (!step.endLat || !step.endLng) return

      const dist = store._calcDist(lat, lng, step.endLat, step.endLng)
      const now  = Date.now()
      const MIN_INTERVAL = 8000   // 两次导航播报最少间隔 8 秒

      // ---- 到达步骤终点（15米内）→ 推进到下一步并播报 ----
      if (dist <= 15) {
        store.navStepIndex = idx + 1
        const nextIdx = store.navStepIndex
        if (nextIdx < store.navSteps.length && now - _lastNavSayTime >= MIN_INTERVAL) {
          const next     = store.navSteps[nextIdx]
          const distStr  = next.distance >= 1000
            ? (next.distance / 1000).toFixed(1) + '公里'
            : Math.round(next.distance) + '米'
          publishSay(next.instruction + '，行驶' + distStr)
          _lastNavSayTime = now
          // 重置下一步的档位记录
          delete _navAnnounced[nextIdx + '_300']
          delete _navAnnounced[nextIdx + '_100']
        }
        return
      }

      const nextIdx = idx + 1
      if (nextIdx >= store.navSteps.length) return  // 最后一步，靠arrive检测接手

      // ---- 300米提前播报下一步 ----
      if (dist <= 300 && !_navAnnounced[idx + '_300']) {
        if (now - _lastNavSayTime >= MIN_INTERVAL) {
          _navAnnounced[idx + '_300'] = true
          const next = store.navSteps[nextIdx]
          publishSay('前方' + Math.round(dist) + '米，' + next.instruction)
          _lastNavSayTime = now
        }
      }

      // ---- 100米再提醒一次 ----
      if (dist <= 100 && !_navAnnounced[idx + '_100']) {
        if (now - _lastNavSayTime >= MIN_INTERVAL) {
          _navAnnounced[idx + '_100'] = true
          const next = store.navSteps[nextIdx]
          publishSay('即将' + next.instruction)
          _lastNavSayTime = now
        }
      }
    },

    // 到达检测：进入30米范围时弹窗提醒 + LED闪烁
    // 30米兼容实际GPS误差（校园环境通常15~25米）
    _checkArrival(lat, lng) {
      store.orders.forEach(o => {

        // ===== 检测取餐点到达 =====
        if (o.status === 'pending_pickup') {
          const dist = store._calcDist(lat, lng, o.shopLat, o.shopLng)
          const key  = o.id + '_shop'
          console.log('[到达检测-取餐]', o.shopName, '距离:', Math.round(dist) + 'm', '状态:', o.status, '已通知:', _notified.has(key), '坐标:', o.shopLat, o.shopLng)

          if (dist <= 80 && !_notified.has(key)) {
            _notified.add(key)
            // 让对应夹子LED闪烁：骑手一眼看到该拿哪个夹子；scene=pickup让ESP32说"X号取餐请刷卡"
            publishArrive(o.clipId, 'pickup')
            store.triggerAlarm(
              '📍 已到达取餐点',
              o.shopName + '\n' + o.clipId + '号夹子正在闪烁，刷卡取餐',
              'info'
            )
          }
          // 离开100米后重置，下次进入再提醒
          if (dist > 100) _notified.delete(key)
        }

        // ===== 检测送达地点到达 =====
        if (o.status === 'delivering' || o.status === 'arriving') {
          const dist = store._calcDist(lat, lng, o.lat, o.lng)
          const key  = o.id + '_dest'
          console.log('[到达检测-送达]', o.address?.slice(0,15), '距离:', Math.round(dist) + 'm', '状态:', o.status, '坐标:', o.lat, o.lng)

          if (dist <= 80) {
            // 切换到"即将到达"状态，同步通知管理员网页 + 通知ESP32让夹子LED闪烁
            if (o.status === 'delivering') {
              o.status = 'arriving'
              publishStatus(o.id, 'arriving')
              // 告诉ESP32：把clipId发给STM32，让对应LED开始闪烁；scene=deliver让ESP32说"X号送达请刷卡"
              publishArrive(o.clipId, 'deliver')
            }
            if (!_notified.has(key)) {
              _notified.add(key)
              store.triggerAlarm(
                '📍 已到达送餐地点',
                o.address + '\n' + o.clipId + '号夹子正在闪烁，刷卡确认送达',
                'info'
              )
            }
          }

          // 离开100米后恢复配送中状态，重置提醒
          if (dist > 100 && o.status === 'arriving') {
            o.status = 'delivering'
            _notified.delete(key)
          }
        }
      })
    }
  }
}
</script>

<style>
/* 全局公共样式 */
page { background-color: #f5f5f5; }
</style>
