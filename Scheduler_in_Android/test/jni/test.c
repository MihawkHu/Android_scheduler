#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>

int main()
{
    double a = 1254125135;
    int cnt1 = 15000000;
    while (cnt1 > 0) {
        cnt1--;
        a = sqrt(abs(a));
    }
    // printf("done!\n");
    
    return 0;
}
