#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif

#define _POSIX_C_SOURCE 200819L
#define _XOPEN_SOURCE 700

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
#include <libgen.h>

#define ARRAY_CARDINALITY(x) (sizeof (x) / sizeof ((x)[0]))

#define MSH_TOK_DELIM 	" \t\r\n\v\f"

static size_t msh_cd(const char *const *argv);
static size_t msh_help(const char *const *argv);
static size_t msh_exit(const char *const *argv);
static size_t msh_kill(const char *const *argv);
static size_t msh_whoami(const char *const *argv);

/* 
*	List of builtin commands, followed by their corresponding functions. 
*/
static struct {
    const char *const builtin_str;
    size_t (* const builtin_func)(const char *const *);
} const builtin[] = {
    { "cd", &msh_cd },
    { "help", &msh_help },
    { "exit", &msh_exit },
    { "kill", &msh_kill },
    { "whoami", &msh_whoami },
};

static size_t msh_whoami(const char *const *argv)
{
    const uid_t uid = geteuid();
    const struct passwd *const pw = (errno = 0, getpwuid(uid));

    if (argv[1]) {
        fputs("-msh: extra operand to \"whoami\".\n", stderr);
    } else if (!pw || errno) {
        perror("-msh: ");
    } else {
        puts(pw->pw_name);
    }
    return 1;
}

static size_t msh_kill(const char *const *argv)
{
    if (!argv[1] || !argv[2]) {
        fputs("-msh: expected argument to \"kill\".\n", stderr);
    } else
        if (kill((pid_t) strtol(argv[2], 0, 10), (int) strtol(argv[1], 0, 10)) == -1) {
        perror("-msh ");
    }
    return 1;
}

static size_t msh_cd(const char *const *argv)
{
    if (!argv[1]) {
        fputs("-msh: expected argument to \"cd\".\n", stderr);
    } else if (chdir(argv[1])) {
        perror("-msh");
    }
    return 1;
}

static size_t msh_help(const char *const *argv)
{
    (void) argv;
    puts("Z-Shell\n"
         "Type program names and arguments, and hit enter.\n"
         "The following are built-in:\n");

    for (size_t i = 0; i < ARRAY_CARDINALITY(builtin); ++i) {
        puts(builtin[i].builtin_str);
    }

    puts("Use the man command for information on other programs.\n");
    return 1;
}

static size_t msh_exit(const char *const *argv)
{
    (void) argv;
    return 0;
}

/* Calls fork and execvp to duplicate and replace a process, returns 0 on failure and 1 on success
*/
static size_t msh_launch(const char *const *argv)
{
    int status = 0;
    int pid = fork();
    
    if (!pid) {
        if (execvp(argv[0], (char *const *) argv) == -1) {
            perror("msh: ");
            return 0;
        }
    }

    if (pid == -1) {
        perror("msh: ");
        return 0;
    }

    do {
        waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    return 1;
}

/* Returns 1 in the absence of commands or a pointer to a function if argv[0] was a built-in command.
*/
static size_t msh_execute(const char *const *argv)
{
    if (!argv[0]) {
        /* No commands were entered. */
        return 1;
    }

    for (size_t i = 0; i < ARRAY_CARDINALITY(builtin); ++i) {
        if (!strcmp(argv[0], builtin[i].builtin_str)) {
            return (*builtin[i].builtin_func) (argv);
        }
    }
    return msh_launch(argv);
}

/* Returns a char pointer on success, or a null pointer on failure.
*  Caller must free the line on success.
*  Otherwise, msh_read_line frees all allocations and set them to point to NULL.
*/
static char *msh_read_line(int *err_code)
{
    const size_t page_size = BUFSIZ;
    size_t position = 0;
    size_t size = 0;
    char *content = 0;

    clearerr(stdin);

    for (;;) {
        if (position >= size) {
            size += page_size;
            char *new = realloc(content, size);

            if (!new) {
                *err_code = ENOMEM;
                return 0;
            }
            content = new;
        }
        int c = getc(stdin);

        if (c == EOF || c == '\n') {
            if (feof(stdin)) {
                free(content);
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
static char **msh_parse_args(char *line)
{
    const size_t page_size = 128;
    size_t position = 0;
    size_t size = 0;
    char **tokens = 0;

    for (char *next = line; (next = strtok(next, MSH_TOK_DELIM)); next = 0) {
        if (position >= size) {
            size += page_size;
            char **tmp = realloc(tokens, size * sizeof *tmp);

            if (!tmp) {
                free(tokens);
                return 0;
            }
            tokens = tmp;
        }
        tokens[position++] = next;
    }
    tokens[position] = 0;
    return tokens;
}

static size_t msh_loop(void)
{
    char *line = 0;
    char **args = 0;
    size_t status = 0;

    const uid_t uid = getuid();
    const struct passwd *const pw = getpwuid(uid);
    
    char *cwd = getcwd(0, 0);
    char *base_name = cwd ? basename(cwd) : 0;

    do {
        printf("%s:~/%s $ ", pw ? pw->pw_name : "", base_name? base_name : "");

        int err_code = 0;

        line = msh_read_line(&err_code);
        if (!line) {
            if (err_code == ENOMEM) {
                perror("-msh ");
            }
            fputc('\n', stdout);
            return 0;
        }

        if (!(args = msh_parse_args(line))) {
            perror("-msh ");
            free(line);
            return 0;
        }

        status = msh_execute((const char *const *) args);
        free(line);
        free(args);
    } while (status);
    
    free(cwd);
    return 1;
}

int main(void)
{
    /*
     * Load config files, if any. 
     */

    /*
     * Run command loop. 
     */
    if (!msh_loop()) {
        return EXIT_FAILURE;
    }

    /*
     * Perform any shutdown/cleanup. 
     */

    return EXIT_SUCCESS;
}
