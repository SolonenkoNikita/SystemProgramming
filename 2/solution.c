#include "parser.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>

#define SIZEOF_PIPE 2

static char** build_args(const struct command* cmd)
{
    assert(cmd);
    char** args = malloc((cmd->arg_count + 2) * sizeof(char*));
    args[0] = cmd->exe;
    for (uint32_t i = 0; i < cmd->arg_count; ++i)
        args[i + 1] = cmd->args[i];
    args[cmd->arg_count + 1] = NULL;
    return args;
}

static bool open_fd(int* fd, const struct command_line* line)
{
    assert(line);
    int flags = O_WRONLY | O_CREAT;
    if (line->out_type == OUTPUT_TYPE_FILE_APPEND)
        flags |= O_APPEND;
    else
        flags |= O_TRUNC;
    *fd = open(line->out_file, flags, 0644);
    return *fd >= 0;
}

static void execute_cd(const struct command* cmd)
{
    assert(cmd);
    if (cmd->arg_count < 1) 
    {
        fprintf(stderr, "cd: missing argument\n");
        exit(EXIT_FAILURE);
    }
    if (chdir(cmd->args[0]))
        perror("Error");
}

static void execute_exit(const struct command* cmd, bool is_in_pipeline)
{
    assert(cmd);
    int status = 0;
    if (cmd->arg_count > 0)
        status = atoi(cmd->args[0]);
    
    if (!is_in_pipeline)
        exit(status);
    _exit(status);
}

static int execute_pipeline(const struct command_line* line)
{
    assert(line);
    int prev_pipe[SIZEOF_PIPE] = {-1, -1};
    const struct expr* head = line->head;
    int status, exit_status = 0;
    size_t child_count = 0, array_size = 32;

    pid_t* array = malloc(array_size * sizeof(pid_t));
    if (!array) 
    {
        perror("malloc");
        return 1;
    }
    while (head && head->type == EXPR_TYPE_COMMAND) 
    {
        int next_pipe[SIZEOF_PIPE] = {-1, -1};
        bool is_last_command = (!head->next) || (head->next->type != EXPR_TYPE_PIPE);

        if (!is_last_command && pipe(next_pipe) < 0) 
        {
            perror("pipe");
            free(array);
            return 1;
        }

        if (child_count >= array_size) 
        {
            array_size *= 2;
            pid_t* new_array = realloc(array, array_size * sizeof(pid_t));
            if (!new_array) 
            {
                perror("realloc");
                free(array);
                return 1;
            }
            array = new_array;
        }

        pid_t pid = fork();
        if (pid < 0) 
        {
            perror("fork");
            free(array);
            return 1;
        }
        else if (pid == 0) 
        {
            free(array);
            
            if (prev_pipe[0] != -1) 
            {
                dup2(prev_pipe[0], STDIN_FILENO);
                close(prev_pipe[0]);
                close(prev_pipe[1]);
            }

            if (!is_last_command) 
            {
                close(next_pipe[0]);
                dup2(next_pipe[1], STDOUT_FILENO);
                close(next_pipe[1]);
            }
            else if (line->out_type != OUTPUT_TYPE_STDOUT) 
            {
                int fd;
                if (!open_fd(&fd, line)) 
                {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            if (next_pipe[0] != -1) 
                close(next_pipe[0]);
            if (next_pipe[1] != -1) 
                close(next_pipe[1]);

            if (!strcmp(head->cmd.exe, "cd")) 
            {
                execute_cd(&head->cmd);
                exit(EXIT_SUCCESS);
            }

            if (!strcmp(head->cmd.exe, "exit")) 
            {
                execute_exit(&head->cmd, true);
                exit(EXIT_SUCCESS);
            }

            char** args = build_args(&head->cmd);
            execvp(head->cmd.exe, args);
            perror("execvp");
            free(args);
            exit(EXIT_FAILURE);
        }
        else 
        {
            array[child_count++] = pid;
            if (prev_pipe[0] != -1) 
            {
                close(prev_pipe[0]);
                close(prev_pipe[1]);
            }

            if (!is_last_command) 
            {
                prev_pipe[0] = next_pipe[0];
                prev_pipe[1] = next_pipe[1];
            }
            else 
            {
                if (next_pipe[0] != -1) 
                    close(next_pipe[0]);
                if (next_pipe[1] != -1) 
                    close(next_pipe[1]);
            }

            head = is_last_command ? NULL : head->next->next;
        }
    }

    if (prev_pipe[0] != -1) 
    {
        close(prev_pipe[0]);
        close(prev_pipe[1]);
    }

    if(!line->is_background)
    {
        for (size_t i = 0; i < child_count; ++i) 
        {
            if (i == child_count - 1) 
            {
                waitpid(array[i], &status, 0);
                exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            }
            else 
                waitpid(array[i], NULL, 0);
        }
    } 
    else 
        exit_status = 0;

    free(array);
    return exit_status;
}

static int execute_command_line(const struct command_line* line)
{
    assert(line);
    assert(line->head);

    if (line->head->next && line->head->next->type == EXPR_TYPE_PIPE) 
        return execute_pipeline(line);
    if (line->head->type == EXPR_TYPE_COMMAND) 
    {
        const struct command* cmd = &line->head->cmd;

        if (!strcmp(cmd->exe, "cd")) 
        {
            execute_cd(cmd);
            return 0;
        }

        if (!strcmp(cmd->exe, "exit")) 
        {
            execute_exit(cmd, false);
            return 0;
        }

        int fd = STDOUT_FILENO;
        if (line->out_type != OUTPUT_TYPE_STDOUT) 
        {
            if (!open_fd(&fd, line)) 
            {
                perror("open");
                return 1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) 
        {
            perror("fork");
            return 1;
        }
        else if (pid == 0) 
        {
            if (fd != STDOUT_FILENO) 
            {
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            char** args = build_args(cmd);
            execvp(cmd->exe, args);
            perror("execvp error");
            free(args);
            exit(EXIT_FAILURE);
        }
        else 
        {
            if (fd != STDOUT_FILENO)
                close(fd);
            if(line->is_background)
                return 0;
            int status;
            waitpid(pid, &status, 0);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }
    return 0;
}

int main(void)
{
    const size_t buf_size = 1024;
    char buf[buf_size];
    int rc, exit_status = 0;
    struct parser* p = parser_new();
    
    while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) 
    {
        parser_feed(p, buf, rc);
        struct command_line* line = NULL;
        while (true) 
        {
            enum parser_error err = parser_pop_next(p, &line);
            if (err == PARSER_ERR_NONE && line == NULL)
                break;
            if (err != PARSER_ERR_NONE) 
            {
                fprintf(stderr, "Error: %d\n", (int)err);
                continue;
            }
            exit_status = execute_command_line(line);
            command_line_delete(line);
        }
    }
    
    parser_delete(p);
    exit(exit_status);
    return 0;
}