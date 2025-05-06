#include <stdio.h>
#include "csapp.h"

void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void clientError(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(rio_t *request_riop, char *request_buf, int serverfd, char *hostname, char *port);


// 나중에 request_hdrs에서 사용할 변수.
const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";


const int is_local_test = 1;

// 처음 실행시 입력은 포트 번호
int main(int argc, char **argv)
{
    // 리슨소켓, 연결소켓
    int listenfd, connfd;

    char hostname[MAXLINE], port[MAXLINE];
    struct sockaddr_storage clientaddr;    // 얘는 클라이언트 IP 주소 및 포트 정보를 저장할 '버퍼'
    socklen_t clientlen;    // 얘는 그 '버퍼의 크기'를 나타내는 애.

    if (argc != 2)
    {
        // fprintf와 printf의 차이점.
        // 둘다 문자열을 출력하는 함수지만, fprintf는 stderr(표준 에러 출력), printf는 stdout(표준 출력)
        fprintf(stderr, "usage : %s <port> \n", argv[0]);
        // exit(0) : 정상 종료일 경우.
        // exit(1) : 비정상 종료일 경우.
        exit(1);
    }

    // 리스닝 소켓 오픈
    listenfd = Open_listenfd(argv[1]);

    // 이제 리스닝 소켓 오픈했으면, 앞으로 컨넥한 뒤에 계속 소켓 연결해줄 일만 남음.
    while(1)
    {
        clientlen = sizeof(clientaddr); // clientlen : 버퍼 크기 나타내주는 애. 따라서 sizeof(clientaddr)

        // accept한다는건, 새로 연결할 소켓을 장만한다는 말.
        // 컨넥 소켓 초기화
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        
        // 이제 또 뭐해야할까... 이제 클라쪽에서 보낸 정보를 읽어야지.
        // 그래서 
        // Rio_readinitb() 이거라고 생각했는데 아니네
        // 그 다음으로 해야할 건, getnameinfo
        // 클라이언트의 IP 주소와 포트 번호를 사람이 읽을 수 있는 문자열로 변환해주는 역할!
        // 왜 readinitb보다 이걸 먼저하냐?
        // 로그에 띄워서 잘 연결됏는지 먼저 확인하려고.
        // 기능이 뭔가 중요한 애는 아님. 그냥 로그 확인용 함수.
        Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

        // 로그를 확인해봐야겠지?
        // 지금까지 한것들 중 확인해볼건? hostname, port 제대로 설정 됐는지만 확인하면 됨.
        printf("Accepted connection from (%s %s).\n", hostname, port);

        // 이제 그 다음에 할건?
        // read라고 생각했는데 생각해보니까 아하!
        // 우리는 각종 기능들을 그냥 doit에다가 박아놓고 사용했었지.
        // 그래서 이제 그냥 doit()만 해주면 됨.
        // 클라에 연결할 소켓에 대해서 doit!
        doit(connfd);

        // doit이 끝났으면?
        // 이제 소켓 닫고 다시 소켓 열고 또 doit하면서 작동하면 됨.
        Close(connfd);
    }

    return 0;
}

void doit(int connfd)
{
    // 각종 기능들에 대해서 구현해야 함.
    // 지금 어디까지 했냐면 accept까지 했음.
    // 이후엔 클라에서 보낸 정보 읽고, 원 서버로 보내고,
    // 그다음에 다시 원 서버에서 보낸 정보 읽고, 클라로 보내면 됨.

    // 원 서버와 소통할 소켓
    int serverfd;

    // 받은 데이터 저장할 buf, 보낼 데이터 저장할 buf
    char request_buf[MAXLINE], response_buf[MAXLINE];

    // 보내려면 일단 보낼 걸 저장해둘 그릇이 필요하겠지.
    // 그리고 요청 메소드, uri, version도 저장해야함.
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    // srcp?? -> 메모리가 매핑된 시작주소를 가리키는 포인터.
    char *srcp, filename[MAXLINE], cgiargs[MAXLINE];

    // 클라가 요청한 URL에서 파싱한 결과 저장해둘 변수들
    char hostname[MAXLINE], path[MAXLINE];

    // 클라가 요청한 URL에서 추출한 포트 번호
    char port[MAXLINE];

    // 내가 쭉 봤을 때, 얘네는 뭔가 소켓에서 정보를 받아와서,
    // 나중에 사용할 버퍼에 값을 전달할 매개체 같음.
    // 그래서 내 느낌상, rio_t 구조체는 버퍼를 위한 중간 버퍼 느낌??
    // -> gpt는 그게 맞대,
    //  rio = request buf, rio_endserver = response buf
    rio_t request_rio, response_rio;

    // listen을 통해서 만들어준 소켓으로 클라가 데이터 전송해놨음.
    // 그거 이제 읽어야되는데, 먼저 읽기 전에 항상 초기화.
    // 읽고 쓸때는 rio_t 구조체를 통해서!
    // 따라서 프록시 내부의 rio를 우선 클라에서 받은 소켓으로 초기화
    // -> 아직 connfd에는 read한 정보가 아무것도 없음.
    // 지금 소켓에 있는건 통신을 위한 채널 정보만 있는거
    Rio_readinitb(&request_rio, connfd);

    // 이제 초기화 했으니 읽어야지.
    // 읽은 내용들을 전부 프록시의 buf에 저장해두기. - X
    // -> 이건 rio에 초기화한 소켓 채널 정보같은거를 buf로 넘겨주는거.
    // 알아둬야할 건, 소켓은 뭔가 데이터를 갖고 다니는 바구니가 아님!!!!
    // 그냥 통로를 열어주는 애일 뿐!!! connfd에는 읽은 내용이 하나도 없다!
    Rio_readlineb(&request_rio, request_buf, MAXLINE);

    // 읽은 뒤엔 뭐해야돼?
    // 이제 원서버로 보낼 소켓 만들어야지
    // 그래서 writen을 해야한다고 생각했지만, 항상 로그가 중요하다!
    // 지금까지 잘 됐는지 먼저 로그를 통해서 출력
    printf("Request headers: \n");
    printf("%s\n", request_buf);
    // 이거 치고나서 알았는데, 지금 버퍼에 있는 값들은 헤더에 관한 값들임.
    // connfd를 통해 통로가 어디로, 어떻게 뚫려있는지 확인하는거임!

    // 이제 다시 writen해야지.
    // 먼저 buf에 저장된 애들을 파싱먼저 해줌
    // ex) buf = GET /index.html HTTP/1.1 이런식이면,
    // method = GET
    // uri = /index.html
    // version = HTTP/1.1
    // 이렇게 되는거임.
    sscanf(request_buf, "%s %s %s", method, uri, version);

    // 오케이 이제 여기까지 했으면 또 뭐해야돼
    // 이제 진짜 writen해야지.
    // 근데 그전에, 우리는 지금 tiny라서 GET 메소드밖에 사용 못함.
    // 그래서 이외의 요청들 전부 에러처리 해주는게 좋음
    // strcasecmp : 변수 안에, 해당 문자열이 있는지 대소문자 구분없이 비교.
    // 해당 문자열이 있으면 0 반환, 아니면 양수 or 음수 리턴.
    if (strcasecmp(method, "GET"))
    {
        // 상태 코드 501 : 서버에 요청을 수행할 수 있는 기능이 없을 때,
        printf("Error code : 501\n");
        return;
    }

    // 자 이제 진짜 writen 해보자.
    // ㅋㅋㅋㅋㅋ 근데 그전에 파싱 먼저 해줘야 함.
    // ??? : 아까 파싱했는데요?
    // -> 아까한건 대강 method, uri, version 이렇게 큰 틀로 파싱한거고,
    // 이제 해줘야하는건 uri를 통해서 좀 더 자세히 파일 경로도 알아야하고, CGI인지도 판단하고 하는
    // 좀 더 자세한 파싱이 필요함.
    parse_uri(uri, hostname, port, path);

    // 이제 파싱했으니, 각각 hostname, path, port는 알맞은 값을 갖고 있음.
    // 이제 진짜 writen할때 된거 아니냐?
    // 아니. 항상 뭔가를 했으면 로그로 확인을 잘 해줘야 함.
    sprintf(request_buf, "%s %s %s\r\n", method, path, version);

    // 이제 진짜 writen해줘야 함!!!!!!
    // 근데 그 전에... 항상 writen하기 전에 미리 통로를 뚫어두는게 먼저임.
    serverfd = Open_clientfd(hostname, port);

    // 이제 진짜 writen을 해야겠지?
    // 근데 일단 serverfd 소켓이 잘 연결돼있는지 먼저 체크
    if (serverfd < 0)
    {
        // 502 상태코드 = Bad Gateway, 에러 설명 : 프록시가 최종 서버와 연결하려 했지만 실패했을 때, 나타나는 에러.
        clientError(connfd, method, "502", "Bad Gateway", "Failed to establish connection with the end server");
        return;
    }
    
    // 이제 잘 연결돼있다면, writen 시작해야겠지?
    // request_buf에는 딱 한줄 들어잇음. GET /index.html HTTP/1.0\r\n 이런거.
    // 그래서 일단 원서버에 이거 먼저 보내고
    Rio_writen(serverfd, request_buf, strlen(request_buf));

    // 클라한테 받은 내용은 그 한줄이 아님. 엄청 많을거잖아.
    // 그걸 이제 읽어서 원서버로 보내는 애가 이 read_requesthdrs 함수.
    // rio 구조체에서 나머지 내용들을 읽어서 보내는거임.
    read_requesthdrs(&request_rio, request_buf, serverfd, hostname, port);
    // 이제 헤더 다 writen해서 원 서버로 보냈음.

    // 이제 뭐해야돼? -> 이제 서버에서 보낸 응답을 처리해야지.
    // 서버에서 보낸 응답을 프록시에서 읽어서, 다시 클라쪽으로 보내줘야 함.
    // 그럼 일단 서버에서 보낸 응답을 읽어야겠지.
    // 그럼 다시 리슨해야하는거 아닌가?
    // 근데 아님. 리슨은 한번만 하면 됨. 이미 원 서버랑 통신할수 있는 통로는 serverfd로 갖고 있잖아.
    Rio_readinitb(&response_rio, serverfd);
    Rio_readlineb(&response_rio, response_buf, MAXLINE);

    // 왜 여기서 getnameinfo 안해줌? -> 왜냐면 오픈해서 뭐 연결한적이 없으니까.
    // 이미 연결된 애들로만 하고 있으니까 로그 확인할 필요는 없음.

    // 여기서 connfd는 클라와 연결된 소켓임.
    // 클라 소켓에, 서버 응답 버퍼를 넣어서 writen하기
    Rio_writen(connfd, response_buf, strlen(response_buf));

    // 갑자기 뜬금없이 왜 content_length가 나오는가?
    // HTTP 응답의 본문(body)를 얼마나 읽어서 클라이언트에게 전달해야하는지 정확히 알기 위해서
    int content_length;

    while (strcmp(response_buf, "\r\n"))
    {
        // 한줄씩 읽어오고
        Rio_readlineb(&response_rio, response_buf, MAXLINE);
        
        if (strstr(response_buf, "Content-length"))
            content_length = atoi(strchr(response_buf, ':') + 1);

        Rio_writen(connfd, response_buf, strlen(response_buf));
    }

    if (content_length)
    {
        srcp = malloc(content_length);
        Rio_readnb(&response_rio, srcp, content_length);
        Rio_writen(connfd, srcp, content_length);
        free(srcp);
    }
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
    // 자 이제 어디까지 하다가 여기까지 왔냐
    // accept -> readline 하는 과정에서 여기로 왔음.
    // 얘 역할은 uri 파싱하는거임.
    // 그러면 파싱한 정보들 저장해야할 그릇? -> 그건 이미 인자로 받았음.
    // 그럼 뭐가 필요해?
    // -> 문자열 파싱은 내가 나누고 싶은 지점에 '\0'만 넣어주면 되는거임.
    // 그럼 그 나누고 싶은 지점을 파악해야하니, 그 지점을 가리키는 포인터를 변수로 두면 좋겠지?
    
    // hostname의 끝을 알려줄 포인터.
    // 만약 uri에 // 가 있으면 그 뒤부터 hostname. // 가 없으면, uri 처음부터 hostname.
    char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;

    // 그 다음은 포트 번호 체크할 포인터.
    // hostname 에서 : 가 있는 곳부터 확인하면 됨.
    char *port_ptr = strchr(hostname_ptr, ':');

    // 그 다음은 파일 경로 체크할 포인터.
    // hostname에서 / 이후.
    char *path_ptr = strchr(hostname_ptr, '/');

    // uri : ~//hostname:port/path 임.
    // 따라서 path에는 path만 존재하게 되기 때문에 path에 path_ptr 넣어줘도 무방.
    strcpy(path, path_ptr);

    // 이제 뭐해야하냐...
    // 파일 경로에 파일이 없다면 오류 체크
    // ㅋㅋ 이건줄 알았지만, port가 있는 경우와 없는 경우로 나눠서 진행.
    if (port_ptr)
    {
        // 여기는 포트가 존재하는 경우!

        // port에 port만 넣으려고 하는거임.
        // port_ptr + 1 부터, 포트 번호 길이 만큼을 port에 넣겠다.
        strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1);
        
        // 포트의 끝을 설정해줘서 거기까지 스캔했으면 멈추게끔.
        port[path_ptr - port_ptr - 1] = '\0';

        // 마찬가지로 hostname 에 hostname만 넣으려고 하는거.
        // hostname 시작부터, hostname 길이 만큼 hostname에 넣겠다.
        strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
    }    
    else
    {
        // 여기는 포트가 존재하지 않는 경우!

        // 포트가 없으면, 임의로 그냥 정해주면 됨.
        // 갑자기 is local test 얘 어디서 나온거니?
        // -> 얘는 그냥 전역으로 선언해두는게 좋겟다. 컴파일 할때마다 바꾸는건 좀 맘에 안드는데 흠...
        if (is_local_test)
            strcpy(port, "80");
        else
            strcpy(port, "8000");
        
        // hostname 만 넣어주면 되겠지?
        strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
    }

    strcpy(path, path_ptr);
    return;
    // 이게 uri 파싱 끝.
}

// 클라이언트에 에러 전송하는 함수
// cause: 오류 원인, errnum: 오류 번호, shortmsg: 짧은 오류 메세지, longmsg: 긴 오류 메세지
void clientError(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    // 항상 뭔가를 '전송'하려면 그릇이 있어야 함.
    char buf[MAXLINE], body[MAXLINE];

    // 이제 에러 메세지 만들 본문 생성
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=ffffff>\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    // 이제 응답 출력
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-Type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *request_riop, char *request_buf, int serverfd, char *hostname, char *port)
{
    // 프록시는 클라이언트 요청을 중간에서 받아 서버로 전달할 때,
    // 프록시가 기본 헤더를 보장해야 함. 클라이언트 요청을 파싱하며 각 헤더 존재 여부를 기록해두기 위해
    // 이 변수들을 사용한다.
    // 이게 뭔말임? 왜 기본 헤더를 보장해야돼?
    // -> 왜 보장해줘야 하냐? 각종 기술 요구사항, 표준을 만족시키기 위해서
    // 그냥 플래그처럼 쓰는거임.

    // host 헤더는 HTTP/1.1에서 필수임. 클라가 안보냈어도, 프록시는 생성해서 넣어줘야함.
    // 없으면 원 서버는 400 Bad Request 오류냄.
    int is_host_exist = 0;
    // 얘네는 서버가 연결을 끊을지 유지할지 판단하는 힌트.
    int is_connection_exist = 0;
    int is_proxy_connection_exist = 0;
    // 서버나 로그 시스템이 클라이언트 종류를 알아내기 위해 사용.
    int is_user_agent_exist = 0;

    // 좀 헷갈렸었는데, readline할 때는 항상 MAXLINE으로 읽어야함.
    // strlen으로 처리하는건 writen인 경우만.
    // 요청 메세지의 첫째 줄 읽기.
    Rio_readlineb(request_riop, request_buf, MAXLINE);

    // 버퍼에서 읽은 줄이 \r\n이 아닐 때까지 계속 반복.
    while (strcmp(request_buf, "\r\n"))
    {
        // 일단 request_buf에 Proxy-Connection라는 단어가 포함되어 있는지 검사.
        if (strstr(request_buf, "Proxy-Connection") != NULL)
        {
            // 만약에 포함되어 있다면, 
            // 클라이언트가 보낸 Proxy-Connection을 close로 강제 수정.
            // 근데 왜 close해줘야함?
            // -> 프록시는 원 서버와 연결을 독립적으로 관리해야 하기 때문에, 원 서버와의 연결은 요청 하나마다 닫히는게 안전해서.
            // 안그러면 서버가 응답후에도 연결을 계속 열어놔, 메무리 누수, 과도한 연결 유지로 이어질 수 있대.
            sprintf(request_buf, "Proxy-Connection: close\r\n");
            // 그리고 이건 원래 클라 요청에 Proxy-Connection이 있었다는걸 알려주는거.
            is_proxy_connection_exist = 1;
        }
        
        // 그 다음 다른것들도 포함되어있는지 검사해야겠죠?
        if (strstr(request_buf, "Connection") != NULL)
        {
            sprintf(request_buf, "Connection: close\r\n");
            is_connection_exist = 1;
        }

        if (strstr(request_buf, "User-Agent") != NULL)
        {
            sprintf(request_buf, user_agent_hdr);
            is_user_agent_exist = 1;
        }   

        if (strstr(request_buf, "Host") != NULL)
        {
            is_host_exist = 1;
        }

        // 이제 구성 정보들 한줄한줄씩, 소켓을 통해 원서버로 보냄.
        Rio_writen(serverfd, request_buf, strlen(request_buf));
        // 그리고 이제 그 다음줄 읽어야지
        Rio_readlineb(request_riop, request_buf, MAXLINE);
    }

    // 해서 이제 다 읽고, 원서버로 다 보냈으면 혹시나 안보낸 것들이 있는지 살펴봐야됨.
    // 원래 클라에서 요청이 없었어도, 원서버에서 원한다면 채워서 보내줘야 함.
    // 그래서 플래그로 다 표시해놨잖아.
    // 플래그가 0인것들만 따로 또 보내주면 됨.

    if (!is_connection_exist)
    {
        sprintf(request_buf, "Connection: close\r\n");
        Rio_writen(serverfd, request_buf, strlen(request_buf));
    }

    if (!is_proxy_connection_exist)
    {
        sprintf(request_buf, "Proxy-Connection: close\r\n");
        Rio_writen(serverfd, request_buf, strlen(request_buf));
    }
    
    if (!is_host_exist)
    {
        if (!is_local_test)
            hostname = "52.79.234.188";
        
        sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
        Rio_writen(serverfd, request_buf, strlen(request_buf));
    }

    if (!is_user_agent_exist)
    {
        sprintf(request_buf, user_agent_hdr);
        Rio_writen(serverfd, request_buf, strlen(request_buf));
    }

    // 이제 모든 헤더 보냈으니 끝났으면 문단을 나눠줘야 나중에 로그 보기 편하잖아.
    // 그래서 \r\n 도 보내서 문단 나누기
    sprintf(request_buf, "\r\n");
    Rio_writen(serverfd, request_buf, strlen(request_buf));
    return;
}
