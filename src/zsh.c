#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>

#define ARRAY_CARDINALITY(x) (sizeof (x) / sizeof ((x)[0]))

#define ZSH_TOK_DELIM 	" \t\r\n\v\f"

static size_t zsh_cd(char *const *argv);
static size_t zsh_help(char *const *argv);
static size_t zsh_exit(char *const *argv);
static size_t zsh_kill (char *const *argv);
static size_t zsh_whoami (char *const *argv);

/* 
*	List of builtin commands, followed by their corresponding functions. 
*/
struct {
    const char *const builtin_str;
    size_t (*builtin_func) (char *const *);
} const builtin[] = {
    { "cd", &zsh_cd },
    { "help", &zsh_help },
    { "exit", &zsh_exit },
    { "kill", &zsh_kill },
	{ "whoami", &zsh_whoami },
};

static size_t zsh_whoami (char *const *argv) 
{
	uid_t uid = geteuid ();
	struct passwd *pw = (errno = 0, getpwuid (uid));

	if (argv[1]) {
		fputs ("-zsh: extra operand to \"whoami\".\n", stderr);
	} else if (!pw || errno) {
		perror ("-zsh: ");
	} else {
		puts (pw->pw_name);
	}
	return 1;
}

static size_t zsh_kill (char *const *argv)
{
    if (!argv[1] || !argv[2]) {
        fputs ("-zsh: expected argument to \"kill\".\n", stderr);
    } else if (kill ( (pid_t) strtol (argv[2], 0, 10), (int) strtol (argv[1], 0, 10)) ==
               -1) {
        perror ("-zsh ");
    }
    return 1;
}

static size_t zsh_cd (char *const *argv)
{
    if (!argv[1]) {
        fputs ("-zsh: expected argument to \"cd\".\n", stderr);
    } else if (chdir (argv[1])) {
        perror ("-zsh");
    }
    return 1;
}

static size_t zsh_help (char *const *argv)
{
    (void) argv;
    puts ("Z-Shell\n"
          "Type program names and arguments, and hit enter.\n"
          "The following are built-in:\n");

    for (size_t i = 0; i < ARRAY_CARDINALITY (builtin); i++) {
        puts (builtin[i].builtin_str);
    }

    puts ("Use the man command for information on other programs.\n");
    return 1;
}

static size_t zsh_exit (char *const *argv)
{
    (void) argv;
    return 0;
}

/* Calls fork and execvp to duplicate and replace a process, returns 0 on failure and 1 on success
*/
static size_t zsh_launch (char *const *argv)
{
    pid_t pid;
    int status = 0;

    pid = fork ();
    if (!pid) {
        if (execvp (argv[0], argv) == -1) {
            perror ("zsh: ");
            return 0;
        }
    }

    if (pid == -1) {
        perror ("zsh: ");
        return 0;
    }

    do {
        waitpid (pid, &status, WUNTRACED);
    } while (!WIFEXITED (status) && !WIFSIGNALED (status));

    return 1;
}

/* Returns 1 in the absence of commands or a pointer to a function if argv[0] was a built-in command.
*/
static size_t zsh_execute (char *const *argv)
{
    if (!argv[0]) {
        /*
         * No commands were entered.
         */
        return 1;
    }

    for (size_t i = 0; i < ARRAY_CARDINALITY (builtin); i++) {
        if (!strcmp (argv[0], builtin[i].builtin_str)) {
            return (*builtin[i].builtin_func) (argv);
        }
    }
    return zsh_launch (argv);
}

/* Returns a char pointer on success, or a null pointer on failure.
*  Caller must free the line on success.
*  Otherwise, zsh_read_line frees all allocations and set them to point to NULL.
*/
static char *zsh_read_line (int *err_code)
{
    const size_t page_size = BUFSIZ;
    size_t position = 0;
	size_t size = 0;
	char *content = 0;

    clearerr (stdin);

    for (;;) {
        if (position >= size) {
			size += page_size;
			char *new = realloc (content, size);
			if (!new) {
				*err_code = ENOMEM;
				return 0;
			}
			content = new;
		}
		int c = getc (stdin);

        if (c == EOF || c == '\n') {
            if (feof (stdin)) {
                free (content);
                *err_code = EOF;
                return 0;
            } else {
                content[position] = '\0';
                return content;
            }
        } else {
            content[position] = (char) c;
        }
        position++;
    }
}

/* Returns a pointer to pointers to null-terminated strings, or a NULL pointer on failure. 
*  Does not free the passed char * in case of failure. 
*/
static char **zsh_parse_args (char *line)
{
    const size_t page_size = 128;
    size_t position = 0;
	size_t size = 0;
    char **tokens = 0;

    for (char *next = line; (next = strtok (next, ZSH_TOK_DELIM));
         next = 0) {
        if (position >= size) {
			size += page_size;
			char **tmp = realloc (tokens, size * sizeof *tmp);

			if (!tmp) {
				free (tokens);
				return 0;
			}
			tokens = tmp;
		}
		tokens[position++] = next;
	}
	tokens[position] = 0;
	return tokens;
}

static size_t zsh_loop (void)
{
    char *line = 0;
    char **args = 0;
    size_t status = 0;

    const uid_t uid = getuid ();
    struct passwd *pw = getpwuid (uid);

    do {
        printf ("%s $ ", pw ? pw->pw_name : "");

        int err_code = 0;

        line = zsh_read_line (&err_code);
        if (!line) {
            if (err_code == ENOMEM) {
                perror ("-zsh ");
            }
            fputc ('\n', stdout);
            return 0;
        }

        if (!(args = zsh_parse_args (line))) {
            perror ("-zsh ");
            free (line);
            return 0;
        }

        status = zsh_execute (args);
        free (line);
        free (args);
    } while (status);

    return 1;
}

int main (void)
{
    /*
     * Load config files, if any. 
     */

    /*
     * Run command loop. 
     */
    if (!zsh_loop ()) {
        return EXIT_FAILURE;
    }

    /*
     * Perform any shutdown/cleanup. 
     */

    return EXIT_SUCCESS;
}
