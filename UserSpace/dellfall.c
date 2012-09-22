/* Disk protection for DELL machines based on hpfall.c from linux3.5 Documentation
 *
 * Copyright 2008 Eric Piel
 * Copyright 2009 Pavel Machek <pavel@ucw.cz>
 * Copyright 2012 Sonal Santan
 *
 * GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sched.h>
#include <syslog.h>

static char unload_heads_path[64];
static char device_path[32];
static const char app_name[] = "DELL FREE FALL";

static int set_unload_heads_path(char *device)
{
	char devname[64];

	if (strlen(device) <= 5 || strncmp(device, "/dev/", 5) != 0)
		return -EINVAL;
	strncpy(devname, device + 5, sizeof(devname));
        strncpy(device_path, device, sizeof(device_path));

	snprintf(unload_heads_path, sizeof(unload_heads_path),
                 "/sys/block/%s/device/unload_heads", devname);
	return 0;
}

static int valid_disk(void)
{
	int fd = open(unload_heads_path, O_RDONLY);
	if (fd < 0) {
		perror(unload_heads_path);
		return 0;
	}

	close(fd);
	return 1;
}

static void write_int(char *path, int i)
{
	char buf[1024];
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(1);
	}
	sprintf(buf, "%d", i);
	if (write(fd, buf, strlen(buf)) != strlen(buf)) {
		perror("write");
		exit(1);
	}
        const char *str = (i == 0) ? "Unparked" : "Parked";
        syslog (LOG_INFO, "%s %s disk head\n", str, device_path);
	close(fd);
}

static void protect(int seconds)
{
	write_int(unload_heads_path, seconds*1000);
}


static void ignore_me(int signum)
{
	protect(0);
}

int main(int argc, char **argv)
{
	int fd, ret;
	struct sched_param param;

	if (argc == 1)
		ret = set_unload_heads_path("/dev/sda");
	else if (argc == 2)
		ret = set_unload_heads_path(argv[1]);
	else
		ret = -EINVAL;

	if (ret || !valid_disk()) {
		fprintf(stderr, "usage: %s <device> (default: /dev/sda)\n",
                        argv[0]);
		exit(1);
	}

	fd = open("/dev/freefall", O_RDONLY);
	if (fd < 0) {
		perror("/dev/freefall");
		return EXIT_FAILURE;
	}

	daemon(0, 0);
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &param);
	mlockall(MCL_CURRENT|MCL_FUTURE);

	signal(SIGALRM, ignore_me);
        openlog(app_name,  LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
        
	for (;;) {
		unsigned char count;

		ret = read(fd, &count, sizeof(count));
		alarm(0);
		if ((ret == -1) && (errno == EINTR)) {
			/* Alarm expired, time to unpark the heads */
			continue;
		}

		if (ret != sizeof(count)) {
			perror("read");
			break;
		}

		protect(21);
                alarm(2);
	}
        
        closelog();
	close(fd);
	return EXIT_SUCCESS;
}
