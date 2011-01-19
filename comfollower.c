/* $Id$ */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>


enum { PORT = 8187 };

typedef struct san_follow_context {
    char *filename;
    off_t current_position;
    off_t filesize;
    int _fd;
} san_follow_context;

////////////////////

typedef struct san_aggregator_connection {
    int socket;
} san_aggregator_connection;

san_aggregator_connection*
san_aggregator_connection_new()
{
    san_aggregator_connection* conn = (san_aggregator_connection*)malloc(sizeof(san_aggregator_connection));

    return conn;
}

int
san_aggregator_connection_open(san_aggregator_connection* conn, const char* addr, int port)
{
    struct addrinfo hints, *ai, *ai0 = NULL;
    int err, s = -1;
    char sport[16];

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = PF_UNSPEC;
    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(sport, sizeof(sport), "%d", port);

    err = getaddrinfo(addr, sport, &hints, &ai0);
    if (err) {
        fprintf(stderr, "getaddrinfo: %s", gai_strerror(err));
        return -1;
    }

    for (ai = ai0; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (s < 0) {
	    continue;
	}

	if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
	    close(s);
	    continue;
	}
	conn->socket = s;
	break;
    }
    freeaddrinfo(ai0);

    return conn->socket != -1 ? 0 : -1;
}

int
san_aggregator_connection_send_data(san_aggregator_connection* conn, const char* data, ssize_t len)
{
    ssize_t cb = 0; //, n = len;

    // while (n) {
        cb = send(conn->socket, data, len, 0);
	if (cb == -1) {
	    return -1;
	}

	//
	// n -= cb;
	// usleep(1000);
    // }

    return cb;
}

void
san_aggregator_connection_destroy(san_aggregator_connection* conn)
{
    close(conn->socket);
    free(conn);
}

////////////////////

san_follow_context*
san_follow_context_new(const char* fname)
{
    san_follow_context* cxt = NULL;
    
    cxt = (san_follow_context*)malloc(sizeof(san_follow_context));
    if (cxt) {
        memset(cxt, 0, sizeof(san_follow_context));

        cxt->filename = strdup(fname);
	if (!cxt->filename) {
	    free(cxt);
	    cxt = NULL;
	}

        // we should read control files for 'fname'
    }

    return cxt;
}

int
san_follow_context_open(san_follow_context* cxt)
{
    struct stat st;

    cxt->_fd = open(cxt->filename, O_RDONLY);
    if (cxt->_fd <= 0) {
        return -1;
    }

    fstat(cxt->_fd, &st);
    cxt->filesize = st.st_size;
    lseek(cxt->_fd, cxt->current_position, SEEK_SET);
    return 0;
}

int
san_follow_context_run(san_follow_context* cxt, san_aggregator_connection* conn)
{
    char buf[2048], *p = NULL;
    ssize_t n = 0;

    memset(buf, 0, sizeof(buf));

    while (1) {
	ssize_t cb = 0;
        if (n == 0) {
	    cb = read(cxt->_fd, buf, sizeof(buf));
	    if (cb <= 0) {
                sleep(1);
		continue;
	    }

	    p = buf;
	    n = cb;
	}

        cb = san_aggregator_connection_send_data(conn, p, n);
	if (cb == -1) {
	    // error occurred
	    break;
	}
	n -= cb;
	p += cb;
        //
    }
}

void
san_follow_context_destroy(san_follow_context* cxt)
{
    free(cxt->filename);
    free(cxt);
}

int
main(int argc, char** argv)
{
    san_follow_context* cxt = NULL;
    san_aggregator_connection* conn = NULL;

    const char* fname = (argc > 1 ? argv[1] : "default");
    const char* servhost = (argc > 2 ? argv[2] : "log");

    cxt = san_follow_context_new(fname);
    san_follow_context_open(cxt);

    printf("cxt = %p\n", cxt);

    conn = san_aggregator_connection_new();
    san_aggregator_connection_open(conn, servhost, PORT);

    printf("conn = %p\n", conn);

    return san_follow_context_run(cxt, conn);
}
