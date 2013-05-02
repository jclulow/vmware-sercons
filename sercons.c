#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <strings.h>
#include <pthread.h>

typedef enum end_reason {
	ER_NONE = 0,
	ER_ESCAPE_CHAR = 1,
	ER_ERROR = 2,
	ER_EOF = 3
} end_reason_t;

static int orig_tios_stored = 0;
static struct termios orig_tios;

static pthread_cond_t g_end_cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_end_mtx = PTHREAD_MUTEX_INITIALIZER;
static end_reason_t g_end_reason = ER_NONE;

static void *
post_end(end_reason_t reason)
{
	if (reason == ER_NONE)
		abort();

	if (pthread_mutex_lock(&g_end_mtx) != 0)
		abort();

	if (g_end_reason == ER_NONE) {
		g_end_reason = reason;
		if (pthread_cond_broadcast(&g_end_cv) != 0)
			abort();
	}

	if (pthread_mutex_unlock(&g_end_mtx) != 0)
		abort();

	return (NULL);
}

static end_reason_t
wait_for_end(void)
{
	end_reason_t reason = ER_NONE;
	for (;;) {
		if (pthread_mutex_lock(&g_end_mtx) != 0)
			abort();
		if (pthread_cond_wait(&g_end_cv, &g_end_mtx) != 0)
			abort();

		reason = g_end_reason;

		if (pthread_mutex_unlock(&g_end_mtx) != 0)
			abort();

		if (reason != ER_NONE)
			return (reason);
	}
}

typedef struct copy_args {
	int ca_fd_src;
	int ca_fd_src_is_terminal;
	int ca_fd_dst;

	char ca_escape_char;
} copy_args_t;

void *
copy_thread(void *arg)
{
	char c1 = '\0', c2 = '\0';
	copy_args_t *ca = arg;

	for (;;) {
		ssize_t sz;
		char c;

		sz = read(ca->ca_fd_src, &c, 1);
		switch (sz) {
		case 0:
			if (ca->ca_fd_src_is_terminal)
				continue;
			else
				return (post_end(ER_EOF));
		case 1:
			break;
		default:
			return (post_end(ER_ERROR));
		}

		if ((sz = write(ca->ca_fd_dst, &c, 1)) != 1) {
			return (post_end(ER_ERROR));
		}

		if (ca->ca_escape_char == '\0')
			continue;

		/*
		 * Escape character sequence detection:
		 */
		if ((c2 == '\r' || c2 == '\n') &&
		    c1 == ca->ca_escape_char &&
		    c == '.') {
			return (post_end(ER_ESCAPE_CHAR));
		}
		c2 = c1;
		c1 = c;
	}
}

static int
reset_mode(int term_fd)
{
	if (tcsetattr(term_fd, TCSAFLUSH, &orig_tios) == -1)
		return (-1);

	return (0);
}

static int
raw_mode(int term_fd)
{
	struct termios raw;

	if (orig_tios_stored == 0) {
		orig_tios_stored = 1;
		if (tcgetattr(term_fd, &orig_tios) == -1) {
			return (-1);
		}
	}

	raw = orig_tios;

	/*
	 * Various raw-mode settings:
	 */
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	/*
	 * We want read() on the tty to block until there is at least one byte:
	 */
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 10;

	if (tcsetattr(term_fd, TCSAFLUSH, &raw) == -1)
		return (-1);

	return (0);
}

int
make_conn(const char *path)
{
	int fd;
	struct sockaddr_un ua;

	if (strlen(path) > sizeof (ua.sun_path) - 1)
		return (-1);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		return (-1);

	bzero(&ua, sizeof (ua));
	ua.sun_family = AF_UNIX;
	ua.sun_len = sizeof (ua);
	strcpy(ua.sun_path, path);

	if (connect(fd, (struct sockaddr *)&ua, ua.sun_len) == -1) {
		(void) close(fd);
		return (-1);
	}

	return (fd);
}

int
main(int argc, char **argv)
{
	copy_args_t ca_a, ca_b;
	pthread_t thr_a, thr_b;
	int connfd = -1, termfd;
	end_reason_t reason;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <socket_path>\n", argv[0]);
		exit(1);
	}

	while (connfd == -1) {
		static int firstloop = 1;
		if ((connfd = make_conn(argv[1])) == -1) {
			if (errno == ENOENT || errno == EACCES ||
			    errno == EPERM || errno == ECONNREFUSED) {
				fprintf(stderr, firstloop ? " * Waiting for "
				    "socket..." : ".");
				fflush(stderr);
				sleep(1);
			} else {
				perror("opening serial socket");
				exit(1);
			}
		}
		firstloop = 0;
	}

	fprintf(stderr, "\n * Connected.  Escape sequence is <CR>#.\n");

	if ((termfd = open("/dev/tty", O_RDWR | O_NOCTTY)) == -1) {
		perror("opening controlling terminal");
		exit(1);
	}

	if (raw_mode(termfd) == -1) {
		fprintf(stderr, "could not set raw mode on terminal\n");
		exit(1);
	}

	/*
	 * Read from terminal, send to remote:
	 */
	ca_a.ca_fd_src = termfd;
	ca_a.ca_fd_src_is_terminal = 1;
	ca_a.ca_fd_dst = connfd;
	ca_a.ca_escape_char = '#';
	if (pthread_create(&thr_a, NULL, copy_thread, &ca_a) != 0)
		goto out;

	/*
	 * Read from remote, send to terminal:
	 */
	ca_b.ca_fd_src = connfd;
	ca_b.ca_fd_src_is_terminal = 0;
	ca_b.ca_fd_dst = termfd;
	ca_b.ca_escape_char = '\0';
	if (pthread_create(&thr_b, NULL, copy_thread, &ca_b) != 0)
		goto out;

	reason = wait_for_end();
out:
	/*
	 * Attempt to reset terminal and pty attributes:
	 */
	(void) write(termfd, "\e[0m", 4);
	reset_mode(termfd);

	switch (reason) {
	case ER_ERROR:
		fprintf(stderr, "\n * Unknown Error.\n");
		exit(1);
		break;
	case ER_ESCAPE_CHAR:
		fprintf(stderr, "\n * Escape Character Received.\n");
		exit(0);
		break;
	case ER_EOF:
		fprintf(stderr, "\n * EOF on read.\n");
		exit(1);
		break;
	default:
		abort();
	}
}
