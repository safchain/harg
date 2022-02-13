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

#include "Manchester.h"
#include "homemade.h"

void homemade_transmit(unsigned int gpio, unsigned short address1,
        unsigned short address2, unsigned char receiver, unsigned char ctrl, unsigned char code)
{
  unsigned char data[sizeof(struct homemade_payload) + 1];
  struct homemade_payload *homemade = (struct homemade_payload *)(data + 1);

  data[0] = sizeof(struct homemade_payload);

  homemade->address1 = address1;
  homemade->address2 = address2;
  homemade->ctrl = ctrl;
  homemade->receiver = receiver;
  homemade->code = code;
  homemade->size = 0;

  memset(homemade->data, 0, sizeof(homemade->data));
  man.transmitArray(sizeof(struct homemade_payload), data);
}
