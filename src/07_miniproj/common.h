
#define MQ_MAX_MSG 10
#define MQ_MAX_SIZE sizeof(struct fan_msg)+1
#define MQ_NAME "/fan"

typedef enum {
    FAN_MSG_MODE,
    FAN_MSG_FREQUENCY,
} fan_msg_t;

struct fan_msg {
    fan_msg_t msg_type;
    char data[100];
};