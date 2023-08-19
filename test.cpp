
#include<stdio.h>
using namespace std;

typedef union {
long value;
struct {
char a;
char b;
}v;
} Type;

int main(){
    Type t; 
    t.v.a = 0x5A;
    t.v.b = 0xA5;
    printf("%x\n",t.value);
    return 0;
}