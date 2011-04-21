
#define PORT 1234

#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <boost/smart_ptr.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <iostream>

#include "epoll.h"

//#define log printf
#define log(...)

static void fatal(const char * msg) {
    printf("fatal: %s\n", msg);
    exit(-1);
}

class EpollSession_t;
class Context_t;

typedef srv::Epoll_t<EpollSession_t, boost::shared_ptr<Context_t> > ServerEpoll_t;


class Context_t {
public:
     virtual ~Context_t() {
        log("D'tor: Context_t\n");     
     }
     virtual uint32_t event(ServerEpoll_t &epl, int fd, uint32_t evn) = 0;
};



class EpollSession_t {
public:
    EpollSession_t(boost::shared_ptr<Context_t> context) : context(context) {
    }
    uint32_t event(ServerEpoll_t &epl,
            int fd, uint32_t evn) {
        return context->event(epl, fd, evn);
    }
private:
    boost::shared_ptr<Context_t> context;
};

#include "data.h"

namespace protocol {

class Http_t : public Context_t {
public:
    int id;
    int rd_cnt;
    struct timeval tm1;
    Http_t(int id) : id(id), rd_cnt(0) {
//        std::cout << "***; " << id << std::endl;
        ::gettimeofday(&tm1, 0);

    }

    ~Http_t() {
        struct timeval tm2;
        ::gettimeofday(&tm2, 0);
        uint64_t t1 = tm1.tv_sec * 1000000 + tm1.tv_usec;
        uint64_t t2 = tm2.tv_sec * 1000000 + tm2.tv_usec;
        uint64_t t = t2 - t1;
//        if (t > 5000)
//         std::cout << t << "; id:" << id << " rd_cnt:" << rd_cnt << std::endl;
        
    }

    size_t written;

    uint32_t event(ServerEpoll_t &epl, int fd, uint32_t evn) {
        if (evn == srv::READ)
            return read(epl, fd, evn);
        if (evn == srv::WRITE)
            return write(epl, fd, evn);
        return srv::ERROR;
    }

    uint32_t write(ServerEpoll_t &, int fd, uint32_t) {
           ssize_t wr;
           log("Writing!\n");

           for(;;) {
               wr = ::write(fd, _tmp_a + written, sizeof(_tmp_a) - written);

               if (wr == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                   return srv::WRITE;               
               } else if (wr == 0) {
                   return srv::CLOSE;
               } else {
                   written += wr;
               }
           
               if (written == sizeof(_tmp_a)) {
                   written = 0;
                   return srv::CLOSE;
              //     return srv::READ;
               }

               log("smrt1\n");

           }

           return srv::CLOSE;
    }

    uint32_t read(ServerEpoll_t &, int fd, uint32_t) {
        log("Read\n");
        char buf[10];
        ssize_t sz;

        do {
            sz = ::read(fd, buf, sizeof(buf));
            if (sz > 0) {
                rd_cnt += sz;
                data.append(buf, sz);
            }            
        } while(sz > 0);

        log("  %d\n", cnt);

        if (sz == 0)
            return srv::CLOSE;
       
        
        if (sz < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /*
            log("=======================================================\n");
            log("%s\n", data.c_str());
            log("=======================================================\n");
            */
           
            size_t l = data.size();
            const char *c = data.c_str();
            if (c[l - 1] == '\n' && c[l - 2] == '\r') {
                log("Reply now!\n");
                written = 0;
                return srv::WRITE;
            }
            
            return srv::READ;
        }

        return srv::CLOSE;
    }

    std::string data;
};

class Accept_t : public Context_t {
    int cnt;
    void set_nonblock(int socket) 
    { 
        int flags; 
        flags = fcntl(socket,F_GETFL,0); 
        assert(flags != -1); 
        fcntl(socket, F_SETFL, flags | O_NONBLOCK); 
    } 
public:
     Accept_t(int fd) : fd(fd), cnt(0) { 
         set_nonblock(fd);     
     }
     uint32_t event(ServerEpoll_t &epl, int fd, uint32_t evn) {
         for(;;) {
             struct sockaddr_in client_addr;
             socklen_t soc_len = sizeof(client_addr);
             int cnfd = ::accept(fd, (struct sockaddr *)&client_addr, &soc_len);
             if (cnfd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                 return srv::READ;

             if (cnfd == -1)
                 fatal("accept");

             log("Accept(fd=%d, cnt=%d)\n", cnfd, cnt);
//             printf("Accept(fd=%d, cnt=%d)\n", cnfd, cnt);
             set_nonblock(cnfd);
        
             boost::shared_ptr<Context_t> http(static_cast<Context_t *>
                (new protocol::Http_t(cnt)));
             cnt++;
             
             epl.add(cnfd, srv::READ, http);
         }
     }

private:
    int fd;
};
}

int open_tcp() {
    int soc_des, soc_rc;
    soc_des = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int flag = 1;
    if (setsockopt(soc_des, SOL_SOCKET, SO_REUSEADDR,
                (void *) &flag, sizeof(int)) < 0)   {   
        fatal("setsockopt");
    } 

    
    if (soc_des == -1)
        fatal("socket");

    struct sockaddr_in serv_addr;
    ::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);
    soc_rc = ::bind(soc_des, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if (soc_rc < 0) fatal("bind");

    soc_rc = ::listen(soc_des, 128);
    if (soc_rc < 0) fatal("listen");
    return soc_des;
}


void * run_epoll (void *v) {
    ServerEpoll_t *epl = (ServerEpoll_t *)v;
    try {
        while(epl->run() > 0) { }
    } catch(srv::Error_t e) {
        e.dump();
        exit(1);
    }

    printf("Thread exited!\n");

    return 0;
}

int main(int, char **) {
    int acpt_fd = open_tcp();
    if (acpt_fd < 0) fatal("open_tcp");
    boost::shared_ptr<Context_t> acpt(static_cast<Context_t *>
            (new protocol::Accept_t(acpt_fd)));


    ServerEpoll_t epl;
    try {
        epl.add(acpt_fd, srv::READ, acpt);
   
    } catch(srv::Error_t e) {
        e.dump();
        exit(1);
    }

    int t = 0;
    pthread_t threads[50];
/*
    pthread_create(&threads[t], 0, run_epoll, (void *)&epl); t++;
    pthread_create(&threads[t], 0, run_epoll, (void *)&epl); t++;
    pthread_create(&threads[t], 0, run_epoll, (void *)&epl); t++;
*/
    pthread_create(&threads[t], 0, run_epoll, (void *)&epl); t++;

    for(;;) { 
        sleep(5);    
    }

    ::close(acpt_fd);

    return 0;
}



