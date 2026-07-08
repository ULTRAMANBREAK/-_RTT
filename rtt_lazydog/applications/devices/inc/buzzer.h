#ifndef APPLICATIONS_BUZZER_H__
#define APPLICATIONS_BUZZER_H__

#include <rtthread.h>

typedef enum
{
    BUZZER_PATTERN_OFF = 0,
    BUZZER_PATTERN_ON,
    BUZZER_PATTERN_TURN,
    BUZZER_PATTERN_FALL,
    BUZZER_PATTERN_WARN,
} buzzer_pattern_t;

int buzzer_init(void);
rt_err_t buzzer_set_pattern(buzzer_pattern_t pattern);
const char *buzzer_pattern_name(buzzer_pattern_t pattern);

#endif
