#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "jobs.h"

static struct job jobs[MAX_JOBS];
static int job_count = 0;

int add_job(pid_t pid, const char* command) {
    if (job_count >= MAX_JOBS) {
        fprintf(stderr, "Maximum number of jobs reached\n");
        return -1;
    }
    
    jobs[job_count].pid = pid;
    jobs[job_count].job_id = job_count + 1;
    strncpy(jobs[job_count].command, command, sizeof(jobs[job_count].command) - 1);
    jobs[job_count].command[sizeof(jobs[job_count].command) - 1] = '\0';
    
    job_count++;
    return job_count - 1;
}

void print_jobs(void) {
    int i;
    for (i = 0; i < job_count; i++) {
        int status;
        pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
        
        if (result == 0) {
            // Process still running
            printf("[%d] Running\t\tPID: %d\t%s\n", 
                   jobs[i].job_id, jobs[i].pid, jobs[i].command);
        } else if (result > 0) {
            // Process finished
            printf("[%d] Done\t\tPID: %d\t%s\n", 
                   jobs[i].job_id, jobs[i].pid, jobs[i].command);
        } else {
            // Error or process doesn't exist
            printf("[%d] Finished\tPID: %d\t%s\n", 
                   jobs[i].job_id, jobs[i].pid, jobs[i].command);
        }
    }
}

void cleanup_jobs(void) {
    int i;
    for (i = 0; i < job_count; i++) {
        int status;
        waitpid(jobs[i].pid, &status, WNOHANG);
    }
}