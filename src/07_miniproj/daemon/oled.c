#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "ssd1306.h"

#define TEMP_POSITION 0,3
#define FREQ_POSITION 0,4
#define MODE_POSITION 0,5

#define OLED_WIDTH 16
#define OLED_HEIGHT 8

void oled_init() {
    ssd1306_init();
}

void oled_close() {
    ssd1306_clear_display();
}

void oled_format_line(char* buf, size_t max_len, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    // fill with space
    memset(buf, ' ', OLED_WIDTH);
    int size = vsnprintf(buf, max_len, fmt, args);
    // remove the \0
    buf[size] = ' ';
    // end the string
    buf[OLED_WIDTH] = '\0';

    va_end(args);
}

void oled_set_freq(const char* freq) {
    char buf[100] = {0};
    oled_format_line(buf, sizeof(buf)-1, "Freq: %sHz", freq);

    ssd1306_set_position (FREQ_POSITION);
    ssd1306_puts(buf);
}

void oled_set_mode(const char* mode) {
    char buf[100] = {0};
    oled_format_line(buf, sizeof(buf)-1, "Mode: %s", mode);

    ssd1306_set_position (MODE_POSITION);
    ssd1306_puts(buf);
}

void oled_set_temperature(const char* temp) {
    char buf[100] = {0};
    oled_format_line(buf, sizeof(buf)-1, "Temp: %s'C", temp);
    
    ssd1306_set_position(TEMP_POSITION);
    ssd1306_puts(buf);
}



void oled_header() {
    ssd1306_set_position (0,0);
    ssd1306_puts(" CSEL1a - FAN");
    ssd1306_set_position (0,1);
    ssd1306_puts(" Luca & Louka");
    ssd1306_set_position (0,2);
    ssd1306_puts("--------------");
    ssd1306_set_position (0,OLED_HEIGHT-1);
    ssd1306_puts("--------------");
}

void oled_display_all() {
    oled_header();

    oled_set_temperature("0");
    
    oled_set_freq("0");
    
    oled_set_mode("auto");
}