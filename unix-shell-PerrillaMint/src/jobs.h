#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

#define MAX_JOBS 100
#define MAX_COMMAND_LEN 256

struct job {
    pid_t pid;
    int job_id;
    char command[MAX_COMMAND_LEN];
};

int add_job(pid_t pid, const char* command);
void print_jobs(void);
void cleanup_jobs(void);

#endif