/**
 * @file localMqtt.h
 * @brief Local MQTT Module for Raspberry Pi connection
 * @details Handles MQTT connection to local Mosquitto broker using mDNS
 */

#ifndef LOCAL_MQTT_H
#define LOCAL_MQTT_H

#include <Arduino.h>

/**
 * @brief Initialize Local MQTT and mDNS
 * @note Call in setup()
 */
void localMqttSetup(void);

/**
 * @brief Handle MQTT loop and reconnection
 * @note Call in loop()
 */
void localMqttLoop(void);

/**
 * @brief Publish sensor data to Local MQTT
 * @param waterTemp Water Temperature (°C)
 * @param airTemp Air Temperature (°C)
 * @param humidity Humidity (%)
 * @param tds TDS Value (ppm)
 * @param light Light Intensity (lux)
 * @param ph pH Value (0-14)
 */
void localMqttPublishData(float waterTemp, float airTemp, float humidity,  float tds, float light, float ph);

/**
 * @brief Check if connected to Local MQTT
 */
bool localMqttIsConnected(void);

void localMqttPublishLog(const char* logMsg);

#endif // LOCAL_MQTT_H
