#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stddef.h>

// Convert bytes → hex string
void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex_out);

// Read entire file into memory
uint8_t *util_read_file(const char *path, size_t *len_out);

// Write entire file
bool util_write_file(const char *path, const uint8_t *data, size_t len);

// Ensure directory exists
bool util_mkdir_p(const char *path);

bool util_path_exists(const char *path);

#endif
