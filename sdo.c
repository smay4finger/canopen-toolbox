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
#include <stdarg.h>
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

#define SDO_ERROR_PROTOCOL_TIMED_OUT (0x05040000ul)
#define SDO_ERROR_GENERAL_ERROR      (0x08000000ul)

#define SDO_TIMEOUT_MS (200)


static void DATA(struct can_frame *frame, uint32_t data, size_t size) {
    frame->data[4] = size > 0 ? data >> 0 & 0xFF : 0;
    frame->data[5] = size > 1 ? data >> 8 & 0xFF : 0;
    frame->data[6] = size > 2 ? data >> 16 & 0xFF : 0;
    frame->data[7] = size > 3 ? data >> 24 & 0xFF : 0;
}

static uint32_t data32(struct can_frame frame) {
    return (uint32_t) frame.data[7] << 24
         | (uint32_t) frame.data[6] << 16
         | (uint32_t) frame.data[5] << 8
         | (uint32_t) frame.data[4] << 0;
}

static uint32_t data24(struct can_frame frame) {
    return (uint32_t) frame.data[6] << 16
         | (uint32_t) frame.data[5] << 8
         | (uint32_t) frame.data[4] << 0;
}

static uint16_t data16(struct can_frame frame) {
    return (uint32_t) frame.data[5] << 8
         | (uint32_t) frame.data[4] << 0;
}

static uint8_t data8(struct can_frame frame) {
    return (uint32_t) frame.data[4] << 0;
}



static uint8_t CS(int num) {
    return (num & 0x7) << 5;
}

static uint8_t E(int num) {
    return (num & 0x1) << 1;
}

static uint8_t S(int num) {
    return (num & 0x1) << 0;
}

static uint8_t N(int num) {
    return (num & 0x3) << 2;
}

static uint8_t T(int t) {
    return (t & 0x1) << 4;
}

static int cs(struct can_frame frame) {
    return frame.data[0] >> 5 & 0x7;
}

static int n(struct can_frame frame) {
    if ( cs(frame) == 0 ) {
        return frame.data[0] >> 1 & 0x7;
    }
    else {
        return frame.data[0] >> 2 & 0x3;
    }
}

static int e(struct can_frame frame) {
    return frame.data[0] >> 1 & 0x1;
}
static int s(struct can_frame frame) {
    return frame.data[0] >> 0 & 0x1;
}

static int t(struct can_frame frame) {
    return frame.data[0] >> 4 & 0x1;
}

static int c(struct can_frame frame) {
    return frame.data[0] >> 0 & 0x1;
}



static bool is_sdo_confirmation(struct can_frame frame, uint8_t node_id) {
    return frame.can_id == 0x580 + node_id && frame.can_dlc == 8;
}

int is_expected_canopen_object(const struct can_frame* frame, uint16_t index, uint8_t subindex) {
    return frame->data[1] == (index >> 0 & 0xFF)
        && frame->data[2] == (index >> 8 & 0xFF)
        && frame->data[3] == subindex;
}

static int is_upload_segment_response(struct can_frame frame) {
    return cs(frame) == 0;
}

static int is_download_segment_response(struct can_frame frame) {
    return cs(frame) == 1;
}

static int is_upload_initiate_response(struct can_frame frame, uint16_t index, uint8_t subindex) {
    return cs(frame) == 2 && is_expected_canopen_object(&frame, index, subindex);
}

static int is_download_initiate_response(struct can_frame frame, uint16_t index, uint8_t subindex) {
    return cs(frame) == 3 && is_expected_canopen_object(&frame, index, subindex);
}

static int is_abort_transfer_request(struct can_frame frame, uint16_t index, uint8_t subindex) {
    return cs(frame) == 4 && is_expected_canopen_object(&frame, index, subindex);
}



static void print_sdo_error(uint32_t error_code) {
    char* text;
    switch (error_code) {
    case 0x05030000: text = "Toggle bit not alternated."; break;
    case 0x05040000: text = "SDO protocol timed out."; break;
    case 0x05040001: text = "Client/server command specifier not valid or unknown."; break;
    case 0x05040002: text = "Invalid block size (block mode only)."; break;
    case 0x05040003: text = "Invalid sequence number (block mode only)."; break;
    case 0x05040004: text = "CRC error (block mode only)."; break;
    case 0x05040005: text = "Out of memory."; break;
    case 0x06010000: text = "Unsupported access to an object."; break;
    case 0x06010001: text = "Attempt to read a write only object."; break;
    case 0x06010002: text = "Attempt to write a read only object."; break;
    case 0x06020000: text = "Object does not exist in the object dictionary."; break;
    case 0x06040041: text = "Object cannot be mapped to the PDO."; break;
    case 0x06040042: text = "The number and length of the objects to be mapped would exceed PDO length."; break;
    case 0x06040043: text = "General parameter incompatibility reason."; break;
    case 0x06040047: text = "General internal incompatibility in the device."; break;
    case 0x06060000: text = "Access failed due to an hardware error."; break;
    case 0x06070010: text = "Data type does not match, length of service parameter does not match"; break;
    case 0x06070012: text = "Data type does not match, length of service parameter too high"; break;
    case 0x06070013: text = "Data type does not match, length of service parameter too low"; break;
    case 0x06090011: text = "Sub-index does not exist."; break;
    case 0x06090030: text = "Invalid value for parameter (download only)."; break;
    case 0x06090031: text = "Value of parameter written too high (download only)."; break;
    case 0x06090032: text = "Value of parameter written too low (download only)."; break;
    case 0x06090036: text = "Maximum value is less than minimum value."; break;
    case 0x060A0023: text = "Resource not available: SDO connection"; break;
    case 0x08000000: text = "General error"; break;
    case 0x08000020: text = "Data cannot be transferred or stored to the application."; break;
    case 0x08000021: text = "Data cannot be transferred or stored to the application because of local control."; break;
    case 0x08000022: text = "Data cannot be transferred or stored to the application because of the present device state."; break;
    case 0x08000023: text = "Object dictionary dynamic generation fails or no object dictionary is present (e.g. object dictionary is generated from file and generation fails because of an file error)."; break;
    case 0x08000024: text = "No data available"; break;
    default: text = "Unknown"; break;
    }
    fprintf(stderr, "SDO error 0x%08lX (%s)\n", error_code, text);
}

static void dump_data_binary(struct can_frame frame, int offset) {
    int i;
    for (i = offset; i < (8 - n(frame)); i++) {
        printf("%c", frame.data[i]);
    }
}



struct timeval timeout;
static void init_timeout(void) {
    timeout.tv_sec  = (SDO_TIMEOUT_MS / 1000UL);
    timeout.tv_usec = (SDO_TIMEOUT_MS % 1000UL) * 1000UL;
}

static bool await_sdo_confirmation(struct can_frame* frame_ptr, uint8_t node_id) {
    while (socketcan_read(frame_ptr, &timeout)) {
        if (is_sdo_confirmation(*frame_ptr, node_id))
            return true;
    }
    return false; // timed out
}

static void sdo_abort_transfer(uint8_t node_id, uint16_t index,
        uint8_t subindex, uint32_t abort_code) {
    struct can_frame frame;
    bzero(&frame, sizeof(frame));

    frame.can_id = 0x600 + node_id;
    frame.can_dlc = 8;
    frame.data[0] = CS(4);
    frame.data[1] = index >> 8 & 0xFF;
    frame.data[2] = index >> 0 & 0xFF;
    frame.data[3] = subindex;
    frame.data[4] = abort_code >> 0 & 0xFF;
    frame.data[5] = abort_code >> 8 & 0xFF;
    frame.data[6] = abort_code >> 16 & 0xFF;
    frame.data[7] = abort_code >> 24 & 0xFF;

    socketcan_write(frame);
    init_timeout();
}

static sdo_download_initiate_request(uint8_t node_id, uint16_t index,
        uint8_t subindex, uint32_t data, sdo_type_specifier_t type) {
    struct can_frame frame;
    bzero(&frame, sizeof(frame));

    frame.can_id = 0x600 + node_id;
    frame.can_dlc = 8;
    frame.data[0] = CS(1);
    frame.data[1] = index >> 0 & 0xFF;
    frame.data[2] = index >> 8 & 0xFF;
    frame.data[3] = subindex;

    switch (type) {
    case SDO_TYPE_U32:
    case SDO_TYPE_I32:
        frame.data[0] |= E(1) | S(1) | N(0);
        DATA(&frame, data, 4);
        break;
    case SDO_TYPE_U24:
    case SDO_TYPE_I24:
        frame.data[0] |= E(1) | S(1) | N(1);
        DATA(&frame, data, 3);
        break;
    case SDO_TYPE_U16:
    case SDO_TYPE_I16:
        frame.data[0] |= E(1) | S(1) | N(2);
        DATA(&frame, data, 2);
        break;
    case SDO_TYPE_U8:
    case SDO_TYPE_I8:
        frame.data[0] |= E(1) | S(1) | N(3);
        DATA(&frame, data, 1);
        break;
    case SDO_TYPE_UNSPECIFIED:
        frame.data[0] |= E(1) | S(0);
        DATA(&frame, data, 4);
        break;
    default:
        fprintf("undefined data type %d\n", (int)type);
        exit(EXIT_FAILURE);
    }

    socketcan_write(frame);
    init_timeout();
}

static void sdo_upload_initiate_request(uint8_t node_id, uint16_t index,
        uint8_t subindex) {
    struct can_frame frame;
    bzero(&frame, sizeof(frame));

    frame.can_id = 0x600 + node_id;
    frame.can_dlc = 8;
    frame.data[0] = CS(2);
    frame.data[1] = index >> 0 & 0xFF;
    frame.data[2] = index >> 8 & 0xFF;
    frame.data[3] = subindex;

    socketcan_write(frame);
    init_timeout();
}

static void sdo_upload_segment_request(uint8_t node_id, int toggle) {
    struct can_frame frame;
    bzero(&frame, sizeof(frame));

    frame.can_id = 0x600 + node_id;
    frame.can_dlc = 8;
    frame.data[0] = CS(3) | T(toggle);

    socketcan_write(frame);
    init_timeout();
}



void sdo_upload(char* can_interface, uint8_t node_id, uint16_t index,
        uint8_t subindex) {
    socketcan_open(can_interface);

    sdo_upload_initiate_request(node_id, index, subindex);

    struct can_frame confirmation;
    while (await_sdo_confirmation(&confirmation, node_id)) {
        if (is_upload_initiate_response(confirmation, index, subindex)) {
            if (e(confirmation) == 0 && s(confirmation) == 1) {
                // d contains the number of bytes to be uploaded
                sdo_upload_segment_request(node_id, 0);
                continue;
            }
            else if (e(confirmation) == 1 && s(confirmation) == 1) {
                // d contains the data of length 4-n to be uploaded
                switch(n(confirmation)) {
                case 0: printf("0x%X", data32(confirmation)); break;
                case 1: printf("0x%X", data24(confirmation)); break;
                case 2: printf("0x%X", data16(confirmation)); break;
                case 3: printf("0x%X", data8(confirmation)); break;
                }
            }
            else if (e(confirmation) == 1 && s(confirmation) == 0) {
                // d contains unspecified number of bytes to be uploaded
                printf("0x%X", data32(confirmation));
            }
            printf("\n");
            socketcan_close();
            exit(EXIT_SUCCESS);
        }
        else if (is_upload_segment_response(confirmation)) {
            dump_data_binary(confirmation, 1);
            if ( c(confirmation) == 0 ) {
                sdo_upload_segment_request(node_id, t(confirmation)+1);
            } else {
                printf("\n");
                socketcan_close();
                exit(EXIT_SUCCESS);
            }
        }
        else if (is_abort_transfer_request(confirmation, index, subindex)) {
            uint32_t error_code = data32(confirmation);
            print_sdo_error(error_code);
            socketcan_close();
            exit(EXIT_FAILURE);
        }
    }

    /* timeout*/
    sdo_abort_transfer(node_id, index, subindex, SDO_ERROR_PROTOCOL_TIMED_OUT);
    fprintf(stderr, "SDO timeout\n");
    exit(EXIT_FAILURE);
}

/*
 * only expedited download is implemented
 */
void sdo_download(char* can_interface, uint8_t node_id, uint16_t index,
        uint8_t subindex, uint32_t data, sdo_type_specifier_t type) {
    socketcan_open(can_interface);

    sdo_download_initiate_request(node_id, index, subindex, data, type);

    struct can_frame confirmation;
    while (await_sdo_confirmation(&confirmation, node_id)) {
        if (is_download_initiate_response(confirmation, index, subindex)) {
            socketcan_close();
            exit(EXIT_SUCCESS);
        }
        else if (is_download_segment_response(confirmation)) {
            sdo_abort_transfer(node_id, index, subindex, SDO_ERROR_GENERAL_ERROR);
            fprintf(stderr, "SDO download unexpected segment response.");
            socketcan_close();
            exit(EXIT_FAILURE);
        }
        else if (is_abort_transfer_request(confirmation, index, subindex)) {
            uint32_t error_code = data32(confirmation);
            print_sdo_error(error_code);
            socketcan_close();
            exit(EXIT_FAILURE);
        }
    }

    /* timeout*/
    sdo_abort_transfer(node_id, index, subindex, SDO_ERROR_PROTOCOL_TIMED_OUT);
    fprintf(stderr, "SDO timeout\n");
    socketcan_close();
    exit(EXIT_FAILURE);
}
