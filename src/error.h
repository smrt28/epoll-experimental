#ifndef SRV_ERROR_H
#define SRV_ERROR_H

#include <stdio.h>
#include <string>

#define ERR(id) do { throw Error_t(id, "UNKNOWN", __FILE__, __LINE__); } while(0) 
#define ERR2(id, msg) do { throw Error_t(id, msg, __FILE__, __LINE__); } while(0)
#define WARN(...) do {} while(0)

namespace srv {
enum ErrorId_t {
	UNKNOWN = 0,
	EPOLL_CREATE,
	EPOLL_INSERT,
	EPOLL_MODIFY,
	EPOLL_DELETE,
};

class Error_t {
public:
	Error_t(ErrorId_t id = UNKNOWN, const char *msg = "UNKNOWN", const char * ccFile="",
			int ccLine=-1) :
		msg(msg), id(id), ccFile(ccFile), ccLine(ccLine) {
	}

    void dump() {
        printf("Unhandled exception: id(%d); at %s:%d\n", id, ccFile.c_str(), ccLine);
    }

    ~Error_t() {
    }

private:
	std::string msg;
	ErrorId_t id;
	std::string ccFile;
	int ccLine;
};

}

#endif

