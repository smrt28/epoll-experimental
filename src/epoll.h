#ifndef SRV_EPOLL_H
#define SRV_EPOLL_H

#include <sys/epoll.h>
#include <stdint.h>

#include <boost/thread/mutex.hpp>
#include <boost/thread.hpp>
#include <vector>

#include "error.h"


namespace srv {
    static const uint32_t READ = (1 << 0);
    static const uint32_t WRITE = (1 << 1);
    static const uint32_t CLOSE = 0x1000000;
    static const uint32_t ERROR = 0x2000000;
 
template<typename RawSession_t, typename Context_t>
class Epoll_t {
    class Session_t : public RawSession_t, 
        public boost::enable_shared_from_this<Session_t> {
        public:
            Session_t(Context_t context) :
                RawSession_t(context) { }
            int fd;
            bool live;
    };
public:

  
    Epoll_t() {
        efd = ::epoll_create(10);
        if (efd == -1)
            ERR(EPOLL_CREATE);
    }

    ~Epoll_t() {
        if (::close(efd) == -1)
            WARN("close failed");
    }

    void del(int fd) {
        ::boost::mutex::scoped_lock lock(mutex);

        if (fd >= static_cast<int>(sessions.size()))
            ERR(EPOLL_DELETE);

        delete sessions[fd];
        sessions[fd] = 0;
       
        struct epoll_event ep_evt;
        if (::epoll_ctl(efd, EPOLL_CTL_DEL, fd, &ep_evt) == -1)
            ERR(EPOLL_DELETE);        
    }

    void mod(int fd, uint32_t evt) {
        ::boost::mutex::scoped_lock lock(mutex);
        
        if (fd >= static_cast<int>(sessions.size()))
            ERR(EPOLL_MODIFY);

        Session_t *session = sessions[fd];
        struct epoll_event ep_evt;
        ep_evt.events = 0;
        if ((evt & READ)) ep_evt.events |= EPOLLIN;
        if ((evt & WRITE)) ep_evt.events |= EPOLLOUT;
        if (ep_evt.events == 0) ERR(EPOLL_INSERT);
        ep_evt.events |= EPOLLONESHOT;
        ep_evt.data.ptr = reinterpret_cast<void *>(session);
        if (::epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ep_evt) == -1)
            ERR(EPOLL_INSERT);
        session->live = true;
    }

    void add(int fd, uint32_t evt, Context_t item) {
        ::boost::mutex::scoped_lock lock(mutex);

        if (fd < 0)
            ERR(EPOLL_INSERT);

        ensureSession(fd);

        if (sessions[fd])
            ERR(EPOLL_INSERT);

        Session_t *session;        
        session = new Session_t(item);
        session->fd = fd;
        sessions[fd] = session;

        struct epoll_event ep_evt;
        ep_evt.events = 0;
        if ((evt & READ)) ep_evt.events |= EPOLLIN;
        if ((evt & WRITE)) ep_evt.events |= EPOLLOUT;
        if (ep_evt.events == 0) ERR(EPOLL_INSERT);
        ep_evt.events |= EPOLLONESHOT;
        ep_evt.data.ptr = reinterpret_cast<void *>(session);

        if (::epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ep_evt) == -1)
            ERR(EPOLL_INSERT);
    }

    static const size_t eee = 100;

    int run(int timeout = -1) {
        int rv;
        struct epoll_event ep_evt[eee];
        rv = ::epoll_wait(efd, ep_evt, eee, timeout);
/*
        if (rv > 1)
            printf("%d\n", rv);
*/
        if (rv < 1) 
            return rv;


        for (int i = 0; i < rv; i++) {
            Session_t *session = 0;
            uint32_t evn = 0;
            if ((ep_evt[i].events & EPOLLIN)) evn |= READ;
            if ((ep_evt[i].events & EPOLLOUT)) evn |= WRITE;
            session = reinterpret_cast<Session_t *>(ep_evt[i].data.ptr);
            session->live = false;
            uint32_t e_new = session->event(*this, session->fd, evn);
            
            if (e_new == CLOSE || e_new == ERROR) {
                int fd = session->fd;
                del(fd);
                ::close(fd);
            }
            else {
                mod(session->fd, e_new);
            }
        }

        return rv;       
    }


private:
    void ensureSession(int fd) {
        while(fd >= static_cast<int>(sessions.size())) {
            sessions.push_back(0);
        }
    }

    void shutdown() {
        size_t i;
        ::boost::mutex::scoped_lock lock(mutex);
        for(i = 0;i<sessions.size();i++) {
            //TODO
        }
    }

private:
    int efd;
    std::vector<Session_t *> sessions;
    boost::mutex mutex; 
};

}

#endif

