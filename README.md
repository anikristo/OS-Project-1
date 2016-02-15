The following is some clarification on Part A of the project.

While communicating with the children processes through the message queues, the
parent process sends a structure called `mq_item` with a char array of size 64 and
line number.

Upon receiving the messages out of the queue, the worker children save the
information in a linked list that looks like below. There are nodes of type
`word_item` and `lines_item` saving the word and the line number respectively. Each
`word_item` stores the word and a linked list of line numbers.

	-------------------				-------------------
	|      "word1"    |				|      "word2"    |
	-------------------				-------------------
	|    •   |   •----|----------->	|    •   |   •----|----- .  .    .
	-------------------				-------------------
	     |				     			|
	     |				     			|
	     |				     			|
	     ˅					     		˅
	------------					------------
	| line_nr1 |					| line_nr1 |
	------------					------------
	|    •     |					|    •     |
	------------					------------
	     |				     			|
	     |				     			|
	     |				     			|
	     ˅								 .
	------------
	| line_nr2 |			      	 	.
	------------
	|    •     |
	------------			     	  	.
	     |
	     |
	     |
	     .

	     .


	     .
