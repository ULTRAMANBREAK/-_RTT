#ifndef APPLICATIONS_RC522_PARSER_H__
#define APPLICATIONS_RC522_PARSER_H__

#include <rtthread.h>

#include "uart4_app.h"

void rc522_parser_input(rt_uint8_t channel, const rt_uint8_t *buf, rt_size_t len);

#endif
