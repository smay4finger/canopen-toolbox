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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <curses.h>

static volatile int can_fd = -1;

static void exit_failure(char* format, ...)
{
	endwin();

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    socketcan_close();

    exit(EXIT_FAILURE);
}

int socketcan_open(char* interface_name) {
	if ((can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		exit_failure("socket failed: %s\n", strerror(errno));
	}

	struct ifreq ifr;
	strncpy(ifr.ifr_name, interface_name, sizeof(ifr.ifr_name));
	if (ioctl(can_fd, SIOCGIFINDEX, &ifr) < 0) {
		exit_failure("failed to enumerate can interface: %s\n", strerror(errno));
	}

	struct sockaddr_can addr;
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if (bind(can_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		exit_failure("bind failed: %s\n", strerror(errno));
	}

	return can_fd;
}

void socketcan_write(struct can_frame frame) {
	if ( write(can_fd, &frame, sizeof(struct can_frame)) < 0) {
		exit_failure("write failed: %s\n", strerror(errno));
	}
}

int socketcan_read(struct can_frame* frame, struct timeval* timeout) {
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(can_fd, &rfds);
	if (select(FD_SETSIZE, &rfds, NULL, NULL, timeout) < 0) {
		exit_failure("select failed: ", strerror(errno));
	}
	if (FD_ISSET(can_fd, &rfds)) {
		if (read(can_fd, frame, sizeof(struct can_frame)) < 0) {
			exit_failure("read failed: ", strerror(errno));
		}
	}
	return FD_ISSET(can_fd, &rfds);
}

void socketcan_close(void) {
    if ( can_fd > 0 ) {
    	close(can_fd);
    }
}
