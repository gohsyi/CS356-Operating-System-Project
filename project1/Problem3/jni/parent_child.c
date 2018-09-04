#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
 
int main() 
{
    printf("[ INFO ]\ttesting Problem 3\n");

    pid_t pid;
    pid = fork(); // new child process

    if (pid == -1) {
        printf("[ ERROR ]\tfork error\n");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0) {
        printf("516030910306-Child is %d\n", getpid());
        printf("using execl to execute pstree\n");
        execl("./pstree", "pstree", NULL);
        _exit(EXIT_SUCCESS);
    }
    else {
        waitpid(pid, NULL, 0);
        printf("516030910306-Parent is %d\n", getpid());
    }

    return EXIT_SUCCESS;
}

