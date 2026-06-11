#define BLINKER_WIFI

#include <Arduino.h>
#include <ctype.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <Blinker.h>

// =========================
// WT32-ETH01 / LAN8720 配置
// 如果你不是 WT32-ETH01，而是其他 ESP32 + LAN8720 模块，
// 这里的 PHY 地址和引脚可能需要按你的模块修改。
// =========================
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  1
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER 16
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN

#include <ETH.h>

#if __has_include(<esp_arduino_version.h>)
  #include <esp_arduino_version.h>
#endif

// =========================
// 点灯科技组件键名
// App 里的按钮数据键名必须是 btn-wol
// App 里的文本数据键名必须是 tex-status
// =========================
BlinkerButton ButtonWOL("btn-wol");
BlinkerText TextStatus("tex-status");

// =========================
// Web 配网相关
// =========================
WebServer webServer(80);
DNSServer dnsServer;
Preferences prefs;

const byte DNS_PORT = 53;
bool portalMode = false;
bool blinkerReady = false;

// =========================
// 保存的配置
// =========================
String cfgSSID;
String cfgPASS;
String cfgAUTH;
String cfgMAC;

char blinkerAuth[96];
char wifiSSID[64];
char wifiPASS[64];

uint8_t TARGET_MAC[6];

// =========================
// 以太网口虚拟网段
// 不要和校园网/家里 Wi-Fi 网段重复。
// 这个 ETH 口只负责给笔记本有线网卡发 WOL，不做网关。
// =========================
IPAddress ETH_IP(172, 31, 250, 1);
IPAddress ETH_GW(0, 0, 0, 0);
IPAddress ETH_MASK(255, 255, 255, 0);
IPAddress ETH_BROADCAST(172, 31, 250, 255);

WiFiUDP udp;

// =========================
// 工具函数
// =========================

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

void copyToCharArray(char *dst, size_t dstSize, const String &src) {
  memset(dst, 0, dstSize);
  snprintf(dst, dstSize, "%s", src.c_str());
}

bool parseMacString(String mac, uint8_t out[6]) {
  mac.trim();
  mac.replace(":", "");
  mac.replace("-", "");
  mac.replace(" ", "");

  if (mac.length() != 12) return false;

  for (int i = 0; i < 12; i++) {
    if (!isxdigit((unsigned char)mac[i])) return false;
  }

  for (int i = 0; i < 6; i++) {
    String byteStr = mac.substring(i * 2, i * 2 + 2);
    out[i] = (uint8_t) strtoul(byteStr.c_str(), nullptr, 16);
  }

  return true;
}

String macToString(const uint8_t mac[6]) {
  char buf[18];
  snprintf(
    buf,
    sizeof(buf),
    "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
  );
  return String(buf);
}

String normalizeMacString(String mac) {
  uint8_t tmp[6];
  if (!parseMacString(mac, tmp)) return "";
  return macToString(tmp);
}

// =========================
// 配置读取/保存
// =========================

void loadConfig() {
  prefs.begin("wolcfg", true);

  cfgSSID = prefs.getString("ssid", "");
  cfgPASS = prefs.getString("pass", "");
  cfgAUTH = prefs.getString("auth", "");
  cfgMAC  = prefs.getString("mac", "");

  prefs.end();

  cfgSSID.trim();
  cfgAUTH.trim();
  cfgMAC.trim();

  Serial.println();
  Serial.println("========== 已保存配置 ==========");
  Serial.print("Wi-Fi SSID: ");
  Serial.println(cfgSSID);
  Serial.print("Wi-Fi PASS: ");
  Serial.println(cfgPASS.length() > 0 ? "(已填写)" : "(空密码/开放网络)");
  Serial.print("Blinker AUTH: ");
  Serial.println(cfgAUTH.length() > 0 ? "(已填写)" : "(未填写)");
  Serial.print("Target MAC: ");
  Serial.println(cfgMAC);
  Serial.println("================================");
}

bool hasValidConfig() {
  if (cfgSSID.length() == 0) return false;
  if (cfgAUTH.length() == 0) return false;
  if (!parseMacString(cfgMAC, TARGET_MAC)) return false;
  return true;
}

void saveConfigFromWeb() {
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  String auth = webServer.arg("auth");
  String mac  = webServer.arg("mac");

  ssid.trim();
  auth.trim();
  mac.trim();

  uint8_t testMac[6];

  if (ssid.length() == 0) {
    webServer.send(400, "text/plain; charset=utf-8", "Wi-Fi 名称不能为空");
    return;
  }

  if (auth.length() == 0) {
    webServer.send(400, "text/plain; charset=utf-8", "点灯科技设备密钥不能为空");
    return;
  }

  if (!parseMacString(mac, testMac)) {
    webServer.send(400, "text/plain; charset=utf-8", "电脑有线网卡 MAC 格式错误，例如 AA:BB:CC:DD:EE:FF");
    return;
  }

  String normalizedMac = macToString(testMac);

  prefs.begin("wolcfg", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("auth", auth);
  prefs.putString("mac", normalizedMac);
  prefs.end();

  String page =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>保存成功</title></head><body>"
    "<h2>保存成功</h2>"
    "<p>设备将在 2 秒后重启，并尝试连接 Wi-Fi 与点灯科技。</p>"
    "</body></html>";

  webServer.send(200, "text/html; charset=utf-8", page);

  delay(2000);
  ESP.restart();
}

void clearConfig() {
  prefs.begin("wolcfg", false);
  prefs.clear();
  prefs.end();

  webServer.send(200, "text/plain; charset=utf-8", "配置已清空，2 秒后重启");
  delay(2000);
  ESP.restart();
}

// =========================
// WOL 发包
// =========================

bool sendOneWolPacket(IPAddress ip, uint16_t port) {
  uint8_t packet[102];

  for (int i = 0; i < 6; i++) {
    packet[i] = 0xFF;
  }

  for (int i = 1; i <= 16; i++) {
    memcpy(&packet[i * 6], TARGET_MAC, 6);
  }

  if (!udp.beginPacket(ip, port)) {
    Serial.println("UDP beginPacket failed.");
    return false;
  }

  size_t written = udp.write(packet, sizeof(packet));
  bool ok = udp.endPacket();

  Serial.printf(
    "WOL packet to %s:%u, written=%u, ok=%d\n",
    ip.toString().c_str(),
    port,
    (unsigned int)written,
    ok ? 1 : 0
  );

  return ok && written == sizeof(packet);
}

bool sendWOL() {
  if (!parseMacString(cfgMAC, TARGET_MAC)) {
    Serial.println("Invalid target MAC, WOL aborted.");
    return false;
  }

  Serial.println();
  Serial.println("========== 发送 WOL ==========");
  Serial.print("Target MAC: ");
  Serial.println(macToString(TARGET_MAC));
  Serial.print("ETH broadcast: ");
  Serial.println(ETH_BROADCAST);
  Serial.println("==============================");

  udp.begin(40000);

  bool anyOK = false;

  // 端口 9 最常用，端口 7 也兼容发一下
  for (int i = 0; i < 5; i++) {
    anyOK |= sendOneWolPacket(ETH_BROADCAST, 9);
    delay(120);
    anyOK |= sendOneWolPacket(ETH_BROADCAST, 7);
    delay(120);
  }

  return anyOK;
}

// =========================
// Web 页面
// =========================

void handleSendWolWeb() {
  if (!hasValidConfig()) {
    webServer.send(400, "text/plain; charset=utf-8", "配置无效，不能发送 WOL");
    return;
  }

  bool ok = sendWOL();

  if (ok) {
    webServer.send(200, "text/plain; charset=utf-8", "WOL 已发送");
  } else {
    webServer.send(500, "text/plain; charset=utf-8", "WOL 发送失败，请检查 ETH 链路和 MAC");
  }
}

void handleRoot() {
  String wifiMac = WiFi.macAddress();
  String ethMac = ETH.macAddress();

  String wifiStatus;
  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus = "已连接，IP：" + WiFi.localIP().toString();
  } else {
    wifiStatus = "未连接";
  }

  String page =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 WOL 配置</title>"
    "<style>"
    "body{font-family:Arial,'Microsoft YaHei',sans-serif;margin:20px;background:#f6f7f9;color:#222;}"
    ".card{max-width:560px;margin:auto;background:#fff;padding:20px;border-radius:14px;box-shadow:0 4px 18px rgba(0,0,0,.08);}"
    "h2{margin-top:0;}"
    "label{display:block;margin-top:14px;font-weight:bold;}"
    "input{width:100%;box-sizing:border-box;padding:10px;margin-top:6px;border:1px solid #ccc;border-radius:8px;font-size:15px;}"
    "button{margin-top:18px;width:100%;padding:12px;border:0;border-radius:10px;background:#1677ff;color:white;font-size:16px;}"
    ".danger{background:#d93025;}"
    ".green{background:#16a34a;}"
    ".tip{font-size:13px;color:#666;line-height:1.7;}"
    "code{background:#eee;padding:2px 4px;border-radius:4px;}"
    "</style></head><body><div class='card'>"

    "<h2>ESP32 远程开机 WOL 配置</h2>"

    "<p class='tip'>首次使用：连接热点 <code>WOL-SETUP-xxxx</code>，打开 <code>192.168.4.1</code>。"
    "校园网无感认证请绑定下面的 <b>ESP32 Wi-Fi MAC</b>。</p>"

    "<p class='tip'>ESP32 Wi-Fi MAC：<code>" + wifiMac + "</code><br>"
    "ESP32 ETH MAC：<code>" + ethMac + "</code><br>"
    "注意：校园网后台一般绑定 Wi-Fi MAC，不是 ETH MAC。</p>"

    "<form action='/save' method='POST'>"

    "<label>Wi-Fi 名称 SSID</label>"
    "<input name='ssid' value='" + htmlEscape(cfgSSID) + "' placeholder='例如 SDU-WLAN / CMCC / 家里WiFi' required>"

    "<label>Wi-Fi 密码，可空</label>"
    "<input name='pass' value='" + htmlEscape(cfgPASS) + "' placeholder='校园开放网络/无感认证可留空'>"

    "<label>点灯科技设备密钥 Auth Key</label>"
    "<input name='auth' value='" + htmlEscape(cfgAUTH) + "' placeholder='Blinker App 添加设备后得到的 Secret Key' required>"

    "<label>笔记本有线网卡 MAC 地址</label>"
    "<input name='mac' value='" + htmlEscape(cfgMAC) + "' placeholder='AA:BB:CC:DD:EE:FF' required>"

    "<button type='submit'>保存并重启</button>"
    "</form>";

  if (hasValidConfig()) {
    page +=
      "<form action='/sendwol' method='POST'>"
      "<button class='green' type='submit'>本地测试发送 WOL</button>"
      "</form>";
  }

  page +=
    "<form action='/clear' method='POST'>"
    "<button class='danger' type='submit'>清空配置</button>"
    "</form>"

    "<p class='tip'>当前模式："
    + String(portalMode ? "配网热点模式" : "正常联网模式")
    + "<br>Wi-Fi 状态：" + wifiStatus
    + "<br>ETH IP：<code>" + ETH.localIP().toString() + "</code>"
    + "<br>目标电脑 MAC：<code>" + htmlEscape(cfgMAC) + "</code>"
    + "</p>"

    "</div></body></html>";

  webServer.send(200, "text/html; charset=utf-8", page);
}

void setupWebRoutes() {
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/save", HTTP_POST, saveConfigFromWeb);
  webServer.on("/clear", HTTP_POST, clearConfig);
  webServer.on("/sendwol", HTTP_POST, handleSendWolWeb);

  webServer.onNotFound([]() {
    if (portalMode) {
      webServer.sendHeader("Location", "http://192.168.4.1/", true);
      webServer.send(302, "text/plain", "");
    } else {
      webServer.send(404, "text/plain; charset=utf-8", "Not found");
    }
  });

  webServer.begin();
}

void startConfigPortal() {
  portalMode = true;

  String apName = "WOL-SETUP-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  apName.toUpperCase();

  WiFi.mode(WIFI_AP_STA);

  const char *apPass = "12345678";

  WiFi.softAP(apName.c_str(), apPass);

  IPAddress apIP = WiFi.softAPIP();

  dnsServer.start(DNS_PORT, "*", apIP);
  setupWebRoutes();

  Serial.println();
  Serial.println("========== 配网热点已开启 ==========");
  Serial.print("AP SSID: ");
  Serial.println(apName);
  Serial.print("AP PASS: ");
  Serial.println(apPass);
  Serial.print("配置地址: http://");
  Serial.println(apIP);
  Serial.println("====================================");
}

// =========================
// 以太网启动
// =========================

void startEthernet() {
  Serial.println();
  Serial.println("Starting Ethernet...");

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  // Arduino-ESP32 v3.x
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);
#else
  // Arduino-ESP32 v2.x
  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);
#endif

  delay(1200);

  bool ok = ETH.config(ETH_IP, ETH_GW, ETH_MASK, ETH_GW, ETH_GW);

  Serial.print("ETH config result: ");
  Serial.println(ok ? "OK" : "FAILED");

  Serial.print("ETH IP: ");
  Serial.println(ETH.localIP());

  Serial.print("ETH MAC: ");
  Serial.println(ETH.macAddress());
}

// =========================
// 点灯科技回调
// =========================

void wolButtonCallback(const String &state) {
  Serial.print("Blinker button state: ");
  Serial.println(state);

  bool ok = sendWOL();

  if (ok) {
    ButtonWOL.text("远程开机");
    ButtonWOL.color("#00A90C");
    ButtonWOL.print("off");

    TextStatus.print("WOL 魔术包已从 RJ45 口发送");
    Blinker.print("result", "success");
  } else {
    ButtonWOL.text("发送失败");
    ButtonWOL.color("#EA0909");
    ButtonWOL.print("off");

    TextStatus.print("WOL 发送失败，请检查 MAC 或 ETH 链路");
    Blinker.print("result", "failed");
  }
}

// 支持从点灯 App 调试/文本输入下发：
// MAC=AA:BB:CC:DD:EE:FF
void dataRead(const String &data) {
  Serial.print("Blinker raw data: ");
  Serial.println(data);

  if (data.startsWith("MAC=")) {
    String newMac = data.substring(4);
    newMac.trim();

    uint8_t testMac[6];

    if (parseMacString(newMac, testMac)) {
      String normalizedMac = macToString(testMac);

      prefs.begin("wolcfg", false);
      prefs.putString("mac", normalizedMac);
      prefs.end();

      cfgMAC = normalizedMac;
      memcpy(TARGET_MAC, testMac, 6);

      TextStatus.print("MAC 已更新：" + normalizedMac);
      Blinker.print("MAC", normalizedMac);

      Serial.print("Target MAC updated: ");
      Serial.println(normalizedMac);
    } else {
      TextStatus.print("MAC 格式错误");
      Blinker.print("MAC", "invalid");
    }
  }
}

void heartbeat() {
  ButtonWOL.text("远程开机");
  ButtonWOL.color("#1677FF");
  ButtonWOL.print("off");

  if (hasValidConfig()) {
    TextStatus.print("设备在线，目标 MAC：" + normalizeMacString(cfgMAC));
  } else {
    TextStatus.print("配置无效，请进入网页配置");
  }
}

// =========================
// Wi-Fi 与 Blinker
// =========================

bool connectWiFiManually() {
  Serial.println();
  Serial.print("Connecting Wi-Fi: ");
  Serial.println(cfgSSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  if (cfgPASS.length() == 0) {
    // 开放网络/校园网无感认证
    WiFi.begin(cfgSSID.c_str());
  } else {
    WiFi.begin(cfgSSID.c_str(), cfgPASS.c_str());
  }

  unsigned long start = millis();
  const unsigned long timeoutMs = 25000;

  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected, IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("Wi-Fi MAC，校园网无感认证绑定这个: ");
    Serial.println(WiFi.macAddress());

    return true;
  }

  Serial.println("Wi-Fi connect failed.");
  return false;
}

void startBlinker() {
  copyToCharArray(blinkerAuth, sizeof(blinkerAuth), cfgAUTH);
  copyToCharArray(wifiSSID, sizeof(wifiSSID), cfgSSID);
  copyToCharArray(wifiPASS, sizeof(wifiPASS), cfgPASS);

  BLINKER_DEBUG.stream(Serial);

  Blinker.begin(blinkerAuth, wifiSSID, wifiPASS);

  ButtonWOL.attach(wolButtonCallback);
  Blinker.attachData(dataRead);
  Blinker.attachHeartbeat(heartbeat);

  blinkerReady = true;

  Serial.println("Blinker started.");
}

// =========================
// 主程序
// =========================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("====================================");
  Serial.println("ESP32 / WT32-ETH01 WOL Controller");
  Serial.println("====================================");

  // 先启动 ETH，保证笔记本关机时有线网卡能看到链路
  startEthernet();

  loadConfig();

  if (!hasValidConfig()) {
    Serial.println("No valid config, starting config portal.");
    startConfigPortal();
    return;
  }

  bool wifiOK = connectWiFiManually();

  if (!wifiOK) {
    Serial.println("Wi-Fi failed, starting config portal.");
    startConfigPortal();
    return;
  }

  portalMode = false;
  setupWebRoutes();

  startBlinker();

  Serial.println();
  Serial.println("========== 正常运行 ==========");
  Serial.print("本地配置页: http://");
  Serial.println(WiFi.localIP());
  Serial.println("点灯 App 中点击 btn-wol 按钮即可发送 WOL。");
  Serial.println("==============================");
}

void loop() {
  webServer.handleClient();

  if (portalMode) {
    dnsServer.processNextRequest();
  }

  if (blinkerReady) {
    Blinker.run();
  }
}