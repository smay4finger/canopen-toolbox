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

#include <curses.h>

#define REFRESH_TIME           500 /* milliseconds */
#define HEARTBEAT_FAILURE_TIME 2000 /* milliseconds */
#define BOOTUP_BLIP_TIME       1000
#define BOOTUP_SHOW_TIME       30000
#define MAX_NODEID             127

#define COLOR_DOWN            1
#define COLOR_DOWN_IRRELEVANT 2
#define COLOR_BOOTUP          3
#define COLOR_BOOTUP_BLIP     4
#define COLOR_STOPPED         5
#define COLOR_OPERATIONAL     6
#define COLOR_PREOPERATIONAL  7
#define COLOR_ERROR           8

typedef struct {
    long nmt;
    long pdo;
    long sdo;
    long total;
} packets_t;

static int can_fd = 0;

void exit_success(char* format, ...) {
    va_list args;
    va_start(args, format);
    endwin();
    if (can_fd > 0) {
        close(can_fd);
    }
    vprintf(format, args);
    exit(EXIT_SUCCESS);
}

void exit_failure_with_help(char* format, ...) {
    va_list args;
    va_start(args, format);
    endwin();
    if (can_fd > 0) {
        close(can_fd);
    }
    vfprintf(stderr, format, args);
    exit(EXIT_FAILURE);
}

void sighandler(int signal) {
    if (signal == SIGWINCH) {
        exit_failure_with_help("resize not supported\n");
    }
    else {
        exit_success("bail out...\n");
    }
}

void heartbeat(char* can_interface) {
    struct ifreq ifr;
    struct sockaddr_can addr;

    struct can_frame rx;
    fd_set can_fdset;
    struct timeval timeout;
    struct heartbeat_t {
        struct timeval timestamp;
        unsigned char state;
    } heartbeat[MAX_NODEID + 1];
    struct timeval start;
    struct timeval now;
    struct timeval before;
    int nodeid;
    int maxx, maxy;
    packets_t packets;
    enum {
        MODE_PACKETRATE, MODE_LEGEND
    } mode = MODE_PACKETRATE;
    bool hex = true;
    bool node_present[MAX_NODEID + 1];

    for (nodeid = 1; nodeid <= MAX_NODEID; nodeid++) {
        node_present[nodeid] = false;
    }

    /*
     * read /etc/canopen/managers.conf
     */
    {
        FILE* f;
        char line[128];

        if ((f = fopen("/etc/canopen/managers.conf", "r")) == NULL ) {
            /* file not exists */
            for (nodeid = 1; nodeid <= MAX_NODEID; nodeid++) {
                node_present[nodeid] = true;
            }
        }
        else {
            char nodelist_filename[256] = "";
            while (fgets(line, sizeof(line), f) != NULL) {
                char* interface = strtok(line, " \t");
                char* baudrate = strtok(NULL, " \t");
                char* nodeid = strtok(NULL, " \t");
                char* networkname = strtok(NULL, "\n");

                if (interface != NULL && networkname != NULL
                        && !strcmp(interface, can_interface)) {
                    snprintf(nodelist_filename, sizeof(nodelist_filename),
                            "/etc/canopen/%s/nodelist.cpj", networkname);
                    break;
                }
            }
            fclose(f);

            if ((f = fopen(nodelist_filename, "r")) != NULL ) {
                while (fgets(line, sizeof(line), f) != NULL ) {
                    char* key = strtok(line, "=");
                    char* value = strtok(NULL, "\n");
                    for (nodeid = 1; nodeid <= MAX_NODEID; nodeid++) {
                        char key_cmp[128];
                        sprintf(key_cmp, "Node%dPresent", nodeid);
                        if (strcasestr(key, key_cmp) != NULL ) {
                            if (strtol(value, NULL, 0) == 0x01) {
                                node_present[nodeid] = true;
                            }
                        }
                    }
                }
                fclose(f);
            }
            else {
                /* file not exists */
                for (nodeid = 1; nodeid <= MAX_NODEID; nodeid++) {
                    node_present[nodeid] = true;
                }
            }
        }
    }

    /*
     * initialize CAN interface
     */
    if (strspn(can_interface, "0123456789") == strlen(can_interface)) {
        const int new_can_interface_size = 50;
        char* new_can_interface = malloc(new_can_interface_size);
        snprintf(new_can_interface, new_can_interface_size,
                "can%d", atol(can_interface) - 1);
        can_interface = new_can_interface;
    }
    can_fd = socketcan_open(can_interface);

    /*
     * initialize ncurses
     */
    initscr();
    cbreak();
    noecho();
    nonl();
    curs_set(0);
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);
    start_color();
    init_pair(COLOR_DOWN, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_DOWN_IRRELEVANT, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_BOOTUP, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_BOOTUP_BLIP, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_STOPPED, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_OPERATIONAL, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_PREOPERATIONAL, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_ERROR, COLOR_WHITE, COLOR_RED);
    getmaxyx(stdscr, maxy, maxx);
    if (maxy < 20 || maxx < 80) {
        errno = ENOTSUP;
        exit_failure_with_help("screen size must be minimum 80x20\n");
    }

    /*
     * install signal handler
     */
    signal(SIGHUP, &sighandler);
    signal(SIGINT, &sighandler);
    signal(SIGTERM, &sighandler);
    signal(SIGWINCH, &sighandler);

    /*
     * initialize data structures
     */
    for (nodeid = 1; nodeid <= MAX_NODEID; nodeid++) {
        heartbeat[nodeid].state = -1;
        timerclear(&heartbeat[nodeid].timestamp);
    }
    if (gettimeofday(&start, NULL ) < 0) {
        exit_failure_with_help("gettimeofday(): %s", strerror(errno));
    }
    bzero(&packets, sizeof(packets));

    /*
     * main loop
     */
    while (true) {
        FD_ZERO(&can_fdset);
        FD_SET(can_fd, &can_fdset);
        FD_SET(0, &can_fdset);
        timeout.tv_sec = 0;
        timeout.tv_usec = REFRESH_TIME * 1000;

        /*
         * wait for can frame, keyboard or timeout
         */
        if (select(FD_SETSIZE, &can_fdset, NULL, NULL, &timeout) < 0) {
            exit_failure_with_help("select(): %s\n", strerror(errno));
        }

        /*
         * CAN frame received
         */
        if (FD_ISSET(can_fd, &can_fdset)) { /* received packet */
            bzero(&rx, sizeof(rx));
            if (read(can_fd, &rx, sizeof(rx)) < 0) {
                exit_failure_with_help("read(): %s\n", strerror(errno));
            }
            packets.total++;
            if (rx.can_id > 0x700 && rx.can_id <= 0x700 + MAX_NODEID
                    && rx.can_dlc == 1) { /* heartbeat message */
                nodeid = rx.can_id - 0x700;
                if (ioctl(can_fd, SIOCGSTAMP, &heartbeat[nodeid].timestamp)
                        < 0) {
                    exit_failure_with_help("%s:%d ioctl error: %s", __FILE__, __LINE__, strerror(errno));
                }
                heartbeat[nodeid].state = rx.data[0] & 0x7F;
                packets.nmt++;
            }
            if (rx.can_id == 0) {
                packets.nmt++;
            }
            if (rx.can_id > 0x580 && rx.can_id <= 0x67f) { /* SDO */
                packets.sdo++;
            }
            if (rx.can_id > 0x180 && rx.can_id <= 0x57f) { /* PDO */
                packets.pdo++;
            }
        }

        /*
         * keyboard input received
         */
        if (FD_ISSET(0, &can_fdset)) { /* keyboard */
            int key = getch();
            switch (key) {
            case 'q':
            case 'Q':
            case 'x':
            case 'X':
                exit_success("thanks for using heartbeat\n");
                break;
            case 'l':
                if (mode == MODE_LEGEND) {
                    mode = MODE_PACKETRATE;
                }
                else {
                    mode = MODE_LEGEND;
                }
                break;
            case 'c':
                bzero(&heartbeat, sizeof(heartbeat));
                bzero(&packets, sizeof(packets));
                break;
            case ' ':
                hex = !hex;
                break;
            }
        }

        /*
         * get actual time
         */
        memcpy(&before, &now, sizeof(now));
        if (gettimeofday(&now, NULL ) < 0) {
            exit_failure_with_help("gettimeofday(): %s\n", strerror(errno));
        }

        /*
         * ncurses box
         */erase();
        box(stdscr, 0, 0);
        attrset(A_BOLD);
        mvprintw(0, 3, " CANopen - %s ", can_interface);
        attrset(A_NORMAL);

        /*
         * show status of heartbeat messages
         */
        {
#define ITEMSIZE 9
#define XITEMS   8
#define XBEGIN   4
#define YBEGIN   2

            int nodes_boot = 0;
            int nodes_stopped = 0;
            int nodes_operational = 0;
            int nodes_preoperational = 0;
            int nodes_failure = 0;

            int x, y;

            for (nodeid = 1; nodeid <= MAX_NODEID; nodeid++) {
                double ms_last = (double) heartbeat[nodeid].timestamp.tv_sec
                        * 1000.0
                        + (double) heartbeat[nodeid].timestamp.tv_usec / 1000.0;
                double ms_now = (double) now.tv_sec * 1000.0
                        + (double) now.tv_usec / 1000.0;
                double last_seen = ms_now - ms_last;
                y = nodeid / XITEMS + YBEGIN;
                x = nodeid % XITEMS * ITEMSIZE + XBEGIN;
                char* format = hex ? " %02X:%s" : "%3d:%s";
                if (last_seen < BOOTUP_BLIP_TIME
                        && heartbeat[nodeid].state == 0) {
                    attrset(COLOR_PAIR(COLOR_BOOTUP_BLIP));
                    mvprintw(y, x, format, nodeid, "BOOT");
                    nodes_boot++;
                }
                else if (last_seen < BOOTUP_SHOW_TIME
                        && heartbeat[nodeid].state == 0) {
                    attrset(COLOR_PAIR(COLOR_BOOTUP));
                    mvprintw(y, x, format, nodeid, "BOOT");
                    nodes_boot++;
                }
                else if (last_seen < HEARTBEAT_FAILURE_TIME
                        && heartbeat[nodeid].state == 4) {
                    attrset(COLOR_PAIR(COLOR_STOPPED));
                    mvprintw(y, x, format, nodeid, "STOP");
                    nodes_stopped++;
                }
                else if (last_seen < HEARTBEAT_FAILURE_TIME
                        && heartbeat[nodeid].state == 5) {
                    attrset(COLOR_PAIR(COLOR_OPERATIONAL));
                    mvprintw(y, x, format, nodeid, "OPER");
                    nodes_operational++;
                }
                else if (last_seen < HEARTBEAT_FAILURE_TIME
                        && heartbeat[nodeid].state == 127) {
                    attrset(COLOR_PAIR(COLOR_PREOPERATIONAL));
                    mvprintw(y, x, format, nodeid, "PRE ");
                    nodes_preoperational++;
                }
                else if (last_seen < HEARTBEAT_FAILURE_TIME) { /* unknown state */
                    attrset(COLOR_PAIR(COLOR_ERROR));
                    mvprintw(y, x, format, nodeid, "####");
                    nodes_failure++;
                }
                else if (node_present[nodeid]) {
                    attrset(COLOR_PAIR(COLOR_DOWN));
                    mvprintw(y, x, format, nodeid, "UNKN");
                    nodes_failure++;
                }
                else {
                    attrset(COLOR_PAIR(COLOR_DOWN_IRRELEVANT));
                    mvprintw(y, x, format, nodeid, "UNKN");
                }
            }

            x = maxx - 18;
            y = maxy - 1;
            attrset(A_BOLD);
            mvprintw(y, x - 1, "    /   /   /    ");
            attrset(COLOR_PAIR(COLOR_OPERATIONAL));
            mvprintw(y, x + 0, "%03d", nodes_operational);
            attrset(COLOR_PAIR(COLOR_PREOPERATIONAL));
            mvprintw(y, x + 4, "%03d", nodes_preoperational);
            attrset(COLOR_PAIR(COLOR_STOPPED));
            mvprintw(y, x + 8, "%03d", nodes_stopped);
            attrset(COLOR_PAIR(COLOR_DOWN));
            mvprintw(y, x + 12, "%03d", nodes_failure);
            attrset(A_NORMAL);

        }

        /*
         * CAN status
         */
#if 0
        {
            static double ms_last = 0.0;
            double ms_now = (double)now.tv_sec * 1000.0 + (double)now.tv_usec / 1000.0;
            double ms_diff = ms_now - ms_last;
            int x = 3;
            int y = maxy - 1;

            if(ms_diff > 500.0) {
                ioctl(can_fd, CAN_IOCTL_STATUS, &can_status);
            }

            attrset(A_BOLD); mvprintw(y, x - 1, "    /   /   ");
            attrset( can_status.rx_errors >= can_status.error_warning_limit ? COLOR_PAIR(COLOR_ERROR) : A_NORMAL);
            mvprintw(y, x + 0, "%03d", can_status.rx_errors);
            attrset( can_status.tx_errors >= can_status.error_warning_limit ? COLOR_PAIR(COLOR_ERROR) : A_NORMAL);
            mvprintw(y, x + 4, "%03d", can_status.tx_errors);
            attrset(A_NORMAL);
            mvprintw(y, x + 8, "%02X", can_status.status);

            switch(can_status.type) {
                case CAN_TYPE_SJA1000:
                if(can_status.status & (1<<6)) {
                    attrset(COLOR_PAIR(COLOR_ERROR));
                    mvprintw(y, x + 14, "PASSIVE");
                    attrset(A_NORMAL);
                }
                if(can_status.status & (1<<7)) {
                    attrset(COLOR_PAIR(COLOR_ERROR));
                    mvprintw(y, x + 22, "BUSOFF");
                    attrset(A_NORMAL);
                }
                break;
            }
        }
#endif
#define BIG 30
#define SMALL 24
        if (maxy > SMALL) {
            /*
             * Legend
             */
            if (mode == MODE_LEGEND || maxy >= BIG) {
#define LEGEND_X1 10
#define LEGEND_Y 19
#define LEGEND_X2 (LEGEND_X1 + 30)
                attrset(COLOR_PAIR(COLOR_OPERATIONAL));
                mvprintw(LEGEND_Y + 0, LEGEND_X1, "OPER");
                attrset(A_NORMAL);
                mvprintw(LEGEND_Y + 0, LEGEND_X1 + 4, " - operational");
                attrset(COLOR_PAIR(COLOR_PREOPERATIONAL));
                mvprintw(LEGEND_Y + 1, LEGEND_X1, "PRE ");
                attrset(A_NORMAL);
                mvprintw(LEGEND_Y + 1, LEGEND_X1 + 4, " - pre-operational");
                attrset(COLOR_PAIR(COLOR_BOOTUP));
                mvprintw(LEGEND_Y + 2, LEGEND_X1, "BOOT");
                attrset(A_NORMAL);
                mvprintw(LEGEND_Y + 2, LEGEND_X1 + 4, " - bootup node");
                attrset(COLOR_PAIR(COLOR_STOPPED));
                mvprintw(LEGEND_Y + 0, LEGEND_X2, "STOP");
                attrset(A_NORMAL);
                mvprintw(LEGEND_Y + 0, LEGEND_X2 + 4, " - stopped");
                attrset(COLOR_PAIR(COLOR_ERROR));
                mvprintw(LEGEND_Y + 1, LEGEND_X2, "####");
                attrset(A_NORMAL);
                mvprintw(LEGEND_Y + 1, LEGEND_X2 + 4, " - invalid NMT state");
                attrset(COLOR_PAIR(COLOR_DOWN));
                mvprintw(LEGEND_Y + 2, LEGEND_X2, "UNKN");
                attrset(A_NORMAL);
                mvprintw(LEGEND_Y + 2, LEGEND_X2 + 4, " - heartbeat failure");
            }

            /*
             * packet rate indicator
             */
            if (mode == MODE_PACKETRATE || maxy >= BIG) {
#define RATE_X 4
#define RATE_Y_SMALL 19
#define RATE_Y_BIG 24
#define TIMEINTERVAL 1000.0
#define SECONDS (1000.0 / TIMEINTERVAL)
                int RATE_Y = maxy >= BIG ? RATE_Y_BIG : RATE_Y_SMALL;
                char* format =
                        "%-8s %12d packets, %8.0f packets/s, %6.1f kBit/s";

                struct {
                    double nmt;
                    double pdo;
                    double sdo;
                    double total;
                } rate;
                static packets_t packets_seen;
                static double ms_last = 0.0;
                double ms_now = (double) now.tv_sec * 1000.0
                        + (double) now.tv_usec / 1000.0;
                double ms_diff = ms_now - ms_last;

                if (ms_diff > TIMEINTERVAL) {
                    rate.total = (double) (packets.total - packets_seen.total)
                            * TIMEINTERVAL / ms_diff * SECONDS;
                    rate.pdo = (double) (packets.pdo - packets_seen.pdo)
                            * TIMEINTERVAL / ms_diff * SECONDS;
                    rate.sdo = (double) (packets.sdo - packets_seen.sdo)
                            * TIMEINTERVAL / ms_diff * SECONDS;
                    rate.nmt = (double) (packets.nmt - packets_seen.nmt)
                            * TIMEINTERVAL / ms_diff * SECONDS;
                    ms_last = ms_now;
                    memcpy(&packets_seen, &packets, sizeof(packets_seen));
                }

                mvprintw(RATE_Y + 0, RATE_X, format, "PDO:", packets.pdo, rate.pdo, rate.pdo
                        * 64 / 1024.0);
                mvprintw(RATE_Y + 1, RATE_X, format, "SDO:", packets.sdo, rate.sdo, rate.sdo
                        * 111 / 1024.0);
                mvprintw(RATE_Y + 2, RATE_X, format, "NMT:", packets.nmt, rate.nmt, rate.pdo
                        * 55 / 1024.0);
                mvprintw(RATE_Y + 3, RATE_X, format, "total:", packets.total, rate.total, rate.total
                        * 79 / 1024.0);
            }
        }

        /*
         * rotating indicator
         */
        {
            static int counter = 0;
            char indicator[] = "|/-\\";
            mvprintw(0, 0, "%c", indicator[counter]);
            counter = counter < 2 ? counter + 1 : 0;
        }

        /*
         * refresh curses screen
         */
        refresh();
    }
}
