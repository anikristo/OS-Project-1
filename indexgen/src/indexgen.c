/*
 ============================================================================
 Name        : indexgen.c
 Author      : Ani Kristo
 ============================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>
#include <errno.h>

// Definitions
#define MIN_WORKERS	1
#define MAX_WORKERS	5
#define MAX_WORD_LENGTH 64
#define MAX_WORD_COUNT 100
#define MQ_NAME_SZ 5
#define MQ_NAME_TMPL "/mq_w%d"

// Function prototypes
mqd_t* create_mq(int);
int delete_mq(mqd_t*, int);

// Execution starts here
int main(int argc, char** argv) {

	// Check if the correct command line arguments are passed
	if (argc < 4)
		puts(
				"Insufficient parameters passed!\nMake sure you enter: indexgen <n> <infile> <outfile>");
	else if (argc > 4)
		puts(
				"Too many parameters passed!\nMake sure you enter: indexgen <n> <infile> <outfile>");
	else if (atoi(argv[1]) < MIN_WORKERS || atoi(argv[1]) > MAX_WORKERS)
		printf(
				"%s Make sure the first argument (n) is an integer between 1 and 5!",
				argv[1]);
	else {

		// Valid input was given
		int n = atoi(argv[1]); // n is the number of workers
		mqd_t* descriptors = NULL;

		//TODO create n message queues
		descriptors = create_mq(n);

		// TODO create n child processes

		// TODO assign one message queue per child

		// TODO open the input file

		// TODO read each word, convert to lowercase

		// TODO send word and line number to child

		// TODO wait for all children to finish

		// TODO merge the outfiles and sort

		// TODO delete message queues and temporary output files
		printf("%d", delete_mq(descriptors, n));

		// exit
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

mqd_t* create_mq(int n) {

	mqd_t* mq_descriptors = malloc(n * sizeof(mqd_t));
	char mq_name[MQ_NAME_SZ];
	int flags = O_CREAT | O_EXCL; // TODO not sure about excl
	mode_t permissions = S_IRUSR | S_IWUSR;
	struct mq_attr *attributes = malloc(sizeof(struct mq_attr));

	// Define the mq attributes
	attributes->mq_maxmsg = MAX_WORD_COUNT;
	attributes->mq_msgsize = MAX_WORD_LENGTH;

	// Create message queues
	int i;
	for (i = 0; i < n; i++) {
		sprintf(mq_name, MQ_NAME_TMPL, i);
		mq_descriptors[i] = mq_open(mq_name, flags, permissions, NULL); //TODO
		printf("%d %d\n", mq_descriptors[i], errno);

		if(mq_descriptors[i] == -1){
			puts("Unable to create message queues!");
			exit(EXIT_FAILURE);
		}
	}

	return mq_descriptors;
}

int delete_mq(mqd_t* descriptors, int n) {

	char mq_name[MQ_NAME_SZ];

	int i, result = 0;
	for (i = 0; i < n; i++) {
		sprintf(mq_name, MQ_NAME_TMPL, i);
		result += mq_unlink(mq_name);
	}

	return result == 0 ? 0 : -1;
}
