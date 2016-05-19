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

#if !defined(__AVR__) && !defined(__avr__)
#include <stdio.h>
#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "gpio.h"
#endif

#include "homeasy.h"

#define IS_ON_TIME(X, I, A) X >= I && X <= A

static void _write_bit(unsigned int gpio, unsigned char bit)
{
  if (bit) {
    digitalWrite(gpio, HIGH);
    delayMicroseconds(300);
    digitalWrite(gpio, LOW);
    delayMicroseconds(1300);
  } else {
    digitalWrite(gpio, HIGH);
    delayMicroseconds(300);
    digitalWrite(gpio, LOW);
    delayMicroseconds(300);
  }
}

static void write_bit(unsigned int gpio, unsigned char bit)
{
  if (bit) {
    _write_bit(gpio, 1);
    _write_bit(gpio, 0);
  } else {
    _write_bit(gpio, 0);
    _write_bit(gpio, 1);
  }
}

static void sync_transmit(unsigned int gpio)
{
  digitalWrite(gpio, HIGH);
  delayMicroseconds(275);
  digitalWrite(gpio, LOW);
  delayMicroseconds(10200);
  digitalWrite(gpio, HIGH);
  delayMicroseconds(275);
  digitalWrite(gpio, LOW);
  delayMicroseconds(2600);
}

static void write_interval_gap(unsigned int gpio)
{
  digitalWrite(gpio, HIGH);
  delayMicroseconds(275);
  digitalWrite(gpio, LOW);
  delayMicroseconds(10000);
}

void write_address(unsigned int gpio, unsigned short address, int len) {
  unsigned int mask;

  for (mask = 1 << len; mask != 0x0; mask >>= 1) {
    if (address & mask) {
      write_bit(gpio, 1);
    } else {
      write_bit(gpio, 0);
    }
  }
}

/* address1 is 16 higher bits of address and address2 is 16 lower bits */
void homeasy_transmit(unsigned int gpio, unsigned short address1,
        unsigned short address2, unsigned char receiver, unsigned char ctrl,
        unsigned char group, unsigned int repeat)
{
  unsigned int mask;
  int i;

  for (i = 0; i != repeat + 1; i++) {
    sync_transmit(gpio);

    write_address(gpio, address1, 9);
    write_address(gpio, address2, 15);

    write_bit(gpio, group);
    write_bit(gpio, ctrl);

    for (mask = 0b1000; mask != 0x0; mask >>= 1) {
      if (receiver & mask) {
        write_bit(gpio, 1);
      } else {
        write_bit(gpio, 0);
      }
    }

    write_interval_gap(gpio);
  }
}

static int detect_sync(unsigned int gpio, unsigned int type,
        unsigned int *duration)
{
  static unsigned char sync[MAX_GPIO + 1];
  static char init = 0;

  if (!init) {
    memset(sync, 0, sizeof(sync));
    init = 1;
  }

  if (!type && sync[gpio] == 1 && IS_ON_TIME(*duration, 10200, 10800)) {
    sync[gpio] = 2;
  } else if (type && sync[gpio] == 2 && IS_ON_TIME(*duration, 150, 350)) {
    sync[gpio] = 3;
  } else if (!type && sync[gpio] == 3 && IS_ON_TIME(*duration, 2500, 2900)) {
    sync[gpio] = 0;

    return 1;
  } else if (type && IS_ON_TIME(*duration, 150, 350)) {
    sync[gpio] = 1;
  } else {
    sync[gpio] = 0;
  }

  return 0;
}

static int _read_bit(unsigned int gpio, unsigned int type,
        unsigned int duration, unsigned char *bit)
{
  static unsigned int pass[MAX_GPIO + 1];
  static char init = 0;

  if (!init) {
    memset(pass, 0, sizeof(pass));
    init = 1;
  }

  if (pass[gpio]) {
    pass[gpio] = 0;

    if (IS_ON_TIME(duration, 1000, 1900)) {
      *bit = 1;

      return 1;
    } else if (duration < 800) {
      *bit = 0;

      return 1;
    }
  } else if (duration < 800) {
    pass[gpio]++;

    return 0;
  }
  pass[gpio] = 0;

  return -1;
}

static int read_bit(unsigned int gpio, unsigned int type,
        unsigned int duration, unsigned char *bit)
{
  static unsigned int pass[MAX_GPIO + 1];
  static char init = 0;
  unsigned char b = 0;
  int rc;

  if (!init) {
    memset(pass, 0, sizeof(pass));
    init = 1;
  }

  rc = _read_bit(gpio, type, duration, &b);
  if (rc == -1) {
    pass[gpio] = 0;

    return -1;
  } else if (rc == 1) {
    if (pass[gpio]) {
      if (b == 0) {
        *bit = 1;
      } else {
        *bit = 0;
      }

      pass[gpio] = 0;

      return 1;
    }
    pass[gpio]++;
  }

  return 0;
}

static int read_byte(unsigned int gpio, unsigned char bit, unsigned char *byte)
{
  static unsigned char b[MAX_GPIO + 1];
  static unsigned char d[MAX_GPIO + 1];
  static char init = 0;

  if (!init) {
    memset(b, 0, sizeof(b));
    memset(d, 7, sizeof(d));
    init = 1;
  }

  if (d[gpio] != 0) {
    b[gpio] |= bit << d[gpio]--;

    return 0;
  }
  *byte = b[gpio] | bit;

  b[gpio] = 0;
  d[gpio] = 7;

  return 1;
}

int homeasy_receive(unsigned int gpio, unsigned int type,
        unsigned int duration, struct homeasy_payload *payload){
  static unsigned char sync[MAX_GPIO + 1];
  static unsigned char index[MAX_GPIO + 1];
  static unsigned char bytes[MAX_GPIO + 1][4];
  static char init = 0;
  unsigned char bit;
  unsigned short *s1, *s2;
  int rc;

  if (!init) {
    memset(sync, 0, sizeof(sync));
    memset(index, 3, sizeof(index));
    init = 1;
  }

  if (!sync[gpio]) {
    sync[gpio] = detect_sync(gpio, type, &duration);
    return 0;
  }

  rc = read_bit(gpio, type, duration, &bit);
  if (rc == -1) {
    sync[gpio] = 0;
    index[gpio] = 3;

    return -1;
  } else if (rc == 1) {
    rc = read_byte(gpio, bit, bytes[gpio] + index[gpio]);
    if (rc) {
      if (index[gpio]-- == 0) {
        sync[gpio] = 0;
        index[gpio] = 3;

        s1 = (unsigned short *) bytes[gpio];
        s2 = s1 + 1;

        payload->address1 = *s2 >> 6;
        payload->address2 = ((*s2 & 0x3f) << 10) + ((*s1 >> 6) & 0X3ff);

        payload->group = (*s1 >> 5) & 1;
        payload->ctrl = (*s1 >> 4) & 1;
        payload->receiver = *s1 & 0xF;

        return 1;
      }
    }
  }

  return 0;
}

const char *homeasy_get_ctrl_str(const unsigned char ctrl)
{
  switch (ctrl) {
    case HOMEASY_ON:
      return "ON";
    case HOMEASY_OFF:
      return "OFF";
  }

  return NULL;
}

unsigned char homeasy_get_ctrl_int(const char *ctrl)
{
  if (strcasecmp(ctrl, "on") == 0) {
    return HOMEASY_ON;
  } else if (strcasecmp(ctrl, "off") == 0) {
    return HOMEASY_OFF;
  }

  return HOMEASY_UNKNOWN;
}
