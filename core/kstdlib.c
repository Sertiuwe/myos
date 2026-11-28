#include "kstdlib.h"
#include "kctype.h"
#include "kerrno.h"

int errno = 0;

unsigned long strtoul(const char *s, char **end, int base) {
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s=='-'){ neg=1; s++; } else if (*s=='+') s++;
    if (base==0){
        if (s[0]=='0' && (s[1]=='x'||s[1]=='X')){ base=16; s+=2; }
        else if (s[0]=='0'){ base=8; s++; }
        else base=10;
    } else if (base==16 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) s+=2;
    unsigned long acc=0, cut=0xFFFFFFFFUL/ (unsigned long)base;
    int any=0;
    for (;; s++){
        int d;
        if (isdigit((unsigned char)*s)) d=*s-'0';
        else if (isalpha((unsigned char)*s)) d=toupper((unsigned char)*s)-'A'+10;
        else break;
        if (d>=base) break;
        if (acc>cut){ errno=ERANGE; acc=0xFFFFFFFFUL; }
        else acc=acc*(unsigned long)base+(unsigned long)d;
        any=1;
    }
    if (end) *end=(char*)s;
    if (!any) { if(end)*end=(char*)s; return 0; }
    return neg ? (unsigned long)(-(long)acc) : acc;
}

long strtol(const char *s, char **end, int base) {
    while (isspace((unsigned char)*s)) s++;
    int neg = (*s=='-');
    unsigned long u = strtoul(s, end, base);
    return neg ? -(long)u : (long)u;
}

int atoi(const char *s){ return (int)strtol(s, 0, 10); }
int abs(int x){ return x<0?-x:x; }
long labs(long x){ return x<0?-x:x; }
