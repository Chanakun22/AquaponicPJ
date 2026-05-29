# Fish Tank Sensors Expansion Plan

แผนเพิ่มเซ็นเซอร์วัดคุณภาพน้ำในตู้ปลา (water temp + pH + EC/TDS) ใช้สำหรับ **monitoring เท่านั้น** ไม่นำไปใช้ใน automation

> สถานะ: 📋 **PLANNING** — ยังไม่ implement, รอตัดสินใจก่อนเริ่ม

---

## 1. Goal & Scope

### เป้าหมาย
- เพิ่มจุดวัด TDS, pH, water temp ในตู้ปลา (เพิ่มเติมจากที่วัดในถังผสมอยู่แล้ว)
- ส่งข้อมูลขึ้น dashboard และ NETPIE เพื่อ monitor
- **ไม่ใช้** ในการตัดสินใจ automation (automator ยังคงใช้ค่าจากถังผสมเหมือนเดิม)

### Use case
- Operator เห็นค่าน้ำในตู้ปลาเทียบกับถังผสม → บอกได้ว่าระบบ filter / circulation ทำงาน
- Cross-check sensor — ถ้า 2 จุดต่างกันมากผิดปกติ → ตู้ปลาอาจมีปัญหา (ปุ๋ยตกค้าง, ออกซิเจนน้อย, ฯลฯ)

### Out of scope (เก็บไว้รอบหลัง)
- ใช้ค่าตู้ปลาในการ dose ปุ๋ย
- Per-tank automation
- Alert thresholds เฉพาะตู้ปลา (ใช้ thresholds ร่วมกับถังผสมก่อน)

---

## 2. Hardware Shopping List

| รายการ | จำนวน | ใช้แทน/ขยาย | ราคาประมาณ |
|--------|------|--------------|------------|
| **DS18B20** (water temp probe) | 1 | ทาบขนานบน OneWire bus เดิม | 50 บาท |
| **E-201-C pH Sensor + Module** | 1 | เซ็นเซอร์ pH ตัวที่ 2 | ~620 บาท |
| **TDS/EC sensor probe + module** | 1 | เซ็นเซอร์ TDS ตัวที่ 2 (DFRobot SEN0244 หรือ clone) | ~250-400 บาท |
| **pH Buffer 4.01/6.86/9.18** | 1 ชุด | ใช้ calibrate sensor ใหม่ | 100-150 บาท |
| **TDS standard solution** (500/1413 ppm) | 1 ชุด | ใช้ calibrate TDS ใหม่ | 100-200 บาท |
| Jumper wire + heatshrink | - | ต่อ probe | - |

**รวมประมาณ 1,100-1,400 บาท**

หมายเหตุ: ถ้าซื้อ E-201-C แบบรวม `2E-201-Modu+PHbuf` จะได้ probe + module + buffer ครบในชุดเดียว ประหยัดได้

---

## 3. Pinout Assignment

### ESP32-S3 GPIO ใหม่ที่จะใช้

| Sensor (ตู้ปลา) | GPIO | Function | หมายเหตุ |
|-----------------|------|----------|----------|
| Water temp #2 | **13** (เดิม) | OneWire | ทาบขนานบน DS18B20 bus เดิม — ไม่กิน pin |
| pH #2 | **1** | ADC1_CH0 | ปลอดภัยที่สุด, RTC, Touch1 |
| TDS #2 | **7** | ADC1_CH6 | ปลอดภัย, RTC, Touch7 |

### Constants ที่จะเพิ่มใน `config.h`

```c
// Water temp — DS18B20 OneWire (shared bus, GPIO 13)
// ใช้ device index/address แยกระหว่าง mix tank vs fish tank
#define WATER_TEMP_DEVICE_INDEX_MIX     0   // DS18B20 ตัวแรกที่ scan เจอ
#define WATER_TEMP_DEVICE_INDEX_FISH    1   // ตัวที่สอง

// pH probes
#define PH_SENSOR_MIX_PIN       6      // เดิม
#define PH_SENSOR_FISH_PIN      1      // ใหม่ (ADC1_CH0)

// TDS probes
#define TDS_MIX_PIN             5      // เดิม
#define TDS_FISH_PIN            7      // ใหม่ (ADC1_CH6)
```

⚠️ **ยังไม่ลบ pin defines เดิม** จนกว่า refactor multi-instance จะเสร็จและทดสอบผ่าน

---

## 4. Code Architecture — Multi-Channel Refactor

### กลยุทธ์: Array-Based Channels (ไม่ใช้ class)

เพื่อ minimum disruption กับ code style เดิม (C-style, function-based)

#### `phSensor.h` ใหม่

```c
typedef enum {
    PH_CHANNEL_MIX = 0,     // ถังผสม (เดิม)
    PH_CHANNEL_FISH,        // ตู้ปลา (ใหม่)
    PH_CHANNEL_COUNT
} PhChannel;

void phSetup(void);                                       // init ทุก channel
void phLoop(void);                                        // อ่านทุก channel
float phGetLastValue(PhChannel ch);                       // อ่านค่า cache ของ channel
float phGetLastVoltageMv(PhChannel ch);
bool  phIsReady(PhChannel ch);

// Calibration (per channel)
void phCalibratePh401(PhChannel ch);
void phCalibratePh686(PhChannel ch);
void phCalibratePh918(PhChannel ch);
void phClearCalibration(PhChannel ch);

// Backward compat shims (สำหรับ caller เก่า — assume mix tank)
inline float phRead(void) { return phGetLastValue(PH_CHANNEL_MIX); }
```

#### `TdsSensor.h` ใหม่

```c
typedef enum {
    TDS_CHANNEL_MIX = 0,
    TDS_CHANNEL_FISH,
    TDS_CHANNEL_COUNT
} TdsChannel;

void tdsSetup(void);
void tdsLoop(float temperature);                          // ใช้ temp ตัวเดียวกันทั้ง 2 channel
float tdsGetLastValue(TdsChannel ch);
void tdsSetCalibration(TdsChannel ch, ...);               // calibrate per channel
```

#### `tempSensor.h` ใหม่

```c
typedef enum {
    TEMP_CHANNEL_MIX = 0,
    TEMP_CHANNEL_FISH,
    TEMP_CHANNEL_COUNT
} TempChannel;

float tempGetTemperature(TempChannel ch);
// internal: track DS18B20 address ของแต่ละ channel ใน NVS
```

### Storage strategy

- **Calibration**: NVS namespace `phSensor` → key `_ch<n>_v401`, `_ch<n>_v686`, `_ch<n>_v918`
- **DS18B20 address mapping**: NVS namespace `tempSensor` → key `addr_mix`, `addr_fish` (8 bytes each)
  - First boot: scan bus → save addresses → bind ตัวแรก = MIX, ตัวที่สอง = FISH
  - User สามารถ swap ผ่าน CLI ได้ (`temp swap`)

---

## 5. MQTT / Dashboard Schema Changes

### Local MQTT — `aquaponics/sensors`

#### เดิม
```json
{
  "tds": 980,
  "ph": 6.85,
  "water_temp": 26.5
}
```

#### ใหม่ (backward compatible)
```json
{
  "tds": 980,            ← keep alias = mix (สำหรับ Pi เก่ายังอ่านได้)
  "ph": 6.85,
  "water_temp": 26.5,
  
  "tds_mix": 980,
  "tds_fish": 850,
  "ph_mix": 6.85,
  "ph_fish": 7.20,
  "water_temp_mix": 26.5,
  "water_temp_fish": 27.1
}
```

**Strategy:** ส่งทั้ง legacy key และ new key ในช่วง transition → Pi server อัปเดตให้ใช้ key ใหม่ → ค่อยเอา legacy ออกในรอบหลัง

### NETPIE shadow (`@shadow/data/update`)

เหมือนกัน — เก็บ legacy + เพิ่ม fish key

### Pi Dashboard เปลี่ยน

| หน้า | เปลี่ยนแปลง |
|------|------------|
| `index.html` (Dashboard) | เพิ่ม card 3 ใบสำหรับ fish tank (TDS/pH/Temp) แสดงเทียบ mix tank |
| `graphs.html` | เพิ่ม line series ใหม่ในกราฟแต่ละชนิด — สีต่างจาก mix tank |
| `settings.html` | เพิ่ม section calibration ของ fish tank (แยกจาก mix tank) |
| `full_logs.html` | เพิ่ม column / filter ใหม่ |

### NVS namespace changes

| Module | เดิม | ใหม่ |
|--------|------|------|
| `phSensor` | volt401/volt686/volt918 | **เพิ่ม** ch1_volt401/ch1_volt686/... (ch0 = legacy) |
| `TdsSensor` | (calibration keys) | เพิ่ม prefix `ch1_*` |
| `tempSensor` | - | เพิ่ม `addr_mix`, `addr_fish` |

⚠️ **NVS migration**: รอบแรก firmware ใหม่จะเห็น NVS เดิมเป็น `ch0` (mix) ค่า fish ยังเป็น default → ต้อง calibrate fish sensor ใหม่หลัง deploy

---

## 6. CLI Commands

### Backward compatible (default = mix tank)

| Command | ตอนนี้ | หลัง refactor |
|---------|-------|--------------|
| `ph` | แสดง pH 1 ตัว | แสดงทั้ง 2 channel |
| `cal686` | calibrate pH (1 sensor) | calibrate **mix** (default) |
| `status` | sensor 5 ค่า | sensor 8 ค่า (เพิ่ม fish) |

### Commands ใหม่

| Command | หน้าที่ |
|---------|--------|
| `cal686 fish` | Calibrate pH ตู้ปลาที่ pH 6.86 |
| `cal401 fish` | Calibrate pH ตู้ปลาที่ pH 4.01 |
| `cal918 fish` | Calibrate pH ตู้ปลาที่ pH 9.18 |
| `cal686 mix` | (alias เก่า) calibrate ถังผสม |
| `tds cal fish` | Calibrate TDS ตู้ปลา (ใช้ผ่านหน้าเว็บก็ได้) |
| `temp scan` | Scan DS18B20 บน bus → แสดง address + อุณหภูมิแต่ละตัว |
| `temp bind mix <addr>` | Bind address ใดเป็น mix sensor |
| `temp bind fish <addr>` | Bind address ใดเป็น fish sensor |
| `temp swap` | สลับ binding ระหว่าง mix/fish |

---

## 7. Phased Implementation Plan

### Phase A — Foundation (ไม่ใช้ probe จริง, dry-run code)
**Risk: LOW** | Effort: 4-6 hr

- [ ] เพิ่ม `PhChannel` / `TdsChannel` / `TempChannel` enum ใน header
- [ ] Refactor internal storage เป็น array (ยังไม่ expose channel ใหม่ใน MQTT)
- [ ] สร้าง backward-compat shims
- [ ] Build + native test ผ่าน (ค่า MIX channel อ่านเหมือนเดิม)

### Phase B — Water Temp #2 (ง่ายสุด, ทำก่อน)
**Risk: LOW** | Effort: 2-3 hr

- [ ] Wire DS18B20 ตัวที่ 2 ขนานบน GPIO 13
- [ ] เพิ่ม CLI: `temp scan`, `temp bind mix/fish`
- [ ] เก็บ address mapping ใน NVS
- [ ] ส่ง `water_temp_fish` ใน MQTT (legacy `water_temp` ยังคง = mix)
- [ ] Pi dashboard: เพิ่ม card Fish Tank Temperature
- [ ] ทดสอบ 24 ชั่วโมง — ตรวจค่าเสถียร, ไม่สลับ

### Phase C — pH #2
**Risk: MEDIUM** | Effort: 3-4 hr

- [ ] Wire E-201-C ตัวที่ 2 → GPIO 1 (ADC1_CH0)
- [ ] Refactor `phSensor.cpp` ให้รองรับ 2 channel จริง
- [ ] เพิ่ม CLI: `cal686/401/918 fish/mix`
- [ ] เพิ่ม MQTT: `ph_fish`
- [ ] Pi dashboard: card + graph + calibration UI
- [ ] Calibrate ทั้ง 2 probe ครบ 3 จุด
- [ ] ทดสอบ 24 ชั่วโมง

### Phase D — TDS #2
**Risk: MEDIUM** | Effort: 3-4 hr

- [ ] Wire TDS probe ตัวที่ 2 → GPIO 7 (ADC1_CH6)
- [ ] Refactor `TdsSensor.cpp` ให้รองรับ 2 channel
- [ ] Pi: หน้า settings เพิ่ม calibration ของ fish tank
- [ ] เพิ่ม MQTT: `tds_fish`
- [ ] Calibrate ทั้ง 2 probe
- [ ] ทดสอบ 24 ชั่วโมง

### Phase E — Dashboard polish + cleanup
**Risk: LOW** | Effort: 2-3 hr

- [ ] Pi dashboard: layout ปรับให้ดูเทียบ mix vs fish ง่าย
- [ ] Graphs: 2 series ต่อ chart (mix + fish)
- [ ] CHANGELOG.md update
- [ ] (Optional) Remove legacy `tds`/`ph`/`water_temp` keys หลัง Pi อัปเดตหมดแล้ว

**Total effort estimate: ~14-20 ชั่วโมง** (กระจาย 3-5 วันทำงาน)

---

## 8. Calibration Plan

### pH (E-201-C ทั้ง 2 ตัว)

ทำทีละตัว:

1. ล้าง probe ด้วยน้ำกลั่น → แช่ buffer pH 6.86 → รอค่าเสถียร 2 นาที → CLI: `cal686 mix` (หรือ `fish`)
2. ล้าง → buffer pH 4.01 → รอเสถียร → `cal401 mix`
3. ล้าง → buffer pH 9.18 → รอเสถียร → `cal918 mix`
4. ทำซ้ำกับ probe ตู้ปลา

ระยะเวลา: ~30-40 นาทีต่อ probe (รวม 2 ตัว ~1.5 ชม)

### TDS

ผ่านหน้า Settings ของ Pi:
1. Probe ในน้ำกลั่น (ค่า near 0) → ตั้งจุด LOW
2. Probe ใน standard 1413 ppm → ตั้งจุด HIGH
3. Apply calibration

ทำซ้ำสำหรับ probe ตู้ปลา

### Water Temp

DS18B20 ไม่ต้อง calibrate (factory-calibrated ±0.5°C)
- แค่ `temp scan` → bind ให้ถูกตำแหน่ง

---

## 9. Test Plan

### Unit / Native tests
- `test_ph_native` — ขยายให้ test multi-channel
- `test_tds_native` — ขยายให้ test multi-channel
- ทดสอบว่า calibration ของ channel หนึ่งไม่กระทบอีก channel

### Integration tests (firmware + Pi)
- Boot → ดู serial log "Sensor X channel Y initialized"
- MQTT payload ตรวจครบ 8 keys (mix + fish)
- Pi dashboard แสดงค่า 2 ชุด
- Calibrate fish channel แล้ว — restart → ค่า calibration ยังอยู่ (NVS persist)
- Calibrate mix channel — fish channel ไม่ถูกล้าง

### Hardware tests (ตอน probe มา)
- pH probe: เทียบกับ buffer 6.86 → อ่านได้ ±0.05
- TDS probe: เทียบกับ standard 1413 ppm @ 25°C → อ่านได้ ±50 ppm
- DS18B20: เทียบกับ thermometer มาตรฐาน → ±0.5°C

### Soak test
- รัน 48 ชั่วโมง ดู:
  - sensor ไม่ค้าง / ไม่กระโดดผิดปกติ
  - heap stable
  - ไม่มี WDT reset
  - ค่า mix vs fish ดู correlation (ควรใกล้กันถ้า circulation ทำงาน)

---

## 10. Rollback Plan

ถ้า refactor มีปัญหาหลัง Phase A:

```bash
# Git rollback
git revert <multi-channel-refactor-commit>
pio run -e production --target upload
```

ระหว่าง Phase B/C/D: probe ใหม่เป็น additive — ถ้าไม่อยากใช้ ก็ปล่อย pin ลอย ระบบเดิมยังทำงาน

---

## 11. Risks & Open Questions

### Risks

| Risk | ผลกระทบ | Mitigation |
|------|---------|-----------|
| ADC noise บน GPIO 1 (ใกล้ U0RX) | pH/TDS เพี้ยน | ใช้ shielded cable, capacitor filter, software EMA |
| DS18B20 ตัวที่ 2 หลุด bus → ไม่เห็น | Fish temp NaN | Detect missing → log warning, ใช้ค่า last known |
| MQTT payload ใหญ่ขึ้น (เพิ่ม 3 keys) | บ่ห่างจาก 256 byte limit ของ JsonDocument? | เช็คขนาด payload หลังเพิ่ม → adjust StaticJsonDocument size ถ้าจำเป็น |
| User calibrate ผิด channel | ค่าเพี้ยน | UI แสดง confirmation dialog "ยืนยัน calibrate fish tank?" |

### Open Questions

1. **ตำแหน่งติดตั้ง pH probe ในตู้ปลา** — ใกล้ filter หรือกลางตู้?
2. **Probe lifetime** — pH probe มีอายุ 6-12 เดือน, ต้อง budget ค่าเปลี่ยน probe ทั้ง 2 ตัว
3. **Hardware test page** — เพิ่มปุ่มทดสอบ probe fish tank ไหม?
4. **Alert thresholds** — ใช้ thresholds เดียวกับ mix tank หรือแยก?

---

## 12. References

- Plan สำหรับ MCP23017 → `MCP23017_SETUP.md`
- E-201-C pH module: ใช้ analog output 0-3.3V, calibrate 3 จุด
- DS18B20 OneWire bus: รองรับสูงสุด ~50 sensors บน bus เดียว (ตามทฤษฎี)
- ESP32-S3 ADC2 + WiFi conflict → ใช้ ADC1 only (GPIO 1-10)

---

## ขั้นตอนถัดไป

เมื่อพร้อมเริ่ม implement:

1. ซื้อ hardware ตาม shopping list
2. เริ่ม **Phase A (Foundation)** — refactor ภายในก่อน, ไม่ผูกกับ hardware
3. Wire + ทำ Phase B-D ทีละตัว
4. รวบ Phase E (polish) ตอนทำงานทุก sensor เรียบร้อยแล้ว

ก่อนเริ่ม implement: review plan นี้อีกครั้ง อาจมีจุดที่ต้องปรับตามสถานการณ์จริงตอนนั้น
