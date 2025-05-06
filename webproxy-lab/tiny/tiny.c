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
  // 리슨 파일 디스크립터와 연결될 클라이언트 파일 디스크립터
  int listenfd, connfd;
  
  char hostname[MAXLINE], port[MAXLINE];
  // 클라이언트 주소 구조체의 크기를 담기 위해 필요. 
  socklen_t clientlen;
  // 클라이언트 IP 주소 및 포트 정보를 저장하기 위해 필요.
  struct sockaddr_storage clientaddr;

  // 입력이 하나가 아니면 에러
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 리슨 소켓 하나 만들어서 초기화해주고
  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    // 클라이언트 소켓 공간은 일단 확보를 많이 해두고
    clientlen = sizeof(clientaddr);
    // 연결 허가되면 연결된 소켓으로 connfd 초기화
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

    printf("Accepted connection from (%s, %s)\n", hostname, port);
    
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}


/*
 * doit - handle one HTTP request/response transaction
 */
// 타이니 서버에서 클라이언트 하나의 HTTP 요청을 처리하는 핵심 함수.
void doit(int fd)
{
  // 동적인지 아닌지 판단
  int is_static;
  // 
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  // rio 초기화
  Rio_readinitb(&rio, fd);
  // readlineb로 rio에 받은 소켓 정보들을 토대로 buf에 채우기
  // -> 만약 데이터가 하나도 없다면 0리턴. 나머지 경우 패스.
  if (!Rio_readlineb(&rio, buf, MAXLINE))
    return;

  // 이 아래로는 이제 잘 작동됐거나, 오류난 것들 처리.
  // buf 주소값 출력. ex) GET / HTTP / 1.1
  printf("%s", buf);
  // buf에 저장되어 있던 데이터들을 각각 method, uri, version에 넣어주고,
  sscanf(buf, "%s %s %s", method, uri, version);
  // strcasecmp : 대소문자 비교없이 비교. 비교 전, 다 소문자로 바꾸고 비교. 같을시 return 0.
  if (strcasecmp(method, "GET"))
  {
    // get이 아니면 에러. 타이니서버는 GET이외에 다른것들 못함.
    clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }
  // HTTP 요청 헤더들을 읽고, 모두 출력.
  read_requesthdrs(&rio);

  // 서버에서 요청받은 uri가 동적인지, 정적인지 판단하고,
  // 그에 따라서 파일 경로(filename)와 CGI를 분리해주는 함수가 parse_uri
  // 반환은 true, false 만 해주고, 분리는 함수 내부에서 일어남.
  is_static = parse_uri(uri, filename, cgiargs);
  // 파일 경로가 실제로 존재하는지 확인하고, 파일 정보(sbuf)를 구조체에 저장.
  // stat 구조체에 저장. 
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
    return;
  }

  // 이게 정적이면
  if (is_static)
  {
    // stat 구조체에 있는 st_mode
    // 이 변수에 따라 파일의 종류를 알 수 있고, 파일의 permission도 알 수 있다.
    // 비트연산 매크로로 S_ISREG : 정규 파일인지 확인, S_IRUSR : 사용자(read) 권한이 있는지 확인
    // 이 둘중에 하나라도 false 라면
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      // 에러
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    // static 한 파일을 클라이언트에게 전송
    serve_static(fd, filename, sbuf.st_size);
  }
  else
  { // 만약 동적이면?
    // 또 st_mode에 있는 데이터를 통해서 조건 확인하고
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    // 맞으면 동적 파일 클라이언트에게 전송
    serve_dynamic(fd, filename, cgiargs);
  }
}

/*
 * read_requesthdrs - read HTTP request headers
 */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  // rp의 정보를 일단 buf에 저장하고
  Rio_readlineb(rp, buf, MAXLINE);
  // buf에 대한 내용들 출력
  // readlineb or read 할때마다 rio_t안에 버퍼 포인터가 계속 바뀌니까
  // 같은 표현이라도 다른 출력이 출력됨.
  printf("%s", buf);

  // 모든 헤더의 끝에 \r\n을 쓰기로 했기 때문에, 이렇게 비교하는거.
  // 즉 헤더의 끝까지 내용들을 계속 출력하겠다는거지 뭐.
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}


// 클라이언트가 요청한 URI 분석해서 이 요청이 정적인지(동적이 아닌지), 동적인지(CGI 인지) 판단하는 역할.
// 파일 경로(filename), CGI 인자(cgiargs) 분리해서 채워줌.
// uri : 클라이언트가 보낸 요청 uri
// filename : 요청을 처리할 파일의 경로를 여기 저장.
// cgiargs : CGI 인자(GET 요청의 쿼리 스트링) 여기 저장.
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // uri에 cgi-bin이 포함돼있지 않으면, (정적이라면)
  if (!strstr(uri, "cgi-bin"))
  {
    strcpy(cgiargs, "");    // CGI 인자는 필요없으니 비운다.
    strcpy(filename, ".");  // 상대경로 시작. 루트 디렉토리 : 현재 디렉토리 '.'
    strcat(filename, uri);  // 경로 그냥 uri로 냅다 주기.
    // uri마지막 문자가 /이거면
    if (uri[strlen(uri) - 1] == '/')
      // 경로 마지막에 home.html 붙여주기
      strcat(filename, "home.html");
    // 정적이니까 true
    return 1;
  }
  // 만약에 cgi-bin이 포함되어있으면 (즉, 동적이라면)
  else
  {
    // uri에서 ?가 어디있는지 찾고 위치 저장
    ptr = index(uri, '?');
    // 있으면?
    if (ptr)
    {
      // ? 다음 글자부터 cgiargs에 넣어주면 되고
      strcpy(cgiargs, ptr + 1);
      // ? 는 NULL 로 바꿔준다. 그러면 문장 두개로 나눌 수 있음
      *ptr = '\0';
    }
    // 근데 ?가 없으면?
    else
      // cgiargs 비워주기
      strcpy(cgiargs, "");
    
    strcpy(filename, ".");
    strcat(filename, uri);
    
    // 여기서 넣어준 cgiargs 값은 나중에 쓰니까, 여기서 안쓴다고 걱정 ㄴㄴ
    return 0;
  }
}

// mmap version
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE];

  char buf[MAXBUF];
  char *p = buf;
  int n;
  int remaining = sizeof(buf);

  // 이제 경로에 파일 확장자에 맞게끔 filetype을 정해줌.
  get_filetype(filename, filetype);

  // buf의 시작부터 사용한 바이트 수만큼 반환
  n = snprintf(p, remaining, "HTTP/1.0 200 OK\r\n");
  // 해서 p는 n만큼 뒤로 밀어주고
  p += n;
  // 남은건 n개 사라졌으니 빼주고
  remaining -= n;

  n = snprintf(p, remaining, "Server: Tiny Web Server\r\n");
  p += n;
  remaining -= n;

  n = snprintf(p, remaining, "Connection: close\r\n");
  p += n;
  remaining -= n;

  n = snprintf(p, remaining, "Content-length: %d\r\n", filesize);
  p += n;
  remaining -= n;

  n = snprintf(p, remaining, "Content-type: %s\r\n\r\n", filetype);
  p += n;
  remaining -= n;

  // buf에 있는 내용을 소켓에 태워서 보냄
  Rio_writen(fd, buf, strlen(buf));
  // 이제 헤더 내용들 다 출력
  printf("Response headers:\n");
  printf("%s", buf);

  // 파일 열어서 소켓에 저장
  srcfd = Open(filename, O_RDONLY, 0);
  // 그 파일을 메모리에 매핑
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // 했으면 소켓 닫고
  Close(srcfd);
  // 또 소켓 써서 보내기
  Rio_writen(fd, srcp, filesize);
  // 또 매핑
  Munmap(srcp, filesize);
}

// void serve_static(int fd, char *filename, int filesize)
// {
//   int srcfd;
//   char *filetype[MAXLINE];
//   char buf[MAXBUF];
//   char *p = buf;
//   int n;
//   int remaining = sizeof(buf);

//   // 파일 타입 설정
//   get_filetype(filename, filetype);

//   // HTTP 응답 헤더 생성
//   n = snprintf(p, remaining, "HTTP/1.0 200 OK\r\n");
//   p += n; remaining -= n;

//   n = snprintf(p, remaining, "Server: Tiny Web Server\r\n");
//   p += n; remaining -= n;

//   n = snprintf(p, remaining, "Connection: close\r\n");
//   p += n; remaining -= n;

//   n = snprintf(p, remaining, "Content-length: %d\r\n", filesize);
//   p += n; remaining -= n;

//   n = snprintf(p, remaining, "Content-type: %s\r\n\r\n", filetype);
//   p += n; remaining -= n;

//   // 헤더 전송
//   Rio_writen(fd, buf, strlen(buf));
//   printf("Response headers:\n%s", buf);

//   // 파일 열기
//   srcfd = Open(filename, O_RDONLY, 0);

//   // 파일 내용을 메모리에 동적 할당
//   char *srcbuf = (char *)malloc(filesize);
//   if (!srcbuf) {
//     fprintf(stderr, "malloc failed\n");
//     Close(srcfd);
//     return;
//   }

//   // 파일 전체를 읽어오기
//   if (Rio_readn(srcfd, srcbuf, filesize) != filesize) {
//     fprintf(stderr, "Rio_readn failed\n");
//     free(srcbuf);
//     Close(srcfd);
//     return;
//   }

//   Close(srcfd);

//   // 클라이언트에게 전송
//   Rio_writen(fd, srcbuf, filesize);

//   // 동적 메모리 해제
//   free(srcbuf);
// }


/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype)
{
  // filename = ./index.html 파일이 존재하면, return index.html 이렇게 됨.
  // 없으면 NULL 반환
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  // mpg는 사실 mpeg란다. 올바른 MIME type 확인하기.
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

// 아까 parse_uri에서 저장했던 cgiargs들을 여기서 사용
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};
  pid_t pid;

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* Create a child process to handle the CGI program */
  if ((pid = Fork()) < 0)
  { /* Fork failed */
    perror("Fork failed");
    return;
  }

  if (pid == 0)
  { /* Child process */
    // 여기서 CGI 실행전에 cgiargs 값들을 환경변수 QUERY_STRING에 등록함.
    setenv("QUERY_STRING", cgiargs, 1);

    /* Redirect stdout to client */
    if (Dup2(fd, STDOUT_FILENO) < 0)
    {
      perror("Dup2 error");
      exit(1);
    }
    Close(fd);

    // 그리고 이제 CGI 파일 실행함.
    Execve(filename, emptylist, environ);

    /* If we get here, Execve failed */
    perror("Execve error");
    exit(1);
  }
  else
  { /* Parent process */
    /* Parent waits for child to terminate */
    int status;
    if (waitpid(pid, &status, 0) < 0)
    {
      perror("Wait error");
    }

    printf("Child process %d terminated with status %d\n", pid, status);
    /* Parent continues normally - returns to doit() */
  }
  /* When we return from here, doit() will close the connection */
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}
