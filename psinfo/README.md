# psinfo.c

This program is used to print the process information.
The result will display by a chart. The meanings of each row are
* process name
* process pid
* process scheduler policy
* process priority, 
* process normal priority
* process real time priority

To implement this program, I use the syscall that I wrote in project 1.
But this time I also get extra information included priority and policy.

Type ./psinfo in avd and the process information will display.
