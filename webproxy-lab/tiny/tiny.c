/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr,
                        &clientlen); // line:netp:tiny:accept
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);  // line:netp:tiny:doit
        Close(connfd); // line:netp:tiny:close
    }
}

void doit(int fd)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    /* Short count 문제를 해결해준다. */
    Rio_readinitb(&rio, fd);           /* 한번에 버퍼 크기만큼 읽어 오기 위해 버퍼를 초기화해 준다.*/
    Rio_readlineb(&rio, buf, MAXLINE); /* 텍스트 줄바꿈 문자(\n)를 만나거나 maxlen에 도달할 때까지 한 줄을 읽어서 buf에 저장한다. */
    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) /* 대소문자를 무시하고 비교한다. 같으면 0을 반환한다. */
    {
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }
    read_requesthdrs(&rio); /* 요청 라인 뒤에 오는 헤더를 읽기만 하고 무시 */

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0) /* 디스크 파일의 메타 데이터를 읽어 buf 구조체에 저장한다. 파일 존재 여부와 크기를 알기 위해 호출한다. */
    {
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    if (is_static) /* Serve static content */
    {
        /* st_mode에 있는 파일 정보 및 권한 정보를 읽는다. */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) /* 일반 파일인지 검사(디렉토리나 소켓 파일이 아니라는 걸 확인), 사용자에게 읽기 권한 있는지 확인 */
        {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
            return;
        }
        serve_static(fd, filename, sbuf.st_size); /* 성공 응답 라인과 헤더를 보낸다. 요청 파일을 mmap()한 후 해당 내용을 소켓으로 전송한다. 메모리 매핑 해제도 한다. */
    }
    else /* Serve dynamic content */
    {
        /* st_mode에 있는 파일 정보 및 권한 정보를 읽는다. */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) /* 사용자에게 실행 권한이 있는지 확인 */
        {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program"); /* 클라이언트의 요청에 문제가 있을 때 HTTP 응답 코드와 에러 메시지를 담은 HTML 문자열을 만들어서 클라이언트에 전송 */
            return;
        }
    }
    serve_dynamic(fd, filename, cgiargs); /* 실행 파일을 실행시키고 그 출력 결과를 클라이언트에게 전송한다. dup2를 통해 표준 출력을 소켓으로 연결한다. dup2는 특정 fd에 현재 fd 내용을 복제할 수 있도록 한다. */
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    // char buf[MAXLINE], body[MAXBUF];

    // /* Build the HTTP response body */
    // sprintf(body, "<html><title>Tiny Error</title>");
    // sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    // sprintf(body, "%s%s: %s\r\n", body, longmsg, cause);
    // sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    // sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    // /* Print the HTTP response */
    // sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    // Rio_writen(fd, buf, strlen(buf));
    // sprintf(buf, "Content-type: text/html\r\n");
    // Rio_writen(fd, buf, strlen(buf));
    // sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    // Rio_writen(fd, buf, strlen(buf));
    // Rio_writen(fd, body, strlen(body));

    /* 새로운 버전 */
    char buf[MAXLINE];
    char body[MAXBUF];
    int len = 0; // body 버퍼에 채워진 문자열의 길이를 추적할 변수

    /* 1. Build the HTTP response body safely */
    // snprintf는 작성된 문자열의 길이를 반환. 이를 len에 누적
    // body + len: 기존 문자열이 끝난 지점부터 새로 작성
    // MAXBUF - len: 버퍼에 남은 여유 공간만큼만 안전하게 작성

    len += snprintf(body + len, MAXBUF - len, "<html><title>Tiny Error</title>");
    len += snprintf(body + len, MAXBUF - len, "<body bgcolor=\"ffffff\">\r\n");
    len += snprintf(body + len, MAXBUF - len, "%s: %s\r\n", longmsg, cause);
    len += snprintf(body + len, MAXBUF - len, "<p>%s: %s\r\n", longmsg, cause);
    len += snprintf(body + len, MAXBUF - len, "<hr><em>The Tiny Web server</em>\r\n");

    /* 2. Print the HTTP response headers */
    snprintf(buf, MAXLINE, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf)); /* snprintf는 버퍼를 직접 이동시켜야 하지만 Rio_writen은 내부적으로 버퍼를 이동시켜 준다. */

    snprintf(buf, MAXLINE, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));

    snprintf(buf, MAXLINE, "Content-length: %d\r\n\r\n", len);
    Rio_writen(fd, buf, strlen(buf));

    /* 3. Print the HTTP response body */
    Rio_writen(fd, body, len);
}
