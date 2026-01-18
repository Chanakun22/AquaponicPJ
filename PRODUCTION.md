# Production Deployment Guide

## Pre-Deployment Checklist

### 1. Configuration

- [ ] Copy `include/secrets.h.example` to `include/secrets.h`
- [ ] Fill in NETPIE credentials
- [ ] Set WiFi AP name and password
- [ ] Verify pin configurations match hardware
- [ ] Set log level to `LOG_LEVEL_INFO` for production

### 2. Build Configuration

- [ ] Verify `platformio.ini` board settings
- [ ] Check library versions are stable
- [ ] Enable OTA if needed
- [ ] Set appropriate watchdog timeout

### 3. Testing

- [ ] Test all sensors individually
- [ ] Test WiFi connection and reconnection
- [ ] Test NETPIE MQTT connection
- [ ] Test pH calibration
- [ ] Test light schedule
- [ ] Test factory reset
- [ ] Test OTA update (if enabled)
- [ ] Run for 24+ hours stability test

## Deployment Steps

### Step 1: Build Firmware

```bash
pio run
```

### Step 2: Upload via USB

```bash
pio run -t upload
pio device monitor
```

### Step 3: Initial Configuration

1. ESP32 will create WiFi AP
2. Connect to AP and configure WiFi
3. Verify connection to NETPIE
4. Calibrate pH sensor

### Step 4: Enable OTA (Optional)

After initial WiFi setup, future updates can use OTA:

```bash
pio run -t upload --upload-port <ESP32_IP>
```

## Production Monitoring

### Key Metrics to Monitor

1. **Uptime** - Should be stable (no unexpected resets)
2. **Free Heap** - Should stay above 20KB
3. **Reconnects** - WiFi/MQTT reconnects should be minimal
4. **Sensor Readings** - Values should be within expected ranges

### Logging in Production

Set log level to `LOG_LEVEL_INFO` in `config.h`:

```cpp
#define LOG_LEVEL LOG_LEVEL_INFO  // Production
```

This will:
- Show errors and warnings
- Show important information
- Hide debug messages (reduce serial traffic)

### Remote Monitoring

Monitor system health via NETPIE:
- Check `health` topic for system status
- Monitor sensor data for anomalies
- Set up alerts for disconnections

## Maintenance

### Regular Tasks

1. **Weekly:**
   - Check system health via Serial or NETPIE
   - Verify sensor readings are reasonable
   - Check for firmware updates

2. **Monthly:**
   - Clean pH probe
   - Verify calibration accuracy
   - Review error logs

3. **Quarterly:**
   - Recalibrate pH sensor
   - Check hardware connections
   - Update firmware if available

### Troubleshooting Production Issues

**System Keeps Restarting:**
- Check watchdog timeout
- Review error logs
- Verify power supply stability

**Sensors Reading Incorrectly:**
- Recalibrate pH sensor
- Check sensor connections
- Verify sensor health

**WiFi/MQTT Disconnections:**
- Check network stability
- Review reconnect counts
- Verify credentials

**Memory Issues:**
- Monitor free heap
- Restart if consistently low
- Review code for memory leaks

## Version Management

Current Version: **2.3.0**

Version format: `MAJOR.MINOR.PATCH`

- **MAJOR** - Breaking changes
- **MINOR** - New features
- **PATCH** - Bug fixes

Check version via Serial:
```
health
```

Or in code:
```cpp
systemGetVersion();  // Returns "2.3.0"
```

## Security Considerations

1. **Credentials:**
   - Never commit `secrets.h` to git
   - Use strong WiFi passwords
   - Rotate NETPIE credentials periodically

2. **OTA:**
   - Set OTA password if enabled
   - Use secure WiFi network
   - Verify firmware signatures

3. **Network:**
   - Use WPA2/WPA3 WiFi
   - Consider VPN for remote access
   - Monitor for unauthorized access

## Backup & Recovery

### Backup Configuration

Save these files:
- `include/secrets.h` (credentials)
- pH calibration values (via Serial `ph` command)
- Light schedule settings

### Recovery Procedure

1. Factory reset (BOOT button 5 seconds)
2. Reconfigure WiFi
3. Recalibrate pH sensor
4. Restore light schedule via NETPIE

## Support

For issues or questions:
1. Check Serial logs (`LOG_LEVEL_DEBUG`)
2. Review system health (`health` command)
3. Check NETPIE connection status
4. Verify hardware connections

---

**Last Updated:** 2024
**Firmware Version:** 2.3.0
