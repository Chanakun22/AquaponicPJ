# Water System Flow (Thai)

เอกสารนี้สรุป flow ล่าสุดของระบบน้ำสำหรับ firmware และหน้า Pi โดยยึดแนวคิดว่า `ถังน้ำผสม` คือ control zone หลัก และใช้น้ำเข้าแยกจาก `ถังน้ำสะอาด`

- `ถังผสม` รับน้ำเข้าผ่านโซลินอยด์ที่ ESP32 ควบคุม
- `ถังน้ำสะอาด` รับน้ำเข้าผ่านโซลินอยด์/ลูกลอยเชิงกลของตัวถังเอง
- ไม่มี flow `ถังน้ำสะอาด -> ถังผสม` ภายในการควบคุมของ firmware อีกแล้ว
- route ผ่าน `ตู้ปลา` และ route valve ถือเป็น legacy concept ที่ไม่ใช้ใน plumbing ชุดนี้แล้ว

## State Machine

สถานะหลักของ firmware:

1. `IDLE` = พร้อมทำงาน
2. `MIX_TANK_REFILL` = เติมถังน้ำผสม
3. `WAIT_REFILL_INTERVAL` = รอช่วงกันเติมถี่
4. `MIX_TANK_SETTLING` = รอให้น้ำในถังผสมนิ่ง
5. `FISH_TANK_REFILL` = legacy state ที่ไม่ควรถูกใช้งานใน plumbing ปัจจุบัน
6. `BLOCKED` = ถูกบล็อก
7. `ALARM` = แจ้งเตือน

## Logic ที่ใช้จริง

### 1. เติมถังน้ำผสมอัตโนมัติ

1. อ่าน `mix tank low` และ `mix tank high`
2. ถ้า `mix tank low` ทำงาน
3. เช็ก `refill_min_interval_ms`
4. ถ้ายังไม่พ้นช่วงกันเติมถี่ -> เข้า `WAIT_REFILL_INTERVAL`
5. ถ้าพ้นช่วงแล้ว -> เปิดโซลินอยด์น้ำเข้าถังผสม
6. ระหว่างเติม -> อยู่ใน `MIX_TANK_REFILL`
7. ถ้า `mix tank high` ทำงาน -> ปิดโซลินอยด์
8. เข้าช่วง `MIX_TANK_SETTLING`
9. ครบเวลาคงตัว -> กลับ `IDLE`

### 2. ถังน้ำสะอาดแยกอิสระจาก firmware

1. ถังน้ำสะอาดรับน้ำเข้าจากระบบลูกลอย/โซลินอยด์เชิงกลของตัวถังเอง
2. ESP32 ไม่ได้สั่ง route ผ่านถังน้ำสะอาดเพื่อไปเติมถังผสม
3. การเติมถังผสมใช้ actuator ของถังผสมโดยตรงเท่านั้น
4. ดังนั้น logic `preferred_route`, `allow_direct_sump_refill`, และ `fish refill` เป็น field legacy เพื่อ compatibility มากกว่าการควบคุมจริง

### 3. เมื่อไรถึงเป็น Alarm จริง

1. เซ็นเซอร์ระดับ low/high ของถังผสมขัดแย้งกัน
2. เติมถังน้ำผสมอัตโนมัตินานเกิน `refill_max_runtime_ms`
3. actuator น้ำเข้าถังผสมค้างจนเกิน `refill_max_runtime_ms`

## Settings ที่ควรมีบนหน้า Pi

1. `circulation_enabled`
2. `refill_enabled`
3. `refill_max_runtime_ms`
4. `refill_min_interval_ms`
5. `preferred_route` = legacy field, ควรถูกตรึงไว้
6. `allow_direct_sump_refill` = legacy field, ไม่ควรใช้ตัดสินใจจริง
7. `fish_refill_interval_ms` = legacy field
8. `fish_refill_max_runtime_ms` = legacy field

## Pseudo-code สำหรับ waterSystem.cpp

```text
อ่าน sensor ทุกตัว

ถ้า sump low/high ขัดแย้งกัน:
    เข้า alarm

ถ้า manual_refill เปิดอยู่:
    state = MIX_TANK_REFILL
    เปิดโซลินอยด์น้ำเข้าถังผสม

ถ้า refill_enabled เปิดอยู่ และ mix tank low ทำงาน:
    ถ้ายังไม่พ้น refill_min_interval_ms:
        state = WAIT_REFILL_INTERVAL
    ไม่เช่นนั้น:
        state = MIX_TANK_REFILL
        เปิดโซลินอยด์น้ำเข้าถังผสม
        ถ้าครบ refill_max_runtime_ms:
            เข้า alarm

ถ้าหยุดเติมแล้ว:
    state = MIX_TANK_SETTLING

ถ้าไม่มีงานเติมและไม่มี alarm:
    state = IDLE
```