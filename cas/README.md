# cas.c

This program is used to change the scheduler of processes.

According to different parameter, this program can change the scheduler of one 
process itself or all descendants of it.
You can also appoint priority of the process.

To implement this program, I use the syscall that I write in the first project.
But this time I also get extra information included priority and policy.

The mainly function I used to change the scheduler is 
```c
    sched_setscheduler();
```
    
***

This program has 4 input parameter. 
1. The first one is the descendants option. "-one" means only change itself. "-des" means change all descendants of it.
2. The second parameter is the name of process.
3. The third parameter is the scheduler policy that will change to.
4. The last parameter is the real time priority that will change to.

