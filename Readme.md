# ESP32-WOL-Controller

基于 ESP32 / WT32-ETH01 的远程开机控制器。
设备通过 Wi-Fi 接入网络和点灯科技云平台，通过 RJ45 网口直连电脑有线网卡，在收到 App 按钮、定时任务或本地网页指令后发送 Wake-on-LAN 魔术包，实现笔记本电脑远程开机。

## 功能特点

* 支持 Wake-on-LAN 远程开机
* 支持点灯科技 App 远程控制
* 支持点灯科技 App 定时/倒计时触发开机
* 支持 ESP32 自建热点网页配网
* 支持 Wi-Fi 密码为空，适配校园网开放网络/无感认证环境
* 支持网页填写 Wi-Fi SSID、Wi-Fi 密码、点灯科技 Auth Key、目标电脑 MAC 地址
* 支持本地网页测试发送 WOL
* 支持通过点灯 App 下发新的目标 MAC 地址
* 以太网口只用于发送 WOL，不作为电脑默认网关，避免影响电脑 Wi-Fi 上网

## 项目原理

普通笔记本远程开机通常需要有线网卡保持连接并接收 WOL 魔术包。但在只有 Wi-Fi 的环境下，电脑关机后 Wi-Fi 网卡通常无法接收远程唤醒包。

本项目使用 ESP32 作为中间控制器：

```text
点灯科技 App / 本地网页
        ↓
ESP32 Wi-Fi 接收控制指令
        ↓
ESP32 RJ45 网口发送 WOL 魔术包
        ↓
笔记本电脑有线网卡接收魔术包
        ↓
电脑开机
```

其中 Wi-Fi 用于联网和云端控制，RJ45 网口仅用于和电脑有线网卡建立链路并发送 WOL 包。

## 硬件需求

推荐使用：

* WT32-ETH01 开发板
  ESP32 + LAN8720 + RJ45 网口
* USB-TTL 下载器
* 5V 电源
* 一根网线
* 支持 Wake-on-LAN 的电脑有线网卡

WT32-ETH01 常见烧录接线：

```text
USB-TTL TX  → WT32-ETH01 RX
USB-TTL RX  → WT32-ETH01 TX
USB-TTL GND → WT32-ETH01 GND
USB-TTL 5V  → WT32-ETH01 5V
```

进入下载模式：

```text
IO0 接 GND
重新上电或按复位
烧录完成后断开 IO0 与 GND
重新上电正常启动
```

## 软件环境

推荐使用：

* VS Code
* PlatformIO
* Arduino Framework
* Blinker 点灯科技库

### platformio.ini

```ini
[env:wt32_eth01]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600

lib_deps =
    https://github.com/blinker-iot/blinker-library.git

build_flags =
    -DCORE_DEBUG_LEVEL=1
```

如果编译时遇到库兼容问题，可以尝试固定 ESP32 平台版本：

```ini
platform = espressif32@6.7.0
```

## 使用方法

### 1. 烧录程序

使用 PlatformIO 上传程序。

烧录完成后打开串口监视器，波特率设置为：

```text
115200
```

首次启动时，如果没有保存过配置，ESP32 会自动开启配网热点。

串口会输出类似内容：

```text
AP SSID: WOL-SETUP-XXXXXX
AP PASS: 12345678
配置地址: http://192.168.4.1
```

### 2. 进入网页配置

手机或电脑连接 ESP32 的配网热点：

```text
WOL-SETUP-XXXXXX
```

热点密码：

```text
12345678
```

浏览器打开：

```text
http://192.168.4.1
```

在网页中填写：

```text
Wi-Fi 名称 SSID
Wi-Fi 密码，可为空
点灯科技设备密钥 Auth Key
笔记本有线网卡 MAC 地址
```

保存后设备会自动重启并连接 Wi-Fi 与点灯科技。

### 3. 校园网无感认证说明

如果使用校园网开放 Wi-Fi，且需要在个人后台添加 MAC 地址进行无感认证，请添加网页中显示的：

```text
ESP32 Wi-Fi MAC
```

不要添加：

```text
ESP32 ETH MAC
```

因为校园网认证系统看到的是 ESP32 接入无线网络时的 Wi-Fi MAC，而不是 RJ45 网口的 MAC。

### 4. 点灯科技 App 配置

在点灯科技 App 中添加独立设备，获取设备 Auth Key，并填入 ESP32 配置网页。

App 设备界面建议添加以下组件：

#### 远程开机按钮

```text
组件类型：按键
数据键名：btn-wol
名称：远程开机
```

#### 状态文本

```text
组件类型：文本
数据键名：tex-status
名称：状态
```

代码中对应的组件键名为：

```cpp
BlinkerButton ButtonWOL("btn-wol");
BlinkerText TextStatus("tex-status");
```

按下 `btn-wol` 按钮后，ESP32 会从 RJ45 网口发送 WOL 魔术包。

### 5. 定时开机配置

点灯科技 App 中可以添加定时组件，但定时任务需要先配置可执行动作。

操作流程：

```text
设备界面
→ 进入设备管理
→ 动作配置 / 动作编辑
→ 自动生成动作
→ 选择 btn-wol 对应的动作
→ 保存
```

然后进入定时组件：

```text
添加定时任务
→ 设置任务时间
→ 设置重复方式
→ 执行动作选择“远程开机”或 btn-wol 对应动作
→ 保存
```

定时任务到点后会触发 `btn-wol`，效果等同于手动点击远程开机按钮。

如果 App 支持自定义指令，也可以下发：

```text
WOL
```

或：

```text
BOOT
```

或：

```text
POWERON
```

程序会识别这些命令并发送 WOL 魔术包。

## 电脑端设置

### BIOS/UEFI 设置

进入电脑 BIOS/UEFI，开启类似选项：

```text
Wake on LAN
Wake from S5
Power On by PCI-E
Power On by LAN
```

不同品牌电脑名称可能不同。

### Windows 有线网卡设置

进入：

```text
设备管理器
→ 网络适配器
→ 有线网卡
→ 属性
```

在“电源管理”中勾选：

```text
允许此设备唤醒计算机
只允许幻数据包唤醒计算机
```

在“高级”中启用类似选项：

```text
Wake on Magic Packet
Shutdown Wake-On-Lan
WOL & Shutdown Link Speed
```

### 避免插上网线后电脑断网

ESP32 的 RJ45 网口只用于发送 WOL，不提供正常上网功能。
电脑插上 ESP32 后，以太网可能显示：

```text
未识别的网络
无 Internet
```

这是正常现象。

为了避免 Windows 优先使用这张无 Internet 的以太网卡，可以给以太网配置“备用配置”。

推荐设置：

```text
以太网 IPv4：
常规：自动获得 IP 地址
备用配置：用户配置

IP 地址：172.31.250.2
子网掩码：255.255.255.0
默认网关：留空
DNS：留空
```

这样：

```text
插正常网线时：自动 DHCP 获取 IP，正常有线上网
插 ESP32 时：使用备用 IP，仅用于 WOL 链路
```

## 网络结构

正常运行时：

```text
点灯科技 App
        ↓
点灯科技云平台
        ↓
校园网 / 家庭 Wi-Fi
        ↓
ESP32 / WT32-ETH01
        ↓ RJ45
笔记本电脑有线网卡
```

电脑开机后：

```text
电脑 Wi-Fi：正常上网
电脑有线网卡：只保持 WOL 链路
```

## 本地网页功能

设备正常连接 Wi-Fi 后，串口会输出本地配置页地址，例如：

```text
本地配置页: http://192.168.1.88
```

打开该地址可以：

* 修改 Wi-Fi 配置
* 修改点灯科技 Auth Key
* 修改目标电脑 MAC 地址
* 本地测试发送 WOL
* 清空配置

## 远程修改目标 MAC

支持通过点灯科技 App 调试区或自定义数据下发新的目标电脑 MAC 地址：

```text
MAC=AA:BB:CC:DD:EE:FF
```

设备收到后会保存到 NVS，重启后仍然生效。

## 常见问题

### 1. 点灯 App 显示设备离线

检查：

* Wi-Fi SSID 是否正确
* Wi-Fi 密码是否正确
* 校园网是否已经绑定 ESP32 Wi-Fi MAC
* 点灯科技 Auth Key 是否正确
* 校园网是否允许 ESP32 访问外网

### 2. 手动点击按钮有反应，但电脑不开机

检查：

* 目标 MAC 是否填写的是电脑“有线网卡”的 MAC
* 电脑 BIOS 是否开启 WOL
* Windows 有线网卡是否允许唤醒
* 电脑关机后有线网口灯是否仍然亮
* 网线是否直连 ESP32 与电脑有线网口

### 3. 定时任务显示“该设备无可用动作”

需要先在点灯 App 中为设备生成动作：

```text
设备管理
→ 动作配置 / 动作编辑
→ 自动生成
→ 保存
```

然后再回到定时任务中选择执行动作。

### 4. 插上 ESP32 网线后电脑没网

这是因为 Windows 可能优先选择了无 Internet 的以太网。
建议给以太网设置备用配置，或者提高以太网接口跃点数，让 Wi-Fi 优先上网。

### 5. 为什么不用普通 ESP32 开发板？

普通 ESP32 只有 Wi-Fi，没有 RJ45 网口。
本项目需要电脑有线网卡保持链路并接收 WOL 魔术包，因此推荐使用 WT32-ETH01 或 ESP32 + LAN8720 以太网模块。

## 注意事项

* ESP32 必须保持供电
* 电脑必须支持 Wake-on-LAN
* 电脑关机后有线网卡需要保持供电
* 校园网环境下需要确保 ESP32 能正常访问点灯科技云端
* ETH 虚拟网段默认使用 `172.31.250.0/24`，不要和当前 Wi-Fi 网络网段冲突

## 参考项目

本项目参考了以下思路：

* ESP8266/ESP32 通过点灯科技触发 WOL
* ESP32 WebServer 配网
* ESP32 Preferences 持久化保存配置
* WT32-ETH01 / LAN8720 以太网通信
* Wake-on-LAN 魔术包格式

与普通 WOL 项目不同的是，本项目采用：

```text
Wi-Fi 负责联网
RJ45 负责直连电脑发送 WOL
```

因此 WOL 包不是从 Wi-Fi 局域网广播，而是从 ESP32 的以太网口定向广播到电脑有线网卡所在的虚拟网段。

## License

本项目仅供学习和个人使用。
如需开源发布，建议根据实际情况选择 MIT、Apache-2.0 或 GPL 协议。
