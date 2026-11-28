#pragma once
#include <cerrno>
#include <cstdint>

namespace wind_transport {
enum class Stage { Ready, Socket, Bind };
struct Result {int fd=-1;int error=0;Stage stage=Stage::Socket;};
inline const char* stage_name(Stage stage){
    return stage==Stage::Socket?"socket":stage==Stage::Bind?"netlink bind":"ready";
}
inline bool endpoint_failure(long result){
    return result==-ECONNREFUSED || result==-ETIMEDOUT || result==-EAGAIN ||
        result==-EWOULDBLOCK || result==-EPROTONOSUPPORT || result==-ENOTCONN ||
        result==-EBADMSG || result==-EPROTO || result==-ENOTTY;
}
struct Probe {bool opened=false;long result=-ENOTCONN;};
struct Discovery {int unit=-1;long result=-ENOTCONN;bool target_error=false;};
template<class Attempt>
Discovery discover(int preferred,int minimum,int maximum,Attempt attempt){
    Discovery last;
    // Socket creation alone is not confirmation that a WIND listener exists.
    // Only a successful OP_BIND to the real target selects an endpoint.
    auto test=[&](int unit){
        const Probe probe=attempt(unit);
        last={-1,probe.result,false};
        if(probe.opened&&probe.result==0){last.unit=unit;return true;}
        if(probe.opened&&!endpoint_failure(probe.result)){last.target_error=true;return true;}
        return false;
    };
    if(test(preferred))return last;
    for(int unit=minimum;unit<=maximum;++unit)
        if(unit!=preferred&&test(unit))return last;
    return last;
}
// Transport creation is not a WIND OP_BIND. The module target is selected
// later, when the real target PID is available to initialize().
template<class Ops>
Result open(int unit,uint32_t caller,Ops& ops){
    const int fd=ops.socket(unit);
    if(fd<0)return {-1,ops.error(),Stage::Socket};
    if(ops.bind(fd,caller)==0)return {fd,0,Stage::Ready};
    int error=ops.error();
    if(error==EADDRINUSE){
        // Unlike ignoring EADDRINUSE, explicitly obtain a second usable port.
        if(ops.bind(fd,0)==0)return {fd,0,Stage::Ready};
        error=ops.error();
    }
    ops.close(fd);
    return {-1,error,Stage::Bind};
}
}
