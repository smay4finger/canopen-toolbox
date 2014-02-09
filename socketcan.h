/*
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SOCKETCAN_H_
#define SOCKETCAN_H_

#include <net/if.h>
#include <linux/can.h>

void socketcan_open(char* interface_name);
void socketcan_write(struct can_frame frame);
int socketcan_read(struct can_frame *frame, struct timeval* timeout);

#endif /* SOCKETCAN_H_ */
