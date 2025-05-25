#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>

/*void print_line(const struct command_line* line)
{
	const struct expr* head = line->head;
	while(head)
	{
		printf("==================\nCommand = %s\n", head->cmd.exe);
		for(uint32_t i = 0; i < head->cmd.arg_count; ++i)
		{
			printf("Args:%s\n", head->cmd.args[i]);
		}
		printf("==================\n");
		head = head->next;
	}
}*/

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

/*static void execute_pipeline(const struct command_line* line)
{
	print_line(line);
}*/

static void execute_command_line(const struct command_line* line)
{
	assert(line);
	assert(line->head);
	int is_in_pipeline = !isatty(STDOUT_FILENO); // for correct exit > l || exit | ls || exit
	if(line->head->next)
	{
		//execute_pipeline(line);
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
			int flags = O_WRONLY | O_CREAT;
			if(line->out_type == OUTPUT_TYPE_FILE_APPEND)
				flags = flags | O_APPEND;
			else
				flags = flags | O_TRUNC;
			fd = open(line->out_file, flags, 0644);
			if(fd < 0)
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
			uint32_t arg_count = cmd->arg_count;
			char** args = (char**) malloc((arg_count + 2) * sizeof(char*));
			args[0] = cmd->exe;
			for(uint32_t i = 0; i < arg_count; ++i)
				args[i + 1] = cmd->args[i];
			args[arg_count + 1] = NULL;
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
