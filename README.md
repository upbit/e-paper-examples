# e-paper-examples

ESP32-S3 驱动 2.13" 墨水屏（SSD1675A / GDEH0213B72）的最简化演示。上电后先跑一次自检
（屏幕全刷标定图 + 板载 RGB 灯白色呼吸 3 秒），然后进入 WiFi 流程：没配过网就开配网热点、
屏幕提示怎么连；配过就直接连上并把 IP 显示在屏上，同时提供一个静态网页服务。
墨水屏断电保图，画面会一直留在屏上。

## 硬件

- 主控：ESP32-S3-DevKitC-1（16MB Flash + 8MB Octal PSRAM）
- 屏幕：2.13" 黑白墨水屏，SSD1675A / IL3897 控制器
- 指示灯：板载 WS2812 RGB LED（GPIO48）

关于分辨率，这块屏有**两套数字**，混用会导致画面偏移和右侧截断：

| | 尺寸 | 说明 |
| --- | --- | --- |
| 控制器帧缓冲 | 250 × 122 | SSD1675A 的 RAM 尺寸（物理 122 × 250，旋转后） |
| **实际可视玻璃** | **212 × 104** | 模块真正能显示的区域，是帧缓冲的一个子矩形 |

可视窗口在帧缓冲里的位置是 **x 从 0 开始，y 从 18 开始**——即左对齐，右边约 38px
和顶部 18 行都在玻璃之外，写进去也看不见。这组偏移取自
[upbit/esp32-epaper-monitor](https://github.com/upbit/esp32-epaper-monitor) 的实测值
（`docs/213_UI_NOTES.md`、`display.cpp` 的 `DISK_UI_*` 常量）。

本项目的图形层直接以**可视区**为坐标系（`GFX_W` × `GFX_H` = 212 × 104，原点在可视区左上角），
偏移在 `gfx_pixel()` 里一次性加上，所以业务代码不用关心帧缓冲的存在。
四个值都可以用 `-DEPD_VIEW_*` 覆盖，换模块只改 `platformio.ini`。

接线见 [docs/pinout.md](docs/pinout.md)，SPI 走 `SPI2_HOST`（FSPI）：

| 墨水屏 | GPIO | 墨水屏 | GPIO |
| ------ | ---: | ------ | ---: |
| SCK    |   12 | DC     |    9 |
| SDA    |   11 | RST    |   14 |
| CS     |   10 | BUSY   |    3 |

引脚在 `platformio.ini` 里用 `-DEPD_PIN_*` 传入，换板子改这里即可，不用动源码。

板载 RGB LED 用 `-DBOARD_RGB_LED_GPIO=48`（在 `common.ini`）。注意官方 v1.1 图纸标的是
GPIO38，但本项目实测这块板在 **GPIO48**；若换板后灯不亮而日志仍打印 `WS2812 ready`，
优先怀疑这个引脚。

## 自检流程

`app_main()` 的顺序：

1. 初始化 LED，起呼吸任务（白色，1s 一次呼吸）
2. 初始化屏幕并全刷标定图（约 1.9s）
3. 屏幕进入深度睡眠
4. 补齐 LED 满 3s 后停灯

LED 呼吸跑在独立 FreeRTOS 任务上，所以刷屏期间灯照常呼吸，两者是并行的。第 4 步用
`esp_timer` 记录起始时刻、只补 `3000ms - 已耗时` 的差额，因此刷屏耗时浮动（温度、批次）
不会让启动被额外拖长。亮度用二次曲线缓动——WS2812 的亮度对字节值近似线性，
直接线性渐变肉眼会觉得"亮得太快"。

正常启动日志：

```
I (752)  led: WS2812 ready on GPIO48
I (755)  epd: SSD1675A ready (122x250)
I (2619) epd: full refresh done
I (2795) epd: hibernated
I (3764) main: self-test done (screen refreshed, LED 3000ms)
```

## WiFi 配网

凭据存在 NVS 里、运行时配置，**换 WiFi 环境不需要重新编译**。

### 首次上电（未配置过）

设备开一个**开放热点**（无密码），屏幕上显示热点名和访问地址：

```
┌──────────────────────────────────────┐
│ WiFi SETUP - connect to hotspot      │
│ 1. Join this WiFi:                   │
│      ESP32-ePaper-CE9819             │
│ 2. Open in browser:                  │
│       http://192.168.4.1             │
│         no password needed           │
└──────────────────────────────────────┘
```

热点名后缀取 SoftAP MAC 的后 3 字节，多台设备不会撞名。用手机连上它，浏览器打开
`http://192.168.4.1`，就是配网页：可以从扫描列表里点选网络（带信号强度和加密标识），
也可以手动输入隐藏 SSID。

填好密码点「保存并连接」后，设备**先实际连一次再落盘**：

- 密码错 → 页面立刻报错（"wrong password"），热点不断，可以马上重填
- 连上了 → 页面提示成功 → 写入 NVS → 自动重启进 STA 模式

### 配置之后

重启后直接连保存的网络，屏幕显示拿到的 IP：

```
┌──────────────────────────────────────┐
│ WiFi CONNECTED                       │
│ Open in browser:                     │
│        192.168.66.183                │
│ SSID: MyHome_5G                      │
│    hold BOOT 3s to reconfigure       │
└──────────────────────────────────────┘
```

访问这个 IP 就是 `www/` 下的静态页面（设备状态页）。

30 秒内没连上会显示失败原因并提示按键重置，但**后台仍在退避重试**（1s→2s→4s→8s→15s→30s），
连上后会自动刷新成上面的成功画面。这里刻意不做"连不上就退回热点"——避免路由器
重启期间设备自己跑掉。

### 重新配网

两种方式，任选：

- **长按 BOOT 按钮（GPIO0）3 秒** — 硬件兜底，连不上网、打不开页面时也能恢复
- **状态页上的「清除 WiFi 配置并重启」按钮** — 需要能访问到设备

两者都是清空 NVS 凭据后重启，回到配网热点。

### HTTP 接口

配网模式（AP，`192.168.4.1`）：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/` | 配网页（内嵌在固件里） |
| GET | `/api/scan` | 扫描周边网络 → JSON |
| POST | `/api/save` | 提交凭据，触发连接验证 |
| GET | `/api/status` | 轮询连接进度 |

STA 模式：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/*` | SPIFFS 静态文件，`/` → `index.html` |
| GET | `/api/status` | 设备状态 JSON |
| POST | `/api/reset` | 清凭据并重启 |

## 构建与烧录

```bash
pio run              # 编译
pio run -t upload    # 烧录固件
pio device monitor   # 串口日志，115200
```

`www/` 目录下的静态页面单独烧到 SPIFFS 分区，**改了 www 只需重跑这两步**，不用重刷固件：

```bash
pio run -t buildfs   # 打包 www/ 成 spiffs.bin
pio run -t uploadfs  # 烧到 storage 分区
```

首次烧录建议两步都做。配网页是编进固件的，所以**即使没跑过 `uploadfs` 也能正常配网**——
只是配完后访问 IP 会看到一个提示你去执行 `uploadfs` 的 404 页。

## 画面说明

画面按 **横向 212 × 104**（可视区）坐标系绘制，原点在可视区左上角。

```
┌──────────────────────────────────────┐
│■■  TL  ' ' ' 50' ' ' 100' ' '150 TR ◎│
│                                      │
│              212x104                 │
│ 50─               ┌─┐                │
│                  ─┼─┼─               │
│100─               └─┘                │
│            FB 250x122 Y+18           │
│           SSD1675A 2.13in            │
│◣   BL                            BR ▨│
└──────────────────────────────────────┘
```

- **外边框**：贴着 `(0,0)`–`(211,103)` 画一圈。如果四条边都能完整看到，说明可视区
  偏移配对了；某条边看不见就按下面的方法校准
- **刻度**：四条边每 10px 一个短刻度，每 50px 一个长刻度并标数字；
  上/下边是 X 轴，左/右边是 Y 轴
- **四角图案各不相同**，用来判断上下左右和镜像：
  - 左上：实心方块 + `TL`
  - 右上：同心圆 + `TR`
  - 左下：直角三角形（直角在左下）+ `BL`
  - 右下：棋盘格 + `BR`
- **正中**：十字 + 圆圈，标出 `(106, 52)`
- **`FB 250x122 Y+18`**：当前编译进固件的帧缓冲尺寸和 Y 偏移，方便核对配置

### 如果画面仍然偏移或截断

`212 × 104 / Y+18` 是参考项目在它那块模块上的实测值，同型号不同批次可能有差异。
症状与对策：

| 症状 | 含义 | 调整 |
| --- | --- | --- |
| 右侧内容被切 | 可视区比 212 窄 | 减小 `EPD_VIEW_W` |
| 右边空出一条白边 | 可视区比 212 宽 | 增大 `EPD_VIEW_W` |
| 上方被切 / 下方留白 | `Y` 偏移偏小 | 增大 `EPD_VIEW_Y` |
| 下方被切 / 上方留白 | `Y` 偏移偏大 | 减小 `EPD_VIEW_Y` |

四条边的刻度就是标尺：数一下每边第一个能看清的刻度是多少，差值即需要修正的像素数。
改 `platformio.ini` 里的 `-DEPD_VIEW_*` 重刷即可，源码不用动。

## 代码结构

| 文件 | 作用 |
| ---- | ---- |
| `src/epd_ssd1675a.c/.h` | 屏幕驱动：SPI + GPIO 初始化、复位、LUT、RAM 寻址、全刷、深度睡眠 |
| `src/epd_gfx.c/.h`      | 图形层：DMA 帧缓冲、旋转 + 可视区偏移映射、点/线/矩形/圆/文字 |
| `src/font5x7.h`         | 5×7 点阵 ASCII 字库（0x20–0x7E） |
| `src/screens.c/.h`      | 所有墨水屏页面：标定图、配网提示、连接成功、连接失败 |
| `src/board_led.c/.h`    | 板载 WS2812 呼吸灯（后台任务，二次曲线缓动） |
| `src/board_button.c/.h` | BOOT 按钮长按 3s 清除 WiFi 凭据 |
| `src/wifi_prov.c/.h`    | 凭据 NVS 读写、AP/STA 模式、连接验证、退避重连 |
| `src/web_portal.c/.h`   | httpd：配网接口 + SPIFFS 静态文件服务 |
| `src/storage.c/.h`      | SPIFFS 挂载 |
| `src/main.c`            | 编排：自检 → NVS → 挂载 → 按键监听 → 配网或连接 |
| `portal/portal.html`    | 配网页，编进固件（不依赖 SPIFFS） |
| `www/index.html`        | 设备状态页，烧到 SPIFFS |
| `src/idf_component.yml` | 声明 `led_strip` / `cjson` 依赖，由 component manager 拉取 |

三点约定：

- 帧缓冲 **bit = 1 表示白色**，直接写入 RAM 命令 `0x24`，不做取反
- `gfx_pixel()` 一次完成 90° 旋转和可视区平移：

  ```c
  px = 121 - (y + EPD_VIEW_Y);   // 逻辑 y -> 物理列，含可视区偏移
  py =  x  + EPD_VIEW_X;         // 逻辑 x -> 物理行
  ```

  即把横向 212 × 104 的**可视区逻辑坐标**映射到竖向 122 × 250 的物理 RAM。
  越界的点直接丢弃，所以画到可视区外不会绕行到别的位置。
- 所有动态数据走 `/api/*` 返回 **JSON**，前端一律用 `textContent` 渲染，
  不做 HTML 字符串拼接。SSID 是用户可控输入，带引号或尖括号都不会破页面

刷新流程：`0x74/0x7E` 模拟与数字模块控制 → `0x01` 驱动输出 → 电压/时序寄存器 →
`0x32` 写全刷 LUT → `0x22/0x20` 上电 → `0x24` 与 `0x26` 双缓冲写同一帧 →
`0x22 0xC4` + `0x20` 触发刷新 → `0x10` 进入深度睡眠。BUSY 高电平为忙，超时 30s。

### 分区表

`partitions16.csv`（16MB）：

| 分区 | 类型 | 偏移 | 大小 | 用途 |
| --- | --- | --- | --- | --- |
| `nvs` | data/nvs | 0x9000 | 24KB | WiFi 凭据 + 驱动自用 |
| `phy_init` | data/phy | 0xf000 | 4KB | 射频校准 |
| `factory` | app | 0x10000 | 4MB | 固件（当前用 21%） |
| `storage` | data/spiffs | 0x410000 | 4MB | `www/` 静态文件 |

改动分区表后**必须完整 `pio run -t upload`**（含分区表），只刷 app 会错位。
`nvs` 偏移保持在 0x9000 不变，所以升级不会丢已保存的 WiFi。

### 配网页为何编进固件

`portal/portal.html` 通过 `board_build.embed_txtfiles` + CMake 的 `EMBED_TXTFILES`
编入 `.rodata`，而不是放 SPIFFS。原因是新板子刷完固件时 SPIFFS 还是空的，
如果配网页也依赖 SPIFFS，就会「进不了配网页 → 连不上网 → 没法上传文件」形成死锁。

这两个机制必须**同时**配置：PlatformIO 的 shim 负责生成 `.S` 汇编文件，
CMake 的 `EMBED_TXTFILES` 负责把它声明为源文件并链接。只配任何一个都会失败——
只配 PIO 侧会 `undefined reference to _binary_portal_html_start`；
只配 CMake 侧会 `Source portal.html.S not found`（PlatformIO 只用 CMake 做配置、
自己用 SCons 编译，从不执行 ninja 的自定义 target）。

## 参考

初始化序列、LUT 和刷新时序取自 [upbit/esp32-epaper-monitor](https://github.com/upbit/esp32-epaper-monitor)，
其驱动又是从 [GxEPD2](https://github.com/ZinggJM/GxEPD2)（`GxEPD2_213_B72.cpp`，GPL-3.0）移植的。
本项目把那份 C++ 实现改写成了 C，并砍到只保留全刷所需的部分。
