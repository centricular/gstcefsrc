Socket::Socket(){
    
}
Socket::Socket(int domain, int type, int protocol)
{
    memset(&address_info, 0, sizeof address_info);
    sock = socket(domain, type , protocol);
    if (sock < 0) {
        //exit(1);
        cerr << "opening socket error: " << gai_strerror(errno) << endl;
    }
    address_info.ai_family = domain;
    address_info.ai_socktype = type;
    address_info.ai_protocol = protocol;
    
    port = "";
    address = "";
}

int Socket::bind(string ip, string port){
    if (address_info.ai_family == AF_UNIX) {
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, ip.c_str(), sizeof(addr.sun_path)-1);
        int status = ::bind(sock, (struct sockaddr*)&addr, sizeof(addr));
        if (status < 0) {
            //exit(1);
            cerr << "bind error: " << gai_strerror(errno) << endl;
        }
        return status;
    }
    
    address = ip;
    this->port = port;
    int status;
    struct addrinfo *res;
    address_info.ai_flags = AI_PASSIVE;
    if ((status = getaddrinfo(ip.c_str(), port.c_str(), &address_info, &res)) != 0) {
        cerr << "getaddrinfo error: " << gai_strerror(errno) << endl;
        //exit(1);
        return status;
    }
    address_info.ai_addrlen = res->ai_addrlen;
    address_info.ai_addr = res->ai_addr;
    freeaddrinfo(res);
    status = ::bind(sock, address_info.ai_addr, address_info.ai_addrlen);
    if (status < 0) {
        //exit(1);
        cerr << "bind error: " << gai_strerror(errno) << endl;
    }
    return status;
}

int Socket::connect(string ip, string port){
    if (address_info.ai_family == AF_UNIX) {
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, ip.c_str(), sizeof(addr.sun_path)-1);
        int status = ::connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        if (status < 0) {
            //exit(1);
            cerr << "connect error: " << gai_strerror(errno) << endl;
        }
        return status;
    }
    
    
    address = ip;
    this->port = port;
    struct addrinfo *res;
    int status;
    if ((status = getaddrinfo(ip.c_str(), port.c_str(), &address_info, &res)) != 0){
        cerr << "getaddrinfo error: " << gai_strerror(errno);
        //exit(1);
        return status;
    }
    address_info.ai_addrlen = res->ai_addrlen;
    address_info.ai_addr = res->ai_addr;
    freeaddrinfo(res);
    status = ::connect(sock, address_info.ai_addr, address_info.ai_addrlen);
    if (status < 0) {
        //exit(1);
        cerr << "connect error: " << gai_strerror(status) << endl;
    }
    return status;
}

int Socket::listen(int max_queue){
    int status;
    status = ::listen(sock,max_queue);
    if (status < 0) {
        //exit(1);
        cerr << "listen error: " << gai_strerror(errno) << endl;
    }
    return status;
}

Socket* Socket::accept(){
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    addr_size = sizeof their_addr;
    int newsock = ::accept(sock, (struct sockaddr *)&their_addr, &addr_size);
    if (newsock < 0) {
        //exit(1);
        cerr << "accept error: " << gai_strerror(errno) << endl;
    }
    Socket *newSocket = new Socket(address_info.ai_family,address_info.ai_socktype,address_info.ai_protocol);
    newSocket->sock = newsock;
    newSocket->port = port;
    
    char host[NI_MAXHOST];
    int status = getnameinfo((struct sockaddr *)&their_addr, sizeof(their_addr), host, sizeof(host), NULL, 0, NI_NUMERICHOST);
    if (status < 0) {
        //exit(1);
        cerr << "getnameinfo error: " << gai_strerror(errno) << endl;
    }
    newSocket->address = host;
    newSocket->address_info.ai_family = their_addr.ss_family;
    newSocket->address_info.ai_addr = (struct sockaddr *)&their_addr;
    return newSocket;
}
int Socket::socket_write(string msg){
    const char * buf = msg.c_str();
    int len = (int)strlen(buf);
    int status = (int)send(sock, buf, len, 0);
    if (status < 0) {
        //exit(1);
        cerr << "write error: " << gai_strerror(errno) << endl;
    }
    return status;
}
int Socket::socket_safe_read(string &buf,int len,int seconds){
    vector<Socket> reads;
    reads.push_back(*this);
    int count = Socket::select(&reads, NULL, NULL, seconds);
    if(count < 1){
       //No new Connection
        buf = "";
        return -1;
    }
    char buffer[len];
    bzero(buffer,len);
    int status = (int)recv(sock, buffer, len-1, 0);
    if (status < 0) {
        //exit(1);
        cerr << "read error: " << gai_strerror(errno) << endl;
    }
    buf = string(buffer);
    return status;
}
int Socket::socket_read(string &buf,int len){
    char buffer[len];
    bzero(buffer,len);
    int status = (int)recv(sock, buffer, len-1, 0);
    if (status < 0) {
        //exit(1);
        cerr << "read error: " << gai_strerror(errno) << endl;
    }
    buf = string(buffer);
    return status;
}
int Socket::socket_writeTo(string msg, string ip, string port){
    const char * buf = msg.c_str();
    int len = (int)strlen(buf);
    address = ip;
    this->port = port;
    struct addrinfo *res;
    int status;
    if ((status = getaddrinfo(ip.c_str(), port.c_str(), &address_info, &res)) != 0){
        cerr << "getaddrinfo error: " << gai_strerror(errno) << endl;
        //exit(1);
        return status;
    }
    address_info.ai_addrlen = res->ai_addrlen;
    address_info.ai_addr = res->ai_addr;
    freeaddrinfo(res);
    status = (int)sendto(sock, buf, len, 0, address_info.ai_addr, address_info.ai_addrlen);
    if (status < 0) {
        //exit(1);
        cerr << "writeTo error: " << gai_strerror(errno) << endl;
    }
    return status;
}
int Socket::socket_readFrom(string &buf, int len, string ip, string port){
    char buffer[len];
    bzero(buffer,len);
    struct addrinfo *res;
    int status;
    if ((status = getaddrinfo(ip.c_str(), port.c_str(), &address_info, &res)) != 0){
        cerr << "getaddrinfo error: " << gai_strerror(errno) << endl;
        //exit(1);
        return status;
    }
    address_info.ai_addrlen = res->ai_addrlen;
    address_info.ai_addr = res->ai_addr;
    freeaddrinfo(res);
    status = (int)recvfrom(sock, buffer, len-1, 0,address_info.ai_addr, &address_info.ai_addrlen);
    if (status < 0) {
        //exit(1);
        cerr << "readFrom error: " << gai_strerror(errno) << endl;
    }
    buf = string(buffer); 
    return status;
}

int Socket::socket_set_opt(int level, int optname, void* optval){
    unsigned int len = sizeof(optval);
    int status = ::setsockopt(sock,level,optname,optval,len);
    if (status < 0) {
        //exit(1);
        cerr << "socket_set_opt error: " << gai_strerror(errno) << endl;
    }
    return status;
}

int Socket::socket_get_opt(int level, int optname, void* optval){
    unsigned int len = sizeof(optval);
    int status = ::getsockopt(sock,level,optname,optval,&len);
    if (status < 0) {
        //exit(1);
        cerr << "socket_get_opt error: " << gai_strerror(errno) << endl;
    }
    return status;
}

int Socket::set_blocking(){
    long status = fcntl(sock, F_GETFL, NULL);
    if (status < 0) {
        //exit(1);
        cerr << "set_blocking(get) error: " << gai_strerror(errno) << endl;
    }
    status &= (~O_NONBLOCK);
    status = fcntl(sock, F_SETFL, status);
    if (status < 0) {
        //exit(1);
        cerr << "set_blocking(set) error: " << gai_strerror(errno) << endl;
    }
    return (int)status;
}

int Socket::set_non_blocking(){
    long status = fcntl(sock, F_GETFL, NULL);
    if (status < 0) {
        //exit(1);
        cerr << "set_non_blocking(get) error: " << gai_strerror(errno) << endl;
    }
    status |= O_NONBLOCK;
    status = fcntl(sock, F_SETFL, status);
    if (status < 0) {
        //exit(1);
        cerr << "set_non_blocking(set) error: " << gai_strerror(errno) << endl;
    }
    return (int)status;
}

int Socket::socket_shutdown(int how){
    int status = ::shutdown(sock, how);
    if (status < 0) {
        //exit(1);
        cerr << "shutdown error: " << gai_strerror(errno) << endl;
    }
    return status;
}

void Socket::close(){
    ::close(sock);
}

int Socket::select(vector<Socket> *reads, vector<Socket> *writes, vector<Socket> *exceptions,int seconds){
    int id = reads->at(0).sock;
    struct timeval tv;
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    
    
    FD_ZERO(&readfds);
    
    FD_ZERO(&writefds);
    
    FD_ZERO(&exceptfds);
    
    int maxSock = 0;
    if(reads != NULL){
        for (int i = 0; i < reads->size(); i++) {
            int sockInt = reads->at(i).sock;
            if (sockInt > maxSock) {
                maxSock = sockInt;
            }
            FD_SET(sockInt, &readfds);
        }
    }
    if(writes != NULL){
        for (int i = 0; i < writes->size(); i++) {
            int sockInt = writes->at(i).sock;
            if (sockInt > maxSock) {
                maxSock = sockInt;
            }
            FD_SET(sockInt, &writefds);
        }
    }
    if(exceptions != NULL){
        for (int i = 0; i < exceptions->size(); i++) {
            int sockInt = exceptions->at(i).sock;
            if (sockInt > maxSock) {
                maxSock = sockInt;
            }
            FD_SET(sockInt, &exceptfds);
        }
    }
    
    // Um writefds und exceptfds kümmern wir uns nicht:
    int result = ::select(maxSock+1, &readfds, &writefds, &exceptfds, &tv);
    
    if (result < 0) {
        //exit(1);
        cerr << "select error: " << gai_strerror(errno) << endl;
    }
    if (reads != NULL) {
        for (int i = (int)reads->size()-1; i >= 0; i--) {
            if (!FD_ISSET(reads->at(i).sock, &readfds)) {
                reads->erase(reads->begin()+i);
            }
        }
    }
    if (writes != NULL) {
        for (int i = (int)writes->size()-1; i >= 0; i--) {
            if (!FD_ISSET(writes->at(i).sock, &writefds)) {
                writes->erase(reads->begin()+i);
            }
        }
    }
    if (exceptions != NULL) {
        for (int i = (int)exceptions->size()-1; i >= 0; i--) {
            if (!FD_ISSET(exceptions->at(i).sock, &exceptfds)) {
                exceptions->erase(exceptions->begin()+i);
            }
        }
    }
    return result;
}

string Socket::ipFromHostName(string hostname){
    hostent * record = gethostbyname(hostname.c_str());
    if(record == NULL)
    {
        cerr << "Is unavailable: " << hostname << endl;
        exit(1);
    }
    in_addr * address = (in_addr * )record->h_addr;
    string ip_address = inet_ntoa(* address);
    return ip_address;