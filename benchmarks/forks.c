/* From 4.4 BSD sys/tests/benchmarks/forks.c. */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/wait.h>

/*
 * Benchmark program to calculate fork+wait
 * overhead (approximately).  Process
 * forks and exits while parent waits.
 * The time to run this program is used
 * in calculating exec overhead.
 */

int
main(argc, argv)
	int argc;
	char *argv[];
{
	register int nforks, i;
	char *cp;
	int pid, child, status, brksize;
	time_t starttime, endtime;

	if (argc < 3) {
		printf("usage: %s number-of-forks sbrk-size\n", argv[0]);
		exit(1);
	}
	nforks = atoi(argv[1]);
	if (nforks < 0) {
		printf("%s: bad number of forks\n", argv[1]);
		exit(2);
	}
	brksize = atoi(argv[2]);
	if (brksize < 0) {
		printf("%s: bad size to sbrk\n", argv[2]);
		exit(3);
	}

	time (&starttime);
	cp = (char *)sbrk(brksize);
	if (cp == (void *)-1) {
		perror("sbrk");
		exit(4);
	}
	for (i = 0; i < brksize; i += 1024)
		cp[i] = i;
	while (nforks-- > 0) {
		child = fork();
		if (child == -1) {
			perror("fork");
			exit(-1);
		}
		if (child == 0)
			_exit(-1);
		while ((pid = wait(&status)) != -1 && pid != child)
			;
	}
	time (&endtime);
	printf ("Time: %d seconds.\n", (int) (endtime - starttime));
	exit(0);
}
