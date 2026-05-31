#include <cstddef>

size_t telnetPrintfNonBlocking(const char*, ...) { return 0; }
void localMqttPublishLog(const char*) {}
extern "C" {
void setUp(void) {}
void tearDown(void) {}
}

#include "../test_native/test_dht_native.cpp"