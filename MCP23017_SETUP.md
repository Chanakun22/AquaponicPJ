# MCP23017 I/O Expander Setup Guide

คู่มือการเชื่อมต่อและทดสอบ MCP23017 (CJMCU-2317) เพื่อย้าย output ทั้ง 8 ตัวจาก ESP32 GPIO ไปผ่าน I2C I/O expander

ข้อมูลในเอกสารนี้สอดคล้องกับ firmware ที่มี `gpioOut` abstraction layer (ดู `include/gpioOut.h`, `src/gpioOut.cpp`)

---

## ภาพรวม

### ทำไมต้องใช้ MCP23017

| เหตุผล | รายละเอียด |
|--------|-----------|
| ลด GPIO ที่ใช้ | ESP32-S3 ใช้ output 8 ขา → ย้ายผ่าน I2C เหลือ 2 ขา (SDA/SCL) ที่แชร์กับ BH1750 |
| Free strapping/PSRAM-adjacent pins | GPIO 39, 42 อยู่ใกล้ flash/PSRAM region → ปลอดภัยกว่าถ้าย้ายไป MCP |
| ขยายระบบในอนาคต | MCP23017 = 16 GPIOs → เหลืออีก 8 ขาว่าง สำหรับ output/input เพิ่ม |

### ส่วนประกอบที่ใช้

| รายการ | จำนวน | หมายเหตุ |
|--------|------|----------|
| MCP23017 บนบอร์ด CJMCU-2317 | 1 | I/O expander 16-bit, 25mA/pin |
| Resistor 10kΩ | 1 | สำหรับ RESET pullup |
| Jumper wires | ~12 เส้น | VCC, GND, SDA, SCL, RESET, A0-A2, GPA0-7 |

ไม่ต้องใช้ pullup ที่ output ทุกขา เพราะ optoisolator relay module ที่ใช้มี internal pullup ในตัวแล้ว

---

## Hardware Setup

### Pinout Diagram

```
                  ┌──── 10kΩ ──── +3.3V        (RESET pullup)
                  │
ESP32 GPIO 4 ─────┴──── MCP RESET (pin 18)

ESP32 +3.3V    ──→ MCP VCC  (pin 9)            ⚠️ ห้ามใช้ +5V
ESP32 GND      ──→ MCP VSS  (pin 10)
ESP32 GPIO 8   ──→ MCP SDA  (pin 13)           shared with BH1750
ESP32 GPIO 9   ──→ MCP SCL  (pin 12)           shared with BH1750
GND            ──→ MCP A0/A1/A2 (pins 15-17)   → I2C address = 0x20
                   MCP INTA, INTB              → ปล่อยลอย (ไม่ใช้)

MCP GPA0..GPA7 ──→ Relay Module IN1..IN8       (ตามตารางด้านล่าง)
```

### Pin Mapping ของ Output

| MCP Pin | Logical Output | Relay หน้าที่ | เดิม (ESP32 GPIO) |
|---------|---------------|---------------|-------------------|
| GPA0 | `GPIO_OUT_PUMP_NUTRIENT_A` | ปั๊มปุ๋ย A | 10 |
| GPA1 | `GPIO_OUT_PUMP_NUTRIENT_B` | ปั๊มปุ๋ย B | 11 |
| GPA2 | `GPIO_OUT_LIGHT_RELAY` | ไฟปลูกพืช | 12 |
| GPA3 | `GPIO_OUT_PUMP_CIRCULATION` | ปั๊มหมุนเวียนน้ำ | 16 |
| GPA4 | `GPIO_OUT_FISH_FEEDER` | เครื่องให้อาหารปลา | 21 |
| GPA5 | `GPIO_OUT_REFILL_ROUTE_VALVE` | โซลินอยด์เลือกเส้นทาง | 39 |
| GPA6 | `GPIO_OUT_PUMP_REFILL` | ปั๊มเติมน้ำเข้าตู้ปลา | 42 |
| GPA7 | `GPIO_OUT_EXHAUST_FAN` | พัดลมระบายอากาศ | 2 |
| GPB0-GPB7 | (spare) | สำหรับขยายระบบในอนาคต | - |

### ⚠️ ข้อควรระวังเรื่องไฟ

- **ใช้ VCC = 3.3V เท่านั้น** ห้ามใช้ 5V เพราะ:
  - บอร์ด CJMCU-2317 มี I2C pullup resistors บนบอร์ดไป VCC โดยตรง
  - ถ้า VCC = 5V → I2C bus ถูกดึงไป 5V → ตี ESP32 GPIO ที่รับได้แค่ 3.6V → **ESP32 พังได้**
- ตรวจสอบว่า relay module รองรับ control signal 3.3V (ส่วนใหญ่ optoisolator relay module รับได้ทั้ง 3.3V/5V)

### Wiring Checklist (ก่อนจ่ายไฟครั้งแรก)

ใช้ multimeter ตรวจ:

```
□ VCC ↔ +3.3V       (continuity)
□ VSS ↔ GND         (continuity)
□ A0, A1, A2 ↔ GND  (ทั้ง 3 ขา → 0x20)
□ RST ↔ +3.3V       ผ่าน 10kΩ (วัดได้ ~10kΩ ไม่ใช่ 0Ω)
□ RST ↔ GPIO 4      (continuity)
□ SDA ↔ GPIO 8      (continuity)
□ SCL ↔ GPIO 9      (continuity)
□ GPA0-7 ↔ Relay IN1-8  ตรงตามตาราง pin mapping
□ ไม่มีสาย short ระหว่าง VCC ↔ GND
```

---

## Software Architecture

### Abstraction Layer

ทุก output ของระบบเรียกผ่าน API เดียว ไม่ผูกกับ pin number ตรงๆ:

```cpp
#include "gpioOut.h"

gpioOutSetup();                              // call ครั้งเดียวใน setup()
gpioOutWrite(GPIO_OUT_LIGHT_RELAY, true);    // เปิดไฟ
gpioOutWrite(GPIO_OUT_LIGHT_RELAY, false);   // ปิดไฟ
```

API จะ route ไป ESP32 GPIO หรือ MCP23017 ตาม per-output flag ใน `config.h`:

```c
#define OUT_USE_MCP_LIGHT_RELAY  0   // 0 = ESP32 GPIO, 1 = MCP23017
```

### ลำดับ init ของ output

```
ESP32 boot
  ↓
setup()
  ↓
gpioOutSetup()  ← เริ่มเร็วที่สุด เพื่อให้ relay default OFF
  ├─ Reset MCP23017 (ถ้ามี output ที่ใช้ MCP)
  ├─ ตั้งทุก ESP32 output pin = HIGH (relay OFF)
  └─ ตั้งทุก MCP pin = HIGH (relay OFF)
  ↓
controllers init (lightCtrlSetup, automatorSetup, etc.)
  ↓
loop() ปกติ
```

### ความเข้ากันได้ย้อนหลัง

ตอนนี้ทุก `OUT_USE_MCP_*` flag = 0 → ระบบ **ใช้ ESP32 GPIO เหมือนเดิมทุกประการ** ไม่ต้องการ MCP23017 ก็รันได้ Migration ไป MCP ทำได้ทีละโมดูล โดย flip flag → flash → ทดสอบ

---

## Testing Procedure (4 Phase)

### 📋 Phase 1: Pre-flight Inspection

ก่อนจ่ายไฟ ใช้ multimeter ตรวจตาม Wiring Checklist ด้านบน หากผ่านครบ → ไป Phase 2

### 🔍 Phase 2: I2C Detection

หลัง wire เสร็จ → **ยังไม่ flash firmware ใหม่** เพื่อ verify hardware ก่อน

```bash
# 1. จ่ายไฟ ESP32 + MCP
# 2. เปิด Serial monitor (115200) หรือ Telnet
# 3. พิมพ์ command:
test
```

ดูที่ section `─── 3. I2C BUS SCAN ───`

**ผลลัพธ์ที่ถูกต้อง:**
```
0x20 : Unknown ✅           ← MCP23017
0x23 : BH1750 (Light) ✅
Total: 2 device(s) found
```

| ผลลัพธ์ | สาเหตุ | วิธีแก้ |
|---------|-------|---------|
| ไม่เจอ 0x20 | A0/A1/A2 ลอย, VCC ผิด, สายขาด | ตรวจ wiring ทั้งหมด |
| เจอที่ 0x21 / 0x22 / 0x24 | A0/A1/A2 บางขาไม่ลง GND | บัดกรี/ผูก A0-A2 → GND ให้แน่น |
| เจอ 0x20 แต่หาย/หาเจอบ้าง | ไฟไม่นิ่ง, สาย I2C ยาวเกิน | shorten สาย, decoupling cap 100nF |

### 🔌 Phase 3: Migrate ทีละโมดูล

**ลำดับตาม risk: ต่ำ → สูง**

> **สถานะ migration ปัจจุบัน (2026-05-31):** 7/8 outputs ใช้ MCP23017 แล้ว เหลือเฉพาะ Fish Feeder ที่ยัง paused (`OUT_USE_MCP_FISH_FEEDER = 0`)

| # | Output | ความเสี่ยงถ้าทำงานผิด | สถานะ |
|---|--------|----------------------|-------|
| 1 | Light | ต่ำ (ไฟผิดเวลาไม่อันตราย) | ✅ MCP |
| 2 | Fan | ต่ำ | ✅ MCP |
| 3 | Fish Feeder | กลาง (ปลาอด/อิ่มเกิน 1 มื้อ) | ⏸️ ESP32 GPIO (paused) |
| 4 | Route Valve | กลาง (น้ำไปทางผิด) | ✅ MCP |
| 5 | Circulation Pump | กลาง-สูง (ขาด O2 ในตู้) | ✅ MCP |
| 6 | Refill Pump | สูง (เติมเกิน → ตู้ล้น) | ✅ MCP |
| 7 | Pump Nutrient A | สูงสุด (overdose ปุ๋ย) | ✅ MCP |
| 8 | Pump Nutrient B | สูงสุด | ✅ MCP |

#### ขั้นตอนต่อโมดูล (ใช้ Light เป็นตัวอย่าง)

**Step 1: แก้ flag ใน `include/config.h`**

```c
#define OUT_USE_MCP_LIGHT_RELAY  1    // เปลี่ยนจาก 0 → 1
```

**Step 2: Build + Flash**

```bash
pio run -e production --target upload
```

**Step 3: ดู boot log**

```bash
pio device monitor --baud 115200
```

ต้องเห็น:
```
[GPIO_OUT] MCP23017 initialized at 0x20
```

ห้ามเห็น:
```
[GPIO_OUT] MCP23017 not responding at 0x20
```

**Step 4: ทดสอบจาก Serial CLI**

```
light on    → relay ที่ต่อ MCP GPA2 ต้อง click + LED ติด
light off   → relay ปิด + LED ดับ
light auto  → กลับโหมด schedule (เลิกทดสอบ)
```

**Step 5: ทดสอบจากหน้าเว็บ**

ไปหน้า `/hwtest` → กด "Light ON" / "Light OFF" → ดูว่า relay click ตรงเวลา

**Step 6: Soak test ระยะสั้น**

ปล่อยรัน 30 นาที → ตรวจ:
- ไม่มี relay click ผิดปกติ
- `health` command → heap ไม่ลด
- ไม่มี WDT reset

✅ **ผ่านครบ → ไป output ตัวถัดไป**
❌ **ไม่ผ่าน** → flip flag กลับเป็น 0, flash, debug

#### ทำซ้ำสำหรับ output ตัวอื่น

ทำเหมือนกันสำหรับ Fan → Feeder → Route Valve → Circulation → Refill → Pump A → Pump B

**Flag ที่ต้องเปลี่ยน:**
```c
#define OUT_USE_MCP_EXHAUST_FAN          1
#define OUT_USE_MCP_FISH_FEEDER          1
#define OUT_USE_MCP_REFILL_ROUTE_VALVE   1
#define OUT_USE_MCP_PUMP_CIRCULATION     1
#define OUT_USE_MCP_PUMP_REFILL          1
#define OUT_USE_MCP_PUMP_NUTRIENT_A      1
#define OUT_USE_MCP_PUMP_NUTRIENT_B      1
```

### 🧪 Phase 4: Final Soak Test

หลัง migrate ครบ 8 ตัว:

```
1. Reboot ESP32 → boot log "MCP23017 initialized" สำเร็จ
2. ทดสอบทุก HW Test button → relay click ครบ
3. รัน Flow Test (Circulation / Fish Route / Sump Route / Stop)
4. ปล่อยระบบ auto mode 8-12 ชั่วโมง
5. เช้าวันถัดมาตรวจ:
   □ ไม่มี relay click ผิดเวลา
   □ ไม่มี WDT reset (`crash` command)
   □ Heap stable (`health`)
   □ MCP healthy (ดูจาก gpioOutMcpHealthy() ใน serial log)
6. (optional) ลบ pin defines เก่าใน config.h
7. Update CHANGELOG.md
```

---

## Troubleshooting

### MCP ไม่ตอบใน I2C scan

| Symptom | สาเหตุที่เป็นไปได้ | วิธีแก้ |
|---------|------------------|---------|
| ไม่เจอ device ใดเลย | I2C bus ตาย, SDA/SCL ผิดข้าง, ไม่มี pullup | ตรวจ Wire.begin() ทำงาน, BH1750 ยังเจอไหม |
| เจอแค่ BH1750 | MCP wire ผิด, VCC ผิด, A0-A2 ลอย | ดู Phase 2 troubleshooting |
| เจอ MCP แต่ relay ไม่ click | Relay control side ไม่ต่อกับ GPA0-7 | ตรวจสาย GPA → Relay IN |

### Relay click ตอน boot ทุกครั้ง

- เกิดจาก MCP outputs floating ก่อน firmware init
- ตรวจ `gpioOutSetup()` ถูกเรียกใน setup() ตั้งแต่ต้น (ก่อน WiFi/sensor init)
- ถ้ายังเป็น → relay module ไม่มี internal pullup → ติด external pullup 10kΩ ทุก output ของ MCP → +3.3V

### MCP hang ระหว่างใช้งาน

**Recovery จาก software:**
- `gpioOutMcpReinit()` จะ toggle RESET pin → re-init MCP → re-apply state
- เพิ่มได้ใน health check loop ถ้า `gpioOutMcpHealthy()` คืน false

**Recovery จาก hardware:**
- Power cycle ทั้งบอร์ด

### กลับไปใช้ ESP32 GPIO ทั้งหมด (Rollback)

ถ้า MCP เสีย หรือต้อง debug:

```bash
# 1. แก้ทุก flag กลับเป็น 0 ใน config.h
#    หรือ git checkout HEAD~N -- include/config.h
# 2. Re-flash
pio run -e production --target upload
# ระบบกลับไปใช้ ESP32 GPIO ทันที (ไม่ต้องถอด MCP ออก)
```

---

## Reference

### ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | หน้าที่ |
|------|--------|
| `include/config.h` | MCP_PIN_*, MCP23017_I2C_ADDR, OUT_USE_MCP_* flags |
| `include/gpioOut.h` | Public API (`gpioOutSetup`, `gpioOutWrite`, etc.) |
| `src/gpioOut.cpp` | Implementation (dual-mode routing) |
| `src/main.cpp` | เรียก `gpioOutSetup()` ใน `setup()` |
| `platformio.ini` | Library `Adafruit MCP23017 Arduino Library@^2.3.2` |

### CLI Commands ที่เกี่ยวข้อง

| Command | หน้าที่ |
|---------|--------|
| `test` | I2C scan (ดู MCP ตอบที่ 0x20 ไหม) |
| `light on/off` | ทดสอบ Light Relay output |
| `fan on/off` | ทดสอบ Fan output |
| `feed now` | ทดสอบ Fish Feeder |
| `pump a` / `pump b` | ทดสอบ dose pumps (auto-stop หลัง ~2 วินาที) |
| `circ on/off` | ทดสอบ Circulation pump |
| `refill on/off` | ทดสอบ Refill pump (manual mode) |
| `route fish/sump` | ทดสอบ Route valve |

### Datasheet & References

- [Microchip MCP23017 Datasheet](https://www.microchip.com/wwwproducts/en/MCP23017)
- [Adafruit MCP23X17 Arduino Library](https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library)
- บอร์ด CJMCU-2317: Shopee/Aliexpress (~60-70 บาท)

---

## Migration History

ดู `CHANGELOG.md` หัวข้อ "MCP23017 Output Abstraction Layer" สำหรับรายละเอียด refactoring ที่เกิดขึ้น

ก่อน migration:
- 8 output pins ใช้ ESP32 GPIO ตรงๆ
- `digitalWrite(PUMP_xxx_PIN, PUMP_ON)` กระจายใน 7 ไฟล์

หลัง migration (โครงปัจจุบัน):
- 8 output pins ผ่าน `gpioOut` abstraction
- `gpioOutWrite(GPIO_OUT_xxx, true/false)` per-output flag เลือก ESP32 GPIO หรือ MCP
- รองรับ migrate ทีละโมดูล + rollback ได้

หลัง verify ครบ:
- ลบ pin defines เดิม → MCP-only mode
