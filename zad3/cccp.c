#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/vfs.h>
#include <linux/magic.h>

#define EXT2_IOC_COWDUP _IOW('f', 25, int)

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s OLD NEW\n", argv[0]);
		return EXIT_FAILURE;
	}

	int fd1 = open(argv[1], O_RDONLY);
	if (fd1 < 0) {
		fprintf(stderr, "Error opening source: %m\n");
		return EXIT_FAILURE;
	}

	struct statfs buf;
	int err = fstatfs(fd1, &buf);
	if (err) {
		fprintf(stderr, "Unable to fstatfs: %m\n");
		return EXIT_FAILURE;
	}

	if (buf.f_type != EXT2_SUPER_MAGIC) {
		fprintf(stderr, "This is not an EXT2 file system!\n");
		return EXIT_FAILURE;
	}

	int fd2 = open(argv[2], O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (fd2 < 0) {
		fprintf(stderr, "Error creating target: %m\n");
		return EXIT_FAILURE;
	}

	if(ioctl(fd1, EXT2_IOC_COWDUP, &fd2)) {
		fprintf(stderr, "Error duplicating: %m\n");
		unlink(argv[2]);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

