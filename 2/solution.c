#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>

void print_line(const struct command_line* line)
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
}

static void execute_cd(const struct command* cmd)
{
	assert(cmd);
	if(cmd->arg_count < 1)
	{
		fprintf(stderr, "cd: missing argument\n");
		return;
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

static void execute_command_line(const struct command_line* line)
{
	assert(line);
	assert(line->head);
	print_line(line);
	if(line->head->type == EXPR_TYPE_PIPE)
	{
		printf("!!!!");
	}
	if(line->head->type == EXPR_TYPE_COMMAND)
	{
		const struct command* cmd = &line->head->cmd;
		pid_t pid = fork();
		if(pid < 0)
		{
			perror("fork");
			return;
		}
		else if(pid == 0)
		{
			if(!strcmp(cmd->exe, "cd"))
			{
				execute_cd(cmd);
				return;
			}
			else if(!strcmp(cmd->exe, "exit"))
				execute_exit(cmd);
			uint32_t arg_count = cmd->arg_count;
			char** args = (char**) malloc((arg_count + 2) * sizeof(char*));
			args[0] = cmd->exe;
			for(uint32_t i = 0; i < arg_count; ++i)
				args[i + 1] = cmd->args[i];
			args[arg_count + 1] = NULL;
			execvp(cmd->exe, args);
			perror("execvp error");
			free(args);
		}
		else if(pid > 0) 
		{
			//int status;
			waitpid(pid, NULL, 0);
			//printf("STATUS%d", status);
		}
	}
	if (line->out_type == OUTPUT_TYPE_STDOUT) {
		printf("stdout\n");
	} else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
		printf("new file - \"%s\"\n", line->out_file);
	} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
		printf("append file - \"%s\"\n", line->out_file);
	} else {
		assert(false);
	}
	printf("Expressions:\n");
	const struct expr *e = line->head;
	while (e != NULL) {
		if (e->type == EXPR_TYPE_COMMAND) {
			printf("\tCommand: %s", e->cmd.exe);
			for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
				printf(" %s", e->cmd.args[i]);
			printf("\n");
		} else if (e->type == EXPR_TYPE_PIPE) {
			printf("\tPIPE\n");
		} else if (e->type == EXPR_TYPE_AND) {
			printf("\tAND\n");
		} else if (e->type == EXPR_TYPE_OR) {
			printf("\tOR\n");
		} else {
			assert(false);
		}
		e = e->next;
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
			/*struct expr* exp = line->head;
			while(exp)
			{
				if(exp->type == EXPR_TYPE_COMMAND)
				{
					for(int i = 0; i < (int) exp->cmd.arg_count; ++i)
					{
						printf("%s\n", exp->cmd.args[i]);
					}
					
				}
				exp = exp->next;
			} */
			execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return 0;
}
