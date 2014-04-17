#include "cs537.h"
#include "request.h"

/**
 * server.c: A simple web server
 *
 * Repeatedly handles HTTP requests sent to the port number.
 * Most of the work is done within routines written in request.c
 *
 * Generates a threadpool and buffer size based on specified sizes (both must)
 * be > 0. 
 */
char *schedalg = "Must be FIFO, SNFNF or SFF";
request_buffer *buffer; // a list of connection file descriptors
int buffers = 0; // max buffers
int numitems = 0; // number of items currently in the buffer
int needsort = 0;
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;

/**
 * Usage: 
 * prompt> ./server [portnum] [threads] [buffers] [schedalg]
 * 
 * -portnum: the port number to listen on (above 2000)
 * -threads: size  of server worker thread pool
 * -buffers: # of request connections that can be accepted @ one time. 
 * -schedalg: must be FIFO, SFNF or SFF
 * (threads & buffers must both be > 0)
 * @todo: parse the new arguments
 */
void getargs(int *port, int *threads, int *buffers, char **schedalg, int argc, char *argv[])
{
	if (argc != 5 // Must have 5x arguments
			|| (*threads = atoi(argv[2])) < 1 // Must have 1 or more threads
			|| (*buffers = atoi(argv[3])) < 1 // Must have 1 or more buffers
			|| ( // <schedalg> must be FIFO, SFNF, or SFF
					strcmp("FIFO", (*schedalg = strdup(argv[4]))) != 0
					&& strcmp("SFNF", *schedalg) != 0 
					&& strcmp("SFF", *schedalg) != 0 
					)
			) {
		// prompt> server [portnum] [threads] [buffers] [schedalg]
		fprintf(stderr, "Usage: %s <portnum> <threads> <buffers> <schedalg>\n", argv[0]);
		exit(1);
	}

	*port = atoi(argv[1]);
}

int 
compareKeys(const void *r1, const void *r2)
{
  request_buffer *bf1 = (request_buffer*) r1;
  request_buffer *bf2 = (request_buffer*) r2;
  int parm1 = 0;
  int parm2 = 0;

	if(strcmp("SFNF", schedalg) == 0)
	{
		parm1 = strlen(bf1->filename);
		parm1 = strlen(bf2->filename);
	} 
	else if(strcmp("SFF", schedalg) == 0)
	{
		parm1 = bf1->filesize;
		parm2 = bf2->filesize;
	}

	// Reverse sorting order (-1 / 1 switched)
  if(parm1 > parm2) {
    return -1;
  } else if(parm1 < parm2) {
    return 1;
  }

  return 0;
}

void currBufferDump()
{
	int i = numitems - 1;
	printf("buffer[%d].connfd %d\n", i, buffer[i].connfd);
	printf("buffer[%d].filesize %d\n", i, buffer[i].filesize);
	printf("buffer[%d].filename %s\n", i, buffer[i].filename);
	printf("buffer[numitems].filename %s\n", buffer[i].filename);
}

void bufferDump()
{
	int i;
	// Dump the buffer data
	for (i = 0; i < numitems; i++)
	{
		printf("buffer[%d].connfd %d\n", i, buffer[i].connfd);
		printf("buffer[%d].filesize %d\n", i, buffer[i].filesize);
		printf("buffer[%d].filename %s\n", i, buffer[i].filename);
		printf("buffer[numitems].filename %s\n", buffer[numitems].filename);
	}
	// exit(1);
}

/**
 * Sort the contents of the buffer
 */
void
sortQueue()
{
	// Policies:
	// -First-in-First-out (FIFO)
	// -Smallest Filename First (SFNF)
	// -Smallest File First (SFF)
	if(strcmp("FIFO", schedalg) == 0) return;
  qsort(buffer, numitems, sizeof(request_buffer), compareKeys);
}

/**
 * Fill the queue with HTTP requests. A function for the master (producer) 
 * thread. Requires a port number > 0
 * master thread (producer): responsible for accepting new http connections 
 *  over the network and placing the descriptor for this connection into a 
 *  fixed-size buffer
 *  -cond var: block & wait if buffer is full
 */
void *producer(void *portnum)
{
	int *port = (int*) portnum;
	// printf("(int) port: %d\n", *port);
	int listenfd, clientlen, connfd;
	listenfd = Open_listenfd(*port); // This opens a socket & listens on port #
	struct sockaddr_in clientaddr;

	for (;;) 
	{
		
		// Hold locks for the shortest time possible.
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen); // This blocks; must be outside mutex

		pthread_mutex_lock(&m);
		while (numitems == buffers){
			pthread_cond_wait(&empty, &m);
		}

		buffer[numitems].connfd = connfd;
		queueRequest(&buffer[numitems]);
		// requestHandle(&buffer[numitems]);
		// Close(buffer[numitems].connfd);
		numitems++;
		needsort = 1;
		// printf("xxxxx numitems: %d\n", numitems);
		// printf("Wake up!!!!!!\n");;
		pthread_cond_signal(&fill);
		pthread_mutex_unlock(&m);
	}
}

/**
 * Function for the consumers in the threadpool
 */
void *consumer()
{
	request_buffer *tmp_buf  = (request_buffer *) malloc(buffers * sizeof(request_buffer));

	for (;;) 
	{
		pthread_mutex_lock(&m);
		while (numitems == 0){
			pthread_cond_wait(&fill, &m);
		}

		// printf("thread id: %d\n", (int)  pthread_self() );

		if(needsort) sortQueue(); needsort = 0;
		// Store data in temp struct (while locked)
		tmp_buf->filesize = buffer[numitems - 1].filesize;
		tmp_buf->connfd = buffer[numitems - 1].connfd;
		tmp_buf->is_static = buffer[numitems - 1].is_static;
		strcpy(tmp_buf->buf, buffer[numitems - 1].buf);
		strcpy(tmp_buf->method, buffer[numitems - 1].method);
		strcpy(tmp_buf->uri, buffer[numitems - 1].uri);
		strcpy(tmp_buf->version, buffer[numitems - 1].version);
		strcpy(tmp_buf->filename, buffer[numitems - 1].filename);
		strcpy(tmp_buf->cgiargs, buffer[numitems - 1].cgiargs);
		tmp_buf->sbuf = buffer[numitems - 1].sbuf;
		tmp_buf->rio = buffer[numitems - 1].rio;
		numitems--;
		// Let go of lock as quickly as possible...
		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&m);
		
		// bufferDump();
		// currBufferDump();
		requestHandle(tmp_buf); 
		Close(tmp_buf->connfd);

	}
}

int main(int argc, char *argv[])
{
	int port, threads, i;
	getargs(&port, &threads, &buffers, &schedalg, argc, argv);
	// printf("port:%d, threads:%d, buffers:%d, (did it work?) schedalg:%s \n", port, threads, buffers, schedalg);
	buffer = (request_buffer *) malloc(buffers * sizeof(request_buffer));
	pthread_t pid, cid[threads];

	// Init producer
	pthread_create(&pid, NULL, producer, (void*) &port);
	// Create fixed size pool of consumer threads.
	for (i = 0; i < threads; i++)
	{
		pthread_create(&cid[i], NULL, consumer, NULL);
	}

	pthread_join(pid, NULL);
	for (i = 0; i < threads; i++)
	{
		pthread_join(cid[i], NULL);
	}

	return 0;
}