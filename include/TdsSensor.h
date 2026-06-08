/**
 * @file TdsSensor.h
 * @brief ไลบรารีสำหรับเซ็นเซอร์วัดค่า TDS (Total Dissolved Solids)
 * @details ใช้วัดปริมาณของแข็งละลายน้ำ หน่วย ppm
 */

#ifndef TDS_SENSOR_H
#define TDS_SENSOR_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

typedef enum {
    TDS_CHANNEL_MIX = 0,
    TDS_CHANNEL_FISH,
    TDS_CHANNEL_COUNT
} TdsChannel;

/**
 * @brief เริ่มต้นการทำงานของ TDS Sensor
 * @note เรียกใช้ใน setup()
 */
void tdsSetup(void);

/**
 * @brief อ่านค่า TDS พร้อมชดเชยอุณหภูมิ
 * @param temperature อุณหภูมิปัจจุบัน (°C) สำหรับชดเชยค่า
 * @return ค่า TDS ในหน่วย ppm
 */
float tdsRead(float temperature);

/**
 * @brief ฟังก์ชันหลักสำหรับเรียกใน loop()
 * @param temperature อุณหภูมิปัจจุบัน (°C)
 * @note ใช้ non-blocking delay อ่านค่าทุก TDS_READ_INTERVAL ms
 */
void tdsLoop(float temperature);
void tdsLoopChannels(float mixTemperature, float fishTemperature);

/**
 * @brief ตรวจสอบว่าเก็บ sample ครบแล้วหรือยัง
 * @return true ถ้าพร้อมแสดงผล, false ถ้ายังเก็บไม่ครบ
 */
bool tdsIsReady(void);
bool tdsIsReadyForChannel(TdsChannel channel);

/**
 * @brief อ่าน voltage ปัจจุบันของ TDS sensor (สำหรับ calibration)
 * @return voltage ในหน่วย V
 */
float tdsGetVoltage(void);
float tdsGetVoltageForChannel(TdsChannel channel);

/**
 * @brief ตั้งค่า 2-Point Calibration
 * @param lowPpm ค่า ppm ของน้ำยามาตรฐานจุดต่ำ
 * @param lowVoltage voltage ที่อ่านได้ที่จุดต่ำ
 * @param lowTemperature อุณหภูมิของน้ำยามาตรฐานจุดต่ำ (°C)
 * @param highPpm ค่า ppm ของน้ำยามาตรฐานจุดสูง
 * @param highVoltage voltage ที่อ่านได้ที่จุดสูง
 * @param highTemperature อุณหภูมิของน้ำยามาตรฐานจุดสูง (°C)
 */
bool tdsSetCalibration(float lowPpm,
					   float lowVoltage,
					   float lowTemperature,
					   float highPpm,
					   float highVoltage,
					   float highTemperature,
					   bool rawVoltageInput);
bool tdsSetCalibrationForChannel(TdsChannel channel,
								 float lowPpm,
								 float lowVoltage,
								 float lowTemperature,
								 float highPpm,
								 float highVoltage,
								 float highTemperature,
								 bool rawVoltageInput);

/**
 * @brief ตรวจสอบว่า sensor ได้รับการ calibrate แล้วหรือยัง
 * @return true ถ้า calibrated
 */
bool tdsIsCalibrated(void);
bool tdsIsCalibratedForChannel(TdsChannel channel);

/**
 * @brief ดึงค่า TDS ล่าสุดที่คำนวณแล้วจาก tdsLoop() โดยไม่ทำ analogRead ซ้ำ
 * @return ค่า TDS ในหน่วย ppm หรือ -1 ถ้ายังไม่พร้อม
 */
float tdsGetLastValue(void);
float tdsGetLastValueForChannel(TdsChannel channel);

/**
 * @brief ตรวจว่า K จาก cal อยู่ในช่วงที่เชื่อถือได้ (0 < K <= TDS_CAL_K_REJECT_MAX)
 */
bool tdsIsCalibrationQualityOkForChannel(TdsChannel channel);

/**
 * @brief ดึงค่า K จาก cal ปัจจุบัน (-1 ถ้ายังไม่ cal)
 */
float tdsGetCalibrationKForChannel(TdsChannel channel);

/**
 * @brief ตรวจว่าแรงดันล่าสุดต่ำกว่าจุด cal ต่ำ (เสี่ยง ppm ติด 0 / probe แห้ง)
 */
bool tdsIsVoltageBelowCalibrationRangeForChannel(TdsChannel channel);

#endif // TDS_SENSOR_H