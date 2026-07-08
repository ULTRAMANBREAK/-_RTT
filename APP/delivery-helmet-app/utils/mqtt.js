/**
 * mqtt.js - MQTT连接管理
 * 手写 MQTT 3.1.1 协议，基于 UniApp WebSocket
 *
 * 修改服务器地址：第5行 BROKER 变量
 */

import store from './store.js'

const BROKER = 'ws://broker.emqx.io:8083/mqtt'  // ← 公网Broker

// 主题前缀（防止和别人冲突）
const PREFIX = 'helmet_dxk'

let socket       = null
let pingTimer    = null
let reconnTimer  = null
let isConnected  = false
let reconnDelay  = 2000   // 初始重连延迟2秒，每次失败翻倍，最大30秒

// ===== 开机初始播报双触发标志 =====
// ESP32 发 /ready 和 APP 收到 /orders 哪个先到不确定
// 两个条件都满足时才触发一次初始播报，避免时序问题
let esp32Ready      = false   // 收到 /ready 后置 true
let ordersReceived  = false   // 第一次收到 /orders 数据后置 true
let initAnnounced   = false   // 初始播报已触发，防止重复

// ===== 对外接口 =====

export function mqttConnect() {
  console.log('[MQTT] 开始连接:', BROKER)
  _connect()
}

// 发布订单映射
export function publishOrderMapping() {
  if (!isConnected) return
  const payload = JSON.stringify({
    mappings: store.orders.map(o => ({ clipId: o.clipId, orderId: o.id, address: o.address }))
  })
  _publish(PREFIX + '/orders', payload)
}

// 发布GPS位置给管理员网页（retain=true：broker保存最新位置，网页一连上立刻收到）
export function publishLocation(lat, lng, speed) {
  if (!isConnected) return
  _publish(PREFIX + '/location', JSON.stringify({ lat, lng, speed: parseFloat(speed.toFixed(2)) }), true)
}

// LED由STM32内部控制，无需APP发送指令

// 发布订单状态变化（通知管理员网页同步）
export function publishStatus(orderId, status) {
  if (!isConnected) return
  _publish(PREFIX + '/status', JSON.stringify({ orderId, status }))
}

// 到达通知：通知ESP32让对应夹子LED开始闪烁，并告知场景（取餐/送达）
// clipId：夹子编号，如 '01'、'02'
// scene：'pickup'=取餐点到达，'deliver'=送达地点到达
export function publishArrive(clipId, scene) {
  if (!isConnected) return
  _publish(PREFIX + '/arrive', JSON.stringify({ cardId: clipId, scene: scene || 'pickup' }))
  console.log('[MQTT] 发送到达通知，夹子:', clipId, '场景:', scene)
}

// 外卖锁控制：action='open' 开锁 / 'close' 关锁，ESP32转发给STM32
export function publishLock(action) {
  if (!isConnected) return
  _publish(PREFIX + '/lock', JSON.stringify({ action }))
  console.log('[MQTT] 外卖锁指令:', action)
}

// 让ESP32说一句话（导航播报/初始播报/完成提示）
export function publishSay(text) {
  if (!isConnected) return
  _publish(PREFIX + '/say', JSON.stringify({ text }))
  console.log('[MQTT] 导航播报:', text)
}

// 恢复正常（测试用）：温度发 /sensor，报警状态发 /alert reset
export function publishReset() {
  // 温度恢复正常
  _publish(PREFIX + '/sensor', JSON.stringify({
    FL: 65.2, FR: 62.5, BL: 63.1, BR: 60.8
  }))
  // 通知网页报警状态恢复
  _publish(PREFIX + '/alert', JSON.stringify({ type: 'reset' }))
}

export function testAlarm(type) {
  // 跌倒/防盗 → 发 /alert（网页的 alert 处理器监听这个主题）
  // 温度      → 发 /sensor（网页的 sensor 处理器只处理温度）
  if (type === 'fall') {
    store.updateDeviceData({ fallDetect: 'alarm' })
    _publish(PREFIX + '/alert', JSON.stringify({ type: 'fall' }))
  }
  if (type === 'theft') {
    store.updateDeviceData({ antiTheft: 'alarm' })
    _publish(PREFIX + '/alert', JSON.stringify({ type: 'theft' }))
  }
  if (type === 'temp') {
    store.updateDeviceData({ temperatures: { 'FL': 32.0 } })
    _publish(PREFIX + '/sensor', JSON.stringify({ FL: 32.0 }))
  }
}

// ===== 内部实现 =====

function _connect() {
  clearTimeout(reconnTimer)
  try {
    socket = uni.connectSocket({ url: BROKER, protocols: ['mqtt'],
      fail: () => _scheduleReconn()
    })
    socket.onOpen(_onOpen)
    socket.onMessage(_onMessage)
    socket.onClose(_onClose)
    socket.onError(() => _scheduleReconn())
  } catch(e) { _scheduleReconn() }
}

function _onOpen() {
  console.log('[MQTT] WebSocket已连接，发送CONNECT')
  const clientId = 'app_' + Math.random().toString(16).slice(2, 10)
  const cid = _str2bytes(clientId)
  const varHeader = [0x00,0x04,0x4D,0x51,0x54,0x54, 0x04, 0x02, 0x00,0x3C]
  const payload = [0x00, cid.length, ...cid]
  const remain = varHeader.length + payload.length
  socket.send({ data: new Uint8Array([0x10, remain, ...varHeader, ...payload]).buffer })
}

function _onMessage(res) {
  if (!res.data) return
  let bytes
  if (res.data instanceof ArrayBuffer) {
    bytes = new Uint8Array(res.data)
  } else {
    // 字符串形式，尝试直接解析
    try { store.updateDeviceData(JSON.parse(res.data)) } catch(e) {}
    return
  }

  const type = (bytes[0] & 0xF0) >> 4

  // CONNACK
  if (type === 2) {
    if (bytes[3] === 0) {
      console.log('[MQTT] 连接成功！')
      isConnected = true
      store.device.connected = true
      store.device.mockMode = false
      reconnDelay = 2000   // 连接成功，重置退避延迟
      _subscribe()
      publishOrderMapping()
      _startPing()
    }
    return
  }

  // PUBLISH
  if (type === 3) {
    try {
      let pos = 1
      let remain = 0, shift = 0
      do { remain |= (bytes[pos] & 0x7F) << shift; shift += 7 } while (bytes[pos++] & 0x80)
      const topicLen = (bytes[pos] << 8) | bytes[pos+1]
      const topic = _bytes2str(bytes.slice(pos+2, pos+2+topicLen))
      pos += 2 + topicLen
      const qos = (bytes[0] & 0x06) >> 1
      if (qos > 0) {
        // QoS 1：读出消息ID，发回 PUBACK（告诉 broker"我收到了"，broker才算送达）
        const msgIdHi = bytes[pos], msgIdLo = bytes[pos+1]
        pos += 2
        if (qos === 1) {
          socket.send({ data: new Uint8Array([0x40, 0x02, msgIdHi, msgIdLo]).buffer })
        }
      }

      // 解码 payload，去掉 BOM 和空白
      const payloadBytes = bytes.slice(pos)
      let json = _bytes2str(payloadBytes)
      json = json.replace(/^\uFEFF/, '').trim()

      console.log('[MQTT] 收到 ' + topic + ':', json)
      const data = JSON.parse(json)
      if (topic === PREFIX + '/sensor')  _handleTemp(data)    // 温度数据
      if (topic === PREFIX + '/speed')   _handleSpeed(data)   // 速度数据（STM32 GPS模块）
      if (topic === PREFIX + '/alert')   _handleAlert(data)   // 跌倒/防盗事件
      if (topic === PREFIX + '/nfc')     _handleNfc(data)
      if (topic === PREFIX + '/unlock')  _handleUnlock(data)  // 解锁卡刷卡事件
      if (topic === PREFIX + '/orders')  _handleOrders(data)
      if (topic === PREFIX + '/urge')    _handleUrge(data)
      if (topic === PREFIX + '/status' && data.source === 'admin') _handleAdminStatus(data)
      if (topic === PREFIX + '/ready')   _handleReady(data)   // ESP32上线通知
    } catch(e) { console.error('[MQTT] 解析失败:', e.message) }
    return
  }

  // SUBACK / PINGRESP 忽略
}

function _onClose() {
  if (!isConnected) return   // 已经在重连流程中，忽略重复触发
  console.warn('[MQTT] 断开，' + reconnDelay/1000 + '秒后重连')
  isConnected = false
  store.device.connected = false
  clearInterval(pingTimer)
  _scheduleReconn()
}

function _scheduleReconn() {
  clearTimeout(reconnTimer)
  store.device.connected = false
  reconnTimer = setTimeout(() => {
    console.log('[MQTT] 重连中...')
    _connect()
  }, reconnDelay)
  // 指数退避：每次失败延迟翻倍，最大30秒
  reconnDelay = Math.min(reconnDelay * 2, 30000)
}

function _subscribe() {
  const topics = [PREFIX + '/sensor', PREFIX + '/speed', PREFIX + '/alert', PREFIX + '/nfc',
                  PREFIX + '/unlock', PREFIX + '/orders', PREFIX + '/urge', PREFIX + '/status',
                  PREFIX + '/ready']  // /ready = ESP32上线通知，收到后触发初始导航播报
  // 注：/unlock = 解锁卡刷卡事件；LED由STM32内部控制无需订阅
  let body = [0x00, 0x01]
  topics.forEach(t => { const b = _str2bytes(t); body.push(0x00, b.length, ...b, 0x00) })
  const len = _encLen(body.length)
  socket.send({ data: new Uint8Array([0x82, ...len, ...body]).buffer,
    success: () => console.log('[MQTT] 已订阅:', topics.join(', '))
  })
}

function _startPing() {
  clearInterval(pingTimer)
  pingTimer = setInterval(() => {
    if (!isConnected) return
    socket.send({ data: new Uint8Array([0xC0, 0x00]).buffer })
    console.log('[MQTT] 心跳')
  }, 45000)
}

function _publish(topic, payload, retain) {
  const tb = _str2bytes(topic)
  const pb = _str2bytes(payload)
  const len = _encLen(2 + tb.length + pb.length)
  // 0x30 = 普通发布，0x31 = retain保留（broker缓存最新消息，新订阅者立即收到）
  const flag = retain ? 0x31 : 0x30
  socket.send({ data: new Uint8Array([flag, ...len, 0x00, tb.length, ...tb, ...pb]).buffer })
}

// 处理温度数据：{"type":"temp","FL":62.3,"FR":61.8,"BL":60.5,"BR":61.2}
function _handleTemp(data) {
  const temps = {}
  if (data.FL !== undefined) temps['FL'] = data.FL
  if (data.FR !== undefined) temps['FR'] = data.FR
  if (data.BL !== undefined) temps['BL'] = data.BL
  if (data.BR !== undefined) temps['BR'] = data.BR
  if (Object.keys(temps).length > 0) {
    store.updateDeviceData({ temperatures: temps })
  }
}

// 处理速度数据：{"type":"speed","speed":15.2}
function _handleSpeed(data) {
  if (data.speed !== undefined) {
    store.device.speed = parseFloat(data.speed.toFixed(1))
  }
}

// 处理紧急报警：{"type":"fall"} 或 {"type":"theft"}
function _handleAlert(data) {
  if (data.type === 'fall') {
    store.updateDeviceData({ fallDetect: 'alarm' })
  } else if (data.type === 'theft') {
    store.updateDeviceData({ antiTheft: 'alarm' })
  }
}

function _handleNfc(data) {
  const clipId = String(data.cardId).padStart(2, '0')
  const action = (data.action || '').toUpperCase()

  // STM32负责LED和位置判断，APP只负责更新订单状态和通知管理员
  if (action === 'BIND') {
    const order = store.bindNfc(clipId)
    if (order) {
      // 通知管理员网页
      _publish(PREFIX + '/status', JSON.stringify({ orderId: order.id, status: 'delivering', clipId }))

      // 取餐和送达在同一地点时：BIND后立刻检测送达距离，若已在送达点30米内则直接发arrive
      // 不能等下次GPS tick，因为GPS可能短暂超出30米门限导致漏检
      const riderLat = store.riderLocation?.lat
      const riderLng = store.riderLocation?.lng
      if (riderLat && riderLng) {
        const dist = store._calcDist(riderLat, riderLng, order.addrLat, order.addrLng)
        if (dist <= 80) {  // 用80m宽松门限，容纳GPS误差
          console.log('[NFC BIND] 取送同地，立刻发送送达arrive，距离:', Math.round(dist) + 'm')
          order.status = 'arriving'  // 直接跳到arriving，跳过delivering中间态
          _publish(PREFIX + '/status', JSON.stringify({ orderId: order.id, status: 'arriving', clipId }))
          _publish(PREFIX + '/arrive', JSON.stringify({ cardId: clipId, scene: 'deliver' }))
        }
      }
    } else {
      // 没有匹配订单（调试时或订单未同步）：至少弹窗提示，不能无声无息
      store.triggerAlarm('📦 NFC刷卡', clipId + '号夹子刷卡，但没有待取餐订单匹配', 'warning')
      console.warn('[NFC] BIND clipId=' + clipId + ' 无匹配订单')
    }
  } else if (action === 'UNBIND') {
    const order = store.unbindNfc(clipId)
    if (order) {
      // 通知管理员网页
      _publish(PREFIX + '/status', JSON.stringify({ orderId: order.id, status: 'completed', clipId }))
      // 延迟2秒（等ESP32说完"X号送达完成"），再播报下一步
      setTimeout(() => { _triggerInitialAnnounce() }, 2000)
    } else {
      store.triggerAlarm('📦 NFC刷卡', clipId + '号夹子刷卡送达，但没有配送中订单匹配', 'warning')
      console.warn('[NFC] UNBIND clipId=' + clipId + ' 无匹配订单')
    }
  }
}

// 收到解锁卡刷卡事件 {"type":"unlock"}
// 可在此触发APP解锁逻辑（如弹窗提示、记录日志等）
function _handleUnlock(data) {
  console.log('[MQTT] 收到解锁卡事件')
  store.triggerAlarm('🔓 解锁卡触发', '骑手已使用解锁卡', 'info')
}

// 收到管理员网页推送的订单列表，以网页为根本全量同步
// 网页是唯一数据源，APP只负责显示和状态流转
function _handleOrders(data) {
  if (!data.orders || !Array.isArray(data.orders)) return

  data.orders.forEach(incoming => {
    const existing = store.orders.find(o => o.id === incoming.id)
    if (existing) {
      // 已存在：更新网页字段，但保留APP侧的配送状态（delivering/arriving不回退）
      existing.foodName  = incoming.foodName
      existing.shopName  = incoming.shopName
      existing.shopLat   = incoming.shopLat
      existing.shopLng   = incoming.shopLng
      existing.address   = incoming.address
      existing.lat       = incoming.addrLat
      existing.lng       = incoming.addrLng
      existing.deadline  = incoming.deadline
      existing.clipId    = incoming.clipId
      // 只有待取餐状态才跟网页走，配送中/到达中不回退
      if (existing.status === 'pending_pickup') {
        existing.status = incoming.status || 'pending_pickup'
      }
    } else {
      // 新订单：完整加入
      store.orders.push({
        id:          incoming.id,
        sortOrder:   incoming.sortOrder,
        foodName:    incoming.foodName,
        shopName:    incoming.shopName,
        shopLat:     incoming.shopLat,
        shopLng:     incoming.shopLng,
        address:     incoming.address,
        lat:         incoming.addrLat,
        lng:         incoming.addrLng,
        deadline:    incoming.deadline,
        createdAt:   incoming.createdAt,
        clipId:      incoming.clipId,
        status:      incoming.status || 'pending_pickup',
        clipId_bound: null,
        distance:    0
      })
    }
  })

  // 删除网页已移除的订单（配送中的保留，待取餐的跟着网页删）
  const incomingIds = data.orders.map(o => o.id)
  const toKeep = store.orders.filter(o =>
    incomingIds.includes(o.id) || o.status === 'delivering' || o.status === 'arriving'
  )
  store.orders.splice(0, store.orders.length, ...toKeep)
  store.sortOrders()
  console.log('[MQTT] 订单已同步，共', store.orders.length, '单')
  // 第一次收到订单后标记，若ESP32已就绪则触发初始播报
  if (!ordersReceived && store.orders.length > 0) {
    ordersReceived = true
    _tryInitAnnounce()
  }
}

// 收到管理员催单
function _handleUrge(data) {
  const foodName = data.foodName || '订单'
  const msg = data.message || '请加快配送速度！'
  // 触发全局报警弹窗（复用现有报警系统）
  store.triggerAlarm('📢 管理员催单', foodName + '：' + msg, 'warning')
  // 同时震动提醒
  uni.vibrateShort({ success: () => {} })
  console.log('[MQTT] 收到催单:', foodName)
}

// 收到管理员手动改订单状态
function _handleAdminStatus(data) {
  const order = store.orders.find(o => o.id === data.orderId)
  if (!order) return
  order.status = data.status
  console.log('[MQTT] 管理员改状态:', data.orderId, '->', data.status)
  // 管理员标记已送达：3秒后从列表移除
  if (data.status === 'completed') {
    setTimeout(() => {
      const idx = store.orders.indexOf(order)
      if (idx !== -1) store.orders.splice(idx, 1)
    }, 3000)
  }
}

function _encLen(len) {
  const r = []
  do { let b = len % 128; len = Math.floor(len/128); if(len>0) b|=0x80; r.push(b) } while(len>0)
  return r
}
// 字符串转UTF-8字节数组（正确处理中文）
function _str2bytes(s) {
  const bytes = []
  for (let i = 0; i < s.length; i++) {
    const code = s.charCodeAt(i)
    if (code < 0x80) {
      bytes.push(code)
    } else if (code < 0x800) {
      bytes.push(0xC0 | (code >> 6), 0x80 | (code & 0x3F))
    } else {
      bytes.push(0xE0 | (code >> 12), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F))
    }
  }
  return bytes
}
// 字节数组转字符串（手写UTF-8解码，兼容安卓WebView不支持TextDecoder的情况）
function _bytes2str(b) {
  let str = ''
  let i = 0
  while (i < b.length) {
    const byte1 = b[i]
    if (byte1 < 0x80) {
      // 单字节 ASCII
      str += String.fromCharCode(byte1)
      i += 1
    } else if ((byte1 & 0xE0) === 0xC0) {
      // 双字节
      const code = ((byte1 & 0x1F) << 6) | (b[i+1] & 0x3F)
      str += String.fromCharCode(code)
      i += 2
    } else if ((byte1 & 0xF0) === 0xE0) {
      // 三字节（中文在这里）
      const code = ((byte1 & 0x0F) << 12) | ((b[i+1] & 0x3F) << 6) | (b[i+2] & 0x3F)
      str += String.fromCharCode(code)
      i += 3
    } else if ((byte1 & 0xF8) === 0xF0) {
      // 四字节（emoji等）
      const code = ((byte1 & 0x07) << 18) | ((b[i+1] & 0x3F) << 12) | ((b[i+2] & 0x3F) << 6) | (b[i+3] & 0x3F)
      // 转为代理对
      const cp = code - 0x10000
      str += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF))
      i += 4
    } else {
      i += 1  // 跳过非法字节
    }
  }
  return str
}

// ESP32上线通知：收到 /ready 后标记就绪，若订单已到则立即播报
function _handleReady(data) {
  console.log('[MQTT] ESP32已上线')
  esp32Ready = true
  _tryInitAnnounce()
}

// 双触发检查：ESP32就绪 且 订单已收到，才触发一次初始播报
function _tryInitAnnounce() {
  if (!esp32Ready || !ordersReceived || initAnnounced) return
  initAnnounced = true
  console.log('[MQTT] 双条件满足，触发初始导航播报')
  // 延迟1秒，让ESP32完全就绪
  setTimeout(() => { _triggerInitialAnnounce() }, 1000)
}

// 播报当前最优先的下一步任务
// 规则：有待取餐 → 说取餐；全是配送中 → 说送餐；没有订单 → 说等待
function _triggerInitialAnnounce() {
  // 优先找待取餐订单（shop合并：同一家店只说一次地点）
  const pickup = store.orders.find(o => o.status === 'pending_pickup')
  if (pickup) {
    _publish(PREFIX + '/say', JSON.stringify({ text: '前往' + pickup.shopName + '取餐' }))
    console.log('[导航] 初始播报：前往', pickup.shopName, '取餐')
    return
  }
  // 无待取餐，找配送中的单（找最近的）
  const delivering = store.orders.find(
    o => o.status === 'delivering' || o.status === 'arriving'
  )
  if (delivering) {
    const dest = delivering.address.length > 20
      ? delivering.address.slice(0, 20) : delivering.address
    _publish(PREFIX + '/say', JSON.stringify({ text: '前往' + dest + '送餐' }))
    console.log('[导航] 初始播报：前往', dest, '送餐')
    return
  }
  // 没有订单
  _publish(PREFIX + '/say', JSON.stringify({ text: '暂无配送任务，请等待接单' }))
  console.log('[导航] 初始播报：暂无配送任务')
}

// ===== 模拟数据模式 =====
let mockTimer = null
function _startMock() {
  if (mockTimer) return
  console.log('[模拟] 启动模拟数据')
  store.device.mockMode = true
  mockTimer = setInterval(() => {
    // 模拟温度数据（新格式）
    store.updateDeviceData({
      temperatures: {
        'FL': parseFloat((62 + Math.random()*8).toFixed(1)),
        'FR': parseFloat((61 + Math.random()*8).toFixed(1)),
        'BL': parseFloat((60 + Math.random()*8).toFixed(1)),
        'BR': parseFloat((61 + Math.random()*8).toFixed(1))
      }
    })
    // 模拟速度数据
    store.device.speed = parseFloat((2.8 + Math.random()*4.2).toFixed(2))  // 模拟骑行速度 2.8~7.0 m/s
  }, 3000)
}
