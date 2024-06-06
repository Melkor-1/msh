#undef _POSIX_C_SOURCE
#undef _XOPEN_SOURCE

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
    MSH_EXIT,
};

typedef int builtin_func(int argc, const char *const *argv);

static builtin_func msh_cd;
static builtin_func msh_help;
static builtin_func msh_exit;
static builtin_func msh_kill;
static builtin_func msh_whoami;

/* List of built-in commands, followed by their corresponding functions. */
static struct {
    const char *const builtin_str;
    builtin_func *const func;
} const builtin[] = {
    { "cd", &msh_cd },
    { "help", &msh_help },
    { "exit", &msh_exit },
    { "kill", &msh_kill },
    { "whoami", &msh_whoami },
};

static int msh_whoami(int argc, const char *const *argv)
{
    if (argc != 1) {
        fprintf(stderr, "%s: extra operand to \"whoami\".\n", argv[0]);
        return MSH_SUCCESS;
    }

    const uid_t uid = geteuid();
    const struct passwd *const pw = (errno = 0, getpwuid(uid));

    if (pw == NULL || errno) {
        fprintf(stderr, "%s: %s.\n", argv[0], strerror(errno));
    } else {
        puts(pw->pw_name);
    }
    return MSH_SUCCESS;
}

static int msh_kill(int argc, const char *const *argv)
{
    if (argc < 3) {
        fprintf(stderr, "%s: expected argument to \"kill\".\n", argv[0]);
    } else if (argc > 3) {
        fprintf(stderr, "%s: excess arguments to \"kill\".\n", argv[0]);
    } else if (kill((pid_t) strtol(argv[2], NULL, 10), (int) strtol(argv[1],
                NULL, 10)) == -1) {
        fprintf(stderr, "%s: %s.\n", argv[0], strerror(errno));
    }
    return MSH_SUCCESS;
}

static int msh_cd(int argc, const char *const *argv)
{
    if (argc == 1) {
        fprintf(stderr, "%s: expected argument to \"cd\".\n", argv[0]);
    } else if (argc > 2) {
        fprintf(stderr, "%s: excess arguments to \"cd\".\n", argv[0]);
    } else if (chdir(argv[1]) == -1) {
        fprintf(stderr, "%s: %s.\n", argv[0], strerror(errno));
    }
    return MSH_SUCCESS;
}

static int msh_help(int argc, const char *const *argv)
{
    if (argc != 1) {
        fprintf(stderr, "%s: excess arguments to \"help\".\n", argv[0]);
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

static int msh_exit(int argc, const char *const *argv)
{
    if (argc > 2) {
        fprintf(stderr, "%s: excess arguments to \"exit\".\n", argv[0]);
        return MSH_SUCCESS;
    }

    if (argv[1]) {
        /* We have an exit code. */
        return strtol(argv[1], NULL, 10) & 0XFF;
    }

    return MSH_EXIT;
}

/* Calls fork and execvp to duplicate and replace a process, returns MSH_FAILURE 
 * on failure and MSH_SUCCESS on success. */
static int msh_launch(const char *const *argv)
{
    int status = 0;
    int pid = fork();

    if (pid == 0) {
        if (execvp(argv[0], (char *const *) argv) == -1) {
            fprintf(stderr, "%s: %s.\n", argv[0], strerror(errno));
            return MSH_FAILURE;
        }
    }

    if (pid == -1) {
        fprintf(stderr, "%s: %s.\n", argv[0], strerror(errno));
        return MSH_FAILURE;
    }

    do {
        waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    return MSH_SUCCESS;
}

/* Returns MSH_SUCCESS in the absence of commands or return the result of the 
 * executed command if argv[0] was a built-in command. */
static int msh_execute(int argc, const char *const *argv)
{
    if (!argv[0]) {
        /* No commands were entered. */
        return MSH_SUCCESS;
    }

    for (size_t i = 0; i < ARRAY_CARDINALITY(builtin); ++i) {
        if (strcmp(argv[0], builtin[i].builtin_str) == 0) {
            return (*builtin[i].func) (argc, argv);
        }
    }
    return msh_launch(argv);
}

/* Returns a char pointer on success, or a null pointer on failure.
*  Caller must free the line on success.
*
* On memory allocation failure, sets err_code to ENOMEM, or EOF on end-of-file
* or input error. Caller must use ferror() and feof() to distinguish between
* the two. */
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

            if (new == NULL) {
                *err_code = ENOMEM;
                return NULL;
            }
            content = new;
        }
        int c = getc(stdin);

        if (c == EOF || c == '\n') {
            if (c == '\n' || (feof(stdin) && position > 0)) {
                content[position] = '\0';
                return content;
            }

            free(content);
            *err_code = EOF;
            return NULL;
        } else {
            content[position] = (char) c;
        }
        position++;
    }

    /* UNREACHED */
}

/* Returns a pointer to pointers to null-terminated strings terminated by a null
 * pointer, or a null pointer on failure. 
 *
 * The function does not free line in any case. 
 *
 * Caller can distinguish between a memory allocation error and an empty line
 * by checking the value of *argc. A nonzero value indicates a memory allocation
 * error. */
static char **msh_parse_args(char *line, int *argc)
{
    const size_t initial_capacity = 8;
    size_t count = 0;
    size_t capacity = 0;
    char **tokens = NULL;

    for (char *next = line; (next = strtok(next, MSH_TOK_DELIM)); next = NULL) {
        if (count >= capacity) {
            capacity += initial_capacity;
            
            /* +1 to always have space for the terminating null pointer. */
            char **tmp = realloc(tokens, (capacity + 1) * sizeof *tmp);

            if (tmp == NULL) {
                free(tokens);
                *argc = (int) count;
                return NULL;
            }
            tokens = tmp;
        }
        tokens[count++] = next;
    }

    if (tokens) {
        tokens[count] = NULL;
    }
    *argc = (int) count;
    return tokens;
}

static void msh_prompt(void)
{
    /* getuid() is always successful. */
    const struct passwd *const pw = getpwuid(getuid());
    char *cwd = getcwd(NULL, 0);
    char *base_name = cwd ? basename(cwd) : NULL;

    printf("%s:~/%s $ ", pw ? pw->pw_name : "", base_name ? base_name : "");
    free(cwd);
}

static int msh_loop(void)
{
    int status = MSH_SUCCESS;
    bool fail = false;

    do {
        fail = false;
        char *line = NULL;
        char **args = NULL;

        msh_prompt();

        int err_code = 0;
        errno = 0;
        line = msh_read_line(&err_code);

        if (line == NULL) {
            if (err_code == ENOMEM) {
                fprintf(stderr, "error: failed to read line: cannot allocate memory.\n");
                fail = true;
            }

            if (err_code == EOF && ferror(stdin)) {
                if (errno) {
                    fprintf(stderr, "error: failed to read line: %s.\n", strerror(errno));
                } else {
                    fprintf(stderr, "error: failed to read line: unknown error.\n");
                }
                fail = true;
            } else {
                fputc('\n', stdout);
                return MSH_SUCCESS;
            }

            goto out2;
        }

        if (*line == '\0') {
            goto out1;
        }

        int argc = 0;

        if ((args = msh_parse_args(line, &argc)) == NULL) {
            if (argc) {
                fprintf(stderr, "error: cannot allocate memory.\n");
                fail = true;
            }
            goto out1;
        }

        status = msh_execute(argc, (const char *const *) args);

        free(args);
      out1:
        free(line);
      out2:
        if (fail) {
            return MSH_FAILURE;
        }
    } while (status == MSH_SUCCESS);

    return status;
}

int main(void)
{
    return msh_loop() ? EXIT_FAILURE : EXIT_SUCCESS;
}
