// Created by moisrex on 12/10/24.

#ifndef LIBURING_IXX_H
#define LIBURING_IXX_H

#ifdef __linux__
#    if __has_include(<liburing/liburing-hdr-only.h>)
#        include <liburing/liburing-hdr-only.h>
#    else
#        include <liburing.h>
#    endif
#endif

#endif //LIBURING_IXX_H
