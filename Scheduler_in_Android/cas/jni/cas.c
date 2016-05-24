#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>

// used to get pid by process name
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
    // check input
    if (argc != 5) {
        printf("Input Error 1!\n");
        printf("Please input ./cas #descendants option# #process_name# #schedule_method# #priority#\n");
        exit(-1);
    }
    
    // descendants option, 0: -one, 1: -des
    int des_op = 0;
    if (strcmp("-des", argv[1]) == 0) des_op = 1;
    else if (strcmp("-one", argv[1]) == 0) des_op = 0;
    else {
        printf("Input Error 2!\n");
        printf("The first parameter should be '-one' or '-des'\n");
        printf("'-one' means only this process, '-des' means it and des its descendants\n");
        exit(-1);
    }
    
    // get pid
    // use pstree system_call 223 in project 1
    int pid = 0;
    int descendants[1000] = { 0 };  // pid of descendants
    int des_num = 0;  // number of descendants
    struct prinfo *buf = malloc(1000 * sizeof(struct prinfo));
    int *nr = malloc(sizeof(int));
    if (buf == NULL || nr == NULL) {
        printf("Allocation error!\n");
        exit(-1);
    }
    syscall(223, buf, nr);  // traverse pstree and store pprcess information in buf
    int find = 0;
    // look for this process
    int i = 0;
    while (i < *nr) {
        ++i;
        if (strcmp(argv[2], buf[i].comm) == 0) {
            pid = buf[i].pid;
            find = 1;
            break;
        }
    }
    if (!find) {
        printf("Input Error 3!\n");
        printf("No such process! Please check it.\n");
        exit(-1);
    }
    // get descendants pid
    if (des_op == 1) {
        int pa_index = i++;
        while (buf[i].parent_pid != buf[pa_index].parent_pid) {
            descendants[des_num++] = buf[i++].pid;
        }
    }
    free(buf);  free(nr);
    // printf("Pid: %d\n", pid);
    
    // get sched option
    int sched_option = 0;
    if (strcmp(argv[3], "fifo") == 0) sched_option = SCHED_FIFO;
    else if (strcmp(argv[3], "rr") == 0) sched_option = SCHED_RR;
    else {
        printf("Input Error 4!\n");
        printf("The third parameter should be 'fifo' or 'rr'\n");
        exit(-1);
    }
    
    // set sched parameter
    struct sched_param param;
    int maxpri;
    if (strcmp(argv[4], "-d") == 0) {
        if (sched_option == SCHED_FIFO) maxpri = sched_get_priority_max(SCHED_FIFO);
        else maxpri = sched_get_priority_max(SCHED_RR);
        if(maxpri == -1) {
            printf("Function sched_get_priority_max() failed\n"); 
            exit(-1);
        }
    }
    else {
        maxpri = atoi(argv[4]);
    }
    if (maxpri < 0 || maxpri > 99) {
        printf("Input Error 5!\n");
        printf("The last parameter should be '-d' or any number between 0~99\n");
        printf("'-d' means default\n");
        exit(-1);
    }
    // printf("Max priority: %d\n", maxpri);
    param.sched_priority = maxpri;
    
    // set scheduler
    if (des_op == 0) {
        if (sched_setscheduler(pid, sched_option, &param) == -1) { 
            printf("Function sched_setscheduler() failed!\n"); 
            exit(-1); 
        }
    }
    // descendants
    if (des_op == 1) {
        int i = 0;
        while (i < des_num) {
            if (sched_setscheduler(descendants[i], sched_option, &param) == -1) { 
                printf("Function sched_setscheduler() failed!\n"); 
                exit(-1);
            }
            ++i;
        }
    }
     
    return 0;
}
