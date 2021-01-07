#ifndef SOCKET_H
#define SOCKET_H
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <vector>
#include <sys/un.h>
#include <errno.h>

using namespace std;

class Socket
{
public:
    int sock;
    string address;
    string port;
    struct addrinfo address_info;
    Socket();
    Socket(int domain,int type,int protocol);
    int bind(string ip, string port);
    int connect(string ip, string port);
    int listen(int max_queue);
    Socket* accept();
    int socket_write(string msg);
    int socket_read(string &buf,int len);
    int socket_safe_read(string &buf,int len,int seconds);
    int socket_writeTo(string msg, string ip, string port);
    int socket_readFrom(string &buf, int len, string ip, string port);
    int socket_set_opt(int level, int optname, void* optval);
    int socket_get_opt(int level, int optname, void* optval);
    int set_blocking();
    int set_non_blocking();
    int socket_shutdown(int how);
    void close();
    static int select(vector<Socket> *reads, vector<Socket> *writes, vector<Socket> *exceptions,int seconds);
    static string ipFromHostName(string hostname);
};
#endif

