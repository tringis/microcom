/*
 *  Copyright 2002 Tobias Ringstrom
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#define VERSION_STRING "microcom version 0.9.8"

#define BUF_SIZE 4096

typedef struct UART_BaudTable {
	int baud;
	int code;
} UART_BaudTable_t;

jmp_buf g_env;
struct termios g_saved_stdin_attr;
FILE *g_logfile;

void help(void);
int baud2code(int baud);
int parse_format(const char *format, int *mask);
void copy_from_stdin(int fdout, int fdin, int pause);
void copy_from_serial(int fdout, int fdin);

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

void wait_for_write(int fd)
{
	struct pollfd pfd[1];

	pfd[0].fd = fd;
	pfd[0].events = POLLOUT;

	while (poll(pfd, 1, -1) == -1 && errno == EINTR)
		;
}

void copy_from_stdin(int fdout, int fdin, int pause)
{
	char buf[BUF_SIZE];
	int i, n, m;

	n = read(fdin, buf, BUF_SIZE);
	if (n > 0)
	{
		i = 0;
		while (i < n)
		{
			m = write(fdout, buf + i, pause ? 1 : n - i);
			if (m < 0)
			{
				if (errno == EINTR)
					continue;
				if (errno == EAGAIN)
				{
					wait_for_write(fdout);
					continue;
				}
				fprintf(stderr, "Write error [%s]", strerror(errno));
				exit(1);
			}
			if (pause)
			{
				usleep(pause);
				/* Copy from the serial port */
				copy_from_serial(fdin, fdout);
			}
			i += m;
		}
	}
	else if (n == -1 && errno != EAGAIN)
	{
		fprintf(stderr, "Read error [%s]", strerror(errno));
		exit(1);
	}
}


void copy_from_serial(int fdout, int fdin)
{
	char buf[BUF_SIZE];
	int i, n, m;

	n = read(fdin, buf, BUF_SIZE);
	if (n > 0)
	{
		i = 0;
		while (i < n)
		{
			m = write(fdout, buf + i, n - i);
			if (m < 0)
			{
				if (errno == EINTR)
					continue;
				if (errno == EAGAIN)
				{
					wait_for_write(fdout);
					continue;
				}
				fprintf(stderr, "Write error [%s]", strerror(errno));
				exit(1);
			}
			if (g_logfile)
			{
				fwrite(buf, m, 1, g_logfile);
				fflush(g_logfile);
			}
			i += m;
		}
	}
	else if (n == -1 && errno != EAGAIN)
	{
		fprintf(stderr, "Read error [%s]", strerror(errno));
		exit(1);
	}
}


void interactive(int fd, int pause)
{
	struct termios tio;
	struct pollfd fds[2];
	int status;

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
		return;
	}

	for (;;)
	{
		fds[0].fd = STDIN_FILENO;
		fds[0].events = POLLIN;
		fds[1].fd = fd;
		fds[1].events = POLLIN;
		status = poll(fds, 2, -1);
		if (status == -1)
		{
			if (errno == EINTR)
				continue;
			fprintf(stderr, "Poll failed [%s]", strerror(errno));
			exit(1);
		}
		else if (status > 0)
		{
			/* Is stdin readable? */
			if (fds[0].revents & (POLLIN | POLLERR))
				copy_from_stdin(fd, STDIN_FILENO, pause);
			/* Is the serial port readable? */
			if (fds[1].revents & (POLLIN | POLLERR))
				copy_from_serial(STDOUT_FILENO, fd);
		}
	}
}

int main(int argc, char *argv[])
{
	struct termios tio;
	int baud = 115200, baudCode, optionIndex;
	int pause_us = 0; /* Pause between characters in us. */
	int fd, c, flow = 1, cflags, mask;
	char *microcom_env, *logname = NULL, *format = NULL;
	struct option longOptions[] =
	{
		{"baud", 1, 0, 'b'},
		{"format", 1, 0, 'f'},
		{"no-flow", 1, 0, 'F'},
		{"help", 0, 0, 'h'},
		{"pause", 1, 0, 'p'},
		{"log", 1, 0, 'l'},
		{"version", 0, 0, 'V'},
		{0, 0, 0, 0}
	};

	setlocale(LC_ALL, "");

	microcom_env = getenv("MICROCOM");
	if (microcom_env)
		baud = atoi(microcom_env);

	format = getenv("MICROCOM_FORMAT");

	while ((c = getopt_long(argc, argv, "b:f:Fhp:l:V", longOptions, &optionIndex)) != -1)
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
			flow = 0;
			break;
		case 'h':
			help();
			exit(1);
		case 'p':
			pause_us = atoi(optarg);
			break;
		case 'l':
			logname = optarg;
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
	if (!flow)
		tio.c_cflag &= ~CRTSCTS;
	tio.c_cflag &= ~mask;
	tio.c_cflag |= cflags;
	cfsetospeed(&tio, baudCode);
	cfsetispeed(&tio, baudCode);
	tcsetattr(fd, TCSAFLUSH, &tio);

	if (logname)
	{
		g_logfile = fopen(logname, "a");
		if (!g_logfile)
		{
			fprintf(stderr, "Cannot open log file %s for writing\n", logname);
			return 1;
		}
	}

	if (argc - optind == 1)
	{
		interactive(fd, pause_us);
		tcflush(fd, TCIOFLUSH);
		close(fd);
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
	puts("Usage: microcom [options] <device> [cmd] ...\n"
		 "  -b BAUD  --baud=BAUD  Set baud rate [default 115200]\n"
		 "  -f FORMAT  --format=FORMAT  Line format [default N81]\n"
		 "  -F  --no-flow         Disable flow control\n"
		 "  -h  --help            Show help\n"
		 "  -p US  --pause=US     Pause between characters [microseconds]\n"
		 "  -l FILE  --log=FILE   Log input communication to file");
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
