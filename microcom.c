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

#define BUF_SIZE 4096

typedef struct UART_BaudTable {
	int baud;
	int code;
} UART_BaudTable_t;

jmp_buf env;
struct termios saved_stdin_attr;

void help(void);
int baud2code(int baud);

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
	longjmp(env, signo);
}

void wait_for_write(int fd)
{
	struct pollfd pfd[1];

	pfd[0].fd = fd;
	pfd[0].events = POLLOUT;

	poll(pfd, 1, -1);
}

void copy(int fdout, int fdin)
{
	char buf[BUF_SIZE];
	int i, n, m;

	n = read(fdin, buf, BUF_SIZE);
	if (n > 0)
	{
		i = 0;
		while (i < n)
		{
			m = write(fdout, buf, n);
			if (m < 0)
			{
				if (errno == EAGAIN)
				{
					wait_for_write(fdout);
					continue;
				}
				fprintf(stderr, "Write error [%s]", strerror(errno));
				exit(1);
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


void interactive(int fd)
{
	struct termios tio;
	struct pollfd fds[2];
	int status;

	if (isatty(0))
	{
		tcgetattr(0, &saved_stdin_attr);
		memcpy(&tio, &saved_stdin_attr, sizeof(tio));
		tio.c_iflag &= ~ICRNL;
		tio.c_lflag &= ~(ICANON | ECHO);
		tio.c_lflag |= ISIG;
		tcsetattr(0, TCSAFLUSH, &tio);
	}

	if (setjmp(env) != 0)
	{
		if (isatty(0))
			tcsetattr(0, TCSAFLUSH, &saved_stdin_attr);
		puts("Bye!");
		return;
	}

	for (;;)
	{
		fds[0].fd = 0;
		fds[0].events = POLLIN;
		fds[1].fd = fd;
		fds[1].events = POLLIN;
		status = poll(fds, 2, -1);
		if (status == -1)
		{
			fprintf(stderr, "Poll failed [%s]", strerror(errno));
			exit(1);
		}
		else if (status > 0)
		{
			if (fds[0].revents & (POLLIN | POLLERR))
				copy(fds[1].fd, fds[0].fd);
			if (fds[1].revents & (POLLIN | POLLERR))
				copy(fds[0].fd, fds[1].fd);
		}
	}
}

int main(int argc, char *argv[])
{
	struct termios tio;
	int baud = 115200, baudCode, optionIndex;
	int fd, c, flow = 1;
	char *sterm_env;
	struct option longOptions[] =
	{
		{"baud", 1, 0, 'b'},
		{"no-flow", 1, 0, 'F'},
		{"help", 0, 0, 'h'},
		{0, 0, 0, 0}
	};

	setlocale(LC_ALL, "");

	sterm_env = getenv("STERM");
	if (sterm_env)
		baud = atoi(sterm_env);

	while ((c = getopt_long(argc, argv, "b:Fh", longOptions, &optionIndex)) != -1)
	{
		switch (c)
		{
		case 'b':
			baud = atoi(optarg);
			break;
		case 'F':
			flow = 0;
			break;
		case 'h':
			help();
			exit(1);
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
	cfsetospeed(&tio, baudCode);
	cfsetispeed(&tio, baudCode);
	tcsetattr(fd, TCSAFLUSH, &tio);

	if (argc - optind == 1)
	{
		interactive(fd);
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
	puts("Usage: sterm [options] <device> [cmd] ...\n"
		 "  -b BAUD  --baud=BAUD  Set baud rate [default 115200]\n"
		 "  -F  --no-flow         Disable flow control\n"
		 "  -h  --help            Help");
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
