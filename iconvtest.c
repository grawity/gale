#include <sys/types.h>
#include <stdlib.h>
#include <iconv.h>
#include <netinet/in.h>

wchar_t convert(wchar_t ch) {
    return (4 == sizeof(wchar_t)) ? htonl(ch) : htons(ch);
}

int main() {
    wchar_t wch[3];
    unsigned char utf[7];
    const char *encoding = (4 == sizeof(wchar_t)) ? "UCS-4" : "UCS-2";
    iconv_t from = iconv_open(encoding,"UTF-8");
    iconv_t to = iconv_open("UTF-8",encoding);
    const char *inbuf = (char *) wch;
    size_t inbytes = sizeof(wch);
    char *outbuf = (char *) utf;
    size_t outbytes = sizeof(utf);
    wch[0] = convert(0x007f);
    wch[1] = convert(0x07ff);
    wch[2] = convert(0xffff);
    if ((4 != sizeof(wchar_t) && 2 != sizeof(wchar_t))
    ||  from == (iconv_t) -1 || to == (iconv_t) -1
    ||  0 != iconv(to,&inbuf,&inbytes,&outbuf,&outbytes)
    ||  0 != inbytes || (sizeof(utf) - 6) != outbytes
    ||  utf[0] != 0x7f 
    ||  utf[1] != 0xdf || utf[2] != 0xbf
    ||  utf[3] != 0xef || utf[4] != 0xbf || utf[5] != 0xbf) exit(1);
    inbuf = utf; inbytes = 6;
    outbuf = (char *) wch; outbytes = sizeof(wch);
    if (0 != iconv(from,&inbuf,&inbytes,&outbuf,&outbytes)
    ||  0 != inbytes || (sizeof(wch) - 3*sizeof(wchar_t)) != outbytes
    ||  wch[0] != convert(0x007f) 
    ||  wch[1] != convert(0x07ff) 
    ||  wch[2] != convert(0xffff)) exit(1);
    exit(0);
}  
