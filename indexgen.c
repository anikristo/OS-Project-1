#include <mqueue.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>

// Definitions
#define MIN_WORKERS	1
#define MAX_WORKERS	5
#define MAX_WORD_LENGTH 64
#define MAX_WORD_COUNT 100
#define MQ_NAME_SZ 5
#define MQ_NAME_TMPL "/mq_w%d"
#define TMP_OUTFILE_TMPL "outfile-%d"
#define MSG_SIZE sizeof(struct mq_item)

#define ASCII_a 97
#define ASCII_z 122
#define ENG_CHAR_CNT 26

#define TRUE 1
#define FALSE 0

/*
 * The consumer will maintain a linked list of wor_item nodes for
 * every new word that it processes. These node will maintain information
 * about the word processed and a reference to the list of line numbers
 * where the word appears. This list will also be a linked list with
 * nodes of type lines_item.
 */


// Structure for an item passed as a message
struct mq_item {
	char word[MAX_WORD_LENGTH];
	int line;
};

// Struct for the line numbers kept inside the word_item
struct lines_item{
	int line_nr;
	struct lines_item* next;
};

// Struct for the word items
struct word_item {
	char word[MAX_WORD_LENGTH];
	struct lines_item* line_nrs;
	struct word_item* next;
};

// Function prototypes
void generate_index(mqd_t, int);
void add_word_node(struct word_item**, int, const char*);
void write_file(struct word_item*, char*);
void free_mem_word(struct word_item*);
void free_mem_lines(struct lines_item*);
int word_exists(struct word_item**, const char*, struct word_item**);
int line_exists(struct word_item*, int);
mqd_t* create_mq();
int delete_mq(mqd_t*);
void create_children(mqd_t*);
void wait_children();
void read_input(char*, mqd_t*);
void merge_outfiles(char*);
void read_outfile(struct word_item**, FILE*);
void mergesort(struct word_item**);
struct word_item* merge_sorted(struct word_item*, struct word_item*);
void split(struct word_item*, struct word_item**, struct word_item**);


// Global variable
int n;

// =======================================================
// 						PRODUCER
// =======================================================

// Execution starts here
int main(int argc, char** argv) {

	// Check if the correct command line arguments are passed
	if (argc < 4)
		puts("Insufficient parameters passed!\nMake sure you enter: "
				"indexgen <n> <infile> <outfile>");
	else if (argc > 4)
		puts("Too many parameters passed!\nMake sure you enter: "
				"indexgen <n> <infile> <outfile>");
	else if (atoi(argv[1]) < MIN_WORKERS || atoi(argv[1]) > MAX_WORKERS)
		printf("%s Make sure the first argument (n) "
				"is an integer between 1 and 5!\n", argv[1]);
	else {

		// Valid input was given
		n = atoi(argv[1]); // n is the number of workers
		mqd_t* descriptors = NULL;

		// create n message queues
		descriptors = create_mq();

		// create n child processes
		create_children(descriptors);

		// open the input file
		read_input(argv[2], descriptors);

		// wait for all children to finish
		wait_children();

		// merge the sorted outfiles
		merge_outfiles(argv[3]);

		// delete message queues
		int result = delete_mq(descriptors);
		if (result == -1) {
			perror("Cannot delete message queues!");
		}

		// delete temporary output files
		char filename[MAX_WORD_LENGTH];
		int i;
		for (i = 0; i < n; i++) {
			sprintf(filename, TMP_OUTFILE_TMPL, i);
			unlink(filename);
		}

		// exit
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

mqd_t* create_mq() {

	mqd_t* mq_descriptors = malloc(n * sizeof(mqd_t));
	char mq_name[MQ_NAME_SZ];
	int flags = O_CREAT | O_RDWR;
	mode_t permissions = 0666;

	// Create message queues
	int i;
	for (i = 0; i < n; i++) {
		sprintf(mq_name, MQ_NAME_TMPL, i);
		mq_descriptors[i] = mq_open(mq_name, flags, permissions, NULL);

		if (mq_descriptors[i] == -1) {
			perror("Unable to create message queues!");
			free(mq_descriptors);
			exit(EXIT_FAILURE);
		}
	}
	return mq_descriptors;
}

int delete_mq(mqd_t* descriptors) {

	char mq_name[MQ_NAME_SZ];

	int i, result = 0;
	for (i = 0; i < n; i++) {
		sprintf(mq_name, MQ_NAME_TMPL, i);
		result += mq_unlink(mq_name);
	}

	free(descriptors);
	return result == 0 ? 0 : -1;
}

void create_children(mqd_t* descriptors) {
	int pid;
	int i;

	for (i = 0; i < n; i++) {
		if ((pid = fork()) < 0) {
			perror("Cannot create children_pids! EXITING");
			exit(EXIT_FAILURE);
		} else if (pid == 0) { // Only child process enters here
			// call the child to do some work
			generate_index(descriptors[i], i);
			exit(EXIT_SUCCESS);
		}
	}
}

void wait_children() {
	pid_t pid;
	int sts;

	int i = n;
	while (i > 0) {
		pid = wait(&sts);
		if (sts != 0)
			printf("Child with PID %ld exited with status: 0x%x\n", (long) pid,
					sts);
		--i;
	}
}

void read_input(char* filename, mqd_t* descriptors) {
	FILE* infile = fopen(filename, "r");

	// Open input file
	if (!infile) {
		perror("Cannot open input file! Make sure it exists.");
		exit(EXIT_FAILURE);
	} else {

		// Maintain an array that shows the worker numbers for each letter
		// At index 0 the cell will show the number of the worker for words that start with letter a
		// At index 1 -> b
		// ...
		// At index 25 --> z
		int worker_indices[ENG_CHAR_CNT];
		int i, j, step = ENG_CHAR_CNT / n;
		for (i = 0; i < n; i++) {
			for (j = 0; j < step; j++) {
				worker_indices[i * step + j] = i;
			}

		}

		// the last ENG_CHAR_CNT % n letter of the alphabet
		for (i = ENG_CHAR_CNT % n; i > 0; i--) {
			worker_indices[ENG_CHAR_CNT - i] = n - 1;
		}

		// Start reading the input
		int line_cnt = 0;
		char* cur_line = NULL;
		size_t length = 0;
		ssize_t read_size;
		while ((read_size = getline(&cur_line, &length, infile)) != -1) {

			// increment line count
			line_cnt++;

			// remove the end-of-line character
			if (length > 1 && cur_line[read_size - 1] == '\n') {
				cur_line[read_size - 1] = '\0';
			}

			// convert to lower case
			char* p;
			for (p = cur_line; *p; p++) {
				*p = tolower(*p);
			}

			// read words
			int initial_char_distance;
			struct mq_item mq_msg;
			int result;

			char* dlmt = " ";
			char* word = strtok(cur_line, dlmt);

			while (word != NULL) {
				// Send word to respective message queue
				initial_char_distance = word[0] - ASCII_a;
				mq_msg.line = line_cnt;
				strcpy(mq_msg.word, word);

				mqd_t mqd = descriptors[worker_indices[initial_char_distance]];
				result = mq_send(mqd, (char*) &mq_msg, MSG_SIZE, 1);

				if (result == -1) {
					perror("Message sending failed. EXITING");
					exit(EXIT_FAILURE);
				}

				// Read next word
				word = strtok(NULL, dlmt);
			}

			free(word);
		}
		free(cur_line);

		// Send sentinel node to notify termination
		struct mq_item mq_msg;
		for (i = 0; i < n; i++) {
			mq_msg.line = -1;
			strcpy(mq_msg.word, "");
			mq_send(descriptors[i], (char*) &mq_msg, MSG_SIZE, 0);
		}
	}

	fclose(infile);
}

void merge_outfiles(char* output_filename) {
	// Open all outfiles
	FILE** tmp_outfiles = malloc(n * sizeof(FILE*));
	int i;
	char tmp_filename[MAX_WORD_LENGTH];

	// Open output file
	FILE* output_file = fopen(output_filename, "w");
	if( !output_file){
		perror("Cannot create output file! EXITING");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < n; i++) {
		sprintf(tmp_filename, TMP_OUTFILE_TMPL, i);
		tmp_outfiles[i] = fopen(tmp_filename, "r");
		if (!tmp_outfiles[i]) {
			perror("Cannot open temporary output file! Make sure it exists.");
			free(tmp_outfiles);
			exit(EXIT_FAILURE);
		} else { // read file char by char
			char c; // random initialization
			while((c = (char)fgetc(tmp_outfiles[i])) != EOF){
				fputc(c, output_file);
			}

			fclose(tmp_outfiles[i]);
		}
	}
	free(tmp_outfiles);
}

void read_outfile(struct word_item** root, FILE* f) {
	char* cur_line = NULL;
	size_t length = 0;
	ssize_t read_size;
	while ((read_size = getline(&cur_line, &length, f)) != -1) {

		// remove the end-of-line character
		if (length > 1 && cur_line[read_size - 1] == '\n') {
			cur_line[read_size - 1] = '\0';
		}

		// read word
		char* dlmt = ", ";
		char* token = strtok(cur_line, " ");
		char* word = malloc(MAX_WORD_LENGTH);
		strcpy(word, token);

		// read line numbers
		for (; token != NULL; token = strtok(NULL, dlmt)) {
			if (strcmp(token, word) != 0) {		// not first time
				add_word_node(root, atoi(token), word);

			}
		}
		free(token);
		free(word);
		free(cur_line);
		cur_line = NULL;
	}

	fclose(f);
}

void mergesort(struct word_item** root) {
	struct word_item* head = *root;
	struct word_item* first;
	struct word_item* second;

	if ((head == NULL) || (head->next == NULL)) {
		return;
	}

	// Split head into 'first' and 'second' sublists
	split(head, &first, &second);

	// Sort the sublists
	mergesort(&first);
	mergesort(&second);

	*root = merge_sorted(first, second);
}

struct word_item* merge_sorted(struct word_item* first,
		struct word_item* second) {
	struct word_item* result = NULL;

	if (first == NULL)
		return (second);
	else if (second == NULL)
		return (first);

	if (strcmp(first->word, second->word) <= 0) {
		result = first;
		result->next = merge_sorted(first->next, second);
	} else {
		result = second;
		result->next = merge_sorted(first, second->next);
	}
	return (result);
}

void split(struct word_item* src, struct word_item** front,
		struct word_item** back) {
	struct word_item* fast;
	struct word_item* slow;
	if (src == NULL || src->next == NULL) {
		*front = src;
		*back = NULL;
	} else {
		slow = src;
		fast = src->next;

		while (fast != NULL) {
			fast = fast->next;
			if (fast != NULL) {
				slow = slow->next;
				fast = fast->next;
			}
		}

		*front = src;
		*back = slow->next;
		slow->next = NULL;
	}
}

// =======================================================
// 							WORKER
// =======================================================

void generate_index(mqd_t descriptor, int worker_nr) {

	// Get message queue attributes
	struct mq_attr* attr = malloc(sizeof(struct mq_attr));
	mq_getattr(descriptor, attr);

	// Create buffers
	int msg_size = attr->mq_msgsize;
	char buffer[msg_size];

	// Initialize word list
	struct word_item* root = NULL;

	// Start receiving messages
	int result = mq_receive(descriptor, buffer, msg_size, NULL);
	struct mq_item* msg = ((struct mq_item*) buffer);
	if (result == -1) {
		perror("Cannot receive messages! EXITING");
	}

	// Continue receiving messages
	while (result != -1 && msg->line != -1) {
		// process
		add_word_node(&root, msg->line, msg->word);

		// Receive next message
		result = mq_receive(descriptor, buffer, msg_size, NULL);
		msg = ((struct mq_item*) buffer);
	}

	// Generate outfile
	char filename[MAX_WORD_LENGTH];
	sprintf(filename, TMP_OUTFILE_TMPL, worker_nr);
	mergesort(&root); // sort
	write_file(root, filename);

	mq_close(descriptor);

	// free memory
	free_mem_word(root);
	free(attr);
}

void add_word_node(struct word_item** root, int line, const char* word) {

	struct word_item* word_position = NULL;
	if (word_exists(root, word, &word_position) == TRUE) {
		if (line_exists(word_position, line) == FALSE) {
			//  insert line number
			struct lines_item* cur = cur = word_position->line_nrs;
			// Special case: the line number is the smallest
			if (cur->line_nr > line) {
				// create new node
				struct lines_item* new_line_node = malloc(
						sizeof(struct lines_item));
				new_line_node->line_nr = line;
				new_line_node->next = cur;

				// assign it to the previous node
				word_position->line_nrs = new_line_node;
			}

			for (; cur != NULL; cur = cur->next) {
				if (cur->next == NULL || cur->next->line_nr > line) {
					// insert here (keeping it sorted)

					// create new node
					struct lines_item* new_line_node = malloc(
							sizeof(struct lines_item));
					new_line_node->line_nr = line;
					new_line_node->next = cur->next;

					// assign it to the previous node
					cur->next = new_line_node;
					return;
				}
			}
		}
	} else {

		// Create new node and add it to the list
		struct lines_item* line_node = malloc(sizeof(struct lines_item));
		line_node->line_nr = line;
		line_node->next = NULL;

		struct word_item* word_node = malloc(sizeof(struct word_item));
		word_node->line_nrs = line_node;
		strcpy(word_node->word, word);
		word_node->next = *root;

		*root = word_node;
	}
}

int word_exists(struct word_item** root, const char* word,
		struct word_item** word_position) {
	if (root == NULL || *root == NULL || word == NULL) {
		return FALSE;
	} else {
		// check and return it in word_position
		struct word_item* cur;
		for (cur = *root; cur != NULL; cur = cur->next) {
			if (strcmp(cur->word, word) == 0) {
				*word_position = cur;
				return TRUE;
			}
		}
		return FALSE;
	}
}

int line_exists(struct word_item* word_position, int line) {
	if (word_position == NULL || word_position->line_nrs == NULL || line < 1) {
		return FALSE;
	} else {
		struct lines_item* cur;
		for (cur = word_position->line_nrs; cur != NULL; cur = cur->next)
			if (cur->line_nr == line)
				return TRUE;
		return FALSE;
	}
}

void write_file(struct word_item* root, char* filename) {

	// Open file
	FILE* f = fopen(filename, "w");
	if (!f) {
		printf("Cannot create output file!\n");
		exit(EXIT_FAILURE);
	}

	struct word_item* w_iter;
	struct lines_item* l_iter;
	for (w_iter = root; w_iter != NULL; w_iter = w_iter->next) {
			fprintf(f, "%s ", w_iter->word);
			for (l_iter = w_iter->line_nrs; l_iter != NULL; l_iter = l_iter->next) {
				if (l_iter == w_iter->line_nrs)
					// First number
					fprintf(f, "%d", l_iter->line_nr);
				else
					fprintf(f, ", %d", l_iter->line_nr);
			}
			fprintf(f, "\n");
		}

	fclose(f);
}

void free_mem_word(struct word_item* root){

	struct word_item* to_be_deleted = NULL;
	while(root){
		to_be_deleted = root;
		root = root->next;
		free_mem_lines(to_be_deleted->line_nrs);
		to_be_deleted->line_nrs = NULL;
		to_be_deleted->next = NULL;
		free(to_be_deleted);
	}
}

void free_mem_lines(struct lines_item* root){

	struct lines_item* to_be_deleted = NULL;
	while(root){
		to_be_deleted = root;
		root = root->next;
		to_be_deleted->next = NULL;
		free(to_be_deleted);
	}
}
//END OF FILE

