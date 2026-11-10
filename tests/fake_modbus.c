/*
 * A tiny Modbus TCP device for end-to-end tests. Holding register N reads as
 * N * 3. Addresses at or above 1000 return exception 2. Unit 99 never replies.
 */
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fputs("usage: fake_modbus PORT\n", stderr);
        return 2;
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)atoi(argv[1]));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(lfd, 16) != 0) {
        perror("fake_modbus");
        return 1;
    }

    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) {
            return 1;
        }
        uint8_t q[12];
        size_t have = 0;
        while (have < sizeof q) {
            ssize_t n = read(fd, q + have, sizeof q - have);
            if (n <= 0) {
                break;
            }
            have += (size_t)n;
        }
        if (have == sizeof q && q[6] != 99u) {
            uint8_t r[260];
            unsigned addr = ((unsigned)q[8] << 8) | q[9];
            unsigned qty = ((unsigned)q[10] << 8) | q[11];
            size_t len;
            memcpy(r, q, 2);
            r[2] = 0;
            r[3] = 0;
            r[6] = q[6];
            if (addr >= 1000u) {
                r[4] = 0;
                r[5] = 3;
                r[7] = (uint8_t)(q[7] | 0x80u);
                r[8] = 2;
                len = 9;
            } else {
                r[4] = 0;
                r[5] = (uint8_t)(3u + 2u * qty);
                r[7] = q[7];
                r[8] = (uint8_t)(2u * qty);
                for (unsigned i = 0; i < qty; i++) {
                    unsigned v = (addr + i) * 3u;
                    r[9u + 2u * i] = (uint8_t)(v >> 8);
                    r[10u + 2u * i] = (uint8_t)v;
                }
                len = 9u + 2u * qty;
            }
            if (write(fd, r, len) < 0) {
                perror("fake_modbus");
            }
        } else if (have == sizeof q) {
            sleep(2);
        }
        close(fd);
    }
}
