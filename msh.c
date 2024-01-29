

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

enum {
    MSH_SUCCESS = 0,
    MSH_FAILURE,
};

/* Taking an int like argc would simplify these functions. */
static int msh_cd(const char *const *argv);
static int msh_help(const char *const *argv);
static int msh_exit(const char *const *argv);
static int msh_kill(const char *const *argv);
static int msh_whoami(const char *const *argv);

/* 
*	List of builtin commands, followed by their corresponding functions. 
*/
static struct {
    const char *const builtin_str;
    int (* const builtin_func)(const char *const *);
} const builtin[] = {
    { "cd", &msh_cd },
    { "help", &msh_help },
    { "exit", &msh_exit },
    { "kill", &msh_kill },
    { "whoami", &msh_whoami },
};

static int msh_whoami(const char *const *argv)
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
    return MSH_SUCCESS;
}

static int msh_kill(const char *const *argv)
{
    if (!argv[1] || !argv[2]) {
        fputs("-msh: expected argument to \"kill\".\n", stderr);
    } else if (argv[3]) {
        fputs("-msh: excess arguments to \"kill\".\n", stderr);
    } else if (kill((pid_t) strtol(argv[2], NULL, 10), (int) strtol(argv[1], NULL, 10)) == -1) {
        perror("-msh ");
    }
    return MSH_SUCCESS;
}

static int msh_cd(const char *const *argv)
{
    (void)*argv++;
    const char *const dir = *argv++;
    
    if (!dir) {
        fputs("-msh: expected argument to \"cd\".\n", stderr);
    } else if (*argv) {
        fputs("-msh: excess arguments to \"cd\".\n", stderr);
    } else if (chdir(dir)) {
        perror("-msh");
    }
    return MSH_SUCCESS;
}

static int msh_help(const char *const *argv)
{
    if (argv[1]) {
        fputs("-msh: excess arguments to \"help\".\n", stderr);
        return MSH_SUCCESS;
    }

    puts("M-Shell\n"
         "Type program names and arguments, and hit enter.\n"
         "The following are built-in:\n");

    for (size_t i = 0; i < ARRAY_CARDINALITY(builtin); ++i) {
        puts(builtin[i].builtin_str);
    }

    puts("Use the man command for information on other programs.\n");
    return MSH_SUCCESS;
}

static int msh_exit(const char *const *argv)
{
    if (argv[2]) {
        fputs("-msh: excess arguments to \"help\".\n", stderr);
        return MSH_SUCCESS;
    }

    if (argv[1]) {
        /* We have an exit code. */
        return strtol(argv[1], NULL, 10) & 0XFF;
    }

    return MSH_SUCCESS;
}

/* Calls fork and execvp to duplicate and replace a process, returns MSH_FAILURE 
 * on failure and MSH_SUCCESS on success.
 */
static int msh_launch(const char *const *argv)
{
    int status = 0;
    int pid = fork();
    
    if (!pid) {
        if (execvp(argv[0], (char *const *) argv) == -1) {
            perror("msh: ");
            return MSH_FAILURE;
        }
    }

    if (pid == -1) {
        perror("msh: ");
        return MSH_FAILURE;
    }

    do {
        waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    return MSH_SUCCESS;
}

/* Returns MSH_SUCCESS in the absence of commands or return the result of the 
 * executed command if argv[0] was a built-in command.
 */
static int msh_execute(const char *const *argv)
{
    if (!argv[0]) {
        /* No commands were entered. */
        return MSH_SUCCESS;
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
    char *content = NULL;

    clearerr(stdin);

    for (;;) {
        if (position >= size) {
            size += page_size;
            char *new = realloc(content, size);

            if (!new) {
                *err_code = ENOMEM;
                return NULL;
            }
            content = new;
        }
        int c = getc(stdin);

        if (c == EOF || c == '\n') {
            if (feof(stdin)) {
                free(content);
                *err_code = EOF;
                return NULL;
            } else {
                content[position] = '\0';
                return content;
            }
        } else {
            content[position] = (char) c;
        }
        position++;
    }

    /* UNREACHED */
}

/* Returns a pointer to pointers to null-terminated strings, or a NULL pointer on failure. 
*  The function does not free line in any case.
*/
static char **msh_parse_args(char *line)
{
    const size_t page_size = 128;
    size_t position = 0;
    size_t size = 0;
    char **tokens = NULL;

    for (char *next = line; (next = strtok(next, MSH_TOK_DELIM)); next = NULL) {
        if (position >= size) {
            size += page_size;
            char **tmp = realloc(tokens, size * sizeof *tmp);

            if (!tmp) {
                free(tokens);
                return NULL;
            }
            tokens = tmp;
        }
        tokens[position++] = next;
    }

    if (tokens) {
        tokens[position] = NULL;
    }
    return tokens;
}

static int msh_loop(void)
{
    int status = MSH_SUCCESS;
    bool fail = false;

    do {
        char *line = NULL;
        char **args = NULL;

        /* getuid() is always successful. */
        const uid_t uid = getuid();
        const struct passwd *const pw = getpwuid(uid);
        char *cwd = getcwd(NULL, 0);
        char *base_name = cwd ? basename(cwd) : NULL;

        printf("%s:~/%s $ ", pw ? pw->pw_name : "", base_name? base_name : "");

        int err_code = 0;

        line = msh_read_line(&err_code);
        if (!line) {
            if (err_code == ENOMEM) {
                perror("-msh ");
            }
            fputc('\n', stdout);
            goto out2;
        }
    
        if (!*line) {
            continue;
        }

        if (!(args = msh_parse_args(line))) {
            perror("-msh ");
            goto out1;
        }

        status = msh_execute((const char *const *) args);
      out1:
        free(line);
        free(args);
      out2:
        free(cwd);

        if (fail) return 1;
    } while (status == MSH_SUCCESS);
    
    return status;
}

int main(void)
{
    return msh_loop();
}
