#pragma once
#ifndef SERVER_H
#define SERVER_H
int initListenFd(unsigned short port);
int epollRun(unsigned short port);
int acceptConn(int lfd, int epfd);
int recvHttpRequest(int cfd, int epfd);
int parseRequestLine(int cfd, const char* reqLine);
int sendFile(int cfd, const char* fileName);
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length);
int disConn(int cfd, int epfd);
char* getContentType(const char* fileName);
int sendDir(int cfd, const char* dirName);
void decodeMsg(char* to, char* from);
int hexit(char c);


#endif 