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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <string.h>

#include "canopentool.h"

void nmt(char* can_interface, nmt_command_specifier_t command_specifier, uint8_t node_id) {
	struct can_frame frame;
	bzero(&frame, sizeof(frame));
	frame.can_id = 0;
	frame.can_dlc = 2;
	frame.data[0] = command_specifier;
	frame.data[1] = node_id;

	socketcan_open(can_interface);
	socketcan_write(frame);
	socketcan_close();
}
