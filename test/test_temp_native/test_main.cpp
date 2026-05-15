#include <cstddef>

size_t telnetPrintfNonBlocking(const char*, ...) { return 0; }
void localMqttPublishLog(const char*) {}
void setUp(void) {}
void tearDown(void) {}

#include "../test_native/test_temp_native.cpp"