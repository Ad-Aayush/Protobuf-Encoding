#pragma once

#include <iostream>

#ifndef PROTO_DEBUG
#define PROTO_DEBUG 0
#endif

#if PROTO_DEBUG
#define PB_LOG(expr)                                                          \
  do {                                                                         \
    std::cerr << expr << "\n";                                                 \
  } while (0)
#else
#define PB_LOG(expr)                                                          \
  do {                                                                         \
  } while (0)
#endif
