/**
 * Networking
 * CS 241 - Spring 2017
 */
#pragma once
#include <stddef.h>
#include <sys/types.h>

#define LOG(...)                                                               \
  do {                                                                         \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
  } while (0);

typedef enum { GET, PUT, DELETE, LIST } verb;
