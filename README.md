# Smart Aquaponics System

ระบบนี้คือชุดควบคุมและติดตามคุณภาพน้ำสำหรับงาน aquaponics ที่แยกออกเป็น 2 ฝั่งชัดเจน

- ESP32-S3 ทำหน้าที่อ่านเซ็นเซอร์, ควบคุมอุปกรณ์, และตัดสินใจงานอัตโนมัติที่ต้องตอบสนองเร็ว
- Raspberry Pi ทำหน้าที่เป็น dashboard, MQTT broker ภายในระบบ, OTA endpoint, หน้าทดสอบฮาร์ดแวร์, และเก็บประวัติการใช้งาน

โค้ดใน repository นี้ครอบคลุมทั้ง firmware, Pi web dashboard, test ชุด native, เอกสารทดสอบ, และ flow diagram สำหรับระบบน้ำ

## ภาพรวมระบบ

### สิ่งที่ระบบทำได้ตอนนี้

- อ่านค่าเซ็นเซอร์หลัก 5 กลุ่ม: อุณหภูมิน้ำ, อุณหภูมิอากาศ, ความชื้น, TDS, pH, และแสง
- ควบคุมไฟปลูกพืชตามตารางเวลา หรือสั่ง manual ได้
- ควบคุมเครื่องให้อาหารปลาแบบตั้งเวลา หรือสั่ง manual ได้
- ควบคุมพัดลมระบายอากาศแบบ auto/manual จากอุณหภูมิและความชื้น
- ควบคุมระบบน้ำแยกเป็น circulation, refill, route, overflow protection, และ alarm
- มีระบบ automator สำหรับปรับค่า TDS แบบ staged dosing A -> mix -> B -> cooldown
- ส่งข้อมูลขึ้น NETPIE และส่งข้อมูลภายในระบบไป Raspberry Pi ผ่าน Local MQTT
- ใช้งานผ่าน Web Dashboard, Serial CLI, Telnet, และ OTA
- มี login, role-based access, admin activity logs, และ user management ฝั่ง Pi

### ภาพรวมการไหลของข้อมูล

1. ESP32 อ่านเซ็นเซอร์และเก็บค่าไว้ใน cache
2. TaskControl ใช้ค่าที่อ่านได้ไปตัดสินใจเรื่องไฟ, feeder, fan, ระบบน้ำ, และ automator
3. TaskNetworking ส่งข้อมูลไป Local MQTT ทุก 2 วินาที และ NETPIE ทุก 10 วินาที
4. Raspberry Pi รับข้อมูล, แสดงผลหน้าเว็บแบบ real-time, และบันทึกประวัติสำหรับหน้า graph/logs
5. คำสั่งจาก Pi หรือ NETPIE จะถูกแปลงเป็น MQTT/config payload แล้วส่งกลับมาที่ ESP32

## สถาปัตยกรรม

### ฝั่ง ESP32

เฟิร์มแวร์ใช้ FreeRTOS แยกงานหลักเป็น 3 task

- `TaskNetworking` ดูแล WiFi, Local MQTT, NETPIE, OTA, Telnet, และ CLI
- `TaskSensors` อ่านเซ็นเซอร์ทั้งหมดและทำ validation/filtering
- `TaskControl` รัน `waterSystem`, `lightController`, `fishFeeder`, `fanController`, และ `automator`

แนวคิดหลักของ firmware

- ใช้ non-blocking loop เป็นหลัก เพื่อลดโอกาสค้าง
- ใช้ watchdog และ heartbeat ตรวจจับ task ที่ค้าง
- เก็บ config/calibration ลง NVS
- ทำ validation ของค่าก่อนเผยแพร่ขึ้น dashboard หรือใช้ตัดสินใจ

### ฝั่ง Raspberry Pi

Pi server ในโฟลเดอร์ `pi_server` ใช้ Flask + Flask-SocketIO เป็นศูนย์กลางของระบบหน้าบ้าน

- Dashboard แบบ real-time
- Settings และ calibration UI
- Hardware Test สำหรับสั่งอุปกรณ์จริงทีละตัว
- OTA upload page
- Camera page
- WiFi/terminal/admin pages
- login + session + role separation (`admin`, `user`)
- rate limit สำหรับ admin actions และ activity audit log

## โมดูลหลักของ firmware

### Sensors

| โมดูล | หน้าที่ |
| --- | --- |
| `tempSensor` | อ่านอุณหภูมิน้ำจาก DS18B20 |
| `dhtSensor` | อ่านอุณหภูมิอากาศและความชื้นจาก DHT22 |
| `TdsSensor` | อ่าน TDS, ชดเชยอุณหภูมิ, filter, และ 2-point calibration |
| `phSensor` | อ่าน pH และรองรับ 3-point calibration |
| `lightSensor` | อ่านความเข้มแสงจาก BH1750 |

จุดสำคัญของ TDS ในเวอร์ชันปัจจุบัน

- ใช้ temperature compensation ตอน runtime และตอน calibration
- ใช้ conversion factor ที่ตั้งไว้ใน `include/config.h` ให้ scale ใกล้ handheld meter มากขึ้น
- มี guard ไม่ให้รับ calibration 2 จุดที่ช่วงแรงดันแคบเกินไป เพราะจะขยาย noise
- เพิ่ม filtering, deadband, และ max-step เพื่อลดอาการค่าแกว่งเล็กน้อย

### Automator

โมดูล `automator` ใช้ค่า TDS เป็นตัวตัดสินใจหลักในการโดสปุ๋ยอัตโนมัติ

- มี `target_tds`
- แยกรอบโดส A และ B ออกจากกัน
- มีช่วง `mix_after_a` และ `post_dose_mix`
- มี `tds_hysteresis_ppm` กันอาการจ่ายถี่เกินเมื่อค่าใกล้เป้าหมาย
- จะ pause หรือ block เมื่อระบบน้ำยังไม่พร้อม เช่น refill active, settling active, หรือ alarm

หมายเหตุ: ปัจจุบัน automator ใช้ TDS เป็นตัวคุมหลัก ส่วน pH ยังเป็น monitor/calibration เป็นหลัก ไม่ได้เอาไปตัดสินใจโดสอัตโนมัติใน flow ปัจจุบัน

### Water System

โมดูล `waterSystem` ดูแลการหมุนเวียนน้ำและเติมน้ำ โดยสถานะหลักที่มีอยู่ตอนนี้คือ

- `IDLE`
- `MIX_TANK_REFILL`
- `WAIT_REFILL_INTERVAL`
- `MIX_TANK_SETTLING`
- `FISH_TANK_REFILL`
- `BLOCKED`
- `ALARM`

พฤติกรรมสำคัญของระบบน้ำปัจจุบัน

- ใช้ `sump low` และ `sump high` เป็นตัวดูระดับถังผสม
- ใช้ `fish tank overflow` เป็นตัวกันน้ำล้นตอนเติมผ่านตู้ปลา
- fish-route refill เป็นรอบที่ latch แล้ว: เมื่อเริ่มเติมผ่านตู้ปลาแล้ว ระบบจะวิ่งต่อจนกว่าจะครบเวลา, overflow ทำงาน, หรือ mix-tank high ทำงานเป็น safety stop
- มี `preferred_route`, `allow_direct_sump_refill`, `refill_min_interval_ms`, `fish_refill_interval_ms`, และ `fish_refill_max_runtime_ms`
- ขณะ refill หรือ settling ระบบ automator จะถูกกันไม่ให้จ่ายสารเพื่อไม่ให้วัดค่าผิดช่วง

ดู flow operator-friendly เพิ่มเติมได้ที่ `forTestFlow/water-system-auto-flow.json` และหน้า planner ใน `forTestFlow/index.html`

### Light Control

โมดูล `lightController` คุมไฟปลูกพืชผ่าน relay

- ตั้งเวลาเปิด/ปิดได้เป็นวันและเวลา
- รองรับ manual override
- รองรับ `command_source` เป็น `netpie` หรือ `local_web`
- ใช้ NTP เพื่ออิงเวลาจริง

### Fish Feeder

โมดูล `fishFeeder` คุมการให้อาหารปลา

- ตั้งวันและเวลาให้อาหารได้
- กำหนดระยะเวลาหมุน feeder ได้
- สั่ง manual feed ได้
- รองรับ `command_source` เป็น `netpie` หรือ `local_web`
- เก็บ config ลง NVS

### Fan Controller

โมดูล `fanController` ใช้ค่าจาก DHT22 เป็นตัวช่วยตัดสินใจ

- เปิด/ปิดแบบ auto ตาม `temp_on/off` และ `humidity_on/off`
- หรือสั่ง manual on/off ได้
- มี hysteresis ผ่านการตั้งค่า on/off แยกกัน

## ฮาร์ดแวร์และ pin map

ค่าด้านล่างอ้างอิงจาก `include/config.h` ปัจจุบัน

### Sensor and input pins

| อุปกรณ์ | GPIO |
| --- | --- |
| TDS Sensor | 5 |
| pH Sensor | 6 |
| BH1750 SDA | 8 |
| BH1750 SCL | 9 |
| DS18B20 | 13 |
| DHT22 | 15 |
| Sump Level Low | 40 |
| Sump Level High | 41 |
| Fish Tank Overflow | 47 |
| Factory Reset / BOOT | 0 |

### Output pins

| อุปกรณ์ | GPIO |
| --- | --- |
| Pump Nutrient A | 10 |
| Pump Nutrient B | 11 |
| Light Relay | 12 |
| Circulation Pump | 16 |
| Fish Feeder | 21 |
| Route Valve | 39 |
| Refill Pump | 42 |
| Exhaust Fan | 2 |

หมายเหตุ

- output หลักของระบบใช้ active-low relay logic
- ถ้าฮาร์ดแวร์บางตัวไม่ได้ติดตั้งจริง ให้เช็กทั้งค่า pin และการแสดง `has_*` status บนหน้าเว็บ/CLI ก่อนสั่งงาน

## หน้าเว็บของ Pi Server

### หน้าติดตามระบบ

| หน้า | Route | หน้าที่ |
| --- | --- | --- |
| Dashboard | `/` | ดูค่าหลัก, สถานะ ESP/Pi, และภาพรวมระบบ |
| Live Camera | `/live` | ดูภาพกล้องสดจาก Pi |
| Graphs | `/graphs` | ดูข้อมูลย้อนหลัง |
| Full Logs | `/full_logs` | ดูบันทึกระบบแบบเต็ม |

### หน้าสำหรับสั่งงานและผู้ดูแล

หน้ากลุ่มนี้ถูกผูกกับสิทธิ์ `admin`

| หน้า | Route | หน้าที่ |
| --- | --- | --- |
| Hardware Test | `/hwtest` | ทดสอบ output จริงทีละตัว, flow test, safe stop |
| Settings | `/settings` | ตั้งค่าระบบ, calibration, thresholds, automation, water, fan, feeder, light |
| OTA | `/ota` | อัปโหลดไฟล์ firmware ไป ESP32 |
| WiFi | `/wifi` | จัดการการเชื่อมต่อเครือข่ายของ Pi |
| Terminal | `/terminal` | สั่งงานจากหน้าเว็บ |
| Activity Logs | `/admin/logs` | ดู admin audit trail |
| User Management | `/admin/users` | จัดการผู้ใช้และ role |

### การเข้าใช้ระบบ

| หน้า | Route | หน้าที่ |
| --- | --- | --- |
| Login | `/login` | เข้าสู่ระบบและสร้าง session ของผู้ใช้ |

## การติดตั้งแบบใช้งานจริง

### 1. เตรียม firmware secrets

โปรเจกต์นี้ใช้ `secrets.ini` สำหรับ inject secret เข้า build flags ของ PlatformIO

ตัวอย่าง

```ini
[secrets]
wifi_ap_name = Aquaponics-LAN
wifi_ap_pass = your_ap_password
netpie_client_id = your_netpie_client_id
netpie_token = your_netpie_token
netpie_secret = your_netpie_secret
ota_password = your_ota_password
telnet_password = your_telnet_password
```

ไฟล์ที่ควรรู้

- `secrets.ini` ใช้ตอน build
- `include/secrets.h.example` ใช้เป็นตัวอย่างสำหรับเครื่องใหม่
- `include/config.h` ใช้ปรับค่าคงที่, pin map, MQTT host, และ default behavior

### 2. build และ upload firmware

คำสั่งหลักของโปรเจกต์นี้

```bash
pio run -e production
pio run --target upload
pio run -e ota_upload -t upload
pio device monitor --baud 115200
```

environment ที่มีใน `platformio.ini`

- `production` สำหรับ build ใช้งานจริง
- `ota_upload` สำหรับอัปเดตผ่านเครือข่าย
- `native` สำหรับ test บนเครื่องพัฒนา

### 3. ตั้งค่า Pi server

ไฟล์หลักอยู่ในโฟลเดอร์ `pi_server`

ลำดับที่ควรทำ

1. คัดลอกโฟลเดอร์ `pi_server` ไปยัง Raspberry Pi
2. ติดตั้ง dependency และ service ตามสคริปต์ใน `pi_server/setup.sh`
3. ถ้ามี asset ฝั่งหน้าเว็บที่ต้องโหลดเก็บไว้ในเครื่อง ให้รัน `pi_server/download_assets.sh`
4. ถ้าต้องให้ Pi ทำหน้าที่เป็น access point ให้ดู `pi_server/setup_ap.sh`, `hostapd.conf`, และ `dnsmasq_ap.conf`
5. restart service หลัง deploy

ตัวอย่างคำสั่งที่ใช้บ่อยบน Pi

```bash
cd ~/pi_server
bash download_assets.sh
sudo systemctl restart aquaponics
sudo systemctl restart aquaponics-cam
```

### 4. ตั้งค่า environment variables ฝั่ง Pi

ฝั่ง `pi_server/app.py` รองรับ environment variables สำคัญดังนี้

- `AQUAPONICS_BOOTSTRAP_ADMIN_PASSWORD` รหัส admin เริ่มต้นสำหรับ bootstrap เครื่องใหม่
- `AQUAPONICS_SECRET_KEY` ใช้เป็น Flask session secret
- `AQUAPONICS_SESSION_SECURE` เปิด secure session cookie เมื่อรันหลัง HTTPS/reverse proxy
- `AQUAPONICS_OTA_PASSWORD` รหัสผ่าน OTA ที่หน้าเว็บใช้ส่งต่อ

ถ้าไม่ได้ตั้ง `AQUAPONICS_BOOTSTRAP_ADMIN_PASSWORD` ระบบจะขึ้นสถานะว่าต้อง bootstrap admin ก่อนใช้งานฝั่งเว็บ

## การใช้งานประจำวัน

### ลำดับการเริ่มระบบ

1. เปิด Raspberry Pi ให้ dashboard และ broker ทำงานก่อน
2. เปิด ESP32 แล้วดู Serial หรือ Dashboard ว่าขึ้น online
3. ตรวจหน้า Dashboard ว่าค่าเซ็นเซอร์อัปเดตต่อเนื่อง
4. ถ้าจะทดสอบ output ให้เริ่มจากหน้า Hardware Test
5. ถ้าจะใช้งานจริง ให้ตั้งค่าที่หน้า Settings แล้วปล่อยให้ controller ทำงานตาม config

### จุดที่ operator ควรตรวจทุกครั้ง

- ค่า sensor ไม่ค้าง
- water status ไม่มี alarm
- automator ไม่ติด state ที่ผิดปกติ
- fan/light/feeder ใช้ command source ตรงกับที่ต้องการ
- hardware test ไม่ค้างอยู่ใน flow-test mode หลังเลิกทดสอบ

สำหรับผู้ทดสอบใหม่ ให้เริ่มจาก `TEST_OPERATOR_GUIDE.md`

## Calibration และการตั้งค่า

### pH calibration

ทำผ่าน Serial CLI หรือ Telnet

```text
cal686
cal401
cal918
```

มี alias สั้น

```text
cal7
cal4
```

### TDS calibration

ทำผ่านหน้า Settings ของ Pi

- กรอกจุด low/high ของ standard solution ให้ครบ
- ใช้อุณหภูมิของ standard ให้ใกล้ค่าจริงตอน calibrate
- ถ้าช่วงแรงดัน 2 จุดใกล้กันเกินไป firmware จะปฏิเสธ calibration เพื่อกันค่าเพี้ยน

### command source ของไฟและ feeder

ไฟและ feeder สามารถเลือกได้ว่าใครเป็นคนคุม

- `netpie` ให้ cloud command คุม
- `local_web` ให้ Pi/web/local MQTT คุม

แนวคิดนี้ช่วยกันคำสั่งชนกันระหว่าง cloud กับคนหน้างาน

## CLI Commands

คำสั่งด้านล่างใช้ได้จาก Serial monitor และ Telnet

### ข้อมูลระบบ

| คำสั่ง | หน้าที่ |
| --- | --- |
| `help` | แสดงรายการคำสั่ง |
| `clear` | ล้างหน้าจอ |
| `status` | แสดงค่าเซ็นเซอร์ทั้งหมด |
| `test` | รัน self-test |
| `health` | แสดงสุขภาพระบบ |
| `tasks` | แสดง heartbeat และ stack ของ task |
| `crash` | แสดง last crash / last task stage |
| `wifi` | แสดงข้อมูล WiFi |
| `mqtt` | แสดงสถานะ NETPIE |
| `version` | แสดง firmware version |
| `reboot` | รีสตาร์ทบอร์ด |
| `reset` | factory reset |

### Sensor และ calibration

| คำสั่ง | หน้าที่ |
| --- | --- |
| `ph` | แสดงค่า pH ปัจจุบัน |
| `cal686` | calibrate pH 6.86 |
| `cal401` | calibrate pH 4.01 |
| `cal918` | calibrate pH 9.18 |
| `cal7` | alias ของ `cal686` |
| `cal4` | alias ของ `cal401` |

### Controller commands

| คำสั่ง | หน้าที่ |
| --- | --- |
| `light` | ดูสถานะ light controller |
| `light on` | เปิดไฟแบบ manual |
| `light off` | ปิดไฟแบบ manual |
| `light auto` | กลับไปใช้ schedule |
| `light netpie` | ให้ NETPIE คุมไฟ |
| `light web` | ให้เว็บ/Pi คุมไฟ |
| `feed` | ดูสถานะ feeder |
| `feed now` | ให้อาหารทันที |
| `feed enable` | เปิด schedule feeder |
| `feed disable` | ปิด schedule feeder |
| `feed netpie` | ให้ NETPIE คุม feeder |
| `feed web` | ให้เว็บ/Pi คุม feeder |
| `fan` | ดูสถานะพัดลม |
| `fan on` | เปิดพัดลม manual |
| `fan off` | ปิดพัดลม manual |
| `fan auto` | กลับไปโหมด auto |
| `auto` | ดูสถานะ automator |
| `water` | ดูสถานะระบบน้ำ |
| `circ on` / `circ off` | เปิดหรือปิด circulation |
| `refill on` / `refill off` | เปิดหรือปิด manual refill |
| `route auto` | ใช้ route แบบ auto |
| `route fish` | บังคับ route ผ่านตู้ปลา |
| `route sump` | บังคับ route เข้าถังรวมตรง |
| `water clear` | ล้าง alarm ระบบน้ำ |
| `pump a` / `pump b` | ทดสอบปั๊มโดส |
| `pump stop` | หยุดปั๊มโดสทั้งหมด |

## MQTT Topics

### NETPIE

| Topic | ทิศทาง | หน้าที่ |
| --- | --- | --- |
| `@shadow/data/update` | ESP -> Cloud | ส่งค่า sensor และ state ขึ้น shadow |
| `@shadow/data/updated` | Cloud -> ESP | รับการเปลี่ยนแปลง shadow |
| `@msg/#` | Cloud -> ESP | รับคำสั่งควบคุมจาก NETPIE |

### Local MQTT

| Topic | ทิศทาง | หน้าที่ |
| --- | --- | --- |
| `aquaponics/sensors` | ESP -> Pi | sensor data + health + live status |
| `aquaponics/logs` | ESP -> Pi | log ของระบบ |
| `aquaponics/config/sensors` | Pi -> ESP | เปิด/ปิด sensor |
| `aquaponics/status/sensors` | ESP -> Pi | feedback config sensor |
| `aquaponics/config/automation` | Pi -> ESP | config automator |
| `aquaponics/status/automation` | ESP -> Pi | runtime/config automator |
| `aquaponics/config/fan_control` | Pi -> ESP | config fan |
| `aquaponics/status/fan_control` | ESP -> Pi | status fan |
| `aquaponics/config/light_control` | Pi -> ESP | config light |
| `aquaponics/status/light_control` | ESP -> Pi | status light |
| `aquaponics/config/fish_feeder` | Pi -> ESP | config feeder |
| `aquaponics/status/fish_feeder` | ESP -> Pi | status feeder |
| `aquaponics/config/water_system` | Pi -> ESP | config water system |
| `aquaponics/status/water_system` | ESP -> Pi | status water system |
| `aquaponics/config/tds_cal` | Pi -> ESP | TDS calibration |
| `aquaponics/config/ph_cal` | Pi -> ESP | pH calibration |
| `aquaponics/test/command` | Pi -> ESP | สั่ง hardware test |
| `aquaponics/test/result` | ESP -> Pi | ผลลัพธ์ hardware test |

## การทดสอบและ validation

ก่อน deploy จริง แนะนำอย่างน้อย 3 ขั้นตอนนี้

```bash
pio test -e native -f test_tds_native
pio test -e native -f test_water_system_native
pio run -e production
```

test ชุด native อื่น ๆ มีอยู่ในโฟลเดอร์ `test/test_native` สำหรับ sensor/controller หลายส่วน

ถ้าต้องทดสอบหน้างานจริง

1. เริ่มจาก Dashboard ว่าค่าอัปเดตจริง
2. ใช้หน้า Hardware Test ทดสอบอุปกรณ์ทีละตัว
3. ใช้ Safe Stop ก่อนจบงานทุกครั้ง

คู่มือทดสอบทีละขั้นอยู่ใน `TEST_OPERATOR_GUIDE.md`

## โครงสร้างโปรเจกต์

```text
test/
|- include/                 header และค่าคงที่กลางของระบบ
|- src/                     firmware implementation
|- pi_server/               Flask dashboard, pages, scripts, services
|- test/                    native/unit tests
|- docs/                    เอกสารประกอบและ vault
|- forTestFlow/             flow planner และ JSON diagram
|- lib/WiFiManager/         library ที่ใช้งานร่วมกับ firmware
|- platformio.ini           build environments
|- CHANGELOG.md             ประวัติการเปลี่ยนแปลง
|- PRODUCTION.md            แนวทาง deploy ใช้งานจริง
|- TEST_OPERATOR_GUIDE.md   คู่มือทดสอบหน้างาน
|- HARDWARE_TO_BUY.md       รายการฮาร์ดแวร์ที่เกี่ยวข้อง
```

## ไฟล์สำคัญที่ควรรู้จัก

- `include/config.h` จุดรวม pin map, constants, MQTT topics, และ default behavior
- `src/main.cpp` จุดเริ่มต้นของ firmware และการสร้าง FreeRTOS tasks
- `src/automator.cpp` flow การโดสอัตโนมัติ
- `src/waterSystem.cpp` logic ระบบน้ำ
- `src/localMqtt.cpp` bridge ระหว่าง ESP32 กับ Pi
- `pi_server/app.py` backend หลักของ dashboard และ API
- `pi_server/header.js` shared navigation, alerts, และ role-aware header
- `forTestFlow/index.html` เครื่องมือดู/แก้ flow diagram

## เอกสารที่เกี่ยวข้อง

- `TEST_OPERATOR_GUIDE.md` สำหรับผู้ทดสอบหน้างาน
- `PRODUCTION.md` สำหรับการ deploy ใช้งานจริง
- `HARDWARE_TO_BUY.md` สำหรับเช็กรายการอุปกรณ์
- `PROJECT_REFERENCE.md` สำหรับดูภาพรวม reference เพิ่มเติม
- `CHANGELOG.md` สำหรับประวัติการแก้ไขล่าสุด

## หมายเหตุสำคัญก่อนแก้ระบบ

- ถ้าจะแก้ pin, timing, MQTT topic, หรือ default behavior ให้เริ่มที่ `include/config.h`
- ถ้าจะแก้ logic ตัดสินใจของระบบน้ำหรือ automator ให้ดู interlock ระหว่างสองโมดูลนี้เสมอ
- ถ้าจะแก้หน้าเว็บหรือ asset กลางฝั่ง Pi ให้ตรวจผลกับ `header.js`, `base.css`, และ PWA cache ไปพร้อมกัน
- ถ้าจะแก้ calibration หรือ state payload ให้เช็กทั้ง firmware, Local MQTT payload, และหน้า Settings ว่าใช้ key ตรงกัน

README นี้ตั้งใจให้เป็นจุดเริ่มต้นสำหรับคนที่ต้องรับช่วงงานต่อ, ทดสอบระบบ, หรือ deploy ระบบชุดปัจจุบัน ถ้าต้องลงมือหน้างานทันที ให้เปิด `TEST_OPERATOR_GUIDE.md` ควบคู่กันไปด้วย