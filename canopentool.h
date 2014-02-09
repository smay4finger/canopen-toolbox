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

#ifndef CANOPENTOOL_H_
#define CANOPENTOOL_H_

#include <stdbool.h>
#include <stdint.h>


void heartbeat(char* can_interface);

typedef enum {
	NMT_START_REMOTE_NODE = 1,
	NMT_STOP_REMOTE_NODE = 2,
	NMT_ENTER_PREOPERATIONAL = 128,
	NMT_RESET_NODE = 129,
	NMT_RESET_COMMUNICATION = 130
} nmt_command_specifier_t;
#define NMT_ANY_NODE (0)
void nmt(char* can_interface, nmt_command_specifier_t command_specifier, uint8_t node_id);

typedef enum {
	SDO_TYPE_U32, SDO_TYPE_U24, SDO_TYPE_U16, SDO_TYPE_U8,
	SDO_TYPE_I32, SDO_TYPE_I24, SDO_TYPE_I16, SDO_TYPE_I8,
	SDO_TYPE_UNSPECIFIED
} sdo_type_specifier_t;
void sdo_download(char* can_interface, uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t data, sdo_type_specifier_t type);
void sdo_upload(char* can_interface, uint8_t node_id, uint16_t index, uint8_t subindex);


#endif /* CANOPENTOOL_H_ */
