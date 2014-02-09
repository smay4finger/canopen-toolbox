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

#include "canopentool.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

#include <string.h>

#include <stdarg.h>
#include <libgen.h>

static void show_help() {
	printf("The Swiss Army Knife for CANopen networks\n\n"
			"nmt can-interface [start|stop|preop|reset-comm|reset-node] [node-id]\n"
			"sdo-upload can-interface node-id index subindex\n"
			"sdo-download can-interface node-id index subindex data\n"
			"heartbeat can-interface\n");
}

static nmt_command_specifier_t parse_nmt_command_specifier(char* str) {
    if(!strcasecmp(str, "start")) {
		return NMT_START_REMOTE_NODE;
	} else if (!strcasecmp(str, "stop")) {
		return NMT_STOP_REMOTE_NODE;
	} else if (!strcasecmp(str, "preop")) {
		return NMT_ENTER_PREOPERATIONAL;
	} else if (!strcasecmp(str, "reset-node")) {
		return NMT_RESET_NODE;
	} else if (!strcasecmp(str, "reset-comm")) {
		return NMT_RESET_COMMUNICATION;
	} else {
		fprintf(stderr, "illegal nmt command\n");
		exit(EXIT_FAILURE);
	}
}

static uint8_t parse_node_id(char* str) {
	long int node_id = strtol(str, NULL, 0);
	if ( node_id < 1 || node_id > 127 ) {
		fprintf(stderr, "illegal node id\n");
		exit(EXIT_FAILURE);
	}
	return node_id;
}

static uint16_t parse_canopen_index(char* str) {
	long int index = strtol(str, NULL, 0);
	if ( index < 0 || index > 0xFFFFL) {
		fprintf(stderr, "illegal CANopen index\n");
		exit(EXIT_FAILURE);
	}
	return index;
}

static uint8_t parse_canopen_subindex(char* str) {
	long int subindex = strtol(str, NULL, 0);
	if ( subindex < 0 || subindex > 0xFFL) {
		fprintf(stderr, "illegal CANopen subindex\n");
		exit(EXIT_FAILURE);
	}
	return subindex;
}

static uint32_t parse_sdo_data(char* str) {
	return strtol(str, NULL, 0);
}

static void ensure_user_is_root() {
#define UNLOCK_PASSWORD "I am the master of my fate: I am the captain of my soul."
	bool user_is_root = (getuid() == 0);

	char* env_password = getenv("UNLOCK_DANGEROUS_THINGS");
	bool user_has_unlocked_with_password = env_password != NULL &&
			!strcasecmp(getenv("UNLOCK_DANGEROUS_THINGS"), UNLOCK_PASSWORD);

	if (!user_is_root && !user_has_unlocked_with_password) {
		fprintf(stderr, "sorry, only root can do that.\n");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char** argv) {
	char* program_name = basename(argv[0]);

	if (!strcasecmp(program_name, "canopentool") && argc == 1) {
		show_help();
	}
	else if (!strcasecmp(program_name, "canopentool") && argc == 2) {
		char* can_interface = argv[1];
		heartbeat(can_interface);
	}
	else if (!strcasecmp(program_name, "canopentool")) {
		return main(argc - 1, &argv[1]);
	}
	else if (!strcasecmp(program_name, "nmt") && (argc == 3 || argc == 4)) {
		char* can_interface = argv[1];
		nmt_command_specifier_t command_specifier =
				parse_nmt_command_specifier(argv[2]);
		uint8_t node_id = argc > 3 ? parse_node_id(argv[3]) : NMT_ANY_NODE;

		ensure_user_is_root();
		nmt(can_interface, command_specifier, node_id);
	}
	else if ((!strcasecmp(program_name, "sdo-upload")
			|| !strcasecmp(program_name, "sdo-read")) && argc == 5) {
		char* can_interface = argv[1];
		uint8_t node_id = parse_node_id(argv[2]);
		uint16_t index = parse_canopen_index(argv[3]);
		uint8_t subindex = parse_canopen_subindex(argv[4]);

		sdo_upload(can_interface, node_id, index, subindex);
	}
	else if ((!strcasecmp(program_name, "sdo-download")
			|| !strcasecmp(program_name, "sdo-write")) && argc == 6) {
		char* can_interface = argv[1];
		uint8_t node_id = parse_node_id(argv[2]);
		uint16_t index = parse_canopen_index(argv[3]);
		uint8_t subindex = parse_canopen_subindex(argv[4]);
		uint32_t data = parse_sdo_data(argv[5]);

		ensure_user_is_root();
		sdo_download(can_interface, node_id, index, subindex, data, SDO_TYPE_UNSPECIFIED);
	}
	else if ((!strcasecmp(program_name, "sdo-download")
			|| !strcasecmp(program_name, "sdo-write")) && argc == 7) {
		char* can_interface = argv[1];
		uint8_t node_id = parse_node_id(argv[2]);
		uint16_t index = parse_canopen_index(argv[3]);
		uint8_t subindex = parse_canopen_subindex(argv[4]);
		uint32_t data = parse_sdo_data(argv[5]);
		sdo_type_specifier_t type =
				!strcasecmp(argv[6], "U32") ? SDO_TYPE_U32 :
				!strcasecmp(argv[6], "I32") ? SDO_TYPE_I32 :
				!strcasecmp(argv[6], "U24") ? SDO_TYPE_U24 :
				!strcasecmp(argv[6], "I24") ? SDO_TYPE_I24 :
				!strcasecmp(argv[6], "U16") ? SDO_TYPE_U16 :
				!strcasecmp(argv[6], "I16") ? SDO_TYPE_I16 :
				!strcasecmp(argv[6], "U8") ? SDO_TYPE_U8 :
				!strcasecmp(argv[6], "I8") ? SDO_TYPE_I8 :
						SDO_TYPE_UNSPECIFIED;

		ensure_user_is_root();
		sdo_download(can_interface, node_id, index, subindex, data, type);
	}
	else {
		fprintf(stderr, "syntax error\n");
		exit(EXIT_FAILURE);
	}

	return 0;
}
