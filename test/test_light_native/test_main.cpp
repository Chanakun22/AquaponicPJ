#include <cstddef>

size_t telnetPrintfNonBlocking(const char*, ...) { return 0; }
void localMqttPublishLog(const char*) {}
// setUp and tearDown are implemented in test_light_native.cpp to reset static state

#include "../test_native/test_light_native.cpp"