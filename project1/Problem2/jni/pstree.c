#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PTREE_CALL 287
#define MAX_BUFFER_SIZE 2048

struct prinfo {
    pid_t parent_pid;
    pid_t pid;
    pid_t first_child_pid;
    pid_t next_sibling_pid;
    long state;
    long uid;
    char comm[16]; // 64 is wrong!!!
};

/* print information of processes */
void print_prinfo(struct prinfo *buf, int nTabs)
{
	while(nTabs--) printf("\t");
	printf("%s, %d, %ld, %d, %d, %d, %ld\n", 
    	buf->comm, 
    	buf->pid, 
    	buf->state, 
    	buf->parent_pid, 
    	buf->first_child_pid, 
    	buf->next_sibling_pid, 
    	buf->uid
    );
}

/* print the tree structure */
void print_pstree(struct prinfo *buf, int ps_size) 
{
    int *d;
    int i = 0;
    int j = 0;
    
    d = malloc(ps_size * sizeof(int));
    memset(d, 0, ps_size);
    print_prinfo(buf, 0);
    for (i = 1; i < ps_size; i++) {
        if (buf[i].parent_pid == buf[i - 1].pid) {
            d[i] = d[i - 1] + 1;
        } else {
            for (j = i - 1; j >= 0; j--) {
                if (buf[j].parent_pid == buf[i].parent_pid) {
                    d[i] = d[j];
                    break;
                }
            }
        }
        print_prinfo(buf + i, d[i]);
    }
    free(d);
}
        
int main(int argc, char* argv[])
{
    struct prinfo *buf;
    int buf_size = MAX_BUFFER_SIZE;
    buf = malloc(buf_size * sizeof(struct prinfo));

    if (syscall(PTREE_CALL, buf, &buf_size) != 0) {
        printf("[ ERROR ]\tptree failed!\n");
        return -1;
    }
    print_pstree(buf, buf_size);
    free(buf);
    return 0;
}
