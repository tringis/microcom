/*
 *  Copyright 2009 Tobias Ringstrom
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define VERSION_STRING "microcom development version"

#define BUF_SIZE 4096

typedef struct UART_BaudTable {
    int baud;
    int code;
} UART_BaudTable_t;

jmp_buf g_env;
struct termios g_saved_stdin_attr;
int g_logfd = -1;
int g_log_html;

void help(void);
int baud2code(int baud);
int parse_format(const char *format, int *mask);
void write_buffer(int fd, const char *buf, int *pos, int *end);
int read_buffer(int fd, char *buf, int *end, int buf_size);
int read_buffer_echo(int fd, char *buf, int *end, int buf_size,
                     char *echo_buf, int *echo_end, int echo_buf_size);

static const UART_BaudTable_t sgUART_BaudTable[] = {
    {      50,      B50 },
    {      75,      B75 },
    {     110,     B110 },
    {     134,     B134 },
    {     150,     B150 },
    {     200,     B200 },
    {     300,     B300 },
    {     600,     B600 },
    {    1200,    B1200 },
    {    1800,    B1800 },
    {    2400,    B2400 },
    {    4800,    B4800 },
    {    9600,    B9600 },
    {   19200,   B19200 },
    {   38400,   B38400 },
    {   57600,   B57600 },
    {  115200,  B115200 },
#ifdef B230400
    {  230400,  B230400 },
#endif
#ifdef B460800
    {  460800,  B460800 },
#endif
#ifdef B500000
    {  500000,  B500000 },
#endif
#ifdef B576000
    {  576000,  B576000 },
#endif
#ifdef B921600
    {  921600,  B921600 },
#endif
#ifdef B1000000
    { 1000000, B1000000 },
#endif
#ifdef B1152000
    { 1152000, B1152000 },
#endif
#ifdef B1500000
    { 1500000, B1500000 },
#endif
#ifdef B2000000
    { 2000000, B2000000 },
#endif
#ifdef B2500000
    { 2500000, B2500000 },
#endif
#ifdef B3000000
    { 3000000, B3000000 },
#endif
#ifdef B3500000
    { 3500000, B3500000 },
#endif
#ifdef B4000000
    { 4000000, B4000000 },
#endif
    { -1, -1 }
};

void signal_handler(int signo)
{
    longjmp(g_env, signo);
}

ssize_t safe_write(int fd, const void *buf, size_t count)
{
    size_t left = count;
    ssize_t n;

    while (left > 0)
    {
        n = write(fd, buf, left);
        if (n < 0)
            return -1;
        left -= (size_t)n;
        buf += n;
    }

    return count;
}

void write_buffer(int fd, const char *buf, int *pos, int *end)
{
    int n;

    n = write(fd, buf + *pos, *end - *pos);
    if (n < 0 && errno != EINTR && errno != EAGAIN)
    {
        fprintf(stderr, "Write error [%s]", strerror(errno));
        exit(1);
    }
    else if (n > 0)
    {
        *pos += n;
        if (*pos == *end)
        {
            *pos = 0;
            *end = 0;
        }
    }
}

int read_buffer(int fd, char *buf, int *end, int buf_size)
{
    int n;

    n = read(fd, buf + *end, buf_size - *end);
    if (n < 0 && errno != EINTR && errno != EAGAIN)
    {
        fprintf(stderr, "Read error [%s]", strerror(errno));
        exit(1);
    }
    else if (n == 0)
        return 0;
    else if (n > 0)
        *end += n;

    return 1;
}

static void log_buffer(char *buf, int buf_size)
{
    if (g_logfd == -1)
        return;

    if (safe_write(g_logfd, buf, buf_size) == -1)
    {
        fprintf(stderr, "Log file write error [%s]", strerror(errno));
        exit(1);
    }
}

static int minimum(int x, int y)
{
    return y < x ? y : x;
}

int read_buffer_echo(int fd, char *buf, int *end, int buf_size,
                     char *echo_buf, int *echo_end, int echo_buf_size)
{
    int n;

    n = read(fd, buf + *end,
             minimum(buf_size - *end, echo_buf_size - *echo_end));
    if (n < 0 && errno != EINTR && errno != EAGAIN)
    {
        fprintf(stderr, "Read error [%s]", strerror(errno));
        exit(1);
    }
    else if (n == 0)
        return 0;
    else if (n > 0)
    {
        int i;

        for (i = 0; i < n; ++i)
        {
            echo_buf[*echo_end + i] =
                buf[*end + i] == '\r' ? '\n' : buf[*end + i];
        }
        *end += n;
        *echo_end += n;
    }

    return 1;
}

void interactive(int fd, int local_echo)
{
    struct termios tio;
    char serial_to_stdout_buf[BUF_SIZE];
    int serial_to_stdout_pos = 0, serial_to_stdout_end = 0;
    char stdin_to_serial_buf[BUF_SIZE];
    int stdin_to_serial_pos = 0, stdin_to_serial_end = 0;
    char local_echo_buf[BUF_SIZE];
    int local_echo_pos = 0, local_echo_end = 0;
    int status, stdin_open = 1;

    if (isatty(0))
    {
        tcgetattr(0, &g_saved_stdin_attr);
        memcpy(&tio, &g_saved_stdin_attr, sizeof(tio));
        tio.c_iflag &= ~ICRNL;
        tio.c_lflag &= ~(ICANON | ECHO);
        tio.c_lflag |= ISIG;
        tcsetattr(0, TCSAFLUSH, &tio);
    }

    if (setjmp(g_env) != 0)
    {
        if (isatty(0))
            tcsetattr(0, TCSAFLUSH, &g_saved_stdin_attr);
        puts("Bye!");
        tcflush(fd, TCIOFLUSH); /* Prevent close() from blocking */
        return;
    }

    for (;;)
    {
        int stdin_read_index = -1, stdout_write_index = -1;
        int serial_read_index = -1, serial_write_index = -1;
        struct pollfd fds[4];
        int n = 0;

        /* Anything to write to stdout? */
        if (serial_to_stdout_end - serial_to_stdout_pos > 0 ||
            local_echo_end - local_echo_pos > 0)
        {
            fds[n].fd = STDOUT_FILENO;
            fds[n].events = POLLOUT;
            stdout_write_index = n++;
        }
        /* Anything to write to the serial port? */
        if (fd >= 0 && stdin_to_serial_end - stdin_to_serial_pos > 0)
        {
            fds[n].fd = fd;
            fds[n].events = POLLOUT;
            serial_write_index = n++;
        }
        /* Any space to read from stdin? */
        if (fd >= 0 && stdin_open && stdin_to_serial_end < BUF_SIZE)
        {
            fds[n].fd = STDIN_FILENO;
            fds[n].events = POLLIN;
            stdin_read_index = n++;
        }
        /* Any space to read from the serial port? */
        if (fd >= 0 && serial_to_stdout_end < BUF_SIZE)
        {
            fds[n].fd = fd;
            fds[n].events = POLLIN;
            serial_read_index = n++;
        }
        if (n == 0)
            break;

        status = poll(fds, n, -1);
        if (status == -1)
        {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "Poll failed [%s]", strerror(errno));
            exit(1);
        }
        else if (status > 0)
        {
            /* Write to stdout if possible */
            if (stdout_write_index >= 0 && fds[stdout_write_index].revents)
            {
                write_buffer(STDOUT_FILENO, local_echo_buf,
                             &local_echo_pos, &local_echo_end);
                write_buffer(STDOUT_FILENO, serial_to_stdout_buf,
                             &serial_to_stdout_pos, &serial_to_stdout_end);
            }
            /* Write to the serial port if possible */
            if (serial_write_index >= 0 && fds[serial_write_index].revents)
            {
                write_buffer(fd, stdin_to_serial_buf,
                             &stdin_to_serial_pos, &stdin_to_serial_end);
            }

            /* Read from stdin port if possible */
            if (stdin_read_index >= 0 && fds[stdin_read_index].revents)
            {
                int ok, begin = stdin_to_serial_end;

                if (local_echo)
                {
                    ok = read_buffer_echo(STDIN_FILENO, stdin_to_serial_buf,
                                          &stdin_to_serial_end, BUF_SIZE,
                                          local_echo_buf, &local_echo_end,
                                          BUF_SIZE);
                }
                else
                {
                    ok = read_buffer(STDIN_FILENO, stdin_to_serial_buf,
                                     &stdin_to_serial_end, BUF_SIZE);
                }
                if (ok)
                {
                    log_buffer(stdin_to_serial_buf + begin,
                               stdin_to_serial_end - begin);
                }
                else
                    stdin_open = 0;
            }
            /* Read from the serial port if possible */
            if (serial_read_index >= 0 && fds[serial_read_index].revents)
            {
                int begin = serial_to_stdout_end;

                if (!read_buffer(fd, serial_to_stdout_buf,
                                 &serial_to_stdout_end,
                                 BUF_SIZE))
                {
                    close(fd);
                    fd = -1;
                }
                if (g_logfd != -1)
                {
                    log_buffer(serial_to_stdout_buf + begin,
                               serial_to_stdout_end - begin);
                }
            }
        }
    }

    if (isatty(0))
        tcsetattr(0, TCSAFLUSH, &g_saved_stdin_attr);
}

int main(int argc, char *argv[])
{
    struct termios tio;
    int baud = 115200, baudCode, optionIndex, local_echo = 0;
    int fd, c, cflags, mask, ctsrts = 1, xonxoff = 0, flush = 0;
    char *microcom_env, *logname = NULL, *format = NULL;
    struct option longOptions[] =
    {
        {"baud", 1, 0, 'b'},
        {"format", 1, 0, 'f'},
        {"flow-control", 1, 0, 'F'},
        {"flush", 0, 0, 'u'},
        {"help", 0, 0, 'h'},
        {"log", 1, 0, 'l'},
        {"local-echo", 0, 0, 'L'},
        {"version", 0, 0, 'V'},
        {0, 0, 0, 0}
    };

    setlocale(LC_ALL, "");

    microcom_env = getenv("MICROCOM");
    if (microcom_env)
        baud = atoi(microcom_env);

    format = getenv("MICROCOM_FORMAT");

    while ((c = getopt_long(argc, argv, "b:f:F:hl:LV",
                            longOptions, &optionIndex)) != -1)
    {
        switch (c)
        {
        case 'b':
            baud = atoi(optarg);
            break;
        case 'f':
            format = optarg;
            break;
        case 'F':
            if (strcasecmp(optarg, "none") == 0)
            {
                ctsrts = 0;
                xonxoff = 0;
            }
            else if (strcasecmp(optarg, "ctsrts") == 0)
            {
                ctsrts = 1;
                xonxoff = 0;
            }
            else if (strcasecmp(optarg, "xonxoff") == 0)
            {
                ctsrts = 0;
                xonxoff = 1;
            }
            else if (strcasecmp(optarg, "both") == 0)
            {
                ctsrts = 1;
                xonxoff = 1;
            }
            else
            {
                fprintf(stderr, "Unknown flow control mode ('%s')\n", optarg);
                return 1;
            }
            break;
        case 'h':
            help();
            exit(1);
        case 'l':
            logname = optarg;
            break;
        case 'L':
            local_echo = 1;
            break;
        case 'u':
            flush = 1;
            break;
        case 'V':
            puts(VERSION_STRING);
            return 0;
        default:
            printf ("?? getopt returned character code 0%o ??\n", c);
        }
    }

    if (argc - optind < 1)
    {
        help();
        return 1;
    }

    baudCode = baud2code(baud);
    if (baudCode == -1)
    {
        fprintf(stderr, "Unsupported baud rate (%d)\n", baud);
        return 1;
    }

    if (!format)
        format = "N81";

    cflags = parse_format(format, &mask);
    if (cflags == -1)
    {
        fprintf(stderr, "Bad line format [%s]", format);
        exit(1);
    }

    signal(SIGINT, &signal_handler);
    signal(SIGQUIT, &signal_handler);
    signal(SIGTERM, &signal_handler);

    fd = open(argv[optind], O_RDWR | O_NONBLOCK);
    if (fd == -1)
    {
        perror("open");
        return 1;
    }
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CRTSCTS;
    if (!ctsrts)
        tio.c_cflag &= ~CRTSCTS;
    if (xonxoff)
        tio.c_iflag |= IXON | IXOFF;
    tio.c_cflag &= ~mask;
    tio.c_cflag |= cflags;
    cfsetospeed(&tio, baudCode);
    cfsetispeed(&tio, baudCode);
    tcsetattr(fd, TCSANOW, &tio);

    if (flush)
        tcflush(fd, TCIOFLUSH);

    if (argc - optind == 1)
    {
        if (logname)
        {
            g_logfd = open(logname, O_WRONLY | O_APPEND | O_CREAT, 0666);
            if (g_logfd == -1)
            {
                fprintf(stderr, "Cannot open log file `%s' for writing\n",
                        logname);
                return 1;
            }
        }
        interactive(fd, local_echo);
    }
    else
    {
        fcntl(fd, F_SETFL, 0); /* Make it blocking again */
        dup2(fd, 0);
        dup2(fd, 1);
        close(fd);
        execvp(argv[optind + 1], &argv[optind + 1]);
    }

    return 0;
}

void help(void)
{
    puts("Usage: microcom [options] <device> [command] [argument] ...\n"
         "  -b BAUD, --baud=BAUD    Set baud rate. [115200]\n"
         "  -f FORMAT, --format=FORMAT\n"
         "                          Line format as a three character combination. [N81]\n"
         "  -F MODE, --flow-control=MODE\n"
         "                          Configure flow control, where MODE is none, ctsrts,\n"
         "                          xonxoff or both. [ctsrts]\n"
         "  --flush                 Flush I/O at program start\n"
         "  -h, --help              Show help.\n"
         "  -L, --local-echo        Enable local echo\n"
         "  -l FILE, --log=FILE     Log input communication to file.\n"
         "  -V, --version           Show version.");
}

int baud2code(int baud)
{
    const UART_BaudTable_t *entry = sgUART_BaudTable;

    while (entry->baud != -1)
    {
        if (entry->baud == baud)
            return entry->code;
        entry++;
    }

    return -1;
}

int
parse_format(const char *format, int *mask)
{
    char parity_char, size_char, stop_char;
    int cflags = 0;

    *mask = CSTOPB | PARENB | PARODD | CSIZE;

    /* Two formats are allowed: N81 and N-8-1 */
    if (strlen(format) == 3)
    {
        parity_char = format[0];
        size_char = format[1];
        stop_char = format[2];
    }
    else if (strlen(format) == 5 && format[1] == '-' && format[3] == '-')
    {
        parity_char = format[0];
        size_char = format[2];
        stop_char = format[4];
    }
    else
        return -1;

    switch (parity_char)
    {
    case 'n':
    case 'N':
        break;
    case 'o':
    case 'O':
        cflags |= PARENB | PARODD;
        break;
    case 'e':
    case 'E':
        cflags |= PARENB;
        break;
    default:
        return -1;
    }

    switch (size_char)
    {
    case '5':
        cflags |= CS5;
        break;
    case '6':
        cflags |= CS6;
        break;
    case '7':
        cflags |= CS7;
        break;
    case '8':
        cflags |= CS8;
        break;
    default:
        return -1;
    }

    switch (stop_char)
    {
    case '1':
        break;
    case '2':
        cflags |= CSTOPB;
        break;
    default:
        return -1;
    }

    return cflags;
}
