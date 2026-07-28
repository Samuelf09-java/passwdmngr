#ifndef BACKEND_H
#define BACKEND_H

#include <stdbool.h>
#include "app.h"

// Attempt login: returns true on success
bool backend_attempt_login(AppState *app,
                           const char *username,
                           const char *password);

// Create a new account
bool backend_create_account(const char *username,
                            const char *password);

#endif
