#include "parser.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>

static char** build_args(const struct command* cmd)
{
	assert(cmd);
	char** args = (char**) malloc((cmd->arg_count + 2) * sizeof(char*));
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
	if (*fd < 0) 
		return false;
	return true;
}

static void execute_cd(const struct command* cmd)
{
	assert(cmd);
	if(cmd->arg_count < 1)
	{
		fprintf(stderr, "cd: missing argument\n");
		exit(EXIT_FAILURE);	
	}
	if(chdir(cmd->args[0]))
		perror("Error");
	
}

static void execute_exit(const struct command* cmd)
{
	assert(cmd);
	int status = 0;
    if (cmd->arg_count > 0) 
        status = atoi(cmd->args[0]);
    exit(status);
}

static void execute_pipeline(const struct command_line* line)
{
	assert(line);
	int prev_pipe[2] = {-1, -1};
	const struct expr* head = line->head;
	pid_t pid;
	int last_pid = -1;
	while(head && head->type == EXPR_TYPE_COMMAND)
	{
		int next_pipe[2] = {-1, -1};
		bool is_last_command = (!head->next) || (head->next->type != EXPR_TYPE_PIPE);
		if (!is_last_command) 
		{
            if (pipe(next_pipe) < 0) 
			{
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }
		pid = fork();
		if (pid < 0)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
		else if (pid == 0) 
		{ 
			if (prev_pipe[0] != -1)
            {
                if (dup2(prev_pipe[0], STDIN_FILENO) < 0)
                {
                    perror("dup2 stdin");
                    exit(EXIT_FAILURE);
                }
				close(prev_pipe[0]);
                close(prev_pipe[1]);
            }

			if (is_last_command && line->out_type != OUTPUT_TYPE_STDOUT) 
			{
                int fd;
				if(!open_fd(&fd, line))
				{
					perror("open");
					exit(EXIT_FAILURE);
				}
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

			else if (next_pipe[1] != -1)
            {
                if (dup2(next_pipe[1], STDOUT_FILENO) < 0)
                {
                    perror("dup2 stdout");
                    exit(EXIT_FAILURE);
                }
                close(next_pipe[1]);
            }

			if (next_pipe[0] != -1) 
				close(next_pipe[0]);

			if(!strcmp(head->cmd.exe, "cd"))
			{
				execute_cd(&head->cmd);
				exit(EXIT_SUCCESS);
			}

			if (!strcmp(head->cmd.exe, "exit")) 
			{
				execute_exit(&head->cmd);
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
			if (prev_pipe[0] != -1) 
			{
                close(prev_pipe[0]);
                close(prev_pipe[1]);
            }

			if (is_last_command) 
                last_pid = pid;
			
			prev_pipe[0] = next_pipe[0];
			prev_pipe[1] = next_pipe[1];
			
			head = head->next;
            if (head && head->type == EXPR_TYPE_PIPE) 
                head = head->next;
		}
    }

	if (prev_pipe[0] != -1) 
	{
        close(prev_pipe[0]);
        close(prev_pipe[1]);
    }

    if (last_pid > 0) 
        waitpid(pid, NULL, 0);
}

static void execute_command_line(const struct command_line* line)
{
	assert(line);
	assert(line->head);
	int is_in_pipeline = !isatty(STDOUT_FILENO); // for correct exit > l || exit | ls || exit
	if(line->head->next && line->head->next->type == EXPR_TYPE_PIPE)
	{
		execute_pipeline(line);
		return;
	}
	if(line->head->type == EXPR_TYPE_COMMAND)
	{
		const struct command* cmd = &line->head->cmd;
		if(!strcmp(cmd->exe, "cd") )
		{
			execute_cd(cmd);
			return;
		}
		if (!strcmp(cmd->exe, "exit") && !is_in_pipeline) 
		{
			execute_exit(cmd);
			return;
		}
		int fd = STDOUT_FILENO;
		if(line->out_type != OUTPUT_TYPE_STDOUT)
		{
			if(!open_fd(&fd, line))
			{
				perror("open");
				return;
			}
		}
		pid_t pid = fork();
		if(pid < 0)
		{
			perror("fork");
			return;
		}
		else if(pid == 0)
		{
			if (!strcmp(cmd->exe, "exit")) 
                execute_exit(cmd);
			if(fd != STDOUT_FILENO)
			{
				if(dup2(fd, STDOUT_FILENO) < 0)
				{
					perror("dup2");
                    exit(EXIT_FAILURE);
				}
				close(fd);
			}
			char** args = build_args(cmd);
			execvp(cmd->exe, args);
			perror("execvp error");
			free(args);
			exit(EXIT_FAILURE);
		}
		else if(pid > 0) 
		{
			if(fd != STDOUT_FILENO)
				close(fd);
			int status;
            waitpid(pid, &status, 0);
            
            if (!strcmp(cmd->exe, "exit") && is_in_pipeline)
                exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
        }
	}
}

int main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser* p = parser_new();
	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) 
	{
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true) 
		{
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) 
			{
				printf("Error: %d\n", (int)err);
				continue;
			}
			execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return 0;
}
