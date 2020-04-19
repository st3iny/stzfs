#ifndef STZFS_ERROR_H
#define STZFS_ERROR_H

// SUCCESS will always evaluate to false
// ERROR   will always evaluate to true
// makes checking for error easy eg. if(method()) { /* error handling */ }

#include <stdbool.h>

typedef bool stzfs_error_t;
#define SUCCESS false
#define ERROR true

#endif // STZFS_ERROR_H
