#pragma once
#include "types.h"

/* Run a shell script file or interactive sh/bash session */
int  sh_run_file(const char *path);
int  sh_run_string(const char *script);
void sh_interactive(bool bash_mode);   /* bash_mode changes PS1 */

/* Execute one pipeline/command from outside (used by shell.c) */
int  sh_exec(const char *cmdline);

/* Last exit status ($?) */
extern int sh_last_status;

const char *sh_getvar(const char *name);
