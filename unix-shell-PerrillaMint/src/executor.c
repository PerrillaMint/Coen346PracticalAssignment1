#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/wait.h>

#include "parser.h"
#include "jobs.h"

/**
 * @brief Executes a single, simple command in another process
 * @param cmd Command name
 * @param args Array of arguments (includes cmd as first element, NULL terminated)
 * @param in Input file descriptor (-1 if none)
 * @param out Output file descriptor (-1 if none)
 * @param bg 1 if background, 0 if foreground
 * @param pipes Array of all pipe fds to close in child
 * @param num_pipes Number of pipes
 * @return PID of child process
 */
int execute_command_with_pipes(char* cmd, char** args, int in, int out, int bg, 
                                int (*pipes)[2], int num_pipes) {
    (void)bg;
    
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork failed");
        return -1;
    }
    
    if (pid == 0) {
        // CHILD PROCESS
        
        // Close all pipe file descriptors
        int i;
        for (i = 0; i < num_pipes; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        
        // Handle input redirection
        if (in != -1) {
            dup2(in, STDIN_FILENO);
            close(in);
        }
        
        // Handle output redirection
        if (out != -1) {
            dup2(out, STDOUT_FILENO);
            close(out);
        }
        
        // Execute the command
        execvp(cmd, args);
        
        // If execvp returns, it failed
        perror("execvp failed");
        exit(EXIT_FAILURE);
    }
    
    // PARENT PROCESS
    return pid;
}

/**
 * Simple wrapper for single command (no pipes)
 */
int execute_command(char* cmd, char** args, int in, int out, int bg) {
    (void)bg;
    
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork failed");
        return -1;
    }
    
    if (pid == 0) {
        // CHILD PROCESS
        if (in != -1) {
            dup2(in, STDIN_FILENO);
            close(in);
        }
        
        if (out != -1) {
            dup2(out, STDOUT_FILENO);
            close(out);
        }
        
        execvp(cmd, args);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    }
    
    return pid;
}

/**
 * @brief Executes a command pipeline.
 * @param l A pointer to a cmdline structure containing the parsed command.
 * @return 0 on success, or a non-zero value on error.
 */
int execute(struct cmdline *l){
    if (l->seq[0] == NULL) {
        return EXIT_SUCCESS;
    }
    
    int num_cmds = 0;
    while (l->seq[num_cmds] != NULL) {
        num_cmds++;
    }
    
    int in_fd = -1;
    int out_fd = -1;
    
    if (l->in) {
        in_fd = open(l->in, O_RDONLY);
        if (in_fd < 0) {
            perror("Failed to open input file");
            return EXIT_FAILURE;
        }
    }
    
    if (l->out) {
        out_fd = open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            perror("Failed to open output file");
            if (in_fd != -1) close(in_fd);
            return EXIT_FAILURE;
        }
    }
    
    // Simple command (no pipe)
    if (num_cmds == 1) {
        char **cmd = l->seq[0];
        pid_t pid = execute_command(cmd[0], cmd, in_fd, out_fd, l->bg);
        
        if (in_fd != -1) close(in_fd);
        if (out_fd != -1) close(out_fd);
        
        if (pid < 0) {
            return EXIT_FAILURE;
        }
        
        if (!l->bg) {
            waitpid(pid, NULL, 0);
        } else {
            add_job(pid, cmd[0]);
            printf("[Background] PID: %d\n", pid);
        }
        
        return EXIT_SUCCESS;
    }
    
    // Multiple pipes
    int i;
    int prev_pipe_read = in_fd;  // Input for first command
    pid_t *pids = malloc(num_cmds * sizeof(pid_t));
    
    for (i = 0; i < num_cmds; i++) {
        int pipefd[2];
        int pipe_created = 0;
        
        // Create pipe if not the last command
        if (i < num_cmds - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe failed");
                if (prev_pipe_read != -1 && prev_pipe_read != in_fd) 
                    close(prev_pipe_read);
                free(pids);
                return EXIT_FAILURE;
            }
            pipe_created = 1;
        }
        
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            if (pipe_created) {
                close(pipefd[0]);
                close(pipefd[1]);
            }
            if (prev_pipe_read != -1 && prev_pipe_read != in_fd) 
                close(prev_pipe_read);
            free(pids);
            return EXIT_FAILURE;
        }
        
        if (pid == 0) {
            // CHILD PROCESS
            
            // Set up input
            if (prev_pipe_read != -1) {
                dup2(prev_pipe_read, STDIN_FILENO);
                close(prev_pipe_read);
            }
            
            // Set up output
            if (i < num_cmds - 1) {
                // Not last command: write to pipe
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
            } else if (out_fd != -1) {
                // Last command with output redirection
                dup2(out_fd, STDOUT_FILENO);
                close(out_fd);
            }
            
            execvp(l->seq[i][0], l->seq[i]);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
        
        // PARENT PROCESS
        pids[i] = pid;
        
        // Close the previous pipe read end (child has it now)
        if (prev_pipe_read != -1 && prev_pipe_read != in_fd) {
            close(prev_pipe_read);
        }
        
        // Close pipe write end and save read end for next iteration
        if (pipe_created) {
            close(pipefd[1]);  // Parent doesn't need write end
            prev_pipe_read = pipefd[0];  // Next child will read from this
        }
    }
    
    // Close remaining file descriptors
    if (in_fd != -1) close(in_fd);
    if (out_fd != -1) close(out_fd);
    
    // Wait for all children
    if (!l->bg) {
        for (i = 0; i < num_cmds; i++) {
            waitpid(pids[i], NULL, 0);
        }
    } else {
        add_job(pids[0], l->seq[0][0]);
        printf("[Background] PID: %d\n", pids[0]);
    }
    
    free(pids);
    return EXIT_SUCCESS;
}