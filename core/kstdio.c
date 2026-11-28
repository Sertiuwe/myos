#include "kstdio.h"
#include "kstring.h"

static void (*sink)(const char *);
void kstdio_set_sink(void (*fn)(const char *)) { sink = fn; }

typedef struct { char *buf; size_t size; size_t pos; } out_t;

static void putc_o(out_t *o, char c) {
    if (o->buf && o->pos + 1 < o->size) o->buf[o->pos] = c;
    o->pos++;
}
static void puts_o(out_t *o, const char *s, size_t n) { for (size_t i = 0; i < n; i++) putc_o(o, s[i]); }
static void pad_o(out_t *o, char c, size_t n) { for (size_t i = 0; i < n; i++) putc_o(o, c); }

static size_t fmt_uint(char *tmp, size_t cap, uint64_t v, int base, int upper) {
    static const char *dl = "0123456789abcdef", *du = "0123456789ABCDEF";
    const char *d = upper ? du : dl;
    size_t i = cap;
    tmp[--i] = 0;
    if (!v) tmp[--i] = '0';
    while (v) { tmp[--i] = d[v % (unsigned)base]; v /= (unsigned)base; }
    size_t len = (cap - 1) - i;
    memmove(tmp, tmp + i, len + 1);
    return len;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    out_t o = { buf, size, 0 };
    while (*fmt) {
        if (*fmt != '%') { putc_o(&o, *fmt++); continue; }
        fmt++;
        int f_minus=0, f_plus=0, f_space=0, f_zero=0, f_hash=0;
        for (;;) {
            if (*fmt=='-'){f_minus=1;fmt++;}
            else if (*fmt=='+'){f_plus=1;fmt++;}
            else if (*fmt==' '){f_space=1;fmt++;}
            else if (*fmt=='0'){f_zero=1;fmt++;}
            else if (*fmt=='#'){f_hash=1;fmt++;}
            else break;
        }
        size_t width = 0;
        if (*fmt=='*'){ width=(size_t)va_arg(ap,int); fmt++; }
        else while (*fmt>='0'&&*fmt<='9'){ width=width*10+(size_t)(*fmt-'0'); fmt++; }
        int have_prec=0; size_t prec=0;
        if (*fmt=='.'){ have_prec=1; fmt++;
            if (*fmt=='*'){ prec=(size_t)va_arg(ap,int); fmt++; }
            else while (*fmt>='0'&&*fmt<='9'){ prec=prec*10+(size_t)(*fmt-'0'); fmt++; }
        }
        int len=0;
        if (fmt[0]=='h'&&fmt[1]=='h'){len=1;fmt+=2;}
        else if (fmt[0]=='h'){len=2;fmt++;}
        else if (fmt[0]=='l'&&fmt[1]=='l'){len=4;fmt+=2;}
        else if (fmt[0]=='l'){len=3;fmt++;}
        else if (fmt[0]=='z'){len=5;fmt++;}
        char conv=*fmt; if (conv) fmt++;

        char tmp[40]; size_t tlen=0; char sign=0;
        const char *prefix=""; size_t plen=0;
        const char *str=NULL; size_t slen=0; char ch=0;

        switch (conv) {
        case 'd': case 'i': {
            int64_t v;
            if (len==4) v=va_arg(ap,int64_t);
            else if (len==3) v=va_arg(ap,long);
            else if (len==5) v=(int64_t)va_arg(ap,size_t);
            else if (len==2) v=(short)va_arg(ap,int);
            else if (len==1) v=(signed char)va_arg(ap,int);
            else v=va_arg(ap,int);
            if (v<0){ sign='-'; v=-v; }
            else if (f_plus) sign='+';
            else if (f_space) sign=' ';
            if (have_prec) f_zero=0;
            tlen=fmt_uint(tmp,sizeof tmp,(uint64_t)v,10,0);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            uint64_t v;
            if (len==4) v=va_arg(ap,uint64_t);
            else if (len==3) v=va_arg(ap,unsigned long);
            else if (len==5) v=va_arg(ap,size_t);
            else if (len==2) v=(unsigned short)va_arg(ap,unsigned);
            else if (len==1) v=(unsigned char)va_arg(ap,unsigned);
            else v=va_arg(ap,unsigned);
            int base = (conv=='o')?8:((conv=='x'||conv=='X')?16:10);
            if (have_prec) f_zero=0;
            tlen=fmt_uint(tmp,sizeof tmp,v,base,(conv=='X'));
            if (f_hash && v && base==16){ prefix=(conv=='X')?"0X":"0x"; plen=2; }
            else if (f_hash && v && base==8){ prefix="0"; plen=1; }
            break;
        }
        case 'p': {
            uint64_t v=(uint64_t)(uintptr_t)va_arg(ap,void*);
            tlen=fmt_uint(tmp,sizeof tmp,v,16,0);
            prefix="0x"; plen=2; f_zero=0;
            break;
        }
        case 's': {
            str=va_arg(ap,const char*);
            if (!str) str="(null)";
            slen=strlen(str);
            if (have_prec && prec<slen) slen=prec;
            break;
        }
        case 'c': ch=(char)va_arg(ap,int); break;
        case '%': putc_o(&o,'%'); continue;
        default: putc_o(&o,'%'); if (conv) putc_o(&o,conv); continue;
        }

        size_t zeros=0, bodylen;
        if (conv=='s') bodylen=slen;
        else if (conv=='c') bodylen=1;
        else {
            if (have_prec && prec>tlen) zeros=prec-tlen;
            bodylen=plen+(sign?1:0)+tlen+zeros;
        }
        size_t pad = width>bodylen ? width-bodylen : 0;
        int zero_pad = f_zero && !f_minus && conv!='s' && conv!='c';

        if (!f_minus && !zero_pad) pad_o(&o,' ',pad);
        if (sign) putc_o(&o,sign);
        if (plen) puts_o(&o,prefix,plen);
        if (zero_pad) pad_o(&o,'0',pad);
        for (size_t z=0;z<zeros;z++) putc_o(&o,'0');
        if (conv=='s') puts_o(&o,str,slen);
        else if (conv=='c') putc_o(&o,ch);
        else puts_o(&o,tmp,tlen);
        if (f_minus) pad_o(&o,' ',pad);
    }
    if (o.buf && o.size) o.buf[o.pos<o.size?o.pos:o.size-1]=0;
    return (int)o.pos;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int kvprintf(const char *fmt, va_list ap) {
    char buf[256];
    int n = kvsnprintf(buf, sizeof buf, fmt, ap);
    if (sink) sink(buf);
    return n;
}

int kprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = kvprintf(fmt, ap);
    va_end(ap);
    return r;
}
