#include "server.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <strings.h>
#include <dirent.h>

int initListenFd(unsigned short port)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket");
        return -1;
    }
    int opt = 1;
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (ret == -1)
    {
        perror("setsockopt");
        return -1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == -1)
    {
        perror("bind");
        return -1;
    }
    ret = listen(lfd, 128);
    if (ret == -1)
    {
        perror("listen");
        return -1;
    }
    return lfd;

}
int epollRun(unsigned short port)
{
    int lfd = initListenFd(port);
    int epfd = epoll_create(10);
    if (epfd == -1)
    {
        perror("epoll_create");
        return -1;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    //将lfd添加到epfd中
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if (ret == -1)
    {
        perror("epoll_ctl");
        return -1;
    }
    struct epoll_event evs[1024];
    int size = sizeof(evs) / sizeof(evs[0]);
    int flag = 0;
    while (1)
    {
        if (flag)
        {
            break;
        }
        int num = epoll_wait(epfd, evs, size, -1);
        for (int i = 0; i < num; i++)
        {
            int curfd = evs[i].data.fd;
            if (curfd == lfd)
            {
                int ret = acceptConn(lfd, epfd);
                if (ret == -1)
                {
                    int flag = 1;
                    break;
                }
            }
            else {
                recvHttpRequest(curfd, epfd);
            }
        }

    }

    return 0;
}

int acceptConn(int lfd, int epfd)
{
    int cfd = accept(lfd, NULL, NULL);
    if (cfd == -1)
    {
        perror("accept");
        return -1;
    }

    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = cfd;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if (ret == -1)
    {
        perror("epoll_ctl");
        return -1;
    }
    return cfd;
}


int recvHttpRequest(int cfd, int epfd)
{
    char buf[4096];
    char tmp[1024];
    int len, total = 0;
    while ((len = recv(cfd, tmp, sizeof(tmp), 0)) > 0)
    {
        if (total + len < sizeof(buf))
        {
            memcpy(buf + total, tmp, len);

        }
        total += len;
    }

    if (len == -1 && errno == EAGAIN)
    {
        perror("recv");
        char* pt = strstr(buf, "\r\n");
        int reqlen = pt - buf;
        buf[reqlen] = '\0';
        parseRequestLine(cfd, buf);
    }
    else if (len == 0)
    {
        printf("client close\n");
        disConn(cfd, epfd);
        return 0;
    }
    else {
        perror("recv");
        // return ret;
        return -1;
    }
}

int parseRequestLine(int cfd, const char* reqLine)
{
    char method[6];
    char path[1024];
    //GET /helo/world HTTP/1.1
    sscanf(reqLine, "%[^ ] %[^ ]", method, path);
    if (strcasecmp(method, "get") != 0)
    {
        printf("method not support\n");
        return -1;
    }

    char* file = NULL;
    decodeMsg(path, path);
    if (strcmp(path, "/") == 0)
    {

        file = "./";
    }
    else {
        file = path + 1;
    }
    struct stat st;
    int ret = stat(file, &st);
    if (ret == -1)
    {
        sendHeadMsg(cfd, 404, "Not Found", getContentType(".html"), -1);
        sendFile(cfd, "404.html");
    }
    if (S_ISDIR(st.st_mode))
    {
        sendHeadMsg(cfd, 200, "OK", getContentType(".html"), -1);
        sendDir(cfd, file);
    }
    else
    {
        sendHeadMsg(cfd, 200, "OK", getContentType(file), st.st_size);
        sendFile(cfd, file);

    }
    return 0;
}

int sendFile(int cfd, const char* fileName)
{
    int fd = open(fileName, O_RDONLY);
    while (1)
    {
        char buf[1024] = { 0 };
        int len = read(fd, buf, sizeof(buf));
        if (len > 0)send(cfd, buf, len, 0);
        else if (len == 0)break;
        else {
            perror("read");
            return -1;
        }

    }

    return 0;
}
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length)
{
    char buf[4096];
    sprintf(buf, "HTTP/1.1 %d %s\r\n", status, descr);
    sprintf(buf + strlen(buf), "Content-Type:%s\r\n", type);
    sprintf(buf + strlen(buf), "Content-Length:%d\r\n\r\n", length);
    send(cfd, buf, strlen(buf), 0);
    return 0;

}

int disConn(int cfd, int epfd)
{
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if (ret == -1)
    {
        perror("epoll_ctl");
        close(cfd);
        return -1;
    }
    close(cfd);
    return 0;
}

char* getContentType(const char* fileName)
{
    char* p = strrchr(fileName, '.');
    if (p == NULL)
    {
        return "text/plain; charset=utf-8";
    }
    if (strcmp(p, ".html") == 0 || strcmp(p, ".htm") == 0)
    {
        return "text/html; charset=utf-8";
    }
    if (strcmp(p, ".jpg") == 0 || strcmp(p, ".jpeg") == 0)
    {
        return "image/jpeg";
    }
    if (strcmp(p, ".png") == 0)
    {
        return "image/png";
    }
    if (strcmp(p, ".gif") == 0)
    {
        return "image/gif";
    }
    if (strcmp(p, ".css") == 0)
    {
        return "text/css; charset=utf-8";
    }
    if (strcmp(p, ".js") == 0)
    {
        return "application/javascript; charset=utf-8";
    }
    if (strcmp(p, ".ico") == 0)
    {

        return "image/x-icon";
    }
    if (strcmp(p, ".txt") == 0)
    {
        return "text/plain; charset=utf-8";
    }
    if (strcmp(p, ".mp4") == 0)
    {
        return "video/mp4";
    }
    if (strcmp(p, ".mp3") == 0)
    {
        return "audio/mp3";
    }
    if (strcmp(p, ".pdf") == 0)
    {
        return "application/pdf";
    }
    if (strcmp(p, ".xml") == 0)
    {
        return "text/xml; charset=utf-8";
    }
    if (strcmp(p, ".json") == 0)
    {
        return "application/json; charset=utf-8";
    }
    if (strcmp(p, ".zip") == 0)
    {
        return "application/zip";
    }
    if (strcmp(p, ".tar") == 0)
    {
        return "application/x-tar";
    }
    if (strcmp(p, ".gz") == 0)
    {
        return "application/x-gzip";
    }
    if (strcmp(p, ".rar") == 0)
    {
        return "application/x-rar-compressed";
    }
    if (strcmp(p, ".7z") == 0)
    {
        return "application/x-7z-compressed";
    }
    if (strcmp(p, ".doc") == 0)
    {
        return "application/msword";
    }
    if (strcmp(p, ".docx") == 0)
    {
        return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    }
    if (strcmp(p, ".xls") == 0) {
        return "application/vnd.ms-excel";
    }
    if (strcmp(p, ".xlsx") == 0)
    {
        return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    }
    if (strcmp(p, ".ppt") == 0)
    {
        return "application/vnd.ms-powerpoint";
    }
    if (strcmp(p, ".pptx") == 0)
    {
        return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    }
    return "text/plain; charset=utf-8";
}

int sendDir(int cfd, const char* dirName)
{
    char buf[4096];
    struct dirent** namelist;
    sprintf(buf, "<html><head><title>Index of %s</title></head><body><table>", dirName);
    int num = scandir(dirName, &namelist, NULL, alphasort);
    for (int i = 0; i < num; i++)
    {
        char* name = namelist[i]->d_name;
        char subpath[1024];
        sprintf(subpath, "%s/%s", dirName, name);
        struct stat st;
        int ret = stat(subpath, &st);
        if (S_ISDIR(st.st_mode))
        {
            sprintf(buf + strlen(buf), "<tr><td><a href=\"%s/\">%s/</a></td><td></td></tr>", name, name);
        }
        else
        {
            sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", name, name, st.st_size);
        }
        send(cfd, buf, strlen(buf), 0);
        memset(buf, 0, sizeof(buf));
        free(namelist[i]);
    }
    sprintf(buf, "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    free(namelist);

    return 0;
}

int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

void decodeMsg(char* to, char* from)
{
    for (; *from != '\0'; from++, to++)
    {
        if (*from == '%' && isxdigit(*(from + 1)) && isxdigit(*(from + 2)))
        {
            *to = ((hexit(*(from + 1)) * 16) + hexit(*(from + 2)));
            from += 2;
        }
        else
        {
            *to = *from;
        }
    }
    *to = '\0';
}

