#pragma once
#include <photon/common/string_view.h>
#include <photon/common/conststr.h>
namespace photon {
namespace net {
DEFINE_ENUM_STR(Verb, verbstr, UNKNOWN, DELETE, GET, HEAD, POST, PUT, CONNECT,
                OPTIONS, TRACE, COPY, LOCK, MKCOL, MOV, PROPFIND, PROPPATCH,
                SEARCH, UNLOCK, BIND, REBIND, UNBIND, ACL, REPORT, MKACTIVITY,
                CHECKOUT, MERGE, MSEARCH, NOTIFY, SUBSCRIBE, UNSUBSCRIBE, PATCH,
                PURGE, MKCALENDAR, LINK, UNLINK);
Verb string_to_verb(std::string_view v);
}
}
