/**
 * @file TdsSensor.cpp
 * @brief Implementation สำหรับ TDS Sensor
 */

#include "TdsSensor.h"
#include "logger.h"

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static unsigned long _tdsLastReadTime = 0;
static int _tdsBuffer[TDS_SAMPLE_COUNT];
static int _tdsBufferIndex = 0;
static int _tdsSampleCollected = 0;  // จำนวน sample ที่เก็บได้
static bool _tdsReady = false;       // flag บอกว่าเก็บ sample ครบหรือยัง

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief คำนวณหาค่ากลาง (Median) จากอาเรย์
 * @param bArray อาเรย์ข้อมูล
 * @param iFilterLen ขนาดอาเรย์
 * @return ค่ากลาง
 */
static int _getMedian(int bArray[], int iFilterLen) {
    int bTab[iFilterLen];
    
    // คัดลอกข้อมูล
    for (int i = 0; i < iFilterLen; i++) {
        bTab[i] = bArray[i];
    }
    
    // เรียงลำดับจากน้อยไปมาก (Bubble Sort)
    for (int j = 0; j < iFilterLen - 1; j++) {
        for (int i = 0; i < iFilterLen - j - 1; i++) {
            if (bTab[i] > bTab[i + 1]) {
                int temp = bTab[i];
                bTab[i] = bTab[i + 1];
                bTab[i + 1] = temp;
            }
        }
    }
    
    // คืนค่ากลาง
    if ((iFilterLen & 1) > 0) {
        return bTab[(iFilterLen - 1) / 2];
    } else {
        return (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void tdsSetup(void) {
    pinMode(TDS_PIN, INPUT);
    
    // เริ่มต้นค่า buffer ให้เป็น 0
    for (int i = 0; i < TDS_SAMPLE_COUNT; i++) {
        _tdsBuffer[i] = 0;
    }
    _tdsBufferIndex = 0;
    _tdsSampleCollected = 0;
    _tdsReady = false;
    
    LOG_INFO("TDS sensor initialized, collecting %d samples...", TDS_SAMPLE_COUNT);
}

float tdsRead(float temperature) {
    // อ่านค่า ADC เก็บลง Buffer
    _tdsBuffer[_tdsBufferIndex] = analogRead(TDS_PIN);
    _tdsBufferIndex++;
    
    // นับจำนวน sample ที่เก็บได้
    if (!_tdsReady) {
        _tdsSampleCollected++;
        if (_tdsSampleCollected >= TDS_SAMPLE_COUNT) {
            _tdsReady = true;
            LOG_INFO("TDS buffer ready! Starting measurements...");
        }
    }
    
    // วน buffer กลับมาเริ่มต้น
    if (_tdsBufferIndex >= TDS_SAMPLE_COUNT) {
        _tdsBufferIndex = 0;
    }
    
    // ถ้ายังเก็บ sample ไม่ครบ ให้คืน -1
    if (!_tdsReady) {
        return -1.0f;
    }
    
    // คัดลอกไปใส่ตัวแปรชั่วคราวเพื่อคำนวณ
    int tempBuffer[TDS_SAMPLE_COUNT];
    for (int i = 0; i < TDS_SAMPLE_COUNT; i++) {
        tempBuffer[i] = _tdsBuffer[i];
    }
    
    // แปลงค่า ADC เฉลี่ย (Median) เป็น Voltage
    float averageVoltage = _getMedian(tempBuffer, TDS_SAMPLE_COUNT) * TDS_VREF / TDS_ADC_RESOLUTION;
    
    // ชดเชยอุณหภูมิ (Temperature Compensation)
    float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    float compensationVoltage = averageVoltage / compensationCoefficient;
    
    // สูตรแปลง Voltage เป็น TDS ppm
    float tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage 
                    - 255.86 * compensationVoltage * compensationVoltage 
                    + 857.39 * compensationVoltage) * 0.5;
    
    return tdsValue;
}

bool tdsIsReady(void) {
    return _tdsReady;
}

void tdsLoop(float temperature) {
    // ตรวจสอบเวลา (Non-blocking delay)
    if (millis() - _tdsLastReadTime >= TDS_READ_INTERVAL) {
        _tdsLastReadTime = millis();
        
        float tds = tdsRead(temperature);
        
        // แสดงผลเฉพาะเมื่อเก็บ sample ครบแล้ว
        if (_tdsReady) {
            // Serial.print(F("[TDS] Value: "));
            // Serial.print(tds, 1);
            // Serial.println(F(" ppm"));
        } else {
            // Serial.print(F("[TDS] Collecting samples: "));
            // Serial.print(_tdsSampleCollected);
            // Serial.print(F("/"));
            // Serial.println(TDS_SAMPLE_COUNT);
        }
    }
}