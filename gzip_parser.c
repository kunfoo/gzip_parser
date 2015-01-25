/* gzip_parser parses the header and trailer of a file provided as commandline
   argument according to the gzip specification in RFC 1952

   Copyright (C) 2015 Kai Kunschke

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

// flags used in the flag-field of the gzip-header
#define FTEXT       0
#define FHCRC       1
#define FEXTRA      2
#define FNAME       3
#define FCOMMENT    4

/* this is only the first part of the gzip-header, since it can contain optional
 * fields, such as an original filename, a comment, a header checksum, or other
 * additional information
 */
struct gzip_header {
    uint8_t id1;    // always 0x1f
    uint8_t id2;    // always 0x8b
    uint8_t cm;     // always 0x8
    uint8_t flag;   // see defines above
    uint32_t mtime; // timestamp when compression happened
    uint8_t xfl;    // extra flags for compression algorithm
    uint8_t os;     // type of filesystem original file lay on (for end-of-line marker)
    uint16_t xlen;  // optional field, but struct is aligned to 12 bytes anyway
};

int main(int argc, char *argv[])
{
    unsigned int bytes_read;
    int fd;
    struct gzip_header header;
    uint8_t bit = 1;

    if ( argc != 2) {
        printf("please provide a filename\n");
        return -1;
    }

    memset(&header, 0, sizeof(struct gzip_header));
    fd = open(argv[1], O_RDONLY);
    bytes_read = read(fd, (void *)&header, sizeof(struct gzip_header));
    if (bytes_read < sizeof(struct gzip_header)) {
        printf("read too few bytes!\n");
        close(fd);
        return -1;
    }

    if (header.id1 == 0x1f && header.id2 == 0x8b) printf("valid gzip file\n");
    else {
        printf("invalid gzip file\n");
        return -1;
    }
    if (header.cm == 0x8) printf("standard gzip compression method \"deflate\"\n");
    if (header.flag) {
        printf("flags set: ");
        if (header.flag & (bit << FTEXT)) printf("FTEXT |");
        if (header.flag & (bit << FHCRC)) printf("FHCRC |");
        if (header.flag & (bit << FEXTRA)) printf("FEXTRA |");
        if (header.flag & (bit << FNAME)) printf("FNAME |");
        if (header.flag & (bit << FCOMMENT)) printf("FCOMMENT |");
        printf("\b \b\n"); // delete last unnecessary "|"
    }
    if (header.mtime) {
        time_t mtime = (time_t) header.mtime;
        struct tm *tm = localtime(&mtime);
        char timestamp[22];
        if (tm) {
            strftime(timestamp, sizeof(timestamp), "%d.%m.%Y %T", tm);
            printf("creation time: %s\n", timestamp);
        }
        else printf("%lu\n", sizeof(time_t));
    }
    printf("XFL: 0x%x\n", header.xfl);
    printf("OS: %s\n", header.os == 0x3 ? "UNIX" : "non-UNIX"); // there are only 10 types of operating systems ...

    if (header.flag & (bit << FEXTRA)) { // optional fields are present in the header, should not occur
        uint8_t extra_field[header.xlen];
        bytes_read = read(fd, (void *)extra_field, header.xlen);
        if (bytes_read < header.xlen) {
            printf("read too few bytes while reading extra header!\n");
            close(fd);
            return -1;
        }
        printf("extra field: ");
        for (uint16_t i = 0; i < header.xlen; ++i) printf("0x%x ", extra_field[i]);
        printf("\n");
    }

    if (header.flag & (bit << FNAME)) { // original filename is present in the header as zero-terminated string
        char filename[128];
        char c;
        uint8_t len = 0;

        if ( !(header.flag & (bit << FEXTRA)) ) // no header.xlen present, since it's optional
            lseek(fd, -2, SEEK_CUR);    // go 2 bytes back, filename begins there

        do {
            bytes_read = read(fd, (void *)&c, 1);
            if (bytes_read < 1) {
                printf("read too few bytes while reading filename!\n");
                close(fd);
                return -1;
            }
            filename[len++] = c;
        } while (c != 0 && len < 128);
        filename[127] = 0;  // zero-terminate if filename was longer than 128 bytes
        printf("filename: %s\n", filename);
    }

    if (header.flag & (bit << FCOMMENT)) { // some comment was given on compression
        char comment[BUFSIZ];
        char c;
        uint32_t len = 0;

        do {
            bytes_read = read(fd, (void *)&c, 1);
            if (bytes_read < 1) {
                printf("read too few bytes while reading filename!\n");
                close(fd);
                return -1;
            }
            comment[len++] = c;
        } while (c != 0 && len < BUFSIZ);
        comment[BUFSIZ-1] = 0;  // zero-terminate if filename was longer than BUFSIZ bytes
        printf("comment: %s\n", comment);
    }

    if (header.flag & (bit << FHCRC)) { // checksum of the header is present
        uint16_t header_crc;
        if ( (bytes_read = read(fd, (void *)&header_crc, 2)) < 2) {
            printf("read too few bytes while reading header checksum!\n");
            close(fd);
            return -1;
        }
        // TODO verify checksum
        printf("header checksum: 0x%x\n", header_crc);
    }

    /* now the data, follows, we can ignore this
     * the last 8 bytes contain a checksum of the uncompressed original data,
     * and the size of the original (uncompressed) input data modulo 2^32.
     */
    if ( lseek(fd, -8, SEEK_END) == -1 ) {
        printf("error positioning file offset at last 8 bytes!\n");
        close(fd);
        return -1;
    }

    uint32_t checksum;
    if ( (bytes_read = read(fd, (void *)&checksum, 4)) < 4) {
        printf("read too few bytes while reading data checksum!\n");
        close(fd);
        return -1;
    }
    printf("checksum: 0x%x\n", checksum);   // to verify checksum, uncompression is necessary

    uint32_t isize;
    if ( (bytes_read = read(fd, (void *)&isize, 4)) < 4) {
        printf("read too few bytes while reading data size!\n");
        close(fd);
        return -1;
    }
    printf("isize: 0x%x\n", isize);

    close(fd);
}
