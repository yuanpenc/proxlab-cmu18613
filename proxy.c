/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "cache.h"
#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "User-Agent: Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1\r\n";

static const char *end_str = "\r\n";
static const char *host_str = "Host";
static const char *User_Agent_str = "User-Agent";
static const char *Connection_str = "Connection";
static const char *Proxy_Connection_str = "Proxy-Connection";

static const char *res_connect_str = "Connection: close\r\n";
static const char *res_Proxy_connect_str = "Proxy-Connection: close\r\n";
static queue *cache_queue;

typedef struct sockaddr SA;

void *thread(void *vargp);
void doit(int connfd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void request_header(char *header_http, char *path, rio_t *rio);

// copy from tiny.c

int main(int argc, char **argv) {
    printf("%s\n", header_user_agent);

    int listenfd;
    int *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    int res;

    // check command args
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    Signal(SIGPIPE, SIG_IGN);
    pthread_t tid;

    // init cache queue
    cache_queue = create_cache();

    listenfd = open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);

        res = getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port,
                          MAXLINE, 0);
        if (res == 0) {
            printf("Accepted connection from %s:%s\n", hostname, port);
        } else {
            // TODO: figure out error
            // fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));
            printf("getnameinfo failed");
        }

        // TODO: directly &connfd? what if connfd is defined as int

        pthread_create(&tid, NULL, thread, connfd);
    }

    free_queue(cache_queue);
    return 0;
}

// TODO: thread
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    close(connfd);
    return NULL;
}

// copy from tiny.c
void doit(int connfd) {

    char buf[MAXLINE], method[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], header_http[MAXLINE];
    char port[MAXLINE];
    char uri[MAXLINE];
    rio_t rio, rio_sever;
    // Read request line and headers
    rio_readinitb(&rio, connfd);
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) {
        return;
    }

    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcmp(method, "GET")) {
        clienterror(connfd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    // make sure port would not change

    parse_uri(uri, hostname, path, port);
    request_header(header_http, path, &rio);

    // TODO: size is a bug here

    size_t response_size = 0;
    char *response;
    response = read_from_cache(cache_queue, uri, &response_size);
    // hit
    if (response != NULL) {
        // printf("-----------\n");
        // printf("reponse=%s\n", response);
        // printf("-----------\n");
        rio_writen(connfd, response, response_size);
        Free(response);
        return;
    }

    // miss

    int client_fd;
    client_fd = open_clientfd(hostname, port);
    // printf("client_fd=%d\n", client_fd);
    if (client_fd < 0) {
        printf("Cannot connect to %s with %s\r\n", hostname, port);
        return;
    }

    rio_readinitb(&rio_sever, client_fd);
    size_t header_length = strlen(header_http);
    rio_writen(client_fd, header_http, header_length);

    size_t n;
    bool exceed = false;
    size_t count = 0;
    char data[MAX_OBJECT_SIZE];
    char *ptr;

    // TODO: can rio_readlind work?
    while ((n = rio_readnb(&rio_sever, buf, MAXLINE)) != 0) {

        size_t temp = count + n;
        if (temp > MAX_OBJECT_SIZE) {
            exceed = true;
        } else {
            ptr = data + count;
            memcpy(ptr, buf, n);
            count += n;
        }

        rio_writen(connfd, buf, n);
    }

    if (exceed) {
        printf("exceed MAX_OBJECT_SIZE");
    } else {
        // printf("uri = %s\n, data=%s\n", uri, data);
        in_cache(cache_queue, uri, data, count);
    }

    close(client_fd);
}

void request_header(char *header_http, char *path, rio_t *rio) {
    char host_header[MAXLINE], buf[MAXLINE], request_line[MAXLINE],
        x_str[MAXLINE];

    sprintf(request_line, "GET %s HTTP/1.0\r\n", path);

    while (rio_readlineb(rio, buf, MAXLINE) > 0) {
        int end = strcmp(buf, end_str);
        if (end != 0) {
            // "Host:"
            if (!strncasecmp(buf, host_str, strlen(host_str))) {
                strncpy(host_header, buf, strlen(buf));
            } else if (!strncasecmp(buf, User_Agent_str,
                                    strlen(User_Agent_str))) {

            } else if (!strncasecmp(buf, Connection_str,
                                    strlen(Connection_str))) {

            } else if (!strncasecmp(buf, Proxy_Connection_str,
                                    strlen(Proxy_Connection_str))) {

            } else {
                strcat(x_str, buf);
            }

        } else {
            sprintf(header_http, "%s%s%s%s%s%s%s", request_line, host_header,
                    header_user_agent, res_connect_str, res_Proxy_connect_str,
                    x_str, end_str);
            return;
        }
    }
}

// slightly copy from tiny.c
void parse_uri(char *uri, char *hostname, char *path, char *port) {

    if (strstr(uri, "http://") != uri) {
        printf("URL error\n");
        exit(1);
    }

    // clean char
    memset(hostname, '\0', MAXLINE);
    memset(path, '\0', MAXLINE);
    memset(port, '\0', MAXLINE);

    // ptr: "www.cmu.edu(:8080)/hub/index.html"
    char *uri_temp = strstr(uri, "//");
    uri_temp = uri_temp + 2;
    char *path_temp = strstr(uri_temp, "/");
    memcpy(path, path_temp, MAXLINE);

    int hostLength = path_temp - uri_temp;
    char *port_temp = strstr(uri_temp, ":");

    // default port

    if (port_temp == NULL) {

        // TODO: FIGURE OUT THE BUG
        // memcpy(port, "80", MAXLINE);
        strncpy(port, "80", 2);
        strncpy(hostname, uri_temp, hostLength);

    }

    // assigned port
    else {

        int portLength = path_temp - port_temp - 1; // 1 means substract ":"
        // printf("portLength=%d \n", portLength);
        // printf("hostnamelength=%d \n", hostLength - portLength - 1);
        strncpy(port, port_temp + 1, portLength);
        strncpy(hostname, uri_temp, hostLength - portLength - 1);
    }

    printf("####################\n");
    printf(" hostname: %s, port: %s, path: %s\n", hostname, port, path);
    printf("####################\n");
    return;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body,
            "%s<body bgcolor="
            "ffffff"
            ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}
