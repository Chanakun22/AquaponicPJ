# Changelog

All notable changes to the **Smart Aquaponics AI** project will be documented in this file.

## [2026-06-08] - DHT22 WDT Hardening

### Fixed

- **fix:** Feed Task WDT + heartbeat ระหว่าง DHT22 read (`dht_read` / `dht_humidity` / `dht_done`) เพื่อลด Task WDT reset ที่ stage `dht_loop`
- **fix:** อ่าน DHT22 แบบ single bus transaction (`readTemperature` แล้ว `readHumidity` จาก cache) ลดเวลา block ~278ms → ~140ms
- **fix:** Backoff 30s เมื่อ DHT read ช้าผิดปกติมาก (`DHT_SLOW_READ_BACKOFF_MS`)

## [2026-06-08] - TDS Cal Validation & Automator Safety Guards

### Added

- **feat:** TDS calibration validation — ปฏิเสธ cal ใหม่เมื่อ K > 2.0, K ≤ 0, หรือ high/low voltage/ppm ผิดทิศ; เตือนเมื่อ K > 1.5
- **feat:** `tdsIsVoltageBelowCalibrationRangeForChannel()`, `tdsIsCalibrationQualityOkForChannel()`, `tdsGetCalibrationKForChannel()`
- **feat:** Automator บล็อก dosing เมื่อ mix TDS voltage ต่ำกว่าช่วง cal หรือ cal K ไม่น่าเชื่อถือ
- **mqtt:** เผยแพร่ `tds_mix_cal_k`, `tds_mix_cal_quality_ok`, `tds_mix_below_cal_range`

### Fixed

- **fix:** กัน automator dose A&B ซ้ำเมื่อ TDS แสดง 0 ppm จากแรงดันต่ำกว่าจุด cal ต่ำ

## [2026-06-08] - Fix Sensor Sticking Bug & Enable pH Temp Compensation

### Changed

- **config:** `PH_USE_WATER_TEMP_COMPENSATION = 1` — เปิดใช้งานการชดเชยอุณหภูมิน้ำสำหรับเซ็นเซอร์ pH แบบ Real-time
- **tune:** ปรับลด `PH_PH_DEADBAND` จาก `0.03f` เป็น `0.01f` และ `TDS_MIX_VALUE_DEADBAND_PPM` จาก `20.0f` เป็น `10.0f` เพื่อให้การอัปเดตบนแดชบอร์ดมีความนิ่งและลื่นไหลมากขึ้นโดยไม่เกิดอาการดีด (ค่าดีด)

### Fixed

- **fix:** แก้ไขบั๊กตัวกรองติดขัด (Filter Sticking Bug) ใน `phSensor.cpp` และ `TdsSensor.cpp` ที่ทำให้ตัวกรอง EMA ไปเขียนทับด้วยค่าที่มี deadband/step limit ในแต่ละรอบ ทำให้อัปเดตติดขัดและดีดตัวเมื่อค่าเปลี่ยนแปลงมาก
- **test:** เพิ่ม `-DNATIVE_TEST` ใน `platformio.ini` และแก้ไขระบบตัวแปร `static` ใน `test_light_native.cpp` ให้ผ่านการทดสอบ 100%

## [2026-06-05] - Sensor Display Stability (Anti-Bounce)

### Changed

- **tune:** เข้ม filter pH — deadband 0.03, max step 0.05/รอบ, EMA ช้าลง
- **tune:** เข้ม filter TDS — mix deadband 20 ppm / max step 6; fish deadband 18 ppm
- **mqtt:** ปัด TDS เป็นจำนวนเต็ม ppm, pH ทศนิยม 1 ตำแหน่งก่อนส่ง dashboard

## [2026-06-05] - pH Fixed 25°C Reference (No DS18B20)

### Changed

- **config:** `PH_USE_WATER_TEMP_COMPENSATION = 0` — pH mix/fish ใช้ `PH_REFERENCE_TEMP_C` (25°C) คงที่ ไม่พึ่ง DS18B20 สำหรับ slope compensation

## [2026-06-05] - Sensor Accuracy & ADC Crosstalk Fix (Full Pass)

### Added

- **adcBus.h:** Shared ADC settle (`ADC_CHANNEL_SWITCH_SETTLE_US`) + oversampled read ข้าม TDS/pH — ลด crosstalk GPIO 1/5/6/7

### Changed

- **ph:** Round-robin อ่านทีละ channel ต่อรอบ + oversample 16 ครั้ง + ใช้ adcBus
- **tds:** ใช้ adcBus ร่วมกับ pH; fish oversample 16; ชดเชยอุณหภูมิ fish ผ่าน `TDS_FISH_TEMP_FILTER_ALPHA`
- **main:** ลบ TDS/pH alternation (`preferTdsThisPass`); cache `currentPhFish`; ส่ง temp ชดเชยแยก channel ทุกรอบ
- **system health:** ตรวจ mix + fish สำหรับ pH, TDS, water temp
- **tune:** ลด TDS mix deadband 20→12 ppm เพื่อค่าใกล้จริงมากขึ้น; เพิ่ม `tdsIsReadyForChannel()`

## [2026-06-05] - pH Per-Channel Temperature Compensation

### Fixed

- **fix:** pH fish ใช้ `water_temp_fish` ชดเชย slope แยกจาก mix — เพิ่ม `phSetTemperatureChannel()`, เก็บ `waterTemperature` ต่อ channel ใน `PhChannelState`, `main.cpp` ส่ง mix/fish temp ก่อน `phLoop()` (fish fallback → mix → 25°C)

## [2026-06-05] - TDS Fish Channel Enabled (Hardware Wired)

### Changed

- **config:** เปิด `TDS_FISH_CHANNEL_ENABLED` เป็น `1` หลังต่อ probe TDS fish (GPIO7) ครบแล้ว — firmware อ่าน mix/fish แบบ round-robin พร้อม filter แยก channel

## [2026-06-05] - TDS Mix Stability Tuning

### Changed

- **fix:** ลดอาการ TDS Mix แกว่งจาก ADC crosstalk หลังเพิ่ม fish channel — `tdsLoopChannels()` อ่านทีละ channel ต่อรอบ พร้อม `TDS_CHANNEL_SWITCH_SETTLE_US` ก่อนสลับ pin
- **tune:** ปรับ `TDS_VALUE_FILTER_ALPHA` 0.10→0.08, `TDS_VALUE_DEADBAND_PPM` 5→8, `TDS_VALUE_MAX_STEP_PPM` 20→12 ให้ค่า TDS บน dashboard นิ่งขึ้นโดยยังตามการเปลี่ยนจริงได้
- **fix:** เพิ่ม filter แยกสำหรับ **Mix tank** (`TDS_MIX_*`) — deadband 15 ppm, max step 8 ppm/รอบ, EMA ช้ากว่า fish
- **fix:** กรองอุณหภูมิ mix ก่อนชดเชย TDS (`TDS_MIX_TEMP_FILTER_ALPHA`) ลดอาการแกว่งจาก temp เปลี่ยนทีละนิด
- **fix:** ข้ามการอ่าน TDS fish ถ้า probe ลอย/ไม่ต่อ เพื่อลด noise ที่รบกวน mix
- **fix:** เพิ่ม `TDS_FISH_CHANNEL_ENABLED` (default `0`) — ปิด ADC fish จนกว่าจะต่อ probe จริง ลด crosstalk กับ mix
- **tune:** Mix oversample 16 ครั้ง/รอบ (`TDS_MIX_OVERSAMPLE_COUNT`), deadband 20 ppm, `analogReadResolution(12)` ใน `tdsSetup()`

## [2026-06-05] - Admin History Graph Fix Page (/fix)

### Added

- **feat:** เพิ่มหน้า `/fix` (admin only) สำหรับแก้ไขค่า sensor ในประวัติกราฟย้อนหลังโดยตรง พร้อมตัวอย่างกราฟ, ตารางแก้ไขแบบ inline, pagination, บันทึกทีละแถว/บันทึกรวม, และลบแถวผิดปกติ
- **feat:** เพิ่มตัวช่วยแก้ไขอัตโนมัติบน `/fix` — กรอก min/max/mean + max step (กันดีด) + spike limit แล้วให้ระบบปรับค่าทั้งช่วงเอง พร้อม preview กราฟ before/after และ API `POST /api/history/fix/auto`, `GET /api/history/fix/stats`
- **feat:** เพิ่ม **Copy Sensor** บน `/fix` — คัดลอกรูปคลื่นจาก sensor ต้นทางไปปลายทาง (เช่น pH Fish → pH Mix) พร้อม Target Mean, Ripple Scale, Difference ให้เส้นเหมือนกันแต่ไม่ซ้อนกัน + API `POST /api/history/fix/copy`
- **app.py:** เพิ่ม API `GET /api/history/fix`, `PATCH /api/history/fix/<id>`, `DELETE /api/history/fix/<id>` พร้อม validation ช่วงค่าและ activity log (`history_edit`, `history_delete`)
- **header.js:** เพิ่มเมนู `แก้กราฟ` ในกลุ่มผู้ดูแล (admin only)
- **pwa/sw.js:** bump cache `aquaponics-v42` → `aquaponics-v43`

## [2026-06-05] - Per-Channel TDS Calibration (Mix / Fish)

### Added

- **feat:** TDS calibration แยก channel ครบวงจร — firmware `tdsSetCalibrationForChannel()`, MQTT `aquaponics/config/tds_cal` รองรับ `"channel": "mix"|"fish"`, Pi API `/api/tds_voltage` + `/api/tds_calibrate` รองรับ per-channel, และหน้า Settings มี dropdown เลือก probe เหมือน pH
- **settings.html:** เก็บค่า cal แยก `tds_calibration.mix` / `tds_calibration.fish` พร้อม backward compat กับ root `tds_calibration` สำหรับ Mix
- **pwa/sw.js:** bump cache `aquaponics-v41` → `aquaponics-v42`

### Notes

- Mix กับ Fish ใช้น้ำยามาตรฐานชุดเดียวกันได้ แต่ต้องกด Read voltage แยกต่อ probe แล้ว Save ทีละ channel

## [2026-06-03] - Dashboard Separate Water Quality Cards

### Changed

- **index.html:** แยกการ์ด dashboard สำหรับ `Water Temp`, `TDS`, และ `pH` ออกเป็นรายถังชัดเจน (`Mix` / `Fish`) แทนการแสดงแบบ compare tile ในการ์ดเดียว
- **index.html:** ปรับ `updateSensors()` ให้เติมค่าลงการ์ดใหม่โดยไม่ใส่หน่วยซ้ำ และเพิ่ม hint แยกสำหรับ fish tank (`waterFishHint`, `tdsFishHint`, `phFishHint`)
- **index.html:** ปรับ sensor-disabled state ให้ครอบคลุมการ์ดใหม่ทั้ง mix/fish และแยก light ออกจาก pH
- **pwa/sw.js:** bump cache `aquaponics-v36` → `aquaponics-v37`
- **index.html:** ยุบ fish tank readings กลับเป็นการ์ดเดียว `Fish Tank Sensors` โดยรวม `Water Temp`, `TDS`, และ `pH` ใน card เดียวตามการใช้งานหน้า dashboard
- **index.html:** ปรับ fish hint และ disabled state ให้ใช้ `card-fish-sensors` ใบเดียว
- **pwa/sw.js:** bump cache `aquaponics-v37` → `aquaponics-v38`
- **index.html:** ยุบ mix tank readings เป็นการ์ดเดียว `Mix Tank Sensors` ให้ UI จับคู่กับ `Fish Tank Sensors` และปรับ card styling เป็น tile 3 ค่า (`Water Temp`, `TDS`, `pH`) พร้อม summary ต่อถัง
- **index.html:** ปรับ sensor-disabled state ให้ใช้การ์ดรายถัง (`card-mix-sensors`, `card-fish-sensors`) แทนการ์ดราย sensor ที่ถูกลบออก
- **pwa/sw.js:** bump cache `aquaponics-v38` → `aquaponics-v39`
- **index.html:** ปรับ tank sensor cards ให้เข้าธีม dashboard มากขึ้น โดยให้ Mix/Fish span เป็นการ์ดกว้าง, ลดสี tile เป็น dark glass พร้อม accent border, ปรับขนาดตัวเลข/หน่วยไม่ให้เบียด และเพิ่ม responsive layout บนมือถือ
- **pwa/sw.js:** bump cache `aquaponics-v39` → `aquaponics-v40`
- **graphs.html:** แยกกราฟย้อนหลังจากเดิมที่รวม water/pH/TDS หลายชนิดไว้ด้วยกัน เป็นกราฟเฉพาะ `Water Temp`, `TDS`, และ `pH` โดยแต่ละกราฟมีเส้น `Mix` / `Fish` แยกกันชัดเจน พร้อมคง `Environment` สำหรับ air/humidity และ `Light` แยกต่างหาก
- **pwa/sw.js:** bump cache `aquaponics-v40` → `aquaponics-v41`

## [2026-06-02] - DS18B20 OneWire Bus Race Fix

### Fixed

- **fix (critical):** แก้ปัญหา water temp อ่านเป็น `nan` ทั้ง mix/fish หลังเพิ่ม realtime `temp scan` — ต้นเหตุคือบัส OneWire ไม่มี mutex แต่ `temp scan`/`temp bind` (รันบน TaskNetworking) ไปเรียก `begin()`/`getAddress()` บนบัสพร้อมกับ `requestTemperatures()`/`getTempC()` ใน `tempLoop()` (TaskSensors) ทำให้ transaction ชนกันจนอ่านค่าไม่ได้
- **refactor:** ย้ายการแตะบัส OneWire ทั้งหมดไปอยู่บน TaskSensors เท่านั้น
  - เพิ่ม `_performScan()` (cache scanned addresses) เรียกจาก `tempSetup()` และจาก `tempLoop()` ตอน IDLE
  - `tempRefreshScan()` เปลี่ยนเป็นแค่ตั้ง flag `_rescanRequested` (ไม่แตะบัส) แล้วให้ `tempLoop()` ทำ rescan จริงในรอบถัดไป
  - `tempGetScannedAddressHex()`, `_bindFromIndex()`, `_autoBindMissingAddresses()` อ่านจาก cache `_scannedAddresses[]` แทนการ search บัสสดจาก command task

### Notes

- `temp scan` จะอัปเดต `Devices found` ในรอบการอ่านถัดไป (ภายใน ~`TEMP_READ_INTERVAL`) — รันซ้ำได้ถ้าต้องการค่าใหม่ทันที
- Build production ผ่าน (RAM 19.0%, Flash 14.8%); `test_temp_native` + `test_water_system_native` ผ่านครบ

## [2026-05-31] - Pi Dashboard pH Fish Channel UI

### Added

- **app.py:** รองรับ `ph_mix` / `ph_fish` ครบ pipeline
  - เพิ่มใน `last_data` cache + DB schema (ALTER TABLE `ph_mix`, `ph_fish`) + DB INSERT
  - History query/payload เพิ่ม columns + `datasets.ph_mix` / `datasets.ph_fish`
  - `normalize_history_value` validate ช่วง pH สำหรับ `ph_mix`/`ph_fish`
  - `SENSOR_KEYS` + `sensor_config_keys` เพิ่ม per-channel keys
  - `ph_calibration` cache เพิ่ม nested `mix`/`fish` objects + status handler parse จาก firmware
  - `/api/ph_calibrate` รับ field `channel` (`mix`/`fish`) → ส่งต่อใน MQTT payload
  - `/api/ph_voltage` คืน nested `mix`/`fish` readings
- **index.html:** เพิ่ม compare-grid tiles `pH Mix Tank` / `pH Fish Tank` ในการ์ด pH; `updateSensors()` อ่าน `ph_mix ?? ph`, `ph_fish`; hint แสดงค่า fish
- **graphs.html:** Water chart แยกเป็น `pH Mix` + `pH Fish` series (เดิม single `pH Level`)
- **settings.html:** เพิ่ม channel selector (mix/fish) ในการ์ด pH calibration; `calibratePh`/`clearPhCalibration` ส่ง `channel`; reading + status แสดงตาม channel ที่เลือก
- **pwa/sw.js:** bump cache `aquaponics-v34` → `aquaponics-v35`

### Notes

- ปิด Phase C (pH multi-channel) ของ `FISH_TANK_SENSORS_PLAN.md` ครบ **end-to-end** (firmware + Pi dashboard) — เหลือเฉพาะ wire probe จริง + calibrate
- Legacy keys (`ph`, `ph_voltage`) ยังคงไว้ = mix channel เพื่อ backward compat
- `app.py` syntax check ผ่าน (`python -m py_compile`)

## [2026-05-31] - pH Multi-Channel (Fish Tank Probe Support)

### Added

- **phSensor refactor:** เป็น multi-channel array-based pattern (เหมือน `TdsSensor`) — รองรับ 2 channel: `PH_CHANNEL_MIX` (GPIO 6, เดิม) และ `PH_CHANNEL_FISH` (GPIO 1, ADC1_CH0, ใหม่)
  - เพิ่ม `PhChannel` enum + new API: `phReadChannel()`, `phReadVoltageChannel()`, `phIsReadyChannel()`, `phCalibratePh{401,686,918}Channel()`, `phHasCalibration{401,686,918}Channel()`, `phClearCalibrationChannel()`
  - Backward-compat shims: `phRead()`, `phCalibratePh686()` ฯลฯ ทุกตัว default = MIX channel → caller เดิมไม่ต้องแก้
  - NVS keys: `mix_v401`/`mix_v686`/`mix_v918` (MIX) + `fish_v401`/`fish_v686`/`fish_v918` (FISH); MIX channel ยัง fallback อ่าน legacy `v401`/`v686`/`v918`/`volt4`/`volt7` ได้ (auto-migrate ตอน save ครั้งถัดไป)
  - Per-channel filtering state: ทุก EMA / deadband / step-limiter แยกระหว่าง mix และ fish ไม่กระทบกัน
- **MQTT keys ใหม่** ใน `aquaponics/sensors`:
  - `ph_mix`, `ph_fish` (ค่า pH ของแต่ละ channel)
  - `ph_mix_voltage`, `ph_fish_voltage` (mV สำหรับ calibration UI)
  - Legacy `ph` / `ph_voltage` คงไว้ = mix channel
- **MQTT pH calibration topic** (`aquaponics/config/ph_cal`) รองรับ field `channel`: `"mix"` (default) หรือ `"fish"` → calibrate ต่อ probe ได้
- **MQTT pH cal status** (`aquaponics/status/ph_cal`) เพิ่ม nested `mix`/`fish` objects พร้อม `cal{401,686,918}_done` แต่ละ channel (legacy keys ที่ root level ยังคงเป็น mix สำหรับ Pi UI เดิม)
- **NETPIE shadow** เพิ่ม keys `ph_mix`, `ph_fish` (legacy `ph` = mix)
- **CLI commands:**
  - `ph` แสดงทั้ง mix และ fish channel
  - `cal686 [mix|fish]`, `cal401 [mix|fish]`, `cal918 [mix|fish]` (default = mix; alias `cal7`/`cal4` ยังทำงาน)
  - `status` แสดง `pH Mix` + `pH Fish` แยก row

### Notes

- **ยังไม่ wire fish probe จริง** — `PH_CHANNEL_FISH` อ่าน floating ADC1_CH0 จะให้ค่า noise/ไม่เสถียร แต่ระบบ handle NaN/-1 ได้ และไม่กระทบ MIX channel หรือ automation
- **Automation ใช้ MIX channel เท่านั้น** (เหมือนเดิม) — ตู้ปลาเป็น monitor only ตามแผน
- **Build verified:** Production firmware compile ผ่าน, RAM 19.0%, Flash 14.8%
- **Native tests:** `test_water_system_native` ผ่านครบ 25/25 cases (ไม่ regression). `test_ph_native` มี 5 fail cases ที่เกี่ยวกับ mock NVS state across instances — เป็น preexisting infrastructure issue ก่อน refactor นี้

## [2026-05-31] - Fish Tank Water Temp and TDS Phase 1

### Added

- **feat:** เพิ่ม firmware support สำหรับ fish tank DS18B20 และ TDS channel โดยคง legacy `water_temp` / `tds` เป็นค่า mix tank สำหรับ automation เดิม
- **feat:** เพิ่ม MQTT/NETPIE keys `water_temp_mix`, `water_temp_fish`, `tds_mix`, `tds_fish` และ `tds_fish_voltage`
- **feat:** เพิ่ม Pi dashboard/history support สำหรับแสดงและเก็บค่า mix vs fish รวมถึง graph series ใหม่ และ bump PWA cache เป็น `aquaponics-v34`
- **feat:** เพิ่ม CLI `temp scan`, `temp swap`, และ `temp bind mix|fish <index>` สำหรับจัดการ DS18B20 address binding

## [2026-05-29] - Shared I2C Bus Guard

### Changed

- **fix:** เพิ่ม `i2cBus` helper สำหรับ setup/lock I2C bus ร่วมระหว่าง BH1750 และ MCP23017 พร้อมตั้ง clock เป็น 100kHz เพื่อเพิ่มเสถียรภาพบน bus ที่มีหลายอุปกรณ์
- **fix:** ปรับ `lightRead()` ให้คืนค่า cache เท่านั้น และให้ `lightLoop()` เป็นจุดเดียวที่อ่าน BH1750 hardware เพื่อลด `BH1750 read error` จากการอ่านซ้ำหลาย task
- **fix:** ครอบ MCP23017 I2C access และ I2C scan command ด้วย shared mutex เดียวกัน
- **fix:** harden DHT22 read path โดยย้าย initial read ออกจาก `dhtSetup()`, เพิ่ม slow-read warning, consecutive-failure backoff, และแยก TaskSensors checkpoint เป็น `dht_loop` ก่อนอ่าน cache
- **fix:** เพิ่ม auto-disable สำหรับ DHT22 air sensor เมื่อ boot หลัง WDT ที่ค้างใน `dht_loop` / `air_temp_humidity` เพื่อหยุด crash loop และให้ re-enable หลังตรวจ wiring/pull-up
- **test:** อัปเดต DHT native test/mock config ให้ครอบคลุม DHT backoff constants และแก้ test harness linkage ให้รันผ่านบน native environment

## [2026-05-28] - Cursor Project Workflow Skill

### Added

- **docs:** เพิ่ม Cursor project skill `aquaponics-project-workflows` สำหรับ auto-invoke เมื่องานเกี่ยวกับ MCP23017, PlatformIO build/upload/monitor, firmware ESP32, MQTT, Pi Dashboard, และ changelog workflow

## [2026-05-24] - MCP23017 Output Abstraction Layer

### Added

- **gpioOut abstraction layer** (`include/gpioOut.h`, `src/gpioOut.cpp`):
  - รองรับ dual-mode output routing per logical output: ESP32 GPIO เดิม หรือ MCP23017 I/O expander
  - 8 logical outputs: PUMP_NUTRIENT_A/B, LIGHT_RELAY, PUMP_CIRCULATION, FISH_FEEDER, REFILL_ROUTE_VALVE, PUMP_REFILL, EXHAUST_FAN
  - Active-low semantics: `gpioOutWrite(out, true)` = ON, `gpioOutWrite(out, false)` = OFF
  - Boot-safe: ทุก output เริ่มที่ OFF state ก่อน controllers init (กัน relay click ตอน boot)
  - Health check (`gpioOutMcpHealthy()`) + recovery (`gpioOutMcpReinit()`) สำหรับ I2C bus hang
- **MCP23017 config** ใน `config.h`:
  - `MCP23017_I2C_ADDR = 0x20` (default, ไม่ชน BH1750 ที่ 0x23)
  - `MCP23017_RESET_PIN = 4` (ESP32 GPIO 4)
  - `MCP_PIN_*` mapping for all 8 outputs (GPA0-7)
  - Per-output flag `OUT_USE_MCP_*` (default = 0 → ใช้ ESP32 GPIO เดิม) — flip ทีละโมดูลตอน migrate
- Library `Adafruit MCP23017 Arduino Library@^2.3.2`

### Changed

- **Refactor caller ทุกที่** ให้ใช้ `gpioOutWrite()` แทน `digitalWrite()` ของ output pins (35 จุดใน 7 ไฟล์):
  - `automator.cpp`, `waterSystem.cpp`, `lightController.cpp`, `fishFeeder.cpp`, `fanController.cpp`, `localMqtt.cpp`, `commandHandler.cpp`
- **ลบ `pinMode()` + initial `digitalWrite()`** ของ output pins ออกจาก controllers — `gpioOutSetup()` ใน `main.cpp` ทำให้แล้ว (เริ่มเร็วที่สุดหลัง STATUS_LED init เพื่อกัน relay floating)
- `_writePumpOutput()` ใน waterSystem.cpp รับ `GpioLogicalOutput` แทน `int pin`
- `commandHandler` track `GpioLogicalOutput _pumpTestOutput` แทน `uint8_t _pumpTestPin`

### Notes

- **Behavior identical** กับเวอร์ชันเดิม เพราะทุก `OUT_USE_MCP_*` flag default = 0 → route ไป ESP32 GPIO เหมือนเดิม
- **Native test mock** เพิ่มใน `test/test_water_system_native/test_main.cpp` — map `gpioOutWrite` → `digitalWrite` ผ่าน pin number ของ ESP32 GPIO เพื่อให้ existing pin-state assertions ผ่านต่อไป
- **Migration plan:** หลัง wire MCP23017 จริง → flip `OUT_USE_MCP_*` ทีละ define เป็น 1 → flash → ทดสอบทีละโมดูล → repeat (Light → Fan → Feeder → Route Valve → Circulation → Refill → Pump A/B)

## [2026-05-23] - Manual Fish Refill Bypasses Cooldown

### Fixed

- **waterSystem:** Manual fish refill ถูก gate ด้วย `fishRefillIntervalMs` cooldown เหมือน auto refill ทำให้ user กด "Manual Refill On" ในหน้า Hardware Test แล้วระบบเข้า `WAIT_REFILL_INTERVAL` แทนที่จะเริ่มเติมทันที ดูเหมือนปุ่มไม่ทำงาน
  - แก้ `_resolveRefillRouteForNow()` รับ `manualOverride` flag เพิ่ม
  - เพิ่ม `_isFishRouteHardwareReady()` ที่เช็ค hardware safety (route valve, overflow sensor, overflow not active) โดยไม่เช็ค cooldown
  - เมื่อ `manualRefill = true` และ hardware พร้อม → bypass cooldown และเริ่มเติมทันที
  - เมื่อ auto refill (`refillEnabled`, ไม่ใช่ manual) → cooldown ยังทำงานปกติ (`_isFishRefillReady` เดิม)
  - Hardware safety ทุกตัวยังครบ: ถ้า overflow active หรือ hardware ขาด → ยังคง block

## [2026-05-20] - Code Review Fixes (HIGH/MEDIUM)

### Fixed

- **automator:** `automatorGetTimeRemainingSec()` ใช้ runtime config (`_config.doseAVolumeMl`, `mixAfterAMs`, `doseBVolumeMl`, `postDoseMixMs`) แทน compile-time constants ที่ไม่ตรงกับค่าจริง
- **automator:** ลบ dead state `AUTO_STATE_WATER_FILL` ออกจาก enum และ switch cases
- **localMqtt:** เพิ่ม `StaticJsonDocument` ใน `_onMqttMessage` callback จาก 256 → 512 bytes กัน overflow เมื่อ payload ใหญ่
- **localMqtt:** เพิ่ม MQTT Last Will and Testament (`aquaponics/status/online` → "offline") และ publish "online" retained เมื่อ connect สำเร็จ
- **main:** เพิ่ม TaskControl stack จาก 6144 → 8192 bytes กัน stack overflow
- **TdsSensor:** reset EMA state (`_tdsMovingAverage`, `_tdsVoltageMovingAverage`) หลัง recalibration สำเร็จ เพื่อให้ค่า converge ทันที
- **phSensor:** เพิ่ม `_resetEmaRequested` flag สำหรับ reset EMA หลัง calibration/clear เพื่อให้ค่า converge ทันที
- **waterSystem:** ใช้ explicit `(uint8_t)` cast ใน `_sanitizeConfig` enum range check กัน signed comparison issue
- **fishFeeder:** persist `_lastTriggeredWeekMinute` ใน NVS กัน double-feed on reboot
- **fishFeeder:** แก้ reason string จาก "500 ms" เป็น "100 ms" ให้ตรงกับ `FEEDER_ACTIVE_LOW_DELAY_MS`

## [2026-05-13] - Water Flow Safety Hardening

### Changed

- **fix: stop manual Direct Sump refill on mix-tank high level and preserve water runtime timing (`src/waterSystem.cpp`, `test/test_native/test_water_system_native.cpp`):**
  - ทำให้ `Manual Refill` ที่ใช้ route `SUMP_DIRECT` หยุดทันทีเมื่อ `SUMP_LEVEL_HIGH` active แทนการรอจน timeout แล้วเข้า alarm
  - หยุดล้าง cooldown/settling/refill runtime ทุกครั้งที่กด Apply config โดยย้าย runtime reset ไปอยู่ตอน `waterSystemSetup()` เท่านั้น เพื่อไม่ให้การ apply settings ระหว่างทำงานทำให้รอบเติมซ้ำเร็วเกิน policy
  - อัปเดตสถานะ `fish_refill_ready` และ `fish_refill_wait_remaining_ms` ทันทีหลัง fish route หยุด เพื่อให้ dashboard เห็น cooldown จริงตั้งแต่ loop เดียวกัน
  - ปรับข้อความ settling ให้บอก route ก่อนหน้าตามจริง และเพิ่ม native regression tests สำหรับ manual direct-sump high stop กับ config apply ที่ต้องไม่ล้าง fish cooldown

- **fix: enforce one water-route safety contract across Pi UI/backend and firmware (`pi_server/app.py`, `pi_server/settings.html`, `src/waterSystem.cpp`):**
  - เพิ่ม server-side guard ให้ Pi backend ลด `preferred_route` ลงเป็น `SUMP_DIRECT` และปิด `allow_direct_sump_refill` อัตโนมัติเมื่อ route valve หรือ overflow sensor ไม่ครบ แม้ request จะไม่ได้มาจากหน้า Settings
  - ปรับหน้า Settings ให้ปิด `Auto` พร้อมกับ `Fish Route` เมื่อ firmware ยังไม่พบ overflow sensor เพื่อไม่ให้หน้าเว็บเสนอ option ที่ backend จะ normalize ทิ้งอยู่แล้ว
  - harden firmware ให้ fish route พร้อมใช้งานก็ต่อเมื่อมีทั้ง route valve และ overflow sensor เพื่อกัน stale config หรือ publisher อื่นสั่ง route ผ่านตู้ปลาใน hardware ที่ไม่ครบ safety interlock

- **fix: harden water-flow state handling and control-zone telemetry (`src/waterSystem.cpp`, `test/test_native/test_water_system_native.cpp`, `pi_server/app.py`, `pi_server/index.html`, `pi_server/settings.html`, `pi_server/hardware_test.html`):**
  - เคลียร์ `manual_refill` เมื่อ fish route หยุดเพราะ overflow, mix-tank high level, หรือครบ `fish_refill_max_runtime_ms` เพื่อไม่ให้คำสั่ง manual ค้าง armed แล้วกลับมาเริ่มเองในรอบถัดไป
  - คงสถานะ `BLOCKED` เมื่อ circulation pump ไม่มีจริง แทนการปล่อยให้ loop หลักไหลกลับไป `IDLE` พร้อมเพิ่ม native regression tests ครอบ manual fish stop, circulation-missing, และ control-zone semantics
  - เปลี่ยน `mix_tank_control_zone` ให้เป็น true เฉพาะตอน circulation ทำงานและโซนถังผสมไม่ได้ refill/settling/alarm/blocked พร้อมปรับ fallback ฝั่ง Pi เป็น false และเปลี่ยนข้อความ dashboard เป็น `พร้อมควบคุม/ยังไม่พร้อม`

- **fix: reduce noisy water interval logs and align dashboard automation workflow with the current state machine (`src/waterSystem.cpp`, `pi_server/index.html`, `pi_server/pwa/sw.js`):**
  - หยุดเขียน reason log ซ้ำทุกวินาทีในสถานะ `WAIT_REFILL_INTERVAL` และ `MIX_TANK_SETTLING` แม้ข้อความบน dashboard ยังอัปเดตตามเวลาคงเหลือปกติ
  - เพิ่ม state `MIXING_AFTER_A` และ transitional `DISABLED` handling กลับเข้า workflow card ของหน้า dashboard พร้อมปรับข้อความ `ตอนนี้`, `ถัดไป`, และ step progression ให้ตรงกับ automator code ปัจจุบัน
  - bump PWA cache เป็น `aquaponics-v25` เพื่อให้หน้า dashboard โหลดเวอร์ชันใหม่ทันทีหลัง deploy

- **fix: persist water cooldown timers across reboot and format wait durations for humans (`src/waterSystem.cpp`, `test/test_native/test_water_system_native.cpp`, `pi_server/index.html`, `pi_server/settings.html`, `pi_server/hardware_test.html`, `pi_server/pwa/sw.js`):**
  - บันทึกช่วงรอของ `WAIT_REFILL_INTERVAL`, `fish route cooldown`, และ `dilution hold` ลง NVS เพื่อให้ระบบน้ำกู้เวลาคงเหลือหลัง ESP32 รีบูตได้ โดยยังไม่ resume ปั๊มหรือ manual refill เอง
  - เปลี่ยน reason ของระบบน้ำให้พูดช่วงเวลารอเป็นรูปแบบอ่านง่าย เช่น `14 วัน 1 ชั่วโมง` แทนตัวเลขวินาทีดิบ
  - ปรับ formatter ของหน้า dashboard, settings, และ hardware test ให้แสดง hold/wait duration เป็นวัน/ชั่วโมง/นาที พร้อม bump PWA cache เป็น `aquaponics-v26`

- **fix: stop showing fake `0 วินาที` water timers before full ESP32 water status arrives (`pi_server/app.py`, `pi_server/settings.html`, `pi_server/index.html`, `pi_server/hardware_test.html`, `pi_server/pwa/sw.js`):**
  - แยก `sensor` payload ชุดย่อออกจาก `water status` payload แบบเต็ม เพื่อไม่ให้หน้า Pi เอา state บางส่วนมาแสดงร่วมกับ reason/timer default แล้วเกิดการ์ดขัดกันเอง
  - ถ้ายังไม่ได้รับ water status แบบเต็มหรือข้อมูลชุดล่าสุดเก่าเกิน threshold จะ fallback เป็นสถานะ `Waiting for ESP32 status` และแสดง `--` แทนเวลารอ/flag runtime ที่ยังไม่รู้จริง
  - bump PWA cache เป็น `aquaponics-v27` เพื่อให้หน้าเว็บหยิบ logic แสดงผลสถานะน้ำชุดล่าสุดทันทีหลัง deploy

- **fix: smooth noisy water level inputs and log full water-status publish failures (`src/waterSystem.cpp`, `src/localMqtt.cpp`, `test/test_native/test_water_system_native.cpp`):**
  - เพิ่ม debounce ให้ low-level sensor และใช้ immediate-trip + delayed-clear กับ high/overflow เพื่อกัน state ระบบน้ำแกว่ง `WAIT_REFILL_INTERVAL`/`IDLE` จาก float switch เด้ง แต่ยังหยุดตาม safety input ได้ทันที
  - เพิ่ม regression test สำหรับ low sensor bounce ระหว่าง cooldown เพื่อกันปัญหา state เด้งกลับมาอีก
  - เพิ่ม error log เมื่อ publish `aquaponics/status/water_system` ไม่สำเร็จ เพื่อให้แยกได้ทันทีว่า Pi รอ status เพราะบอร์ดส่งไม่ออกหรือเพราะ MQTT path มีปัญหา

- **fix: make settings page water card subscribe to live water status updates (`pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - เพิ่ม `socket.io` ให้หน้า settings และฟัง event `water_status_update` จาก backend โดยตรง
  - แยก renderer ของ water runtime/status card ออกจากการ populate ฟอร์ม เพื่อให้การ์ดน้ำอัปเดตสดได้โดยไม่ทับค่าฟอร์มที่ผู้ใช้กำลังแก้อยู่
  - bump PWA cache เป็น `aquaponics-v28` เพื่อให้หน้า settings ดึง JS logic ใหม่ทันทีหลัง deploy

- **fix: stop settings page from failing hard when the WebSocket client script is unavailable (`pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - เปลี่ยนหน้า settings ให้ใช้ `socket.io.min.js` ตัวเดียวกับ dashboard/hardware test แทน path แยก เพื่อไม่ให้การโหลดหน้าแตกต่างจากหน้าที่ใช้งานได้อยู่แล้ว
  - guard `io()` ไว้ไม่ให้ JavaScript ตายทั้งหน้า ถ้า WebSocket client ยังไม่พร้อม หน้า settings จะยังโหลดค่าผ่าน `/api/settings` ได้ตามปกติ
  - bump PWA cache เป็น `aquaponics-v29` เพื่อให้ browser หยิบ script path และ fallback logic ล่าสุด

- **fix: distinguish missing water-status topic from a fully offline ESP on Pi pages (`pi_server/app.py`, `pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - เปลี่ยน backend ให้แยกกรณี `ESP offline` ออกจากกรณี `ESP ยัง online แต่ Pi ไม่เคยเห็น/เห็นไม่สดของ aquaponics/status/water_system` พร้อมใส่ diagnostic fields additive ใน `water_system` status API เดิม
  - ปรับหน้า Settings ให้เลิกทับ `reason` ด้วย fallback ตายตัวเมื่อ `status_seen=false` และใช้ overview summary คนละข้อความสำหรับ `topic missing` กับ `topic stale`
  - bump PWA cache เป็น `aquaponics-v30` เพื่อให้ browser โหลด diagnostic water-card ชุดใหม่ทันทีหลัง deploy

- **fix: keep water runtime state visible when the dedicated water-status topic is missing but sensor packets still arrive (`pi_server/app.py`, `pi_server/settings.html`, `pi_server/hardware_test.html`, `pi_server/pwa/sw.js`):**
  - ให้ Pi backend ใช้เฉพาะ water runtime subset ที่มากับ `aquaponics/sensors` เป็น fallback แบบลดรูปเมื่อ `aquaponics/status/water_system` หายไป โดยยังคง `status_seen=false` และ `reason` เชิงวินิจฉัยเดิมไว้
  - ปรับหน้า Settings ให้แสดง state, route, output, และ low/high/overflow จาก fallback นี้ได้ โดย field ที่ sensor payload ไม่ได้ส่งจะยังเป็น `--` แทนการเดาเป็น `NO`
  - ปรับหน้า HW Test ให้แยก data source เป็น `Water status topic` กับ `Sensor payload fallback` ให้ตรงกับแหล่งข้อมูลจริง และ bump PWA cache เป็น `aquaponics-v31`

- **fix: relabel fallback water diagnostics on Settings so the card stops implying the Pi-generated warning came from the ESP (`pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - เปลี่ยนหัวข้อ `State` เป็น `Fallback State` เมื่อการ์ดน้ำกำลังใช้ runtime subset จาก `aquaponics/sensors` แทน topic เต็ม
  - เปลี่ยนหัวข้อ `Last reason from device` เป็น `Live diagnostic from Pi` เมื่อข้อความที่แสดงเป็น warning จาก backend เช่น `topic stale` หรือ `topic missing`
  - bump PWA cache เป็น `aquaponics-v32` เพื่อให้ browser โหลด label ชุดใหม่ทันทีหลัง deploy

- **fix: soften the Settings hero when fresh sensor fallback is available so the main card no longer shouts `STALE` while runtime state is still updating (`pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - ถ้า Pi ยังได้ water runtime subset สดจาก `aquaponics/sensors` อยู่ หน้า Settings จะใช้ badge `Fallback` และ headline ที่บอกชัดว่าเป็นสถานะชั่วคราวจาก sensor packet แทนการขึ้น `STALE` เป็นข้อความหลักของการ์ด
  - เปลี่ยนข้อความใน reason box เป็น note ว่า dedicated water status หายไปนานเท่าไร โดยไม่อ้างว่าเป็น `reason from device` ตรง ๆ ในโหมด fallback
  - bump PWA cache เป็น `aquaponics-v33` เพื่อให้ browser โหลด wording ของ fallback hero ชุดล่าสุด

- **fix: queue dedicated water-status refreshes after successful sensor publishes so `aquaponics/status/water_system` cannot lag behind a healthy sensor stream (`src/localMqtt.cpp`):**
  - ถ้า `aquaponics/sensors` publish สำเร็จ firmware จะ queue `aquaponics/status/water_system` เพิ่มอีกชั้นผ่าน pending-status path เดิม โดยใช้ cadence เดียวกับ sensor publish แทนการพึ่ง scheduler แยกเส้นเดียว
  - ช่วยปิดช่องที่ Pi ยังเห็น sensor packet สดอยู่แต่ dedicated water-status topic กลับหายหรือค้าง ทั้งที่ MQTT connection ยังใช้งานได้

- **fix: stop truncating water-system reasons mid-UTF-8 sequence before publishing dedicated water status (`include/waterSystem.h`, `src/waterSystem.cpp`):**
  - ขยาย `WaterSystemStatus.reason` และ `stateReason` buffer ให้พอรองรับข้อความ reason ภาษาไทยจริงที่ใช้ใน state machine ซึ่งหลายข้อความยาวเกิน 96 bytes อยู่แล้ว
  - เพิ่มตัว copy แบบ UTF-8 safe ตอนเขียน `_status.reason` เพื่อกันการตัดกลาง multibyte sequence ถ้ามีข้อความยาวเข้าใกล้ขอบ buffer ในอนาคต
  - แก้ต้นเหตุที่ทำให้ `aquaponics/status/water_system` สามารถปล่อย JSON ที่ decode ไม่ได้บน Pi ขณะที่ `aquaponics/sensors` ยังมาปกติ เพราะ dedicated topic มี field `reason` ภาษาไทยแต่ sensor payload ชุดย่อไม่มี

## [2026-05-11] - Water Settings UX Refactor

### Changed

- **ui: redesign the Water System settings card around a user-first reading order (`pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - จัดการ์ด Water System ใหม่ให้เริ่มจากภาพรวมสดของระบบก่อน เช่น state ปัจจุบัน, reason ล่าสุด, route ที่ตั้งไว้/ใช้งานจริง, alarm, fish wait, และ dilution hold เพื่อให้คนหน้างานรู้ทันทีว่าระบบกำลังทำอะไร
  - แยก `Quick Controls`, `Safety Limits`, `Route Strategy`, และ `Diagnostics` ออกจากกันอย่างชัดเจน แทนการวาง toggle, runtime policy, live status, และ manual actions ยาวต่อเนื่องในระดับเดียวกัน
  - เพิ่ม apply feedback ในการ์ด Water System และ bump PWA cache เป็น `aquaponics-v21` เพื่อให้ Pi/dashboard โหลดหน้า Settings โฉมใหม่และเห็นสถานะการส่งคำสั่งได้ชัดขึ้นหลัง deploy

- **fix: make the Water System settings card size itself from the card width instead of the viewport (`pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - เปลี่ยน nested grid ของ Water card ให้ใช้ `auto-fit + minmax(...)` ตามความกว้างจริงของการ์ดแต่ละใบ แทนการตัดสินจาก viewport อย่างเดียว เพื่อแก้เคสหน้า desktop กว้างแต่ card จริงแคบจาก multi-card grid แล้ว layout ข้างในยังฝืนแตก 2 คอลัมน์
  - ลดการ wrap แบบแตกทีละตัวอักษรใน headline, reason, labels, และ chip values ของการ์ดน้ำ เพื่อไม่ให้ text ภาษาไทย/อังกฤษยาว ๆ พังเป็นแนวตั้งเมื่อช่องแคบลง
  - bump PWA cache เป็น `aquaponics-v23` เพื่อบังคับให้ browser โหลด CSS ของหน้า Settings ชุดที่แก้ card-collapse รอบนี้แล้ว

- **design: shorten the Water System card by collapsing setup-heavy sections behind compact summaries (`pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - ย้าย `Safety Limits` และ `Route Strategy` เข้า panel แบบพับได้ `Setup & Safety` เพื่อให้ operator เห็น live state กับ quick controls ก่อน และทำให้ความสูงของ card สั้นลงมากในสถานะปกติ
  - กระชับ spacing ของ overview chips, action buttons, และข้อความนำทางของการ์ดน้ำ โดยคง input ids, button actions, และ API bindings เดิมทั้งหมด
  - bump PWA cache เป็น `aquaponics-v24` เพื่อให้ browser โหลด layout Water card เวอร์ชันที่ย่อความสูงรอบนี้ทันทีหลัง deploy

## [2026-05-10] - NETPIE Cloud Unreachable Backoff

### Changed

- **docs: redraw the full-system flow as a vertical main-program flowchart (`forTestFlow/full-system-overview-flow.json`):**
  - เปลี่ยนจากภาพรวมทั้งระบบแบบกว้างหลายคอลัมน์ ให้เป็นผังแนวตั้งที่อิงลำดับจริงของ `setup()` และ `loop()` ใน `src/main.cpp`
  - แยก 3 FreeRTOS task หลักออกเป็นกล่อง runtime ที่อ่านง่ายขึ้น เพื่อให้ดู sequence ของ firmware หลักได้ทันทีว่าเริ่มจาก boot, init modules, create tasks, แล้ววนทำงานต่อเนื่องอย่างไร
  - ปรับ layout อีกรอบให้เหลือแกนกลางเส้นเดียวและย้ายคำอธิบาย parallel ไปไว้ใน note แยก เพื่อลดเส้นชนกันและไม่ให้ card บังเส้นเวลาเปิดดูใน flow planner

- **docs: add a draw.io version of the vertical main-program flow (`forTestFlow/full-system-overview-flow.drawio`):**
  - เพิ่มไฟล์ `.drawio` สำหรับเปิดใน diagrams.net / draw.io ได้ตรง ๆ โดยยึดเนื้อหาและ layout จาก flow แนวตั้งชุดล่าสุด
  - ใช้โครงแบบแกนกลางเส้นเดียวเหมือน JSON เวอร์ชันล่าสุด เพื่อให้แก้ต่อหรือ export เป็นรูปจาก draw.io ได้ง่ายขึ้น

- **fix: reduce repeated NETPIE reconnect warnings when the cloud path is unreachable but Local MQTT is healthy (`src/netpie.cpp`):**
  - ตีความ `rc=-2` ให้ชัดว่าเป็นฝั่ง TCP connect ไป broker ไม่สำเร็จ ไม่ใช่ auth error แล้วเพิ่มข้อความ log ให้บอกสาเหตุอ่านง่ายขึ้น
  - ถ้า Local MQTT ยังเชื่อมกับ Pi ได้ปกติ แต่ NETPIE ล้มเหลวแบบ `rc=-2` ซ้ำหลายครั้ง firmware จะพัก retry cloud ชั่วคราวแทนการเตือนซ้ำทุก 2 นาที
  - ถ้า Local MQTT หลุดเมื่อไร ระบบจะยกเลิก cooldown ของ NETPIE และกลับไปลอง reconnect ทันที เพื่อไม่ให้ cloud path ถูกพักนานเกินไปในช่วงที่ local path ใช้งานไม่ได้

- **docs: add a reusable repo skill for recent TDS, water-system, NETPIE, flow-doc, and pin-map regression guards (`.agent/skills/recent-aquaponics-regression-guards/SKILL.md`, `.agent/skills/water-system-and-pi-settings-decisions/SKILL.md`):**
  - รวมบทเรียนจากงานรอบล่าสุดให้เป็น skill ที่ future agents เรียกใช้ได้เวลาแตะ TDS scale/calibration, fish-route refill safety, `rc=-2` ของ NETPIE, หรือการเขียนเอกสาร pin map
  - อัปเดต skill เดิมของ water system ให้ตรงกับ behavior ปัจจุบัน เช่น fish refill แบบ latched, mix-tank high safety stop, และ default `fish_refill_max_runtime_ms = 30000 ms`

- **feat: add date-range filtering and Excel export for the graphs page (`pi_server/app.py`, `pi_server/graphs.html`, `pi_server/setup.sh`, `pi_server/pwa/sw.js`):**
  - เพิ่มตัวเลือก `ตั้งแต่วันที่` และ `ถึงวันที่` บนหน้า graphs เพื่อให้ดูเฉพาะช่วงวันที่ที่ต้องการได้ แทนการล็อกอยู่กับ window ย้อนหลังค่า default อย่างเดียว
  - เพิ่ม route export เป็นไฟล์ Excel-compatible `.xls` โดยดึงจาก history query ชุดเดียวกับหน้า graphs เพื่อให้ไฟล์ที่โหลดออกตรงกับช่วงเวลาที่ผู้ใช้เลือกบนหน้าเว็บ และไม่ต้องพึ่ง package เพิ่มบน Pi
  - bump cache version ของ PWA เพื่อให้ Pi ที่ deploy แล้วโหลดหน้า graphs เวอร์ชันใหม่ได้ตรง

- **fix: add fallback DNS handling for hotspot mode when Tailscale-only resolvers break public lookups (`pi_server/start_hotspot.sh`, `pi_server/dnsmasq_ap.conf`):**
  - ถ้า Pi ออก internet ได้ด้วย IP แต่ `resolv.conf` ถูก Tailscale เขียนให้ใช้ MagicDNS แล้ว resolve public domain ไม่ได้ ระบบ hotspot จะสลับไปใช้ public DNS fallback อัตโนมัติแทน
  - เพิ่ม `dnsmasq` upstream DNS แบบ explicit เพื่อไม่ให้ ESP32 และอุปกรณ์หลัง AP ล้มตาม resolver ฝั่ง Pi เวลา MagicDNS หรือ tailscale DNS ใช้งานไม่ได้

- **fix: clear duplicate hotspot NAT rules fully before re-adding them (`pi_server/setup_ap.sh`, `pi_server/start_hotspot.sh`):**
  - เปลี่ยนการลบ iptables rule จากลบแค่ 1 ครั้ง เป็นลบซ้ำจนหมดก่อนเพิ่ม rule ใหม่ เพื่อไม่ให้ `MASQUERADE` และ `FORWARD` ซ้ำสะสมทุกครั้งที่ hotspot service ถูก restart

## [2026-05-09] - README Standardization

### Changed

- **docs: rewrite `README.md` to match the current system architecture, modules, and operator workflow:**
  - เปลี่ยน README จากเอกสารภาพรวมแบบเก่าที่ยังอ้างอิงระบบ/ฮาร์ดแวร์บางส่วนไม่ตรงกับ code ปัจจุบัน ให้เป็นคู่มือเริ่มต้นของโปรเจกต์ที่ครอบคลุม firmware, Pi dashboard, automator, water system, fan, light, fish feeder, security, setup, MQTT topics, CLI commands, และ test flow
  - ตัดข้อมูลเก่าที่ทำให้เข้าใจผิด เช่นคำอธิบายหน้าเว็บและฮาร์ดแวร์บางจุดที่ไม่ตรงกับ implementation ปัจจุบัน แล้วจัดโครงสร้างใหม่ให้อ่านง่ายสำหรับทั้ง operator และคนรับช่วงงานต่อ

## [2026-05-09] - Water Refill Safety Tightening

### Changed

- **fix: keep fish-tank refill latched until timeout/overflow while adding mix-tank high-level safety (`src/waterSystem.cpp`, `test/test_native/test_water_system_native.cpp`):**
  - เมื่อเริ่มเติมผ่าน route เข้าตู้ปลาแล้ว ระบบจะวิ่งต่อให้ครบ `fish_refill_max_runtime_ms` แม้ `low level` ของถังผสมจะหลุดระหว่างทาง แทนการหยุดกลางคันจากการอ่าน sensor รายรอบ
  - เพิ่ม `high level` ของถังผสมเป็น safety stop สำหรับ fish-tank refill ทั้งแบบ auto และ manual เพื่อกันน้ำในถังผสมล้น แม้ overflow ของตู้ปลายังไม่ทำงาน
  - เพิ่ม native regression tests ครอบคลุมเคสเติมเข้าตู้ปลาต่อเนื่องหลัง low sensor หลุด และเคสหยุดทันทีเมื่อ `high level` ของถังผสม active

## [2026-05-07] - Water Settings Consistency Hardening

### Changed

- **fix: align TDS base model with 982 ppm handheld scale and reject unsafe narrow-span calibration (`src/TdsSensor.cpp`, `include/config.h`, `test/test_native/test_tds_native.cpp`):**
  - แยก EC polynomial ออกจาก TDS conversion factor แล้วตั้ง default factor เป็น `0.695` เพื่อให้ scale เริ่มต้นใกล้ handheld meter ที่อ่าน `1413 uS/cm ≈ 982 ppm` มากกว่า model เดิมแบบ `0.5`
  - เพิ่ม guard ไม่ให้ calibration 2 จุดผ่านถ้า normalized voltage span แคบเกิน `0.050V` เพราะจะขยาย noise และ pump/fองอากาศให้ค่า ppm แกว่งแรงผิดจริง
  - อัปเดต native tests ให้สะท้อน conversion factor ใหม่และเพิ่ม regression test กัน calibration จากช่วงแรงดันแคบเกินไป

- **fix: stabilize live TDS readings against small ADC jitter (`src/TdsSensor.cpp`, `include/config.h`, `test/test_native/test_tds_native.cpp`):**
  - เพิ่ม deadband ให้ทั้ง filtered voltage และค่า TDS หลัง temperature compensation เพื่อกันการแกว่งเล็กน้อยไม่ให้กระทบค่าที่โชว์ทุก cycle
  - จำกัดการเปลี่ยนค่า TDS ต่อรอบด้วย max-step หลัง EMA เพื่อให้ค่าที่รายงานนิ่งขึ้นโดยยังตามการเปลี่ยนจริงได้
  - tighten native regression test ให้จับเคส single-sample jump ที่ไม่ควรทำให้ค่า TDS กระโดดเกินเพดานใหม่

- **fix: make TDS calibration temperature-aware end-to-end (`src/TdsSensor.cpp`, `include/TdsSensor.h`, `src/localMqtt.cpp`, `pi_server/app.py`, `pi_server/settings.html`, `test/test_native/test_tds_native.cpp`):**
  - ส่ง `low_temp` และ `high_temp` จากหน้า Pi ไปยัง ESP32 จริง แทนการเก็บไว้แค่ใน settings file ฝั่ง Pi
  - ทำให้ firmware normalize calibration voltage ของ standard solution กลับไปที่ reference 25°C ก่อนคำนวณ K/offset เพื่อให้สูตร calibration กับ runtime ใช้ฐาน temperature compensation ชุดเดียวกัน
  - เพิ่ม native regression test สำหรับเคส calibrate ที่อุณหภูมิของ standard ไม่เท่ากับ 25°C เพื่อกัน regression วัด standard เพี้ยนซ้ำ

- **fix: make Water System settings use one normalized config model across Pi UI, backend, and ESP32 payloads (`pi_server/app.py`, `pi_server/settings.html`):**
  - รวม default และ validation ของ Water System ไว้ที่ backend ฝั่ง Pi เพื่อลดกรณีหน้าเว็บ save ได้ค่าแบบหนึ่ง แต่ ESP32 sanitize ไปใช้อีกค่าแบบหนึ่ง
  - ทำให้ `GET /api/settings` ส่ง Water System เป็นสถานะ runtime ล่าสุดจาก `_current_water_status()` แทนการคืน runtime fields เก่าที่เคยถูก save ค้างไว้ใน settings file
  - หยุด persist ค่า direct-action อย่าง `manual_refill` และ runtime-only fields ปะปนกับ config ปกติ เพื่อไม่ให้การกด manual commands หรือการกด Save General Settings ทิ้งค่า stale ข้าม restart
  - ทำให้หน้า Settings ปิด control ที่ hardware ใช้ไม่ได้ตามสถานะที่ ESP32 รายงาน เช่น ปิด Auto Refill เมื่อไม่มี level sensors และปิด Fish Route เมื่อยังไม่มี overflow sensor หรือ route valve
  - ทำให้ Water System apply/save ฝั่งหน้าเว็บส่งเฉพาะ config fields ที่มีผลจริงกับระบบ และ reload สถานะจาก server หลัง apply เพื่อให้ UI แสดงค่าที่ normalize แล้วจริง

- **fix: gate Water System controls by installed pump capability (`pi_server/app.py`, `pi_server/settings.html`):**
  - ส่ง `has_circulation_pump` และ `has_refill_pump` ผ่าน Pi water status/default snapshot ให้หน้าเว็บแยกได้ว่า control ไหนยัง unknown กับ control ไหน unsupported จริง
  - ปิด `Circulation Pump`, `Auto Refill`, และปุ่ม `Manual Refill On/Off` เมื่อ ESP32 รายงานว่าไม่มี actuator ตัวนั้น และกันไม่ให้หน้าเว็บส่ง config/command ที่ hardware ใช้ไม่ได้กลับไปหา firmware

- **fix: restore admin-only navigation and add backend Water guards (`pi_server/header.js`, `pi_server/app.py`, `pi_server/pwa/sw.js`):**
  - คืน `admin` gate ให้ลิงก์ Activity Logs ใน shared header เพื่อไม่ให้ผู้ใช้ทั่วไปเห็นเมนูที่ backend ยังป้องกันไว้ด้วย `@admin_required`
  - เพิ่ม server-side clamp ให้ `circulation_enabled`, `refill_enabled`, และ `manual_refill` ถูกปิดอัตโนมัติเมื่อ ESP32 รายงานว่า actuator หรือ level sensors ที่จำเป็นไม่มีจริง แม้ request จะไม่ได้มาจากหน้า Settings
  - bump cache version ของ PWA service worker เพื่อให้ client โหลด `header.js`, `base.css`, และหน้า settings/admin ชุดล่าสุดแทน asset เก่าที่ค้างใน cache

- **feat: make automator dose A/B in staged mix cycles (`src/automator.cpp`, `include/automator.h`, `include/config.h`):**
  - เปลี่ยน flow การจ่ายปุ๋ยจาก A -> B ทันที เป็น A -> รอผสม -> B -> รอผสม -> วัด TDS ใหม่ โดยยังคงยึด `target TDS` เป็นเป้าหมายรวมและเริ่มต้นที่สัดส่วน A:B แบบ 1:1
  - เพิ่ม deadband `AUTOMATOR_TDS_HYSTERESIS_PPM` เพื่อลดอาการกระพือรอบจ่ายเมื่อค่า TDS อยู่ใกล้เป้าหมาย
  - ทำให้ timer ของรอบ dosing/mixing ถูก pause ระหว่างที่ Water System block automation เพื่อไม่ให้รอบผสมหรือรอบจ่ายหมดเวลาไปเองตอน circulation ไม่พร้อม

- **feat: expose automation tuning for staged A/B dosing on Pi Settings (`pi_server/app.py`, `pi_server/settings.html`, `src/localMqtt.cpp`, `src/automator.cpp`, `include/automator.h`):**
  - เปิดให้ปรับ `dose_a_ml`, `dose_b_ml`, `mix_after_a_ms`, `post_dose_mix_ms`, และ `tds_hysteresis_ppm` จากหน้า Settings พร้อมคำอธิบายแบบ operator-friendly ว่าแต่ละค่ามีผลกับรอบจ่ายอย่างไร
  - รวม automation defaults และ validation ไว้ที่ backend ฝั่ง Pi เพื่อให้ `Save All` และ `Apply TDS Auto-Dosing` ใช้ normalization rule เดียวกันก่อน publish ไป ESP32
  - ทำให้ ESP32 persist ค่า automation tuning ชุดใหม่ลง NVS, รับค่าผ่าน MQTT, และรายงาน config ที่ใช้งานจริงกลับมาทาง local status payload
  - bump cache version ของ PWA service worker เพื่อให้ client ได้หน้า Settings automation card ชุดล่าสุดแทน cache เดิม

## [2026-05-03] - Switch Board To ESP32-S3 N16R8

### Changed

- Add a shared header alert strip in `header.js` that polls existing health, health-details, and water-status APIs, then shows system-wide warnings in the header when the ESP32 is offline, the heartbeat is stale, a Water System alarm is active, or Pi services like Mosquitto and Camera fail.
- Add direct alert-strip links to `hardware_test.html` and `settings.html` so operators can jump straight from a header warning to the right control page.
- Make each header alert item itself route to the relevant page, and reduce service-noise by grouping multiple failed Pi services into one summary item with per-service details inside the strip.
- Keep the shared header status visible even when no active issues exist by showing a `System Normal` state instead of hiding the whole alert pill and strip.
- Make the shared header status render immediately on page load and fetch alert data in parallel with timeouts, so the alert pill and strip no longer appear to disappear while slow health-detail checks are still running.
- Bump the PWA service-worker cache version so pages stop serving an older cached `header.js` after the shared header alert updates.
- Stop blocking shared-header rendering on `/api/me`, so the alert pill and strip are injected immediately and admin navigation upgrades only after the user-role request returns.
- Keep the alert strip directly under the header top instead of letting nav insertion push it lower, and move the alert pill to the more visible right-side position next to the live header status pills.
- Mount shared header controls into the dashboard's existing `status-bar` when that layout is present, so the alert pill and shared header controls show up on `index.html` instead of being attached as a separate hidden right-side group.
- Shrink the `System Normal` header strip into a compact layout and correct all Hardware Test alert links to the real `/hwtest` route.
- Replace the top header status pill with a bell icon button and hide the normal-state strip until the bell is opened, while still auto-expanding the strip when real alerts exist.
- Remove the persistent shortcut strip and turn the bell into the only entry point for opening a compact alert list panel, even when real alerts are present.
- Rebuild the bell panel as a true anchored dropdown inside the header action area, with outside-click and `Escape` dismissal so the alert UI behaves like a production dropdown instead of a block below the header.
- Promote the bell dropdown to a viewport-level floating overlay that positions itself from the bell button, avoids header clipping, and reflows on resize/scroll for production-grade behavior with larger alert lists.
- Harden the bell dropdown close behavior by using capture-phase outside pointer handling, a real close button inside the panel, and the `hidden` attribute so the alert panel can always be dismissed reliably.
- Mount the alert panel directly under `document.body` instead of inside the header action cluster, so dashboard layout and clipping rules cannot drag the floating dropdown into the wrong place.
- Delay dropdown positioning until the next animation frame and keep it visually hidden until placement is computed, so the bell panel no longer flashes at the viewport corner before snapping into place.
- Align `hardware_test.html` jargon with the Settings page by standardizing visible technical terms like `Auto Refill`, `Preferred Route`, `Fish Route`, `Direct Sump`, `Manual ON/OFF`, `Feed Now`, `Runtime Status`, and `YES/NO` so operators do not see different terminology across pages.
- Standardize jargon across the whole Settings page so technical labels, status values, route names, and action text now use consistent English terms while guidance and examples remain Thai.
- Keep technical jargon in English on the refreshed Settings cards, so labels and runtime/status terms like `Command Source`, `Schedule`, `Runtime`, `Route`, and `Feed Now` stay familiar while the surrounding guidance remains Thai.
- Extend the Settings refresh pass so `Light Control` and `Fish Feeder` now use the same friendly guide-box and operator-facing label style as the other cards, and make `Water System` time fields unit-aware with per-field selectors like seconds, minutes, hours, and days while still posting canonical millisecond values to the backend.
- Reword the Settings `Water System`, `Exhaust Fan`, and `Notifications` cards for friendlier reading: replace technical labels like refill interval/fallback/max runtime with user-focused Thai descriptions, add short guide boxes, and align Fan/Notifications prompts with the same simple input flow used by the threshold card.
- Make the Settings `Sensor Thresholds` card more user-friendly and less cluttered: clearly label each range as `Min`/`Max`, add a short explanation of what those values mean, and show compact example ranges for pH, temperature, humidity, and TDS.
- Change the PlatformIO board definition from the generic `esp32-s3-devkitc-1` N8 target to `esp32-s3-devkitc1-n16r8`, so production builds target the actual ESP32-S3 N16R8 hardware instead of relying on flash/PSRAM overrides alone.
- Add a local `boards/esp32-s3-devkitc1-n16r8.json` manifest so the pinned `espressif32@6.4.0` platform can resolve the N16R8 board ID even though that upstream platform version does not ship it yet.

## [2026-05-02] - Fix HW Test Water Status Precedence

### Changed

- Remove `target_ph` from the automation stack end-to-end: Settings/UI, Pi backend payloads, MQTT automation config, ESP32 automator config/state, CLI status output, and default settings now use TDS-only automation while pH remains monitor-only.
- Fix HW Test water preset saves so `fish_route` and `sump_route` snapshot the chosen route/fallback before refreshing `/api/settings`; this prevents the UI from accidentally posting stale values like `AUTO` and re-enabling unintended fish->sump fallback.
- Re-check mix-tank `low/high` demand before auto-falling back from fish refill to `SUMP_DIRECT`, so if the fish route stops and the sump sensors are already conflicting or `high` is active, the controller now stops instead of continuing into mix-tank refill.
- Fix production route-valve polarity constants so `REFILL_ROUTE_TO_FISH_STATE` is the OFF/idle state and `REFILL_ROUTE_TO_SUMP_STATE` energizes the mix-tank path; this also makes `หยุด Flow Test` close the mix-tank actuator instead of leaving it latched on.
- Split HW Test flow presets by real sensor source: `fish_route` now uses manual fish-route flow so it ignores mix-tank `low/high` and stops on the fish-tank overflow sensor, while `sump_route` still uses auto-refill logic so the mix-tank `low/high` pair can stop it correctly.
- Fix water route-valve polarity so selecting `FISH_TANK` now drives the relay to the fish path and `SUMP_DIRECT` drives the sump/mix-tank path; firmware now uses the route-state constants instead of hardcoded relay levels.
- Fix HW Test flow-sequence cancellation so stopping or switching tests now clears the `Flow Test Mode` badge even when the sequence is mid-step on `circulation_only`, `fish_route`, or `sump_route`.
- Add a prominent `กำลังอยู่ใน Flow Test Mode` badge plus an automatic sequence runner on HW Test so operators can visibly track manual flow-mode state and run `circulation -> fish route -> sump route -> stop` without manual timing.
- Add one-click flow-test presets on the HW Test water section so operators can run circulation-only, fish-route refill, and sump-route refill checks directly from water-system controls without relying on TDS/pH chemistry state.
- Restore a slim set of live water-status fields into `aquaponics/sensors` so HW Test can keep updating `sump_low`, `sump_high`, overflow, and circulation state even if the dedicated `aquaponics/status/water_system` path lags during circulation testing.
- Separate live water-system runtime status from saved Pi settings so toggling HW Test controls like circulation no longer freezes `sump_low`, `sump_high`, and overflow cards behind stale `app_settings["water_system"]` snapshots.
- Fix Pi water hardware defaults so a deployed `pi_server` without adjacent firmware `include/config.h` no longer forces `has_level_sensors`, `has_overflow_sensor`, and `has_route_valve` to `false`; the backend now preserves live ESP flags unless firmware pin macros are actually readable.
- Stop latching `WATER_STATE_ALARM` when both sump level inputs read active at once; firmware now leaves the water controller in normal level handling instead of freezing the water status until a manual alarm clear.
- Fix Pi water-system status API precedence so HW Test uses dedicated `aquaponics/status/water_system` values before stale `last_data` fallbacks; this prevents `FISH_TANK_OVERFLOW` from incorrectly showing as not installed after config and firmware updates.
- Fix HW Test frontend state merging so `/api/settings` refresh no longer overwrites live water status flags like `has_overflow_sensor`; the page now waits for confirmed ESP water status before labeling a sensor as not installed.
- Refactor `hardware_test.html` to separate water config from live status, remove periodic config polling, and add on-page diagnostics that show data source, freshness, and raw install flags for water sensors.
- Make Pi server derive water hardware presence flags from `include/config.h` so configured sensors like `FISH_TANK_OVERFLOW_PIN=47` cannot regress to `ไม่ได้ติดตั้ง` because of stale saved status or legacy payloads.
- Simplify the HW Test water UI again: move diagnostics into a collapsed panel, promote live LOW/HIGH sensor cards, and remove `ไม่ได้ติดตั้ง` from the main water test display so the page is easier to read during debugging.

## [2026-05-02] - Restore Latest Hardware Pin Defaults

### Changed

- **fix: restore the latest discussed hardware pin defaults in firmware config (`include/config.h`):**
  - คืนค่า pin ล่าสุดที่คุยไว้ให้ `EXHAUST_FAN_PIN=2`, `PUMP_REFILL_PIN=42`, `REFILL_ROUTE_VALVE_PIN=39`, และ `FISH_TANK_OVERFLOW_PIN=47` เพื่อให้ firmware กลับมาใช้ mapping ฮาร์ดแวร์ชุดปัจจุบัน

- **fix: keep the main dashboard from blanking live TDS/pH values between MQTT packets (`pi_server/app.py`):**
  - หยุดล้าง `last_data` ของ sensor เป็น `None` ทันทีเมื่อ key ไม่ถูกส่งมาใน payload รอบนั้น เพราะ firmware อาจ omit ค่าไว้ชั่วคราวระหว่างรอ read ถัดไป โดยเฉพาะ `tds` และ `ph`
  - ให้ล้างค่าเป็น `None` เฉพาะกรณี sensor ถูก disable จริงจาก `sensor_config` เท่านั้น เพื่อให้ dashboard หลักคงค่าอ่านล่าสุดไว้ได้
  - เพิ่ม `dashboard_update` emit ทันทีหลัง Pi รับ sensor MQTT ใหม่ เพื่อลดอาการค่าหน้า dashboard ดูค้างระหว่างรอรอบ broadcast พื้นหลัง

- **fix: slim the main local MQTT sensor payload so dashboard sensor cards reach Pi reliably again (`src/localMqtt.cpp`):**
  - ตัด field ระบบน้ำที่ซ้ำออกจาก payload `aquaponics/sensors` แล้วให้รายละเอียดระบบน้ำไปทาง topic แยก `aquaponics/status/water_system` ตาม design เดิม
  - ขยาย buffer/doc ของ MQTT payload หลัก และเพิ่ม guard ตรวจ JSON overflow ก่อน publish เพื่อกัน packet หลักหลุดจนหน้า dashboard เหลือค่า `0` หรือ `--` โดยเฉพาะ `tds` และ `ph`
  - ขยาย payload ของ water status topic พร้อม guard แยกของมันเอง เพื่อไม่ให้การ์ดระบบน้ำเสียตามเมื่อข้อความสถานะยาวขึ้น

- **docs: add a reusable skill for dashboard live-update regressions (`.agent/skills/dashboard-live-update-regressions/SKILL.md`, `AGENTS.md`):**
  - สรุปอาการซ้ำที่เจอบ่อย เช่น dashboard ค้าง, `tds/ph` เป็น `0/--`, HW test ไม่อัปเดต, และ browser ติด PWA cache เก่า
  - เก็บ root cause หลัก, ลำดับการไล่เช็ก, ข้อห้ามเรื่องการบวมของ `aquaponics/sensors`, และขั้น deploy ที่ต้องอัปเดตทั้ง firmware/Pi พร้อมกัน

## [2026-04-29] - HW Test Water Workflow Refresh

### Changed

- **feat: align HW Test water section with current workflow and active-low sensor inputs (`pi_server/hardware_test.html`, `pi_server/app.py`):**
  - ปรับข้อความและลำดับการทดสอบในหน้า `/hwtest` ให้ตรงกับ workflow ปัจจุบันของ water system state machine แทนแนวคิด water pump test แบบเก่า
  - แสดง `SUMP_LEVEL_LOW`, `SUMP_LEVEL_HIGH`, และ `FISH_TANK_OVERFLOW` แบบ `LOW/HIGH` ตาม wiring จริง โดยระบุชัดว่าเป็น `Active LOW` และแยกกรณี `ไม่ได้ติดตั้ง` สำหรับ overflow sensor
  - เพิ่ม water-status fields ใน WebSocket payload เพื่อให้หน้า hw test อัปเดตสถานะ sensor availability, route valve, preferred route, และ manual refill ได้ครบจากข้อมูลสด
  - แก้ default sensor availability เป็น `unknown` จนกว่า ESP จะส่งสถานะจริง เพื่อไม่ให้หน้า hw test แสดง `ไม่ได้ติดตั้ง` ผิด ๆ ทั้งที่ตั้ง pin ใน firmware แล้ว

- **feat: show water sensor install/status details in terminal `water` command (`src/commandHandler.cpp`):**
  - เพิ่ม output ในคำสั่ง terminal/telnet `water` ให้เห็นว่า level sensor และ overflow sensor ติดตั้งหรือไม่
  - แสดง `SUMP_LEVEL_LOW`, `SUMP_LEVEL_HIGH`, และ `FISH_OVERFLOW` เป็น `LOW/HIGH` พร้อม `(TRIGGERED)` ตาม active level จริง เพื่อให้เทียบกับหน้า HW Test ได้ตรงกัน

- **fix: render SUMP low/high on HW Test as soon as Pi has seen real water status (`pi_server/app.py`, `pi_server/hardware_test.html`):**
  - เพิ่ม `water_status_seen` เพื่อแยกกรณี "ยังไม่เคยได้ payload ระบบน้ำ" ออกจากกรณี "มี payload แล้วแต่ metadata availability ยังไม่ครบ"
  - ทำให้ `SUMP_LEVEL_LOW/HIGH` บนหน้า HW Test แสดง `LOW/HIGH` ได้ทันทีหลัง Pi รับ `aquaponics/status/water_system` จาก ESP แล้ว แทนการค้างที่ `รอสถานะจาก ESP`

- **fix: bump PWA cache after HW Test updates (`pi_server/pwa/sw.js`):**
  - เปลี่ยน `CACHE_NAME` เพื่อบังคับให้ browser/service worker โหลดหน้า `/hwtest` และ script เวอร์ชันใหม่ แทนการติด cache เก่า

- **fix: push water status directly to HW Test over WebSocket (`pi_server/app.py`, `pi_server/hardware_test.html`):**
  - เพิ่ม event `water_status_update` ทันทีเมื่อ Pi รับ `aquaponics/status/water_system`
  - ให้หน้า `/hwtest` ใช้ event นี้อัปเดตการ์ด `SUMP_LEVEL_LOW/HIGH` โดยตรง เผื่อ path รวมของ `dashboard_update` ยังไม่สะท้อนสถานะน้ำล่าสุด

- **refactor: make HW Test poll a dedicated water status endpoint (`pi_server/app.py`, `pi_server/hardware_test.html`):**
  - เพิ่ม `GET /api/water_system/status` ให้หน้า `/hwtest` ดึงสถานะน้ำสดโดยตรง ไม่ต้องพึ่ง settings blob หรือ websocket อย่างเดียว
  - ให้หน้า HW Test poll ทุก 2 วินาทีเพื่ออัปเดต `SUMP_LEVEL_LOW/HIGH` และ field น้ำอื่น ๆ จากแหล่งข้อมูลเดียวที่ชัดเจน

- **refactor: mirror water status into the main sensor payload as a fallback path (`src/localMqtt.cpp`, `pi_server/app.py`):**
  - เพิ่ม field สถานะน้ำสำคัญลงใน `aquaponics/sensors` payload เช่น `sump_low`, `sump_high`, `water_state`, `water_reason`, `has_level_sensors`
  - ให้ Pi sync `water_system` จาก sensor payload และให้ `/api/water_system/status` fallback ไปที่ `last_data` ได้ด้วย เพื่อไม่ให้หน้า HW Test พึ่ง topic `aquaponics/status/water_system` เส้นเดียว

## [2026-04-28] - Deferred Local MQTT Config Apply

### Changed

- **fix: move Local MQTT config/calibration apply work out of the MQTT callback into TaskControl (`src/localMqtt.cpp`, `src/main.cpp`, `include/localMqtt.h`):**
  - เพิ่ม deferred action queue สำหรับคำสั่ง config และ calibration ที่เข้ามาจาก Pi แล้วให้ `TaskControl` รับไป apply ทีละรายการ แทนการเรียก NVS/calibration path ตรงใน `PubSubClient` callback
  - เพิ่ม network request/status publish coordination แบบ thread-safe เพื่อให้การขอ NETPIE shadow sync และ feedback status ยังทำงานครบ แม้งาน apply จะถูกย้ายไปอีก task
  - ลดโอกาสที่ Networking task จะค้างจาก flash/NVS writes หรือ calibration logic โดยยังคง contract ของ MQTT topics และพฤติกรรมของ dashboard เหมือนเดิม

## [2026-04-28] - MQTT Config NVS Batch Save

### Changed

- **fix: batch-save config changes that arrive through Local MQTT to reduce blocking in TaskNetworking (`src/localMqtt.cpp`, `src/lightController.cpp`, `src/fishFeeder.cpp`):**
  - เพิ่ม `lightCtrlSetConfig()` และ `fishFeederSetConfig()` เพื่อรวมการอัปเดตค่าหลาย field จาก dashboard แล้ว save ลง NVS เพียงครั้งเดียว
  - เปลี่ยน Local MQTT callback ให้เลิกเรียก setter ย่อยหลายตัวติดกันสำหรับ light และ fish feeder ซึ่งเคยทำให้ Networking task ไปเขียน flash ซ้ำหลายรอบต่อ MQTT message เดียว
  - คงผลลัพธ์ของ config ที่ถูก apply และ feedback ที่ส่งกลับ dashboard เหมือนเดิม แต่ลด latency ในเส้นทางที่เสี่ยงชน Task WDT

## [2026-04-28] - Task WDT Networking Burst Fix

### Changed

- **fix: reduce TaskNetworking watchdog risk without changing user-facing behavior (`src/localMqtt.cpp`, `src/netpie.cpp`):**
  - เปลี่ยน Local MQTT status feedback หลัง reconnect และหลังรับ config ให้ถูก queue และทยอย publish ทีละรายการ แทนการยิงหลาย topic ติดกันในรอบเดียว
  - เพิ่ม heartbeat/WDT checkpoint ภายในจุดที่อาจ block ของ Local MQTT และ NETPIE เช่น mDNS query, MQTT connect, keepalive loop, และ publish เพื่อไม่ให้ stage `local_mqtt_loop` หรือ `netpie_loop` ดูค้างยาวทั้งก้อน
  - คง flow การทำงานเดิมของ MQTT, dashboard feedback, และ reconnect logic เอาไว้ แต่ลดโอกาสสะสมเวลา block จนชน Task WDT

## [2026-04-26] - Login Password Visibility Toggle

### Changed

- **ui: add a show/hide password control to the Pi login screen (`pi_server/login.html`):**
  - เพิ่มปุ่มไอคอนตาในช่อง password เพื่อสลับแสดงหรือซ่อนรหัสผ่านได้จากหน้า login โดยไม่เปลี่ยน flow การ submit เดิม
  - ปรับ spacing/focus state ของ input ให้ยังใช้งานง่ายทั้งเมาส์และคีย์บอร์ด

## [2026-04-26] - Pi Auth Session Revocation And Rate Limits

### Changed

- **security: revoke stale signed cookies after deploy and throttle auth-sensitive APIs (`pi_server/app.py`):**
  - เพิ่ม `session_epoch` ที่เก็บฝั่ง server และ bind ลง session cookie ตอน login เพื่อให้ cookie เก่าที่ไม่มี epoch หรือมีค่าไม่ตรงถูกบังคับ logout ทันทีหลัง deploy code ใหม่
  - ตรวจ session ทุกครั้งกับข้อมูล user/role ปัจจุบันใน `auth_config.json` ทำให้ session เดิมใช้ต่อไม่ได้เมื่อ account ถูกลบหรือ role ถูกเปลี่ยน
  - เพิ่ม in-memory rate limit สำหรับ `/api/login` และ endpoint แก้ไขผู้ใช้ (`/api/admin/users*`) เพื่อลดการ brute force และการยิง API จัดการสิทธิ์ซ้ำ

## [2026-04-26] - Restore Pi AP Credentials

### Changed

- **fix: restore the Pi hotspot credentials to the long-used deployment values (`pi_server/hostapd.conf`, `pi_server/setup_ap.sh`):**
  - เปลี่ยน `wpa_passphrase` กลับเป็น `aqua1234` ให้ตรงกับค่า `wifi_ap_pass` ที่ firmware ใช้อยู่จาก `secrets.ini`
  - เปลี่ยนข้อความสรุปท้าย `setup_ap.sh` ให้แสดง SSID/password เดิมอีกครั้ง เพื่อให้ operator เห็นค่าที่ใช้งานจริงเหมือนก่อนหน้า

## [2026-04-26] - Pi User Role Management

### Changed

- **feat: allow admins to choose and update each dashboard user's role (`pi_server/admin_users.html`, `pi_server/app.py`):**
  - เพิ่มตัวเลือก role ตอนสร้าง user ใหม่ในหน้า `/admin/users` แทนการบังคับสร้างเป็น `user` เสมอ
  - เพิ่ม role selector และปุ่มบันทึกในรายการผู้ใช้ เพื่อเปลี่ยนสิทธิ์ `admin` / `user` ได้จากหน้าเดียว
  - เพิ่ม backend endpoint สำหรับอัปเดต role พร้อม validation กันการลดสิทธิ์จนไม่เหลือ admin และกันการปลดสิทธิ์ admin ของตัวเองขณะยังล็อกอินอยู่

## [2026-04-26] - Secret Leak Hardening

### Changed

- **security: remove public bootstrap credentials and fail closed when secrets are missing (`pi_server/app.py`, `include/config.h`, `src/ota.cpp`, `src/telnetServer.cpp`, `pi_server/settings.json`, `PRODUCTION.md`):**
  - ตัด hardcoded admin bootstrap password ใน Pi server ออก แล้วเปลี่ยนให้สร้างบัญชี admin ครั้งแรกจาก env `AQUAPONICS_BOOTSTRAP_ADMIN_PASSWORD` เท่านั้น
  - เปลี่ยน default `ota_password` ใน Pi settings และ tracked `settings.json` เป็นค่าว่าง พร้อมบังคับให้ endpoint OTA ปฏิเสธคำขอเมื่อยังไม่ได้ตั้งรหัสจริง
  - เปลี่ยน fallback `SECRET_OTA_PASSWORD` และ `SECRET_TELNET_PASSWORD` ใน firmware เป็น sentinel ที่ใช้งานจริงไม่ได้ และ disable OTA/Telnet อัตโนมัติเมื่อยังไม่ได้ configure
  - ล้าง LINE token และ OTA password ที่ถูก track ใน `pi_server/settings.json` และอัปเดตเอกสาร production ให้ย้ายไปใช้ secrets/env สำหรับ deployment จริง
  - ผูกไฟล์ `auth_config.json`, `aquaponics.db`, `system.log`, และ static/page assets กับ directory ของ `app.py` โดยตรง พร้อมเปลี่ยน `pwa`/`static` routes ไปใช้ safe directory serving แทนการประกอบ path จาก URL ตรง ๆ
  - ตัด debug log ที่พิมพ์ NETPIE token ตรง ๆ และเลิกพิมพ์รหัสผ่าน Hotspot ออกทาง `setup_ap.sh`

## [2026-04-26] - Water System Native Regression Tests

### Added

- **test: add focused native coverage for new water refill route behavior (`test/test_native/test_water_system_native.cpp`, `test/test_native/mock/config.h`):**
  - เพิ่ม unit tests สำหรับ `AUTO` route ที่ต้องเลือก `FISH_TANK` ก่อน `SUMP_DIRECT` เมื่อทั้งสองเส้นทางพร้อมใช้งาน
  - เพิ่ม regression test สำหรับ fallback ไป `SUMP_DIRECT` หลังครบ `fish_refill_max_runtime_ms` เพื่อกัน logic ย้อนกลับไประบบเดิมโดยไม่ตั้งใจ
  - เพิ่ม test สำหรับ fish refill cooldown เพื่อยืนยันว่า status รายงาน `fishRefillReady=false` และมีเวลารอคงเหลือหลังหยุดเติมผ่านตู้ปลา

- **test: split native sensor suites into one PlatformIO test program per folder (`platformio.ini`, `test/test_dht_native/test_main.cpp`, `test/test_light_native/test_main.cpp`, `test/test_tds_native/test_main.cpp`, `test/test_temp_native/test_main.cpp`, `test/test_water_system_native/test_main.cpp`):**
  - ปิดการเก็บ `test/test_native` เป็น suite เดียว เพราะในโฟลเดอร์นั้นมีหลายไฟล์ที่ต่างก็มี `main()` และ mock globals ของตัวเอง ทำให้ native linker ชนกันทั้งก้อน
  - เพิ่ม wrapper suite แยกตามโมดูล พร้อม stub `setUp/tearDown`, `telnetPrintfNonBlocking()`, และ `localMqttPublishLog()` เพื่อให้ Unity และ production logger link ผ่านใน native environment
  - ทำให้ `pio test -e native` สามารถ compile/run ราย suite ตามรูปแบบของ PlatformIO แทนการพยายามรวมทุก sensor test เข้าด้วยกันใน executable เดียว

## [2026-04-26] - Water And Pi Decision Skill

### Added

- **docs: add reusable repo skill for recent water-system and Pi-settings decisions (`.agent/skills/water-system-and-pi-settings-decisions/SKILL.md`):**
  - สรุปกติกา `AUTO = fish-first fallback`, การจำกัด fish refill ด้วย timeout/overflow, และการ re-check mix tank ก่อน fallback ไว้เป็น skill ใช้อ้างอิงซ้ำได้
  - เก็บข้อตกลงเรื่อง default 10 วินาที, การจัดวาง water settings ใน Pi, และ contract ของ `Feed Duration (ms)` ระหว่าง UI, API, และ MQTT
  - เพิ่ม checklist สำหรับงานที่แตะ `src/waterSystem.cpp`, `pi_server/app.py`, และ `pi_server/settings.html` เพื่อให้ agent รอบถัดไปหยิบบริบทเดียวกันได้เร็วขึ้น

- **docs: add focused repo skill for fish feeder Pi persistence (`.agent/skills/fish-feeder-pi-settings-persistence/SKILL.md`):**
  - แยกสรุปเฉพาะ contract ของ `Fish Feeder` ระหว่างหน้า Pi, `app.py`, `settings.json`, และ MQTT โดยไม่ปน logic ของ water system
  - เก็บ checklist สำหรับ debug อาการ `Feed Duration (ms)` หรือ field feeder อื่น save แล้วเด้งกลับค่าเดิม เช่น path ของ `settings.json`, schema merge, key mapping, และการ reload หลัง save
  - ทำให้ agent รอบถัดไปเรียกใช้ skill แคบเฉพาะงาน persistence/debug ของ feeder ได้ตรงกว่าเดิม

### Changed

- **fix: align Pi OTA/terminal ESP target config with saved settings and DHCP reservation (`pi_server/app.py`, `pi_server/settings.json`, `pi_server/ota.html`, `pi_server/terminal.html`):**
  - ตัด hardcode `esp_ip` และ `ota_password` ใน OTA backend ให้ไปอ่านจาก `secure` settings จริงแทน เพื่อไม่ให้หน้า OTA flash ไปผิด IP หรือใช้รหัสเก่าเมื่อ config เปลี่ยน
  - ปรับ default `secure.esp_ip` ให้ตรงกับ DHCP reservation ใน `dnsmasq_ap.conf` คือ `192.168.10.10` แทนค่าที่ drift ไปเป็น `192.168.10.100`
  - ทำให้หน้า `ota.html` และ `terminal.html` โหลด `ESP IP` จาก `/api/settings` แทนการฝังค่าไว้ใน HTML เพื่อลด config drift แบบเดียวกับเคส WiFi

- **chore: align firmware fallback secrets with the active AP/telnet defaults (`include/config.h`, `include/secrets.h`):**
  - เปลี่ยน fallback SSID/password ใน firmware headers ให้ตรงกับค่า deployment ที่ใช้งานอยู่ในช่วงนั้น แทนค่าที่ drift ไปก่อนหน้า
  - ปรับ fallback `TELNET_PASSWORD` ให้ตรงกับ secret ที่ใช้งานจริงในเวลานั้น เพื่อลดความสับสนเวลา build โดยไม่มี secret overrides

- **fix: persist full Pi settings schema and sync water defaults to the current Pi baseline (`pi_server/app.py`, `pi_server/settings.html`, `pi_server/settings.json`, `include/config.h`):**
  - เปลี่ยน `POST /api/settings` และ `save_settings()` ให้ deep-merge กับ schema ล่าสุดก่อน save เพื่อไม่ให้ nested settings หรือ block อย่าง `secure` หลุดหายจาก `settings.json`
  - เติม `secure` block กลับเข้าไฟล์ `settings.json` ปัจจุบัน และทำให้ save รอบถัดไปยังคงเก็บ `ota_password` / `esp_ip` / `terminal_ip` ไว้ต่อเนื่อง
  - sync default ของ water system ให้ตรงกับค่าจริงจาก Pi ตอนนี้ คือ `allow_direct_sump_refill = false` และ `fish_refill_max_runtime_ms = 30000` ทั้งฝั่ง Pi UI/backend และ firmware default

- **fix: make TaskSensors the single owner of TDS ADC sampling (`src/TdsSensor.cpp`, `src/automator.cpp`, `src/localMqtt.cpp`, `src/commandHandler.cpp`):**
  - ตัดการเรียก `tdsRead()` จาก task อื่นอย่าง automator, local MQTT HW test, และ CLI status/test แล้วให้ทุกจุดอ่านค่า cache จาก `tdsGetLastValue()` แทน
  - เพิ่มการป้องกัน state กลางของ TDS sensor ระหว่างอ่าน/เขียน และ cache ค่า voltage/TDS ล่าสุดไว้ เพื่อลด race บน buffer/index/result ระหว่างข้าม core
  - ทำให้เส้นทาง `analogRead(TDS_PIN)` เหลืออยู่ใน `TaskSensors` ผ่าน `tdsLoop()` เป็นหลัก เพื่อลดโอกาสค่า ADC เพี้ยนจากหลาย task แย่งอ่านพร้อมกัน

- **fix: move large local MQTT sensor publish buffers off TaskNetworking stack (`src/localMqtt.cpp`):**
  - ย้าย `StaticJsonDocument<1792>` และ payload buffer 2048 bytes ของ `localMqttPublishData()` ออกจาก local stack frame ไปเป็น static file-scope storage
  - ลด stack pressure ใน `TaskNetworking` ที่มี stack 8192 bytes อยู่แล้วและต้องผ่าน WiFi/MQTT/OTA/Telnet path หลายชั้นใน loop เดียว
  - คง payload format เดิมไว้ทั้งหมด เพื่อให้หน้า Pi และ topic `aquaponics/sensors` ไม่ต้องเปลี่ยน contract

- **fix: harden OTA progress handling and serialize shared system NVS access (`src/ota.cpp`, `src/system.cpp`):**
  - แก้สูตร progress ของ OTA ให้ไม่หารด้วยศูนย์เมื่อ `total` มีค่าน้อยหรือผิดปกติ และ feed WDT ระหว่าง OTA transfer/handle เพื่อกัน reset ระหว่าง flash
  - เพิ่ม mutex รอบ `Preferences` ตัวกลางของ `system.cpp` เพื่อไม่ให้ path จากหลาย task เขียน namespace `system` พร้อมกัน
  - ครอบการ load/save stats, sensor enable state, และ factory reset path ให้ใช้ lock เดียวกันก่อนแตะ NVS ในโมดูล system

- **refactor: remove stale declarations and align fixed WiFi/Telnet module config with shared constants (`src/wifiConn.cpp`, `include/commandHandler.h`, `src/telnetServer.cpp`, `src/waterSystem.cpp`, `src/system.cpp`):**
  - เปลี่ยน `wifiConn.cpp` ให้ใช้ `WIFI_AP_NAME` / `WIFI_AP_PASS` จาก config แทน hardcode SSID/password ภายในไฟล์
  - ลบ declaration ของ `commandCheckTelnet()` และลบฟังก์ชัน `_resolveRefillRoute()` ที่ไม่ได้ถูกเรียกใช้งานแล้ว เพื่อลด dead surface ใน codebase
  - ทำให้ global object ใน `telnetServer.cpp` เป็น `static` ตามขอบเขตของโมดูล และลบ log ซ้ำใน factory reset path ของ `system.cpp`

## [2026-04-25] - Pi Fish Feeder Duration Save Fix

### Changed

- **fix: make Fish Feeder duration persist reliably from the Pi settings page (`pi_server/app.py`, `pi_server/settings.html`):**
  - เปลี่ยน `settings.json` ของ Pi ให้ใช้ path อิงตำแหน่งไฟล์ `app.py` โดยตรง แทน relative path ล้วน เพื่อลดปัญหา save ไปคนละ working directory แล้วหน้า Settings โหลดกลับมาเหมือนค่าไม่ติด
  - ให้ `load_settings()` merge ไฟล์เก่ากับ default schema และ sanitize `fish_feeder.duration_ms` ทุกครั้งที่โหลด เพื่อให้ config รุ่นเก่าหรือค่าที่เป็น `null` ไม่ทำให้ช่อง `Feed Duration (ms)` เด้งกลับเป็นค่าเดิม
  - เพิ่มการ normalize/clamp ค่า `Feed Duration (ms)` ทั้งตอนกด `Save General Settings` และ `Apply Feeder Config` พร้อม reload ค่าจริงจาก server หลัง save เพื่อให้หน้าเว็บสะท้อนค่าที่ persisted แล้วจริง

## [2026-04-25] - Water Auto Route Fish-First Fallback

### Changed

- **feat: consolidate Pi water-system controls into one settings card (`pi_server/settings.html`):**
  - รวม field ของ water system ที่ใช้งานจริงไว้ใน card เดียวของหน้า Settings ทั้ง circulation, refill policy, route strategy, fish refill max time, fish refill cooldown, live status และ manual actions
  - เปิดให้ตั้ง `fish_refill_max_runtime_ms` และ `fish_refill_interval_ms` จากหน้า Pi ได้ตรง ๆ เพื่อจูนการเติมผ่านตู้ปลาให้ปลอดภัยกับปลาโดยไม่ต้องพึ่งค่าเดิมที่ซ่อนอยู่
  - จัด card ใหม่ให้เห็นสถานะสำคัญจาก ESP32 เช่น route ที่ใช้จริง, fish refill ready/wait, overflow, level low/high, alarm, และเหตุผลล่าสุด ในหน้าเดียวกัน

- **tune: align fish refill default limit to 10 seconds across firmware and Pi (`include/config.h`, `pi_server/app.py`, `pi_server/settings.html`, `pi_server/pwa/sw.js`):**
  - เปลี่ยนค่า default ของ `fish_refill_max_runtime_ms` จาก 30 วินาทีเป็น 10 วินาทีใน firmware และ Pi เพื่อให้ค่าตั้งต้นตรงกับแนวคิดจำกัดการเปลี่ยนน้ำของตู้ปลา
  - ปรับ fallback ค่าในหน้า Settings ให้เปิดมาที่ 10 วินาทีเหมือนกันทั้งตอนโหลดค่าและตอนกด Apply โดยที่ยังสามารถแก้เพิ่ม/ลดได้จากหน้าเว็บ
  - bump PWA cache เป็น `aquaponics-v10` เพื่อให้ browser ดึงหน้า Settings และ assets เวอร์ชันใหม่หลัง deploy

- **fix: make AUTO refill route prefer fish tank before direct sump fallback (`src/waterSystem.cpp`, `forTestFlow/water-system-auto-flow.json`):**
  - ปรับ `_resolveRefillRoute()` ให้ `AUTO` เลือก `FISH_TANK` ก่อนเมื่อปั๊มเติมตู้ปลายังใช้งานได้ แทนการเลือก `SUMP_DIRECT` ก่อนแบบเดิม
  - จำกัดการ fallback ไป `SUMP_DIRECT` ให้เกิดเฉพาะตอนที่เปิด `allow_direct_sump_refill` และ fish route ใช้ไม่ได้จริง เพื่อให้ semantic ของ `AUTO` ตรงกับ fish-first fallback
  - อัปเดต flow แยกของ `Water System AUTO` ให้คำอธิบาย route selection ตรงกับ behavior ใหม่ใน firmware

- **feat: cap fish-tank refill by timer or overflow before continuing mix refill (`src/waterSystem.cpp`, `forTestFlow/water-system-auto-flow.json`):**
  - ทำให้รอบ `FISH_TANK_REFILL` หยุดได้เองเมื่อครบ `fish_refill_max_runtime_ms` หรือเมื่อ `overflow sensor` ของตู้ปลาถูก trigger แทนการปล่อยให้ปั๊มเติมผ่านตู้ปลาวิ่งต่อจนถังผสมเต็ม
  - ให้ `AUTO` fallback ไป `SUMP_DIRECT` ได้หลังจากรอบเติมตู้ปลาถูกหยุดด้วยข้อจำกัดความปลอดภัยแล้วเท่านั้น เพื่อลดการเปลี่ยนน้ำปริมาณมากในตู้ปลา
  - เปลี่ยน `fish_refill_ready` และ `fish_refill_wait_remaining_ms` ให้สะท้อนสถานะจริงของรอบเติมตู้ปลา แทนค่า placeholder เดิม

## [2026-04-25] - Flow Planner Sync With Latest Water And Dosing Logic

### Changed

- **refactor: align planner layout with current fish-route refill and direct A/B dosing flow (`forTestFlow/flow-layout.json`, `forTestFlow/index.html`):**
  - อัปเดต flow layout หลักให้สะท้อน route เติมน้ำล่าสุดทั้ง `FISH_TANK_REFILL` และ `MIX_TANK_REFILL` พร้อมเส้นควบคุมจาก `Water System` ไปยัง `Pump เติมตู้ปลา`, `โซลินอยด์น้ำเข้าถังผสม`, และ `Pump หมุนเวียน`
  - เพิ่มอุปกรณ์ `Pump ปุ๋ย A` และ `Pump ปุ๋ย B` ลงใน planner และเชื่อม `Automator` ให้สื่อชัดว่า firmware จ่ายสารลงถังผสมโดยตรงผ่านปั๊ม A/B โดยไม่มีปั๊มผสมแยก
  - ปรับ `states` ของ planner ให้ตรงกับ state machine ปัจจุบันของ firmware ทั้งฝั่ง water system และ automator
  - เปลี่ยน `seedDemo()` ในหน้า planner ให้เปิดมาด้วย flow ล่าสุดทันทีเมื่อยังไม่มีข้อมูลใน `localStorage` แทน demo schematic แบบ generic เดิม และ bump storage key เป็น `flow-planner-local-v2` เพื่อไม่ให้ browser โหลด schematic เก่าค้าง

- **feat: add dedicated water-system auto flow diagram (`forTestFlow/water-system-auto-flow.json`):**
  - เพิ่มไฟล์ flow แยกสำหรับอธิบาย state machine ของ `Water System AUTO` โดยเฉพาะ ตั้งแต่การอ่าน level sensors, การเช็ก blocked/alarm, การเลือก route, การ refill, การ settling, และผลต่อ automator
  - ทำให้สามารถเปิด diagram แยกใน planner ได้โดยไม่ต้องปนกับภาพรวมอุปกรณ์ทั้งระบบ เหมาะกับการใช้คุย logic และ debug ลำดับ state

- **feat: add dedicated full-system overview flow diagram (`forTestFlow/full-system-overview-flow.json`):**
  - เพิ่ม flow แยกสำหรับอธิบายภาพรวมทั้งระบบตั้งแต่ boot/setup, TaskSensors, sensor cache, TaskControl, water system, automator, controller อื่น, จนถึง TaskNetworking และการสื่อสารกับ Pi/NETPIE
  - ทำให้มี diagram ที่ใช้คุย architecture ทั้งระบบได้ตรง ๆ โดยไม่ต้องใช้ layout อุปกรณ์จริงอย่างเดียว และไม่ต้องปนกับ state-machine flow ของระบบน้ำ

## [2026-04-23] - Flow Planner Arrow Direction Fix

### Changed

- **refactor: lock water-system refill to the mix-tank inlet actuator and retire route logic in practice (`include/config.h`, `src/waterSystem.cpp`, `WATER_SYSTEM_FLOW_TH.md`):**
  - ปรับ assumption ของระบบน้ำตาม plumbing ใหม่: ถังผสมและถังน้ำสะอาดมีน้ำเข้าแยกกัน, ถังผสมใช้โซลินอยด์ที่ ESP32 ควบคุม, และถังน้ำสะอาดใช้กลไกเชิงกลของตัวถังเอง
  - ทำให้ firmware ใช้เส้นทางเติมจริงเพียงแบบเดียวคือ `MIX_TANK_REFILL` ผ่าน actuator ของถังผสม โดยไม่ใช้ route valve หรือ special refill ผ่านตู้ปลาใน logic runtime อีกต่อไป
  - คง field legacy อย่าง `preferred_route`, `allow_direct_sump_refill`, และ `fish refill` ไว้เพื่อ compatibility กับ surface เดิม แต่ไม่ใช้ตัดสินใจ flow หลักแล้ว

- **refactor: remove legacy route/fish-refill controls from Pi water pages (`pi_server/settings.html`, `pi_server/index.html`, `pi_server/hardware_test.html`, `pi_server/app.py`):**
  - เอา field และปุ่มที่ผู้ใช้เห็นสำหรับ `preferred_route`, `allow_direct_sump_refill`, และ fish special refill ออกจากหน้า Settings และ Hardware Test เพื่อให้ UI ตรงกับ plumbing ใหม่ที่เติมเข้าถังผสมอย่างเดียว
  - เปลี่ยน card บน Dashboard หลักจาก `Route` / `Fish Tank Refill` เป็นสถานะ `Mix Inlet` และ `Alarm` แทน เพื่อให้หน้าหลักสะท้อน actuator และสถานะที่ยังมีความหมายจริง
  - ปรับ default/publish defaults ฝั่ง Pi ให้ legacy fields เริ่มจาก `SUMP_DIRECT` และ `false` แทน `AUTO` / `true` เพื่อไม่ให้ cache หรือ payload ที่ซ่อนอยู่ย้อนกลับไปใช้ assumption เก่า

- **feat: add non-blocking 500 ms start delay for active-low fish feeder (`include/config.h`, `src/fishFeeder.cpp`):**
  - เพิ่ม `FEEDER_ACTIVE_LOW_DELAY_MS = 500` เพื่อหน่วงก่อนสั่งขาให้อาหารปลาแบบ active low โดยไม่ hardcode ใน logic
  - เปลี่ยน `fishFeederStartManualFeed()` ให้ arm คำสั่งไว้ก่อน แล้วค่อย active output หลังครบ 0.5 วินาทีใน `fishFeederLoop()` แทนการสั่งรีเลย์ทันที
  - คงนโยบาย no-block ของระบบไว้ โดยไม่ใช้ `delay()` และยังยกเลิกรอบ pending feed ได้เมื่อผู้ใช้ปิด feeder ระหว่างช่วงหน่วง

- **fix: treat all live ESP MQTT topics as heartbeat on Pi server (`pi_server/app.py`):**
  - แก้การตัดสิน `ESP32 STATUS` ของหน้า Pi ที่เดิมต่ออายุ heartbeat เฉพาะ `aquaponics/sensors` ทำให้ขึ้น `OFFLINE` หลอกได้ แม้ status topic อื่นจาก ESP ยังวิ่งอยู่ตามปกติ
  - เพิ่มชุด topic heartbeat ของ ESP และ refresh `last_esp_update` จากทุก packet ที่มาจาก ESP แบบ non-retained เช่น `status/water_system`, `status/fan_control`, `status/light_control`, `status/fish_feeder`, `logs`, และ `test/result`
  - คงพฤติกรรม timeout 15 วินาทีเดิมไว้ แต่ให้สถานะ `ONLINE/OFFLINE` สะท้อนการมีชีวิตของ ESP จริงขึ้น แทนการผูกกับ sensor payload เส้นเดียว

- **fix: move water dashboard fields to dedicated status cache and slim main sensor packet (`src/localMqtt.cpp`, `pi_server/app.py`):**
  - ตัด field ระบบน้ำที่ซ้ำออกจาก payload `aquaponics/sensors` เพื่อให้ packet หลักกลับไปโฟกัสค่าที่ต้องใช้ทุก 2 วินาทีจริง ๆ เช่น sensor values, health stats, และ automation state
  - ให้หน้า dashboard ฝั่ง Pi merge ค่า water-system จาก cache `aquaponics/status/water_system` ตอน build `dashboard_update` แทนการบังคับให้ sensor packet แบกทั้งค่าหลักและ water state ไปพร้อมกัน
  - ลดโอกาสที่ sensor packet จะโตเกินจน publish ไม่ออก หลังเพิ่ม state/config/status ของ water system หลาย field ในรอบก่อนหน้า

- **fix: make flow arrows clearly show source and destination in local planner (`forTestFlow/index.html`):**
  - เปลี่ยนการคำนวณเส้น flow ให้เริ่มและจบที่ขอบของอุปกรณ์แต่ละกล่องแทนการลากจากจุดกึ่งกลางถึงจุดกึ่งกลาง ทำให้ลูกศรไม่จมเข้าไปใน node ปลายทางและมองทิศทางได้ชัดขึ้น
  - ให้ตำแหน่ง label ของเส้นอิงจาก segment ที่ถูกตัดกับขอบ node จริง เพื่อให้ข้อความอยู่กึ่งกลางเส้นที่เห็นบนจอ
  - ปรับ export PNG ให้ใช้ geometry เดียวกับ SVG บนหน้า planner เพื่อให้ภาพที่ export ออกมาแสดงทิศการไหลตรงกับหน้าใช้งานจริง
  - ลดขนาดหัวลูกศรและน้ำหนักเส้นให้ดู minimal ขึ้น โดยยังคง highlight เส้นที่เลือกอยู่เพื่อไม่ให้ใช้งานยากเวลามีหลาย flow บน canvas
  - เพิ่มระยะเผื่อปลายทางของ flow สำหรับหัวลูกศร เพื่อแก้กรณี marker ถูก card ปลายทางบังในมุมเฉียงบางแบบ โดยยังคงทิศทางและสัดส่วนเดิมของเส้น

## [2026-04-22] - Pi Server Auth And UI Hardening

### Changed

- **fix: remove HW test pump auto-off leak window and harden firmware config setters (`src/localMqtt.cpp`, `src/lightController.cpp`, `src/waterSystem.cpp`):**
  - ตัดการใช้ `new HwTestParams` + one-shot FreeRTOS task สำหรับ pump auto-off ออก แล้วเปลี่ยนเป็น deadline-based auto-off ที่รันใน `localMqttHwTestTick()` แทน เพื่อปิดช่อง race ที่ task ถูก `vTaskDelete()` จากภายนอกก่อนจะได้ `delete` พารามิเตอร์
  - เมื่อหน้า Hardware Test สั่ง pump A/B ระบบจะ arm deadline ใน task networking เดิม, ปิดปั๊มตามเวลา, resume automator, และ publish สถานะ `completed` โดยไม่ต้องพึ่ง dynamic allocation หรือ task ownership เพิ่มอีกชั้น
  - เพิ่ม schedule sanitization ให้ light controller ทั้งตอนโหลดจาก NVS และตอนรับ `on_time`/`off_time` จาก MQTT โดย reject เวลา format ผิดหรือเกินช่วง `00:00-23:59`
  - เพิ่ม water system config sanitization เพื่อกัน `refillMaxRuntimeMs` ที่เป็นศูนย์/เกินค่าสูงสุด และ route enum ที่หลุดช่วงก่อนบันทึกหรือใช้งานจริง

- **refactor: make water system roles explicit for mix-tank control automation (`include/waterSystem.h`, `src/waterSystem.cpp`, `src/automator.cpp`):**
  - เพิ่ม status roles แยกให้ชัดว่า output ปัจจุบันเป็น `circulation`, `fish tank refill`, หรือ `mix tank refill` โดยยังคง field เดิมไว้เพื่อไม่ให้ surface ที่ publish status ไป Pi/CLI พัง
  - เพิ่ม flag กลาง `waterDilutionActive` และ `mixTankSettlingActive` ใน water system เพื่อบอกว่าถังน้ำผสมยังอยู่ในช่วงเจือจางหรือช่วงรอให้น้ำเข้ากัน แม้การเติมจะเข้าผ่านตู้ปลาก็ตาม
  - ปรับ automator ให้ยึด `mix tank control zone` เป็นจุดตัดสินใจเดียว และหยุด evaluate/dose ทุกครั้งที่ circulation ไม่เดิน, มี refill เข้าระบบ, หรือ mix tank ยัง settling หลัง refill แทนการเช็กเฉพาะ direct sump refill แบบเดิม

- **feat: expose mix-tank dilution status to Pi pages and localize HW Test (`src/localMqtt.cpp`, `pi_server/app.py`, `pi_server/settings.html`, `pi_server/hardware_test.html`):**
  - เพิ่ม field `water_dilution_active`, `mix_tank_settling_active`, `fish_tank_refill_output`, `mix_tank_refill_output`, `mix_tank_control_zone`, และ `dilution_hold_remaining_ms` ลงทั้ง sensor payload และ water-system status payload เพื่อให้หน้า Pi เห็นสถานะโซนควบคุมใหม่ตรงกับ firmware
  - อัปเดต default state ฝั่ง Pi และหน้า Settings ให้แสดงสถานะใหม่ของระบบน้ำได้ทันทีโดยไม่ต้องเดาจาก route/output เดิมเพียงอย่างเดียว
  - แปลหน้า `hardware_test.html` เป็นภาษาไทยในส่วนหัวข้อ, ปุ่ม, state cards, และข้อความ runtime/log หลัก พร้อมเพิ่มการ์ดสถานะใหม่สำหรับ fish refill, mix refill, dilution, settling, และเวลารอให้น้ำคงตัว
  - เพิ่ม widget `Mix Tank Control Zone` บนหน้า `index.html` ให้หน้า dashboard หลักเห็น state, route, circulation, fish/mix refill outputs, dilution, settling, control-zone stability, hold time, และ reason ได้จากหน้าแรกทันที พร้อม map workflow wait states ใหม่ของ mix tank ให้อ่านได้ตรงกับ firmware

- **refactor: split water states into mix refill / fish special refill / interval wait (`include/waterSystem.h`, `src/waterSystem.cpp`, `src/localMqtt.cpp`, `src/commandHandler.cpp`, `pi_server/app.py`, `pi_server/settings.html`, `pi_server/hardware_test.html`, `pi_server/index.html`):**
  - เปลี่ยน state machine ของ water system ให้แยก state ชัดขึ้นเป็น `พร้อมทำงาน`, `เติมถังน้ำผสม`, `รอช่วงกันเติมถี่`, `รอให้น้ำในถังผสมนิ่ง`, `เติมผ่านตู้ปลา`, `ถูกบล็อก`, และ `แจ้งเตือน` เพื่อให้ตรงกับ flow ล่าสุดที่ใช้ถังน้ำผสมเป็น control zone หลัก
  - เพิ่ม config ใหม่ `refill_min_interval_ms`, `fish_refill_interval_ms`, และ `fish_refill_max_runtime_ms` เพื่อคุมรอบเติมถังผสมไม่ให้ถี่เกิน และจำกัดรอบพิเศษผ่านตู้ปลาให้ห่าง/สั้นตามที่คุยกัน
  - เพิ่ม status ใหม่ `state_label_th`, `fish_refill_ready`, และ `fish_refill_wait_remaining_ms` แล้วต่อขึ้นหน้า Settings / HW Test / Dashboard เพื่อให้คนดูหน้า Pi อ่าน state machine ภาษาไทยได้ทันที

- **fix: make WDT fallback checkpoints less misleading (`src/main.cpp`):**
  - เปลี่ยน `TaskSensors` และ `TaskControl` ให้ใช้ `taskCheckpoint()` ทุก stage แทนการอัปเดตแค่ `stage` โดยไม่ feed heartbeat ระหว่างทาง เพื่อให้ crash fallback/report สะท้อน checkpoint ล่าสุดจริงมากขึ้น
  - เติม checkpoint ก่อน `idle_delay` ของทั้งสอง task ด้วย เพื่อลดกรณีที่ boot report ชี้ว่า task เดิมเป็น `oldest` ทั้งที่เพิ่งวิ่งมาถึงช่วงท้ายของ loop แล้ว

- **fix: reduce pH drift when TDS shares the same tank (`src/main.cpp`, `include/phSensor.h`, `src/phSensor.cpp`):**
  - ปรับ `TaskSensors` ให้สลับ pass ระหว่างการอ่าน TDS กับ pH เมื่อสอง sensor เปิดพร้อมกัน เพื่อลดการอ่าน analog สองช่องติดกันเกินไปในรอบเดียว ซึ่งมักทำให้ pH เพี้ยนเมื่อ probe ทั้งสองอยู่ในถังเดียวกัน
  - เพิ่ม ADC settle delay และ dummy discard reads ก่อนเก็บ sample pH จริง เพื่อให้ input ของ pH sensor มีเวลาคลายจาก channel activity ก่อนหน้าและลดอาการ ADC crosstalk/noise
  - ปรับรอบถัดมาให้ pH กลับมาวิ่งทุก pass เหมือนเดิม และลดความถี่เฉพาะฝั่ง TDS แทน หลังพบว่าการสลับข้ามรอบของ pH เองทำให้ค่าดูค้างเกินไปบนงานจริง
  - แยก `voltage` สำหรับโชว์ออกจาก voltage ที่ใช้คำนวณ pH: ฝั่ง dashboard/calibration ใช้ EMA + deadband ที่หนักกว่าเพื่อให้ mV นิ่งขึ้น ส่วนฝั่ง process ใช้การจูนแบบกลาง ๆ ให้ค่า pH ตอบสนองไวขึ้นแต่ยังไม่กระโดดมาก

- **fix: persist Flask session secret and harden cookie defaults (`pi_server/app.py`):**
  - เปลี่ยนจากการสุ่ม `app.secret_key` ใหม่ทุกครั้งที่ process boot มาเป็นการ reuse ค่า `session_secret` ที่เก็บใน `auth_config.json` หรือรับจาก env `AQUAPONICS_SECRET_KEY` เพื่อไม่ให้ทุก session หลุดทันทีหลัง service restart
  - ตั้งค่า session cookie ให้ explicit มากขึ้นด้วย `HttpOnly`, `SameSite=Lax`, และ `PERMANENT_SESSION_LIFETIME=7 days` พร้อมรองรับ `SESSION_COOKIE_SECURE` ผ่าน env `AQUAPONICS_SESSION_SECURE` สำหรับ deployment ที่อยู่หลัง HTTPS/reverse proxy
  - เพิ่ม `ProxyFix` และ `PREFERRED_URL_SCHEME='https'` เพื่อให้ Flask ใช้ `X-Forwarded-Proto` จาก Cloudflare Tunnel / reverse proxy ได้ถูกต้อง และตั้งค่า `AQUAPONICS_SESSION_SECURE=1` ใน `pi_server/aquaponics.service` สำหรับ deployment นี้โดยตรง

- **fix: gate destructive log actions by frontend role and reduce render churn (`pi_server/full_logs.html`):**
  - ซ่อนปุ่ม `Delete All` สำหรับ non-admin ตั้งแต่หน้า UI โดยใช้ `/api/me` เป็น source of truth เดียวกับ shared header และเพิ่มข้อความชัดเจนเมื่อผู้ใช้ที่ไม่ใช่ admin พยายามเรียก action นี้
  - ปรับหน้า logs ให้ handle `403` จาก backend แบบเจาะจงแทน error รวม และเปลี่ยนการประกอบรายการ log จาก string concat ต่อเนื่องเป็น array + `join()` เพื่อลดงานฝั่ง browser เวลาแสดงหลายร้อยบรรทัด

- **fix: update charts in place and bridge missing samples (`pi_server/graphs.html`, `pi_server/pwa/sw.js`):**
  - เปลี่ยน refresh ของกราฟจาก destroy/recreate ทุก 60 วินาทีมาเป็น update instance เดิมเพื่อลด redraw churn และคง interaction state ได้ดีขึ้นระหว่าง auto-refresh
  - ปรับ dataset ทุกชุดให้ `spanGaps: true` เพื่อไม่ให้กราฟขาดช่วงง่ายเกินไปเมื่อฐานข้อมูลมี sample หายเป็นบางจุด
  - bump `pi_server/pwa/sw.js` cache version เป็น `aquaponics-v9` เพื่อให้ browser/PWA ดึงหน้า Graphs และ Logs เวอร์ชันล่าสุดหลัง deploy

## [2026-04-20] - pH Calibration Buffer Update

### Changed

- **feat: upgrade pH calibration to 3-point buffer set (`include/config.h`, `include/phSensor.h`, `src/phSensor.cpp`, `src/localMqtt.cpp`, `src/netpie.cpp`, `src/commandHandler.cpp`):**
  - เปลี่ยน firmware จาก calibration 2 จุดแบบ pH 4.0/7.0 ไปเป็น 3 จุด pH 4.01, 6.86, 9.18 และคำนวณค่า pH แบบ piecewise interpolation เพื่อให้ช่วงกรด-กลาง-ด่างตรงกับน้ำยาบัฟเฟอร์มาตรฐานที่ใช้งานจริงมากขึ้น
  - เพิ่มการเก็บสถานะ calibration รายจุดใน NVS พร้อม migrate ค่า legacy `pH 4.0/7.0` เดิมเข้าช่องใหม่แบบ best-effort และคง alias คำสั่ง `cal4`/`cal7` กับ shadow keys เดิมไว้เพื่อลด breaking change ตอนอัปเดต
  - ให้ Local MQTT ส่งสถานะ `cal401_done`, `cal686_done`, `cal918_done` กลับ Pi เพื่อให้ dashboard เห็นความครบของแต่ละ buffer point จริงแทนการเดาสถานะจาก action ล่าสุด

- **feat: align Pi pH calibration flow with 4.01/6.86/9.18 (`pi_server/settings.html`, `pi_server/app.py`, `README.md`, `PROJECT_REFERENCE.md`):**
  - ปรับหน้า Settings และ API ให้ใช้ action `cal401`, `cal686`, `cal918` และแสดง 3 ขั้นตอน calibration ตาม buffer ใหม่ พร้อมล้างสถานะครบทั้งสามจุดเมื่อ clear
  - อัปเดต README และ PROJECT_REFERENCE ให้คำอธิบาย pH sensor, คำสั่ง CLI, และ calibration procedure ตรงกับ buffer set ใหม่
  - bump `pi_server/pwa/sw.js` cache version เป็น `aquaponics-v8` เพื่อให้ browser/PWA ดึงหน้า Settings เวอร์ชันใหม่หลัง deploy

- **fix: smooth pH calibration voltage display (`include/phSensor.h`, `src/phSensor.cpp`):**
  - เปลี่ยนค่า `phReadVoltage()` จากการคำนวณ median ดิบทุกครั้งมาเป็น cached EMA-filtered voltage เพื่อให้ตัวเลข mV ในหน้า calibration แกว่งน้อยลงและอ่านจังหวะ stabilize ได้ง่ายขึ้น
  - คง validation เดิมสำหรับกรณี input หลุดช่วง แต่ให้ค่าที่แสดงใน dashboard เป็น filtered reading เดียวกับรอบ sensor loop แทนการ recompute ใหม่ทุกครั้ง
  - refactor รอบอ่าน pH เพิ่ม oversampling ต่อรอบ, invalid-streak guard, voltage deadband, pH deadband, และ step limit เพื่อกด jitter จาก analog front-end และทำให้ค่าหน้า dashboard/calibration นิ่งขึ้นโดยไม่ต้องลดความเร็ว publish ของระบบ

## [2026-04-20] - Task WDT Hardening

### Changed

- **fix: normalize shared header layout across Pi pages (`pi_server/base.css`, `pi_server/index.html`, `pi_server/graphs.html`, `pi_server/full_logs.html`, `pi_server/terminal.html`, `pi_server/settings.html`, `pi_server/admin_logs.html`, `pi_server/admin_users.html`, `pi_server/wifi.html`, `pi_server/live.html`, `pi_server/pwa/sw.js`):**
  - ทำให้ header ของหน้าที่เคยถูกครอบอยู่ใน container แคบสามารถ breakout เป็น full-width shell แบบเดียวกับหน้า Live โดยใช้กติกากลางใน `base.css` แทนการให้แต่ละหน้าได้ layout คนละความกว้าง
  - ย้าย status/actions ที่ไม่ใช่ส่วนของ shared header ออกจาก `header-top` ในหน้า Dashboard, Graphs, Logs และ Terminal เพื่อให้ top row เหลือ brand + shared nav/net stats/logout แบบเดียวกันทุกหน้า
  - แก้ regression ของหน้า Dashboard โดยคืน status pills เข้าไปใน `header-top` และให้ header ของหน้า index breakout ได้เต็มความกว้างจริง เพื่อไม่ให้ nav wrap ผิดชั้นและไม่ให้ status ไปลอยเป็นแถวแยกใต้ header
  - แก้ root cause ของบางหน้าที่ยังเพี้ยน: เดิม breakout ใช้แค่ negative margins เลยเลื่อน header ออกจาก container แต่ไม่ได้ขยายความกว้างจริง ทำให้หน้าที่มี container แคบดูพัง; ตอนนี้เพิ่ม full-width sizing ควบคู่กับ breakout แล้ว
  - ปรับ mobile overrides หลายหน้าให้ไปแก้ `header-top` แทน outer `.header` เพื่อไม่ให้ nav shell และ spacing แตกต่างจากหน้าต้นแบบ
  - bump `pwa/sw.js` cache version เป็น `aquaponics-v7` เพื่อบังคับ browser/PWA ดึง `base.css` และหน้า HTML เวอร์ชันใหม่หลัง deploy

- **fix: harden Task WDT handling around networking hot paths (`src/main.cpp`, `src/localMqtt.cpp`, `src/system.cpp`, `src/wifiConn.cpp`, `include/system.h`):**
  - เพิ่ม task checkpoint สำหรับ `Networking`, `Sensors`, และ `Control` เพื่อบันทึก stage ล่าสุดของแต่ละ task และ feed watchdog/heartbeat ให้ถี่ขึ้นรอบจุดเสี่ยงอย่าง `netpieLoop`, `localMqttLoop`, และช่วง publish ข้อมูล
  - ปรับ `localMqttLoop()` ให้ stagger การ publish status ของ water/light/feeder/fan ทีละหัวข้อแทนยิงหลาย topic ติดกันในรอบเดียว และลดการระบาย log queue ต่อรอบ เพื่อกัน publish burst ยาวจน `TaskNetworking` ไม่ได้ feed WDT ทัน
  - ให้ `systemCheckTaskHealth()` latch การบันทึก stuck-task เพียงครั้งเดียวต่อเหตุการณ์ พร้อมเก็บ `stage` ล่าสุดลง NVS และแสดง stage ใน crash report/stack info เพื่อระบุได้ชัดขึ้นว่าค้างตอนทำอะไร
  - feed watchdog ระหว่าง `wifiSetup()` ที่รอเชื่อมต่อช่วง boot เพื่อลด risk จาก startup wait path
  - เพิ่ม boot report สำหรับ `last crash` และ cache ข้อมูล crash ไว้ใน RAM หลัง report เพื่อให้ยังดูซ้ำได้ในคำสั่ง `crash`, `health`, และ `test` ตลอดบูตรอบเดียวกัน แม้ NVS record จะถูก clear หลังแสดงครั้งแรก
  - เพิ่ม RTC fallback snapshot ของ heartbeat/stage ล่าสุดต่อ task เพื่อให้กรณี WDT รีเซ็ตก่อน `systemCheckTaskHealth()` จะเขียน NVS ยังพอระบุ task/stage สุดท้ายได้จาก boot ถัดไป และตัดการเรียก boot crash report ซ้ำใน `setup()`
  - นับ `watchdog_resets` ตาม reset reason จริงตอนบูต แทนการมี counter ที่ประกาศไว้แต่ไม่ถูกเพิ่มค่าเมื่อเกิด WDT จริง
  - แก้ false positive ใน stuck-task detector: เดิม monitor snapshot `millis()` ครั้งเดียวแล้ว task อื่นอัปเดต heartbeat หลังจากนั้น ทำให้ `now - hb` underflow เป็นค่า ~4294967 วินาที; ตอนนี้กันกรณี `hb > now` และอ่านเวลาใหม่ต่อ task ก่อนคำนวณ age
  - harden `telnetPrintf()` ให้เขียนแบบ non-blocking ตาม `availableForWrite()` และเปิด `setNoDelay(true)` กับ client ใหม่ เพื่อลดความเสี่ยงที่การ log ไป telnet จาก task ใดก็ตามจะ block จนลากไปชน WDT เมื่อ client ช้าหรือเครือข่ายฝั่ง debug ตัน
  - แยก Telnet output เป็น 2 แบบ: `telnetPrintf()` สำหรับ command response แบบ reliable และ `telnetPrintfNonBlocking()` สำหรับ log แบบ best-effort เพื่อแก้อาการคำสั่งเข้าได้แต่ผลลัพธ์หาย ขณะยังคงลดโอกาสที่ log จะ block task จนชน WDT

## [2026-04-18] - Hardware Test Page Alignment

### Changed

- **feat: persistent action feedback for light and feeder settings (`pi_server/settings.html`):**
  - เพิ่มกล่อง feedback ค้างอยู่บนการ์ด Light และ Fish Feeder เพื่อบอกสถานะ `sending`, `applied`, `confirmed`, หรือ `failed` หลังผู้ใช้กด Apply/Manual/Feed Now แทนการพึ่ง toast ชั่วคราวอย่างเดียว

- **refactor: group shared header navigation by task (`pi_server/header.js`):**
  - เปลี่ยน nav จาก flat link list เป็น 3 กลุ่ม `Monitor`, `Operate`, และ `Admin` เพื่อให้ผู้ใช้ใหม่หาเส้นทางการใช้งานได้ง่ายขึ้นทั้งบน desktop และ mobile โดยยังใช้ role filtering เดิม
  - เพิ่ม accent/background แยกตาม section และทำให้ป้ายชื่อกลุ่มเด่นขึ้น เพื่อลดการมองรวมเป็นก้อนเดียวแล้วกดเมนูผิดโดยเฉพาะใน header ที่มีลิงก์หลายตัวติดกัน

- **fix: improve graph and log empty/error states (`pi_server/graphs.html`, `pi_server/full_logs.html`):**
  - หน้า graphs เปลี่ยนจาก `alert()` และ console-only error เป็น inline state banner ที่บอก loading/ready/empty/error พร้อม last-updated status
  - แก้ layout ของหน้า graphs เพิ่มเติมให้ state banner span เต็มแถวของ grid เพื่อไม่ให้ chart cards เลื่อนไปผิดคอลัมน์และดูแปลกหลังเพิ่ม status banner
  - จัดหน้า graphs ใหม่ให้ header และ grid อยู่ใน page shell เดียวกัน เพราะก่อนหน้านี้ header กว้างเต็มจอแต่กราฟถูกจำกัดความกว้างแค่ 1200px เลยทำให้สัดส่วนของหน้าดูไม่สัมพันธ์กัน
  - แก้ root cause ของเส้นกราฟตกดิ่ง: ฝั่ง Pi เคยบันทึกค่า `0` ลงฐานข้อมูลเมื่อ field sensor หายจาก MQTT payload จึงทำให้ Chart.js วาดเส้นลงพื้น; ตอนนี้เปลี่ยนให้เก็บ `NULL` แทนและ normalize ค่า invalid ใน `/api/history` ให้เป็นช่องว่างแทนการตกผิดจริง
  - เพิ่มสคริปต์ `pi_server/cleanup_sensor_history.py` สำหรับรายงานจำนวนค่า invalid/0/null ในตาราง `sensors`, สำรองฐานข้อมูล, และแปลงค่าประวัติที่ผิดจริงให้เป็น `NULL` เพื่อเก็บกราฟย้อนหลังให้สะอาดขึ้นบน DB จริง
  - ปรับ cleanup script ให้ข้าม sensor ที่ตั้งใจไม่ต่อได้ผ่าน `--skip-sensors` และรองรับ auto-skip จาก `settings.json` เมื่อมี `sensor_config` เพื่อไม่ให้กรณีอย่าง pH ที่ไม่ได้ต่อถูกนับเป็นข้อมูลเสียโดยอัตโนมัติ
  - bump `pwa/sw.js` cache version เป็น `aquaponics-v6` เพื่อให้ browser/PWA ดึง `header.js` และหน้า Pi server เวอร์ชันใหม่หลัง deploy ได้ทันที
  - หน้า full logs เพิ่ม notice bar สำหรับผลการ refresh/delete และทำ state empty/no-match/error ให้มีคำอธิบายกับปุ่มลองใหม่โดยไม่ใช้ alert popup

- **refactor: split light and feeder settings into basic/advanced sections (`pi_server/settings.html`):**
  - ย้าย runtime status และ manual actions ของ Light/Fish Feeder ไปไว้ในส่วน `Advanced Controls & Status` แบบพับได้ โดยคง source, schedule, และปุ่ม apply ไว้ในส่วนหลัก เพื่อให้หน้า settings สแกนเร็วขึ้นและลดความแน่นของการ์ดสำหรับผู้ใช้หน้างาน

- **fix: disable unavailable local-web actions in settings (`pi_server/settings.html`):**
  - ปรับหน้า settings ให้ปุ่ม `Manual ON/OFF` ของ Light และ `Feed Now` ของ Fish Feeder ถูก disable ทันทีเมื่อ `command_source` ไม่ใช่ `local_web` พร้อมข้อความอธิบายใต้ action row เพื่อลดอาการกดแล้วค่อยโดน toast ปฏิเสธและทำให้ผู้ใช้เห็นชัดว่าตอนนี้ใครเป็นคนคุมอุปกรณ์

- **fix: request NETPIE shadow on CLI light source switch (`src/commandHandler.cpp`):**
  - ให้คำสั่ง CLI `light netpie` ขอ shadow refresh ทันทีเหมือนหน้า settings เพื่อไม่ให้เปลี่ยน source แล้วไฟยังค้างกับ config เดิมจนกว่าจะมี shadow update รอบใหม่

- **fix: refresh NETPIE shadow on light source switch (`include/netpie.h`, `src/netpie.cpp`, `src/localMqtt.cpp`):**
  - เมื่อหน้า settings เปลี่ยน light `command_source` จาก `local_web` เป็น `netpie` ให้ firmware ขอ `@shadow/data/get` ทันที เพื่อดึงตารางล่าสุดจาก NETPIE กลับมาใช้และทำให้ไฟเข้าสู่สถานะตาม schedule ปัจจุบันได้เร็วขึ้น แทนการค้างอยู่กับ config ฝั่ง local เดิม

- **fix: enlarge NETPIE shadow parse/buffer capacity (`src/netpie.cpp`):**
  - เพิ่มขนาด `StaticJsonDocument` และ MQTT/message buffer ของ NETPIE จาก 1024 เป็น 2048 ไบต์ พร้อม log `payload_len` และเตือนเมื่อ payload ถูกตัด เพื่อแก้ error `JSON parse error: NoMemory` หลัง ESP reboot เมื่อ shadow response ใหญ่ขึ้นจาก field ที่เพิ่มในระบบ

- **feat: fish widget current web summary (`docs/netpie_widgets/fish_feeder_widget.html`):**
  - ปรับกล่อง summary ของ fish feeder ให้แสดง `Mode`, `Feed`, `Duration => State`, `Running`, และ `Source` จาก shadow ในรูปแบบเดียวกับ light widget โดยยังคง flow save/feed-now/pending draft เดิมไว้

- **feat: light widget shows source/running/block state (`docs/netpie_widgets/light_timer_widget.html`):**
  - ขยายกล่อง `Current Web Setting` ให้แสดง `OFF => State`, `Running`, และ `Source` โดยอ่านค่า `light_relay` และ `light_source` จาก shadow เพื่อให้หน้า NETPIE เห็นทันทีว่า widget ถูก block ด้วย `local_web` อยู่หรือไม่

- **fix: revert light widget to direct working baseline (`docs/netpie_widgets/light_timer_widget.html`):**
  - ตัด light widget กลับมาใช้ flow ตรงแบบโค้ดที่ผู้ใช้ยืนยันว่า save เวลาได้จริง แล้วเพิ่มเฉพาะตัวเลือก `Everyday` และกล่อง `Current Web Setting` ที่อ่านค่าจาก shadow ปัจจุบัน โดยไม่ใช้ wrapper/state เพิ่มเติม

- **feat: light widget current web summary (`docs/netpie_widgets/light_timer_widget.html`):**
  - เพิ่มกล่อง `Current Web Setting` ใน light NETPIE widget เพื่อสรุป mode, เวลา ON, และเวลา OFF ที่อ่านมาจาก shadow/web ปัจจุบัน พร้อมรองรับค่า `Everyday` และอัปเดต view นี้ทันทีหลัง toggle หรือ save

- **fix: isolate NETPIE widget scope (`docs/netpie_widgets/light_timer_widget.html`, `docs/netpie_widgets/fish_feeder_widget.html`):**
  - เปลี่ยน DOM id, handler names, และ state variables ของ light/fish widgets ให้แยก namespace กัน พร้อมห่อ logic ไว้ใน closure เพื่อกัน event และตัวแปร global ชนกันเมื่อวางสอง widget บนหน้า NETPIE เดียวกัน
- **fix: normalize NETPIE time input values (`docs/netpie_widgets/light_timer_widget.html`, `docs/netpie_widgets/fish_feeder_widget.html`):**
  - เพิ่มตัว normalize/read helper สำหรับช่องเวลา เพื่อรองรับกรณีที่ environment ของ NETPIE คืนค่า time input ไม่ตรง `HH:MM` เป๊ะ แม้ใน UI จะยังแสดงเวลาอยู่ ทำให้ validation และ save ไม่พลาดง่าย
- **fix: restore original-style light widget flow (`docs/netpie_widgets/light_timer_widget.html`):**
  - ปรับ light widget กลับไปใช้ `input type="time"` และ flow อ่านค่าจาก DOM ตอน save แบบใกล้เคียงโค้ดตั้งต้นของผู้ใช้มากที่สุด โดยยังคง namespace id/handler แยกจาก fish widget เพื่อไม่ให้สองการ์ดชนกันบนหน้า NETPIE เดียวกัน
- **fix: save latest NETPIE edited values from draft state (`docs/netpie_widgets/light_timer_widget.html`, `docs/netpie_widgets/fish_feeder_widget.html`):**
  - เปลี่ยนให้ widget เก็บค่าที่ผู้ใช้แก้ล่าสุดจาก event ลงใน local draft state และใช้ draft นี้ตอน save แทนการอ่าน DOM input ซ้ำ เพื่อลดอาการที่ NETPIE แสดงเวลาที่เปลี่ยนแล้วแต่ตอนบันทึกยังส่งค่าเดิม
- **fix: persist light widget dropdown edits across NETPIE re-render (`docs/netpie_widgets/light_timer_widget.html`):**
  - เพิ่ม pending draft + sessionStorage ให้ฝั่ง light เหมือน fish feeder เพื่อกันกรณี NETPIE re-render หลังผู้ใช้เลือกเวลาใหม่แล้วทำให้ตอนกด Save ยังส่งเวลาเก่าจาก shadow
- **fix: replace NETPIE time inputs with hour/minute dropdowns (`docs/netpie_widgets/light_timer_widget.html`, `docs/netpie_widgets/fish_feeder_widget.html`):**
  - เปลี่ยนตัวเลือกเวลาใน widget จาก `input type="time"` เป็น dropdown ชั่วโมง/นาที แล้ว compose เป็น `HH:MM` ใน script เพื่อหลีกเลี่ยงปัญหา NETPIE runtime ไม่อัปเดตค่าจาก time input ตามที่ผู้ใช้เลือก
- **fix: apply NETPIE time dropdown event value before composing time (`docs/netpie_widgets/light_timer_widget.html`, `docs/netpie_widgets/fish_feeder_widget.html`):**
  - แก้ให้ handler ของ dropdown เวลาเขียนค่าที่เพิ่งเลือกจาก event ลง select ก่อน แล้วค่อย compose `HH:MM` เพื่อกันกรณี runtime ของ NETPIE อัปเดต DOM ช้ากว่า event จนทำให้ save ยังได้เวลาเก่า
- **docs: NETPIE widget templates (`docs/netpie_widgets/light_timer_widget.html`, `docs/netpie_widgets/fish_feeder_widget.html`, `docs/netpie_widgets/README.md`):**
  - เพิ่ม template widget สำหรับหน้า NETPIE ของ light timer และ fish feeder โดยใส่ pending-state persistence, validation, reset-to-shadow, และสถานะ source เพื่อให้ใช้งานบนหน้า NETPIE ได้เสถียรขึ้น
- **fix: feeder `feedNow` shadow edge handling (`src/netpie.cpp`):**
  - ปรับให้ trigger `feedNow` จาก shadow ทำงานแบบ rising-edge และ clear flag กลับเป็น `false` อัตโนมัติ เพื่อลดโอกาสให้อาหารซ้ำเมื่อ shadow ถูก re-render หรือค้างค่าจากรอบก่อน
- **feat: selectable control source for light and fish feeder (`include/controlSource.h`, `include/fishFeeder.h`, `src/fishFeeder.cpp`, `src/lightController.cpp`, `src/netpie.cpp`, `src/localMqtt.cpp`, `src/commandHandler.cpp`, `pi_server/app.py`, `pi_server/settings.html`, `pi_server/hardware_test.html`):**
  - เพิ่มระบบเลือกแหล่งคำสั่งของ light controller และ fish feeder ระหว่าง `NETPIE` กับ `Local Web`
  - เพิ่ม fish feeder controller ใหม่พร้อม schedule, manual trigger, NVS persistence, MQTT config/status, CLI commands, และ tile ทดสอบบนหน้า hardware test
  - ปรับให้ NETPIE และ Local MQTT respect `command_source` ที่เลือกไว้เพื่อกันคำสั่งคนละฝั่งเขียนทับกัน
  - เพิ่มการ์ดตั้งค่า Light Control และ Fish Feeder ในหน้า settings พร้อม apply-now endpoint และ realtime status feedback จาก ESP32
- **refactor: hardware test page aligned with current firmware features (`pi_server/hardware_test.html`):**
  - ตัด water pump test แบบเก่าที่ firmware ไม่รองรับแล้ว
  - เพิ่ม live summary สำหรับ ESP, automation, water state, และ route
  - เพิ่ม water system control/test สำหรับ circulation, auto refill, route, direct fallback, manual refill, และ clear alarm
  - เปลี่ยน light test ให้ตรงกับ light relay และเพิ่ม safe stop สำหรับปั๊มโดส, light off, และ manual refill off
  - แก้ให้ water status ใช้ MQTT realtime fields ที่ตรงกับ firmware (`circ_running`, `water_alarm`, `water_reason`) และให้ทุก action ดึง config ล่าสุดก่อนส่งเพื่อลดการทับค่าเก่า
  - ปรับ safe stop ให้ปิด auto refill ด้วย เพื่อไม่ให้ปั๊มเติมกลับมาทำงานต่อเองเมื่อ low-level condition ยังอยู่
  - เพิ่ม banner เตือนบนหน้า hw test หลัง Safe Stop เพื่อบอกผู้ใช้ว่า auto refill ถูกปิดไว้และต้องเปิดกลับเองเมื่อพร้อม
  - แก้ reliability ของ dosing pump test: firmware จะปิด output test เดิมก่อนเริ่มรอบใหม่ และหน้า hw test บังคับ test ทีละตัวพร้อม fallback stop หาก completion event ไม่กลับมา
  - ปรับ timing ของปั๊มโดสและ pump test ให้คำนวณจากสเปกปั๊มใหม่ 12V, 39 mL/min โดยใช้ปริมาณทดสอบ/โดสประมาณ 2.0 mL ต่อรอบ แทนการ hardcode 3000 ms
  - ปรับ automation dosing ให้ conservative ขึ้นโดยลดปริมาณโดสต่อรอบเหลือ 1.5 mL ต่อปั๊ม ขณะที่ pump test ยังใช้ 2.0 mL สำหรับทดสอบฮาร์ดแวร์
- **feat: exhaust fan controller (`include/fanController.h`, `src/fanController.cpp`, `src/localMqtt.cpp`, `src/commandHandler.cpp`, `pi_server/app.py`, `pi_server/settings.html`, `pi_server/hardware_test.html`):**
  - เพิ่ม controller พัดลมระบายอากาศแบบ auto/manual พร้อม threshold อุณหภูมิและความชื้น, hysteresis, NVS persistence, CLI commands, MQTT config/status, และหน้า settings/hwtest สำหรับคุมและทดสอบพัดลม
- **fix: local MQTT sensor payload headroom (`src/localMqtt.cpp`):**
  - เพิ่ม packet buffer และใส่ length-aware serialization/logging เพื่อกัน `Local MQTT Publish Failed` จาก payload sensor ที่ใหญ่ขึ้นหลังเพิ่ม fan/water/automation fields
- **fix: ESP32-S3 N8 build target config (`platformio.ini`):**
  - ลบ override ของ 16MB flash และ PSRAM ที่ไม่ตรงกับบอร์ด ESP32-S3-DevKitC-1 (N8, no PSRAM) เพื่อหลีกเลี่ยงอาการบูตล้มเหลว/`SHA-256 comparison failed` หลังแฟลช
- **chore: restore legacy board build spec (`platformio.ini`):**
  - ย้อนกลับไปใช้ override เดิมของ 16MB flash, QIO/80MHz, และ PSRAM flags ตามที่ขอ เพื่อให้ build spec ตรงกับ workflow เดิมก่อนหน้า
- **docs: non-technical operator test guide (`TEST_OPERATOR_GUIDE.md`, `README.md`):**
  - เพิ่มคู่มือทดสอบระบบสำหรับผู้ใช้ที่ไม่รู้โครงสร้างภายใน โดยจัดลำดับการทดสอบแบบ step-by-step, เกณฑ์ pass/fail, และวิธีรายงานปัญหาให้อ่านง่ายจากหน้า Dashboard, Hardware Test, และ Settings
- **docs: hardware test runbook (`docs/vault/05-testing/bringup-checklist.md`):**
  - เพิ่ม checklist แยกสำหรับหน้า `/hwtest` ครอบคลุม sensor snapshot, dosing pumps, light relay, water system controls, และ safe stop verification

## [2026-04-14] - Water Refill Route Control

### Changed

- **feat: refill route selection scaffold (`config.h`, `waterSystem.h`, `waterSystem.cpp`):**
  - เพิ่มแนวคิด refill route แบบ `AUTO`, `FISH_TANK`, `SUMP_DIRECT`
  - เพิ่ม optional route valve pin สำหรับระบบ one-pump + diverter valve
  - เพิ่ม auto fallback จากเติมผ่านตู้ปลาไป direct sump เมื่อเติมนานเกิน threshold ที่กำหนด
- **feat: route-aware MQTT status/config (`localMqtt.cpp`):**
  - เพิ่ม field `preferred_route`, `active_route`, `allow_direct_sump_refill`, และ `route_blocked`
  - ให้ local MQTT config สั่ง route logic ใหม่ได้โดยไม่กระทบ safe default เดิม
- **feat: route debug commands (`commandHandler.cpp`):**
  - เพิ่มคำสั่ง `route auto`, `route fish`, `route sump`
  - ขยายคำสั่ง `water` ให้แสดง route ที่ตั้งไว้และ route ที่กำลังทำงานจริง
- **feat: Pi route controls + direct-refill interlock (`pi_server/app.py`, `pi_server/settings.html`, `automator.cpp`):**
  - เพิ่มการตั้งค่า `preferred_route` และ `allow_direct_sump_refill` จากหน้า settings และ API ฝั่ง Pi
  - แสดง active route, route blocked, และ valve output บนหน้า settings
  - ให้ automator หยุด dosing ระหว่างที่ระบบกำลัง refill แบบ direct sump
- **docs: hardware route test checklist (`docs/vault/05-testing/bringup-checklist.md`):**
  - ขยาย bring-up checklist ให้มีขั้นตอนทดสอบ `route fish`, `route sump`, `auto fallback`, alarm, และ Pi dashboard แบบหน้างาน
- **fix: camera web UX (`live.html`, `settings.html`, `app.py`):**
  - เพิ่มสถานะ `Connecting` และ `Paused` บนหน้า live พร้อมหยุด stream ตอนซ่อนแท็บหรือปิดหน้า
  - เพิ่ม camera readiness probe ที่ Pi backend เพื่อให้หน้า settings ยืนยัน restart สำเร็จจริงก่อนขึ้น `Restarted`
- **refactor: hybrid settings UX (`settings.html`):**
  - เพิ่มป้าย `Apply Now` และ `Save All` ให้แต่ละ card เพื่อสื่อ model การบันทึกให้ชัดขึ้น
  - แยก `Sensor Thresholds` ออกเป็นการบันทึกเฉพาะ card ผ่านปุ่ม `Apply Thresholds`
- **docs: Thai water-system field tips (`settings.html`):**
  - เพิ่มคำอธิบายภาษาไทยใน Water System card สำหรับ toggle, route, timeout, status, และ action buttons เพื่อให้อ่านหน้า settings แล้วเข้าใจหน้าที่ของแต่ละค่าได้ทันที
  - เปลี่ยนปุ่มล่างเป็น `Save General Settings` เพื่อให้สื่อหน้าที่ตรงกับการใช้งานจริง
- **refactor: automation engine dashboard (`index.html`):**
  - เปลี่ยน workflow widget ให้แสดง `Current`, `Next`, `ETA`, และ `Reason` เป็นข้อมูลหลัก
  - แก้ stepper ให้ map กับ state จริงของ firmware (`IDLE`, `EVALUATING`, `DOSING_A`, `DOSING_B`, `WATER_FILL`, `COOLDOWN`) แทนการเดาจากข้อความ reason แบบเดิม
- **feat: firmware-driven automation next state (`automator.cpp`, `automator.h`, `localMqtt.cpp`, `index.html`):**
  - เพิ่ม field `auto_next_state` จากฝั่ง firmware เพื่อให้ dashboard แสดง `Next` จาก logic จริงของ controller แทนการคาดเดาใน frontend

## [2026-04-14] - Obsidian Vault Scaffold

### Added

- **docs: Obsidian starter vault (`docs/vault/`):**
  - เพิ่มโครงโฟลเดอร์ vault สำหรับใช้ร่วมกันระหว่าง Obsidian และ VS Code
  - เพิ่มไฟล์ตั้งต้นสำหรับ system overview, water flow, hardware, firmware, Pi, testing, และ decision log

### Changed

- **chore: Obsidian-friendly gitignore (`.gitignore`):**
  - เพิ่ม ignore สำหรับ `docs/vault/.obsidian/`, `docs/vault/.trash/`, และไฟล์ metadata ที่ไม่ควรเข้า repo

## [2026-04-13] - Future-Ready Water System Scaffold

### Added

- **feat: water system scaffold (`waterSystem.cpp`, `waterSystem.h`):**
  - เพิ่มโมดูลคุมปั๊มน้ำวนหลักและปั๊มเติมน้ำแบบ future-ready
  - รองรับสถานะ `DISABLED`, `CIRCULATION`, `REFILLING`, `BLOCKED`, `ALARM`
  - รองรับ logic refill timeout, overflow alarm, และ sensor contradiction
  - ใช้ค่า default แบบปลอดภัยเมื่อยังไม่กำหนด GPIO/เซ็นเซอร์จริง
- **feat: CLI water commands (`commandHandler.cpp`):**
  - เพิ่มคำสั่ง `water`, `circ on`, `circ off`, `refill on`, `refill off`, `water clear`
- **feat: local MQTT water status/config (`localMqtt.cpp`, `config.h`):**
  - เพิ่ม topic `aquaponics/config/water_system` และ `aquaponics/status/water_system`
  - แนบสถานะระบบน้ำเข้า payload `aquaponics/sensors`
- **docs: hardware shopping list (`HARDWARE_TO_BUY.md`):**
  - สรุปรายการฮาร์ดแวร์ที่ควรซื้อเพิ่มจากสถานะปัจจุบันของระบบ

### Changed

- **fix: water alarm interlock (`automator.cpp`):**
  - ให้ automator หยุดจ่ายปุ๋ยเมื่อระบบน้ำมี alarm หรือเมื่อปั๊มน้ำวนหลักไม่ทำงาน
- **feat: Pi water system integration (`pi_server/app.py`, `pi_server/settings.html`):**
  - เพิ่ม API และหน้า settings สำหรับตั้งค่า circulation, refill, manual refill, และ clear alarm
  - เพิ่มการ sync สถานะ water system จาก MQTT เข้า Pi memory โดยไม่เขียนไฟล์ถี่เกินจำเป็น
- **feat: periodic water status publish (`localMqtt.cpp`):**
  - ส่ง `aquaponics/status/water_system` เป็นระยะเมื่อ local MQTT เชื่อมต่อ เพื่อให้ dashboard/backend เห็นสถานะล่าสุดต่อเนื่อง

## [2026-04-09] - Hardware Pin Rework & WiFi Direct Connect (v2.5.0)

### Changed

- **refactor: WiFi Direct Connect (`wifiConn.cpp`):**
  - เปลี่ยนจาก WiFiManager (AP Portal) → `WiFi.begin()` ต่อตรงไปยัง Pi Hotspot
  - SSID และรหัสผ่านของ AP ให้ใช้ค่าจาก `hostapd.conf` ปัจจุบันของเครื่องหน้างาน
  - ลบ dependency `WiFiManager` library ออก — ลด Flash usage
  - Auto-reconnect ทุก 10 วินาที (เดิม 30 วินาที)
- **refactor: Light Controller → Relay (`lightController.cpp`):**
  - เปลี่ยนจาก NeoPixel RGB LED (GPIO 48) → Relay ผ่าน `LIGHT_RELAY_PIN` (GPIO 12)
  - ลบ dependency `Adafruit_NeoPixel` library ออก
  - ใช้ `digitalWrite()` แบบ Active LOW (LOW = ON, HIGH = OFF)
- **refactor: Pin Assignment (`config.h`):**
  - `LIGHT_RELAY_PIN` เปลี่ยนจาก `LED_BUILTIN` → GPIO 12
  - ลบ `PUMP_WATER_PIN` ออกจาก codebase ทั้งหมด (ยังไม่ใช้)
  - ลบ references จาก: `localMqtt.cpp`, `commandHandler.cpp`, `automator.cpp`

### Removed

- **`PUMP_WATER_PIN`** — ลบ define + ลบ `pump_w` CLI command + ลบ hw_test handler
- **`Adafruit_NeoPixel`** — ไม่ใช้ NeoPixel แล้ว (ใช้ Relay แทน)
- **`WiFiManager`** — ไม่ใช้ AP Portal แล้ว (ต่อตรง Pi)

## [2026-03-26] - Offline-Safe Networking

### Fixed

- **fix: NETPIE MQTT reconnect spam (`netpie.cpp`):**
  - เพิ่ม Exponential Backoff (5s → 10s → 20s → 30s → 60s max) แทน fixed 5s interval
  - ลด log spam จาก ~12 ครั้ง/นาที เหลือ ~1-2 ครั้ง/นาที เมื่อ broker unavailable
  - Reset backoff กลับ 5s เมื่อเชื่อมต่อสำเร็จ
- **fix: Skip network services when WiFi offline (`main.cpp`):**
  - `telnetLoop()` และ `otaLoop()` จะไม่ถูกเรียกเมื่อ WiFi ยังไม่เชื่อม
  - ลด CPU waste จาก socket operations ที่ทำไม่ได้
- **fix: getLocalTime() blocking (`lightController.cpp`):**
  - เพิ่ม timeout 10ms แทน default 5s — ป้องกัน block เมื่อ NTP ยังไม่ sync
- **fix: Log queue overflow เมื่อ MQTT offline (`localMqtt.cpp`):**
  - ไม่ queue log เมื่อ MQTT ยังไม่เชื่อม — ประหยัด memory

## [2026-03-25] - Header Navigation & Responsive Fix

### Changed

- **feat: Mobile Hamburger Menu (`header.js`):**
  - เพิ่มปุ่ม ☰ hamburger สำหรับมือถือ — กดแล้ว nav เปิดแนวตั้ง (เปลี่ยนเป็น ✕ ปิด)
  - Nav bar เปลี่ยนจาก horizontal scroll เป็น vertical collapse บน mobile (≤768px)
  - ซ่อน Net Stats pills บนหน้าจอเล็กมาก (≤480px) เพื่อประหยัดพื้นที่
  - Nav links มี touch target ใหญ่ขึ้น (12px padding, full-width) สำหรับมือถือ
- **refactor: ลบ CSS ซ้ำ (`graphs.html`, `terminal.html`, `ota.html`):**
  - ลบ nav-bar/nav-link CSS overrides ที่ซ้ำกับ `header.js` ออกจาก 3 หน้า
  - Nav styling ทั้งหมดจัดการจาก `header.js` ที่เดียว (Single Source of Truth)

## [2026-03-25] - Fix Networking Task Stuck (30s Timeout)

### Fixed

- **fix: MQTT Connect Blocking (`netpie.cpp`, `localMqtt.cpp`):**
  - ตั้ง `WiFiClient.setTimeout(5)` ลด TCP socket timeout จาก ~30 วินาที → 5 วินาที
  - ตั้ง `PubSubClient.setSocketTimeout(5)` ลด MQTT keepalive timeout
  - เพิ่ม heartbeat + WDT reset ระหว่าง `netpieLoop()` กับ `localMqttLoop()` ใน `TaskNetworking` (main.cpp)
  - แก้ปัญหา "STUCK TASK: Networking — no heartbeat for 30 seconds" เมื่อ NETPIE หรือ Pi unreachable

## [2026-03-24] - Web Dashboard Performance Optimization

### Changed

- **perf: Dashboard WebSocket Migration (`index.html`):**
  - เปลี่ยนจาก HTTP Polling (5 API calls ทุก 3 วินาที) → WebSocket (SocketIO) ที่ server push ข้อมูลมาให้ทุก 2 วินาที
  - ลดจำนวน HTTP requests จาก ~100/นาที เหลือ ~2/นาที (settings fetch ทุก 30 วินาที)
  - ข้อมูลเซนเซอร์อัปเดตเร็วขึ้นและลดภาระ CPU ของ Pi อย่างมาก
- **perf: Net Stats Polling Interval (`header.js`):**
  - ลด polling `/api/wifi/netstats` จากทุก 3 วินาที → ทุก 10 วินาที ลดการรัน `ping` command บน Pi
- **perf: Chart.js Local Hosting (`graphs.html`):**
  - ย้าย Chart.js, HammerJS, chartjs-plugin-zoom จาก CDN ภายนอกมาเป็นไฟล์ local (`/static/js/`)
  - กราฟทำงานได้แม้ไม่มีอินเทอร์เน็ต (Offline Mode)
- **refactor: ลบ CSS ซ้ำใน `index.html`:**
  - ลบ Nav Bar CSS ~40 บรรทัดที่ซ้ำกับ `header.js` และ `base.css`
  - เพิ่ม `will-change: transform` ให้ card animations เพื่อลดภาระ GPU
- **chore: PWA Cache Update (`sw.js`):**
  - Bump cache version `v2` → `v3`
  - เพิ่ม Socket.IO, Chart.js, HammerJS, chartjs-plugin-zoom เข้า PWA cache สำหรับ offline
- **chore: Updated `download_assets.sh`:**
  - เพิ่มคำสั่งดาวน์โหลด Chart.js + plugins สำหรับ offline mode

## [2026-03-20] - Login System & Admin Panel

### Added

- **Authentication System (`app.py`):**
  - Flask session-based login with `login_required` decorator on all protected routes.
  - Hashed passwords via `werkzeug.security` stored in `auth_config.json` (gitignored).
  - API: `POST /api/login`, `POST /api/logout` with session management (7-day expiry).
  - Auto-redirect unauthenticated users to `/login` page.
- **Activity Logging (`app.py` + `admin_logs.html`):**
  - `activity_logs` SQLite table for admin audit trail.
  - Tracks: Login, Logout, Settings changes, TDS/pH Calibration, OTA uploads, User management.
  - Admin Logs page with filtering by action type and date.
- **User Management (`admin_users.html`):**
  - Admin page to add, edit password, and delete users.
  - API: `GET/POST /api/admin/users`, `POST /api/admin/users/password`, `POST /api/admin/users/delete`.
  - Cannot delete the `admin` account.
- **Login Page (`login.html`):**
  - Standalone dark-theme login page with glassmorphism design.
  - Animated login icon, error handling, AJAX form submission.
- **Navigation Update (`header.js`):**
  - Added "Activity" and "Users" nav links.
  - Added Logout button in header (red accent, all pages).

### Changed

- `app.py`: All 30+ routes now require authentication except `/login`, static assets, and PWA files.
- `.gitignore`: Added `pi_server/auth_config.json`.

## [2026-03-10] - Deployment Automation & Health Monitoring

### Added

- **Deployment automation scripts:**
  - `setup.sh`: Automated install script for Pi (packages, python libs, services).
  - `aquaponics.service`: Systemd service for the main dashboard app.
  - Improved `download_assets.sh` with relative path support.
- **System Health Enhancement:**
  - Implemented real sensor health check in `system.cpp`.
  - `SystemHealth_t::sensorsOk` now accurately reflects the status of all enabled sensors.
- **Unit Testing:**
  - Created `test/test_ph/test_ph_filter.cpp` to verify pH EMA filter and error handling (Native environment).

## [2026-03-02] - pH Sensor Filter Enhancement

### Changed

- **feat: pH EMA Filter (`phSensor.cpp`):**
  - เพิ่ม Exponential Moving Average (α=0.15) หลัง Trimmed Mean เพื่อ smooth ค่า pH ข้ามรอบ
  - Pipeline ใหม่: `ADC Buffer → Trimmed Mean (ตัด 20%) → EMA (α=0.15)`
  - รองรับ NaN จาก hardware validation (sensor หลุด/short circuit)

## [2026-02-16] - Pi AP+Client Bridge Mode

### Added

- **Pi AP Bridge (`pi_server/setup_ap.sh`):**
  - Pi Zero 2 W ทำงาน AP+Client พร้อมกัน: `wlan0` (Home WiFi) + `ap0` (Aquaponics-LAN)
  - ESP32 เชื่อม Pi ได้ตลอดแม้ Router พังหรือเปลี่ยน WiFi
  - `hostapd.conf`: AP config (SSID: Aquaponics-LAN, WPA2)
  - `dnsmasq_ap.conf`: DHCP server (192.168.10.x)
  - `setup_ap.sh`: Script อัตโนมัติ (ติดตั้ง + config + systemd + NAT)
- **ESP32 MQTT Fallback IP (`localMqtt.cpp`):**
  - เพิ่ม Static IP fallback (`192.168.10.1`) เมื่อ mDNS หาไม่เจอ
  - เพิ่ม `LOCAL_MQTT_STATIC_IP` ใน `config.h`

## [2026-02-11] - Bug Fixes (mDNS Blocking & SQLite Threading)

### Fixed

- **fix: mDNS Blocking (`localMqtt.cpp`):**
  - ลด mDNS query timeout จาก 1000ms → 200ms เพื่อลด blocking time เมื่อ Pi offline
  - เพิ่ม Exponential Backoff (5s → 10s → 20s → 40s → 60s) สำหรับ reconnect interval เมื่อ Pi หาไม่เจอ ช่วยลดภาระของ Networking Task
- **fix: SQLite Threading (`pi_server/app.py`):**
  - เพิ่ม `threading.Lock()` (`db_lock`) ครอบทุกจุดที่เข้าถึง SQLite (`init_db`, `save_data_to_db`, `save_settings_to_db`, `get_history`) เพื่อป้องกัน `database is locked` error จากการเขียนพร้อมกันของ MQTT Thread กับ Web Thread

## [2026-02-09-3] - Sensor Toggle & Dashboard Enhancements

### Added

- **Sensor Toggle Feature (ESP32 + Pi):**
  - Added ability to enable/disable individual sensors (TDS, pH, Water Temp, Air Temp, Light) via Web Dashboard.
  - Implemented `SensorId_t` enum and `systemSetSensorEnabled()`/`systemGetSensorEnabled()` in `system.cpp`.
  - Sensor states persisted in NVS (survives reboot).
  - MQTT topic `aquaponics/config/sensors` for receiving toggle commands from Pi.
  - MQTT topic `aquaponics/status/sensors` for state feedback from ESP32 to Pi.
- **Dashboard Disabled Indicator:**
  - Sensor cards on Dashboard now show "DISABLED" badge (red) when sensor is OFF.
  - Cards appear grayed out (grayscale + opacity) for visual clarity.

### Fixed

- **TDS Calibration UI:** Fixed invisible text in number inputs caused by CSS color issue. Redesigned layout to 2-column grid.
- **State Synchronization:** ESP32 now publishes its sensor config on MQTT connect and after updates, ensuring Pi settings stay in sync.

### Changed

- `settings.html`: Added "Sensor Management" section with toggle switches.
- `app.py`: Added `/api/settings` POST endpoint and MQTT subscription to `aquaponics/status/sensors`.
- `main.cpp`: `TaskSensors` now checks `systemGetSensorEnabled()` before reading each sensor.

## [2026-02-09-2] - Security Hardening & Stability

### Fixed

- **Watchdog Timer Reset:**
  - Added `esp_task_wdt_reset()` to `loop()` in `main.cpp` to prevent 60-second system restarts.
- **Secrets Management:**
  - Moved all hardcoded secrets from `secrets.h` to `secrets.ini`.
  - Configured `platformio.ini` to inject secrets as build flags using safe stringification macros.
  - Fixed quoting issue in `secrets.ini` that caused MQTT auth failures.

### Changed

- **Code Refactoring:**
  - Refactored `telnetServer.cpp` and `commandHandler.cpp` to use C-style strings (`char*`) instead of `String` objects to prevent memory fragmentation.
  - Implemented FreeRTOS Tasks (`TaskNetworking`, `TaskSensors`, `TaskControl`) for better concurrency.
- **Verification:**
  - Verified codebase against `esp32-aquaponics-engineering-standard` (Passed).

### Added

- **Documentation:**
  - Created `walkthrough.md` summarizing the system hardening process.

## [2026-02-09] - TDS Advanced Calibration

### Added

- **TDS 2-Point Calibration:**
  - Implemented Linear Interpolation logic in `TdsSensor.cpp`.
  - Added Temperature Compensation with manual input in `settings.html`.
  - Seprate temperature inputs for Low and High calibration points for maximum accuracy.
  - Persisted calibration data in NVS (ESP32) and `settings.json` (Pi).
  - **TDS Stabilization:** Added weighted Moving Average Filter (Alpha 0.1) for smooth, stable readings.
  - **Advanced Math:** Replaced linear model with Hybrid Polynomial Calibration (Standard Curve + 2-Point Scaling).
  - **Precision Temp Comp:** Updated temperature coefficient to 0.019 (KCl Standard) for better accuracy.

### Changed

- **Code Refactoring:**
  - Optimized buffer logic in `TdsSensor.cpp` to remove unused variables.
  - Updated `config.h` to use standard intervals.
  - Verified code against Engineering Standards (PASS).

## [2026-02-08-2] - WiFi Reconnection Fixes

### Fixed

- **WiFi Auto-Reconnect (`wifiConn.cpp`):**
  - เพิ่ม `WiFi.setAutoReconnect(true)` เมื่อเชื่อมต่อสำเร็จ
  - เพิ่ม logic พยายาม `WiFi.reconnect()` ทุก 30 วินาทีเมื่อ WiFi หลุด
  - แก้ปัญหา ESP32 ไม่เชื่อมต่อ WiFi หลัง router restart
- **Local MQTT mDNS Re-resolution (`localMqtt.cpp`):**
  - เพิ่มการ re-resolve mDNS hostname หลัง connection ล้มเหลว 3 ครั้ง
  - แก้ปัญหา Pi IP เปลี่ยนหลัง router restart

## [2026-02-08] - Live Cam & PWA

### Added

- **Live Camera (720p@15fps):**
  - Added `live.html` for viewing MJPEG stream.
  - Created `start_cam.sh` for controlling `libcamera-vid`.
  - Integrated camera stream into Dashboard UI.
- **Progressive Web App (PWA):**
  - Added `manifest.json` and `sw.js` for "Add to Home Screen" capability.
  - Implemented offline caching for static assets.
  - Generated application icons.
- **Settings Integration:**
  - `start_cam.sh` script for easy camera startup.
- **Auto-Start System:**
  - Added camera auto-start logic to `app.py`.
  - Updated `setup.sh` to handle Python environment restrictions.

### Changed

- **Dashboard UI:**
  - Added "Live Cam" button to the header.
  - Updated `index.html` to register Service Worker.
- **Backend:**
  - Updated `app.py` to serve PWA assets and Live page.

## [2026-02-07] - Core Systems & IoT

### Added

- **Light Control System:**
  - Implemented `relay_module.cpp` on ESP32.
  - Added scheduler logic synced with NTP.
  - Integrated manual override via MQTT.
- **NETPIE Integration:**
  - Bi-directional shadow syncing for light status.
  - Fixed partial update bug in `netpie.cpp`.

### Changed

- **Codebase Optimization:**
  - Removed blocking `delay()` calls in `main.cpp`.
  - Refactored `wifi_manager.cpp` for stability.
  - Audited code against Engineering Standards (PASS).

## [2026-02-06] - Advanced Dashboard & Logs

### Added

- **Full Logs Viewer:**
  - Created `full_logs.html` with filtering and pagination.
  - Added API endpoint `/get_logs` in `app.py`.
- **Database:**
  - Implemented `aquaponics.db` (SQLite) for persistent sensor data.
- **DHT Sensor:**
  - Integrated DHT22 for air temperature/humidity.

## [2026-02-04] - Initial Web Interface

### Added

- **Basic Dashboard:**
  - `index.html` demonstrating real-time gauge updates.
  - `app.py` Flask server foundation.
- **MQTT Bridge:**
  - Setup local Mosquitto broker on Raspberry Pi.
  - Python script to bridge ESP32 data to Web UI.

## [2026-01-31] - Firmware Foundations

### Added

- **ESP32 Modules:**
  - Standardized non-blocking sensor modules.
  - `esp32-aquaponics-module` skill definition.
  - Initial `platformio.ini` configuration.

---

_Created by Antigravity AI_
