#ifndef CONTROL_SOURCE_H
#define CONTROL_SOURCE_H

typedef enum {
    COMMAND_SOURCE_NETPIE = 0,
    COMMAND_SOURCE_LOCAL_WEB = 1
} CommandSource;

static inline const char* commandSourceToString(CommandSource source) {
    switch (source) {
        case COMMAND_SOURCE_NETPIE:
            return "netpie";
        case COMMAND_SOURCE_LOCAL_WEB:
            return "local_web";
        default:
            return "unknown";
    }
}

#endif