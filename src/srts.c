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
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>

#include "utils.h"
#include "mem.h"
#include "logging.h"
#include "gpio.h"
#endif

#include "srts.h"

#define IS_ON_TIME(X, I, A) X >= I && X <= A

#if !defined(__AVR__) && !defined(__avr__)
extern struct dlog *DLOG;

static char *get_code_file_path(const char *persistence_path,
        unsigned short address1, unsigned short address2)
{
  char *path;
  int size, address;

  address = (address1 << 16) + address2;

  size = snprintf(NULL, 0, "%s/srts/%d", persistence_path, address);
  path = (char *) xmalloc(size + 1);

  sprintf(path, "%s/srts", persistence_path);
  if (mkpath(path, 0755) == -1) {
    dlog(DLOG, DLOG_ERR, "Unable to create the srts code path: %s", path);
    free(path);

    return NULL;
  }
  sprintf(path, "%s/srts/%d", persistence_path, address);

  return path;
}

int srts_get_code(const char *persistence_path, unsigned short address1,
        unsigned short address2)
{
  char *path, code[10], *end;
  FILE *fp = NULL;
  int c, rc;

  path = get_code_file_path(persistence_path, address1, address2);
  if (path == NULL) {
    return 0;
  }

  if ((fp = fopen(path, "r")) == NULL) {
    rc = 0;
    goto clean;
  }

  memset(code, 0, sizeof(code));
  if (fgets(code, sizeof(code), fp) == NULL) {
    rc = -1;
    goto clean;
  }

  c = strtol(code, &end, 10);
  if (errno == ERANGE && (c == LONG_MAX || c == LONG_MIN)) {
    dlog(DLOG, DLOG_ERR, "Unable to parse the srts code for the address %d/%d",
            address1, address2);
    rc = -1;
    goto clean;
  }
  fclose(fp);
  free(path);

  return c;

clean:
  if (fp != NULL) {
    fclose(fp);
  }
  free(path);
  return rc;
}

static int store_code(const char *persistence_path, unsigned short address1,
        unsigned short address2, unsigned short new_code)
{
  char *path, code[10];
  FILE *fp;

  path = get_code_file_path(persistence_path, address1, address2);
  if (path == NULL) {
    return -1;
  }

  if ((fp = fopen(path, "w+")) == NULL) {
    dlog(DLOG, DLOG_ERR, "Unable to create the srts code file: %s", path);
    goto clean;
  }

  sprintf(code, "%d\n", new_code);
  if (fputs(code, fp) < 0) {
    dlog(DLOG, DLOG_ERR, "Unable to write the srts code to the file: %s",
            path);
  }
  fclose(fp);
  free(path);

  return 1;

clean:
  free(path);
  return -1;
}

void srts_print_payload(FILE *fp, struct srts_payload *payload)
{
  fprintf(fp, "key: %d\n", payload->key);
  fprintf(fp, "checksum: %d\n", payload->checksum);
  fprintf(fp, "ctrl: %d\n", payload->ctrl);
  fprintf(fp, "code: %d\n", payload->code);
  fprintf(fp, "address 1: %d\n", payload->address.byte1);
  fprintf(fp, "address 2: %d\n", payload->address.byte2);
  fprintf(fp, "address 3: %d\n", payload->address.byte3);
}

void srts_transmit_persist(unsigned int gpio, unsigned char key,
        unsigned short address1, unsigned short address2, unsigned char ctrl,
        unsigned int repeat, const char *persistence_path)
{
  int i, code = srts_get_code(persistence_path, address1, address2);

  if (code == -1) {
    /* reading code error, defaulting to 1 */
    srts_transmit(gpio, key, address1, address2, ctrl, 1, 0);
    for (i = 0; i < repeat; i++) {
      srts_transmit(gpio, key, address1, address2, ctrl, 1, 1);
    }

    return;
  }

  code += 1;

  srts_transmit(gpio, key, address1, address2, ctrl, code, 0);
  for (i = 0; i < repeat; i++) {
    srts_transmit(gpio, key, address1, address2, ctrl, code, 1);
  }

  store_code(persistence_path, address1, address2, code);
}
#else
static unsigned short htons(unsigned short value)
{
  return (value << 8) | ((value >> 8) & 0xFF);
}
#endif

static void obfuscate_payload(struct srts_payload *payload)
{
  unsigned char *p = (unsigned char *) payload;
  int i = 0;

  for (i = 1; i < 7; i++) {
    p[i] = p[i] ^ p[i - 1];
  }
}

static unsigned char get_checksum(struct srts_payload *payload)
{
  unsigned char *p = (unsigned char *) payload;
  unsigned char checksum = 0;
  int i;

  for (i = 0; i < 7; i++) {
    checksum = checksum ^ p[i] ^ (p[i] >> 4);
  }
  return checksum & 0xf;
}

static void checksum_payload(struct srts_payload *payload)
{
  payload->checksum = get_checksum(payload);
}

static void write_bit(unsigned int gpio, unsigned char bit)
{
  if (bit) {
    digitalWrite(gpio, LOW);
    delayMicroseconds(660);
    digitalWrite(gpio, HIGH);
    delayMicroseconds(660);
  } else {
    digitalWrite(gpio, HIGH);
    delayMicroseconds(660);
    digitalWrite(gpio, LOW);
    delayMicroseconds(660);
  }
}

static void write_byte(unsigned int gpio, unsigned char byte)
{
  unsigned int mask;

  for (mask = 0b10000000; mask != 0x0; mask >>= 1) {
    write_bit(gpio, byte & mask);
  }
}

static void write_payload(unsigned int gpio, struct srts_payload *payload)
{
  unsigned char *p = (unsigned char *) payload;
  int i;

  for (i = 0; i < 7; i++) {
    write_byte(gpio, p[i]);
  }
}

static void write_interval_gap(int gpio)
{
  digitalWrite(gpio, LOW);
  delayMicroseconds(30400);
}

static void sync_transmit(unsigned int gpio, unsigned int repeated)
{
  int count, i;

  if (repeated) {
    count = 7;
  } else {
    digitalWrite(gpio, HIGH);
    delayMicroseconds(12400);
    digitalWrite(gpio, LOW);
    delayMicroseconds(14000);
    count = 2;
  }
  for (i = 0; i != count; i++) {
    digitalWrite(gpio, HIGH);
    delayMicroseconds(2560);
    digitalWrite(gpio, LOW);
    delayMicroseconds(2560);
  }
}

void srts_transmit(unsigned int gpio, unsigned char key,
        unsigned short address1, unsigned short address2, unsigned char ctrl,
        unsigned short code, unsigned int repeated)
{
  struct srts_payload payload;

  sync_transmit(gpio, repeated);

  digitalWrite(gpio, HIGH);
  delayMicroseconds(4800);
  digitalWrite(gpio, LOW);
  delayMicroseconds(660);

  if (!key) {
    key = rand() % 255;
  }
  payload.key = key;
  payload.ctrl = ctrl;
  payload.checksum = 0;
  payload.code = htons(code);
  payload.address.byte1 = address2 & 0xff;
  payload.address.byte2 = (address2 >> 8) & 0xff;
  payload.address.byte3 = address1 & 0xff;

  checksum_payload(&payload);
  obfuscate_payload(&payload);

  write_payload(gpio, &payload);
  write_interval_gap(gpio);
}

static void unobfuscate_payload(unsigned char *bytes,
        struct srts_payload *payload)
{
  unsigned char *p;
  int i = 0;

  p = (unsigned char *) payload;

  p[0] = bytes[0];
  for (i = 1; i < 7; i++) {
    p[i] = bytes[i] ^ bytes[i - 1];
  }
}

static int validate_checksum(struct srts_payload *payload)
{
  unsigned char payload_chk = payload->checksum;
  unsigned char checksum = 0;

  payload->checksum = 0;
  checksum = get_checksum(payload);

  payload->checksum = payload_chk;
  if (payload_chk == checksum) {
    return 1;
  }

  return 0;
}

static int detect_sync(unsigned int gpio, unsigned int type,
        unsigned int *duration)
{
  static unsigned char init_sync[MAX_GPIO + 1];
  static unsigned char hard_sync[MAX_GPIO + 1];
  static unsigned char soft_sync[MAX_GPIO + 1];
  static char init = 0;
  int rc;

  if (!init) {
    memset(init_sync, 0, sizeof(init_sync));
    memset(hard_sync, 0, sizeof(hard_sync));
    memset(soft_sync, 0, sizeof(soft_sync));
    init = 1;
  }

  if (soft_sync[gpio] == 1 && *duration >= 660) {
    if (*duration >= 800) {
      *duration -= 800;
    } else {
      *duration = 0;
    }
    rc = 1;

    goto clean;
  } else if (type && hard_sync[gpio] >= 4 && IS_ON_TIME(*duration, 4200, 5200)) {
      soft_sync[gpio] = 1;
  } else if (IS_ON_TIME(*duration, 1800, 3200)) {
    hard_sync[gpio]++;
  } else {
    rc = 0;

    goto clean;
  }

  return 0;

clean:
  init_sync[gpio] = 0;
  hard_sync[gpio] = 0;
  soft_sync[gpio] = 0;

  return rc;
}

static int read_bit(unsigned int gpio, unsigned int type,
        unsigned int *duration, unsigned char *bit, unsigned int last)
{
  static unsigned char pass[MAX_GPIO + 1];
  static char init = 0;

  if (!init) {
    memset(pass, 0, sizeof(pass));
    init = 1;
  }

  /* maximum transmit length for a bit is around 1600 */
  if (!last && *duration > 1800) {
    pass[gpio] = 0;

    return -1;
  }

  /* duration too long to be a part of only one bit, so split into two parts */
  if (*duration > 1100) {
    *duration /= 2;
  } else {
    *duration = 0;
  }

  /* got the two part of a bit */
  if (pass[gpio]) {
    *bit = type;
    pass[gpio] = 0;

    return 1;
  }
  pass[gpio]++;

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

const char *srts_get_ctrl_str(unsigned char ctrl)
{
  switch (ctrl) {
    case SRTS_MY:
      return "MY";
    case SRTS_UP:
      return "UP";
    case SRTS_DOWN:
      return "DOWN";
    case SRTS_MY_DOWN:
      return "MY_DOWN";
    case SRTS_UP_DOWN:
      return "UP_DOWN";
    case SRTS_PROG:
      return "PROG";
    case SRTS_SUN_FLAG:
      return "SUN_FLAG";
    case SRTS_FLAG:
      return "FLAG";
  }

  return NULL;
}

unsigned char srts_get_ctrl_int(const char *ctrl)
{
  if (strcasecmp(ctrl, "my") == 0) {
    return SRTS_MY;
  } else if (strcasecmp(ctrl, "up") == 0) {
    return SRTS_UP;
  } else if (strcasecmp(ctrl, "my_up") == 0) {
    return SRTS_MY_UP;
  } else if (strcasecmp(ctrl, "down") == 0) {
    return SRTS_DOWN;
  } else if (strcasecmp(ctrl, "my_down") == 0) {
    return SRTS_MY_DOWN;
  } else if (strcasecmp(ctrl, "up_down") == 0) {
    return SRTS_UP_DOWN;
  } else if (strcasecmp(ctrl, "prog") == 0) {
    return SRTS_PROG;
  } else if (strcasecmp(ctrl, "sun_flag") == 0) {
    return SRTS_SUN_FLAG;
  } else if (strcasecmp(ctrl, "flag") == 0) {
    return SRTS_FLAG;
  }

  return SRTS_UNKNOWN;
}

void srts_get_address(struct srts_payload *payload, unsigned short *address1,
        unsigned short *address2)
{
  *address2 = payload->address.byte1;
  *address2 += payload->address.byte2 << 8;
  *address1 = payload->address.byte3;
}

int srts_receive(unsigned int gpio, unsigned int type, unsigned int duration,
        struct srts_payload *payload)
{
  static unsigned char sync[MAX_GPIO + 1];
  static unsigned char index[MAX_GPIO + 1];
  static unsigned char bytes[MAX_GPIO + 1][7];
  static char init = 0;
  unsigned char bit = 0;
  int rc;

  if (!init) {
    memset(sync, 0, sizeof(sync));
    memset(index, 0, sizeof(index));
    init = 1;
  }

  if (gpio > MAX_GPIO) {
    return SRTS_BAD_GPIO;
  }

  if (!sync[gpio]) {
    sync[gpio] = detect_sync(gpio, type, &duration);
    if (!sync[gpio]) {
      return 0;
    }

    /* too short, ignore trailling signal */
    if (duration < 400) {
      return 0;
    }

  }

  while (duration > 0) {
    rc = read_bit(gpio, type, &duration, &bit, index[gpio] == 6);
    if (rc == -1) {
      sync[gpio] = 0;
      index[gpio] = 0;

      return -1;
    } else if (rc == 1) {
      rc = read_byte(gpio, bit, bytes[gpio] + index[gpio]);
      if (rc) {
        if (++index[gpio] == 7) {
          sync[gpio] = 0;
          index[gpio] = 0;

          unobfuscate_payload(bytes[gpio], payload);
          rc = validate_checksum(payload);
          if (rc == 0) {
            return SRTS_BAD_CHECKSUM;
          }
          payload->code = htons(payload->code);

          return rc;
        }
      }
    }
  }

  return 0;
}
