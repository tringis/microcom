#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
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
        {     50,     B50 },
        {     75,     B75 },
        {    110,    B110 },
        {    134,    B134 },
        {    150,    B150 },
        {    200,    B200 },
        {    300,    B300 },
        {    600,    B600 },
        {   1200,   B1200 },
        {   1800,   B1800 },
        {   2400,   B2400 },
        {   4800,   B4800 },
        {   9600,   B9600 },
        {  19200,  B19200 },
        {  38400,  B38400 },
        {  57600,  B57600 },
        { 115200, B115200 },
        { 230400, B230400 },
        { -1, -1 }
};

void signal_handler(int signo)
{
	longjmp(env, signo);
}

void interactive(int fd)
{
	struct termios tio;
	fd_set rfds;
	int status, n;
	char buf[BUF_SIZE];

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
		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		FD_SET(fd, &rfds);
		status = select(fd+1, &rfds, NULL, NULL, NULL);
		if (status == -1)
			perror("select");
		else if (status > 0)
		{
			if (FD_ISSET(0, &rfds))
			{
				n = read(0, buf, BUF_SIZE);
				if (n > 0)
  					write(fd, buf, n);
				else if (n == -1)
				{
					perror("read(0)");
					break;
				}
			}
			if (FD_ISSET(fd, &rfds))
			{
				n = read(fd, buf, BUF_SIZE);
				if (n > 0)
					write(1, buf, n);
				else if (n == -1 && errno != EAGAIN)
				{
					perror("read(tty)");
					break;
				}
			}
		}
	}
}

int main(int argc, char *argv[])
{
	struct termios tio;
	int baud, baudCode = B115200, optionIndex;
	int fd, c;
	struct option longOptions[] =
	{
		{"baud", 1, 0, 'b'},
		{"help", 0, 0, 'h'},
		{0, 0, 0, 0}
	};

	setlocale(LC_ALL, "");

	while ((c = getopt_long(argc, argv, "b:h", longOptions, &optionIndex)) != -1)
	{
		switch (c)
		{
		case 'b':
			baud = atoi(optarg);
			baudCode = baud2code(baud);
			if (baudCode == -1)
			{
				fprintf(stderr, "Unsupported baud rate (%d)\n", baud);
				return 1;
			}
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
		fputs("Usage: sterm [options] <device> [cmd] ...\n", stderr);
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
	cfsetospeed(&tio, baudCode);
	cfsetispeed(&tio, baudCode);
	tcsetattr(fd, TCSAFLUSH, &tio);

	if (argc - optind == 1)
	{
		interactive(fd);
		close(fd);
	}
	else
	{
		dup2(fd, 0);
		dup2(fd, 1);
		close(fd);
		execvp(argv[2], &argv[2]);
	}

	return 0;
}

void help(void)
{
	puts("Usage: sterm [options] <device> [cmd] ...\n"
		 "  -b BAUD  --baud=BAUD  Set baud rate\n"
		 "  -h  --help  Help");
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
