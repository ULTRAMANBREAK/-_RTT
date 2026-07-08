#ifndef APPLICATIONS_LD2451_PARSER_H__
#define APPLICATIONS_LD2451_PARSER_H__

#include <rtthread.h>

void ld2451_parser_reset(rt_uint8_t channel);
void ld2451_parser_input(rt_uint8_t channel, const rt_uint8_t *buf, rt_size_t len);

#endif
