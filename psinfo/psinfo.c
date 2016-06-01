/*
This program is used to print the process information.
The result will display by a chart. The meanings of each row are
    process name, process pid, process scheduler policy, process priority, 
    process normal priority, process real time priority

To implement this program, I use the syscall that I wrote in project 1.
But this time I also get extra information included priority and policy.

Type ./psinfo in avd and the process information will display.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>

// used to get process infomation
struct prinfo {
    pid_t parent_pid;		        /* process id of parent */
    pid_t pid;			            /* process id */
    pid_t first_child_pid;		  /* pid of youngest child */
    pid_t next_sibling_pid;		  /* pid of older sibling */

    long prio;
    long normal_prio;
    long rt_priority;
    
    long state;			            /* current state of process */
    long uid;			              /* user id of process owner */
    char comm[64];			        /* name of program executed */
};


int main(int argc, char const *argv[]) {
    
    // get process infomation using system call 223
    struct prinfo *buf = malloc(1000 * sizeof(struct prinfo));
    int *nr = malloc(sizeof(int));
    if (buf == NULL || nr == NULL) {
        printf("Allocation error!\n");
        exit(-1);
    }
    syscall(223, buf, nr);  // traverse pstree and store pprcess information in buf
    
    // print all process information
    int i = 0;
    printf("                name       pid    policy      prio  nor_prio   rt_prio\n");
    while (i < *nr) {
        printf("%20s%10d%10d%10d%10d%10d\n", 
            buf[i].comm, buf[i].pid, sched_getscheduler(buf[i].pid), buf[i].prio, 
            buf[i].normal_prio, buf[i].rt_priority);
        ++i;
    }
    
    return 0;
}
