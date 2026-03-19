#include <unity.h>
#include <ArduinoFake.h>

using namespace fakeit;

// ตัวอย่างฟังก์ชันที่เราต้องการทดสอบ (ในงานจริงคุณจะ include ไฟล์จาก src)
float calculatePhFromVoltage(int adcValue) {
    if (adcValue < 10 || adcValue > 4085) return -1.0; // Error case
    return 7.0 + ((2048 - adcValue) * (3.3 / 4096.0) * 10.0); // ตัวอย่างสูตรคำนวณ
}

void test_ph_sensor_mock() {
    // 1. จำลอง (Mock) ว่า analogRead คืนค่า 2048
    When(Method(ArduinoFake(), analogRead)).Return(2048);
    
    // 2. เรียกใช้งานฟังก์ชันที่ใช้ analogRead (สมมติว่าเป็นฟังก์ชันในระบบของคุณ)
    int val = analogRead(6); // PH_SENSOR_PIN
    float ph = calculatePhFromVoltage(val);
    
    // 3. ตรวจสอบผลลัพธ์ (Assertion)
    TEST_ASSERT_EQUAL_INT(2048, val);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 7.0, ph);
}

void test_sensor_error_handling() {
    // จำลองว่าสายหลุด (ADC = 0)
    When(Method(ArduinoFake(), analogRead)).Return(0);
    
    float ph = calculatePhFromVoltage(analogRead(6));
    
    // ต้องได้ -1.0 ตาม Logic ที่เราเขียนไว้
    TEST_ASSERT_EQUAL_FLOAT(-1.0, ph);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ph_sensor_mock);
    RUN_TEST(test_sensor_error_handling);
    return UNITY_END();
}
