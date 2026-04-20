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


