#include <stdio.h>

#include "ssd1306.h"

#define TEMP_POSITION 0,3
#define FREQ_POSITION 0,4
#define MODE_POSITION 0,5

void oled_init() {
    ssd1306_init();
}

void oled_close() {
    ssd1306_clear_display();
}

void oled_set_freq(const char* freq) {
    char buf[100] = {0};
    snprintf(buf, sizeof(buf)-1, "Freq: %sHz", freq);

    ssd1306_set_position (FREQ_POSITION);
    ssd1306_puts("Freq: 1Hz");
}

void oled_set_mode(const char* mode) {
    char buf[100] = {0};
    snprintf(buf, sizeof(buf)-1, "Mode: %s", mode);
    
    ssd1306_set_position (MODE_POSITION);
    ssd1306_puts(buf);
}

void oled_set_temperature(const char* temp) {
    char buf[100] = {0};
    snprintf(buf, sizeof(buf)-1, "Temp: %s'C", temp);

    ssd1306_set_position(TEMP_POSITION);
    ssd1306_puts(buf);
}



void oled_header() {
    ssd1306_set_position (0,0);
    ssd1306_puts("  CSEL1a - Miniproj");
    ssd1306_set_position (0,1);
    ssd1306_puts("Fan ctrl - SW");
    ssd1306_set_position (0,2);
    ssd1306_puts("--------------");
}

void oled_display_all() {
    oled_header();

    oled_set_temperature("0");
    
    oled_set_freq("0");
    
    oled_set_mode("auto");
}