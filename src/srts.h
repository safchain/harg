/*
 * Copyright (C) 2015 Sylvain Afchain
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef __SRTS_H
#define __SRTS_H

#if defined(__AVR__) || defined(__avr__)
#include "Arduino.h"

#define MAX_GPIO    12
#endif

#ifdef __cplusplus
extern "C"{
#endif

enum CTRL {
  SRTS_UNKNOWN = 0,
  SRTS_MY = 1,
  SRTS_UP,
  SRTS_MY_UP,
  SRTS_DOWN,
  SRTS_MY_DOWN,
  SRTS_UP_DOWN,
  SRTS_PROG = 8,
  SRTS_SUN_FLAG,
  SRTS_FLAG,
};

enum ERRORS {
  SRTS_BAD_GPIO = -1,
  SRTS_BAD_CHECKSUM = -2
};

struct srts_payload {
  unsigned char key;
  unsigned char checksum:4;
  unsigned char ctrl:4;
  unsigned short code;
  struct address {
    unsigned char byte1;
    unsigned char byte2;
    unsigned char byte3;
  } address;
};

int srts_get_code(const char *persistence_path, unsigned short address1,
        unsigned short address2);
void srts_transmit(unsigned int gpio, unsigned char key,
        unsigned short address1, unsigned short address2, unsigned char ctrl,
        unsigned short code, unsigned int repeated);
int srts_receive(unsigned int gpio, unsigned int type, unsigned int duration,
        struct srts_payload *payload);
void srts_get_address(struct srts_payload *payload, unsigned short *address1,
        unsigned short *address2);
const char *srts_get_ctrl_str(unsigned char ctrl);
unsigned char srts_get_ctrl_int(const char *ctrl);

#if !defined(__AVR__) && !defined(__avr__)
void srts_transmit_persist(unsigned int gpio, unsigned char key,
        unsigned short address1, unsigned short address2, unsigned char ctrl,
        unsigned int repeat, const char *persistence_path);
void srts_print_payload(FILE *fp, struct srts_payload *payload);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
