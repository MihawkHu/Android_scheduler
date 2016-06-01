/*
This program is a easy time cost program. It is used to compete with processtest.
The print value of this program is the execution time of itself.
*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

int main()
{
    double tt = clock();
    
    // some calculation to cost time
    double a = 1254125135;
    int cnt1 = 11000000;
    while (cnt1 > 0) {
        cnt1--;
        a = sqrt(abs(a));
    }
    
    tt = (double)(clock() - tt) / CLOCKS_PER_SEC; 
    printf("Cost %.3f seconds!\n", tt);
    
    return 0;
}
