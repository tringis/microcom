#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <termios.h>
#include <setjmp.h>
#include <signal.h>

#define BUF_SIZE 4096

jmp_buf env;
struct termios saved_stdin_attr;

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
	int fd;

	if (argc < 2)
	{
		fputs("Usage: sterm <device>\n", stderr);
		return 1;
	}

	signal(SIGINT, &signal_handler);
	signal(SIGQUIT, &signal_handler);
	signal(SIGTERM, &signal_handler);

  	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (fd == -1)
	{
		perror("open");
		return 1;
	}
	tcgetattr(fd, &tio);
	cfmakeraw(&tio);
	tio.c_cflag |= CLOCAL | CRTSCTS;
	cfsetospeed(&tio, B115200);
	cfsetispeed(&tio, B115200);
	tcsetattr(fd, TCSAFLUSH, &tio);

	if (argc == 2)
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
