// nefuOS 密码学库 —— 编码/解码实现
#include "codec.h"
#include <string.h>

namespace nefu {
namespace crypto {

// =====================================================================
// Base32 (RFC 4648)
// =====================================================================
static const char* B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
int base32_encode(const uint8_t* in, int len, char* out, int outcap) {
    int pos=0, buf=0, bits=0;
    for (int i=0;i<len;i++) {
        buf=(buf<<8)|in[i]; bits+=8;
        while (bits>=5) { bits-=5; if(pos<outcap-1) out[pos++]=B32[(buf>>bits)&31]; }
    }
    if (bits>0) { if(pos<outcap-1) out[pos++]=B32[(buf<<(5-bits))&31]; }
    // 补 '=' 到 8 的倍数
    while (pos%8!=0) { if(pos<outcap-1) out[pos++]='='; }
    out[pos]=0;
    return pos;
}
int base32_decode(const char* in, uint8_t* out, int outcap) {
    int pos=0, buf=0, bits=0;
    for (const char* p=in; *p; p++) {
        char c=*p; if (c=='=') break;
        int v=-1;
        if (c>='A'&&c<='Z') v=c-'A';
        else if (c>='2'&&c<='7') v=c-'2'+26;
        else continue;
        buf=(buf<<5)|v; bits+=5;
        if (bits>=8) { bits-=8; if(pos<outcap) out[pos++]=(uint8_t)((buf>>bits)&0xFF); }
    }
    return pos;
}

// =====================================================================
// Base36 / Base62（通用除基转换，处理前导零）
// =====================================================================
static int base_conv_encode(const uint8_t* in, int len, const char* alpha, int base,
                            char* out, int outcap) {
    // 复制输入为"大整数"字节数组，反复除以 base
    uint8_t* num = new uint8_t[len];
    for (int i=0;i<len;i++) num[i]=in[i];
    int n=len;
    char tmp[1024]; int tp=0;
    while (n>0) {
        uint32_t rem=0;
        for (int i=0;i<n;i++) {
            uint32_t cur=rem*256+num[i];
            num[i]=(uint8_t)(cur/base);
            rem=cur%base;
        }
        while (n>0 && num[0]==0) { for (int i=0;i<n-1;i++) num[i]=num[i+1]; n--; }
        tmp[tp++]=alpha[rem];
    }
    // 前导零字节
    for (int i=0;i<len && in[i]==0;i++) { if(tp<1024) tmp[tp++]=alpha[0]; }
    // 反序
    int pos=0;
    for (int i=tp-1;i>=0;i--) { if(pos<outcap-1) out[pos++]=tmp[i]; }
    out[pos]=0;
    delete[] num;
    return pos;
}
static int base_conv_decode(const char* in, const char* alpha, int base,
                            uint8_t* out, int outcap) {
    uint8_t num[512]; int n=0;
    for (const char* p=in; *p; p++) {
        int v=-1;
        for (int i=0;alpha[i];i++) if (alpha[i]==*p){v=i;break;}
        if (v<0) continue;
        // num = num*base + v
        uint32_t carry=v;
        for (int i=0;i<n;i++) {
            uint32_t cur=num[i]*base+carry;
            num[i]=(uint8_t)(cur&0xFF);
            carry=cur>>8;
        }
        while (carry) { if(n<512){num[n++]=(uint8_t)(carry&0xFF);carry>>=8;} else break; }
    }
    // num 是小端，反序写到 out
    int pos=0;
    for (int i=n-1;i>=0;i--) { if(pos<outcap) out[pos++]=num[i]; }
    return pos;
}
static const char* ALPH36="0123456789abcdefghijklmnopqrstuvwxyz";
static const char* ALPH62="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
int base36_encode(const uint8_t* in,int len,char* out,int outcap){ return base_conv_encode(in,len,ALPH36,36,out,outcap); }
int base36_decode(const char* in,uint8_t* out,int outcap){ return base_conv_decode(in,ALPH36,36,out,outcap); }
int base62_encode(const uint8_t* in,int len,char* out,int outcap){ return base_conv_encode(in,len,ALPH62,62,out,outcap); }
int base62_decode(const char* in,uint8_t* out,int outcap){ return base_conv_decode(in,ALPH62,62,out,outcap); }

// =====================================================================
// Base58（Bitcoin 字母表）
// =====================================================================
static const char* B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
int base58_encode(const uint8_t* in, int len, char* out, int outcap) {
    return base_conv_encode(in,len,B58,58,out,outcap);
}
int base58_decode(const char* in, uint8_t* out, int outcap) {
    return base_conv_decode(in,B58,58,out,outcap);
}

// =====================================================================
// Base85 / Ascii85
// =====================================================================
int base85_ascii85_encode(const uint8_t* in, int len, char* out, int outcap) {
    int pos=0;
    if(pos<outcap-2){out[pos++]='<';out[pos++]='~';}
    int i=0;
    while (i<len) {
        uint32_t chunk=0; int grp=0;
        for (int b=0;b<4;b++){ chunk<<=8; if(i+b<len){chunk|=in[i+b];grp++;} }
        if (grp==4 && chunk==0) { if(pos<outcap-1) out[pos++]='z'; i+=4; continue; }
        char tmp[6]; int tp=0;
        uint32_t v=chunk;
        for (int b=0;b<5;b++){ tmp[tp++]=(char)(v%85+33); v/=85; }
        for (int b=tp-1;b>=0;b--){ if(pos<outcap-1) out[pos++]=tmp[b]; }
        i+=4;
    }
    if(pos<outcap-2){out[pos++]='~';out[pos++]='>';}
    out[pos]=0;
    return pos;
}
int base85_ascii85_decode(const char* in, uint8_t* out, int outcap) {
    // 跳过 <~ 和 ~>
    const char* p=in;
    if (p[0]=='<'&&p[1]=='~') p+=2;
    int pos=0; uint32_t val=0; int cnt=0;
    for (; *p; p++) {
        if (*p=='~'&&p[1]=='>') break;
        if (*p=='z' && cnt==0) {
            for (int b=0;b<4;b++) if(pos<outcap) out[pos++]=0;
            continue;
        }
        if (*p<33||*p>117) continue;
        val=val*85+(*p-33); cnt++;
        if (cnt==5) {
            for (int b=0;b<4;b++) if(pos<outcap) out[pos++]=(uint8_t)(val>>(8*(3-b)));
            val=0; cnt=0;
        }
    }
    if (cnt>0) {
        for (int b=0;b<cnt-1;b++) if(pos<outcap) out[pos++]=(uint8_t)(val>>(8*(3-b)));
    }
    return pos;
}
// Z85（ZeroMQ，要求输入为 4 字节分组）
static const char* Z85A = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";
int z85_encode(const uint32_t* in_groups, int ngroups, char* out, int outcap) {
    int pos=0;
    for (int i=0;i<ngroups;i++) {
        uint32_t v=in_groups[i];
        char tmp[5];
        for (int b=0;b<5;b++){ tmp[b]=Z85A[v%85]; v/=85; }
        for (int b=4;b>=0;b--){ if(pos<outcap-1) out[pos++]=tmp[b]; }
    }
    out[pos]=0;
    return pos;
}

// =====================================================================
// Base91
// =====================================================================
static const char* B91 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!#$%&()*+,./:;<=>?@[]^_\"{|}~'";
int base91_encode(const uint8_t* in, int len, char* out, int outcap) {
    int pos=0; uint32_t b=0; int n=0;
    for (int i=0;i<len;i++) {
        b|=((uint32_t)in[i])<<n; n+=8;
        if (n>13) {
            uint32_t v=b&8191;
            if (v>88) { b>>=13; n-=13; }
            else { v=b&16383; b>>=14; n-=14; }
            if(pos<outcap-1) out[pos++]=B91[v%91];
            if(pos<outcap-1) out[pos++]=B91[v/91];
        }
    }
    if (n>0) {
        if(pos<outcap-1) out[pos++]=B91[b%91];
        if (n>8 || b>90) { if(pos<outcap-1) out[pos++]=B91[b/91]; }
    }
    out[pos]=0;
    return pos;
}
int base91_decode(const char* in, uint8_t* out, int outcap) {
    int pos=0; uint32_t b=0; int n=0; uint32_t v=0; bool have=false;
    for (const char* p=in; *p; p++) {
        int d=-1;
        for (int i=0;B91[i];i++) if(B91[i]==*p){d=i;break;}
        if (d<0) continue;
        if (!have) { v=d; have=true; continue; }
        v+=d*91;
        b|=v<<n; n+= (v&8191)>88 ? 13 : 14;
        while (n>=8) { if(pos<outcap) out[pos++]=(uint8_t)(b&0xFF); b>>=8; n-=8; }
        have=false;
    }
    if (have) { if(pos<outcap) out[pos++]=(uint8_t)((b|(v<<n))&0xFF); }
    return pos;
}

// =====================================================================
// hex
// =====================================================================
void hex_encode(const uint8_t* in, int len, char* out) {
    static const char* h="0123456789abcdef";
    for (int i=0;i<len;i++){out[2*i]=h[in[i]>>4];out[2*i+1]=h[in[i]&15];}
    out[2*len]=0;
}
static int hexval(char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
int hex_decode(const char* in, uint8_t* out, int outcap) {
    int pos=0;
    for (int i=0; in[i] && in[i+1]; i+=2) {
        int hi=hexval(in[i]), lo=hexval(in[i+1]);
        if (hi<0||lo<0) break;
        if(pos<outcap) out[pos++]=(uint8_t)((hi<<4)|lo);
    }
    return pos;
}

// =====================================================================
// ROT13 / ROT47
// =====================================================================
void rot13(const char* in, char* out) {
    for (; *in; in++) {
        char c=*in;
        if (c>='a'&&c<='z') c=(c-'a'+13)%26+'a';
        else if (c>='A'&&c<='Z') c=(c-'A'+13)%26+'A';
        *out++=c;
    }
    *out=0;
}
void rot47(const char* in, char* out) {
    for (; *in; in++) {
        char c=*in;
        if (c>=33&&c<=126) c=(char)(33+((c-33+47)%94));
        *out++=c;
    }
    *out=0;
}

// =====================================================================
// URL 编码
// =====================================================================
static inline bool unreserved(char c){
    return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||c=='_'||c=='.'||c=='~';
}
void url_encode(const char* in, char* out, int outcap) {
    static const char* h="0123456789ABCDEF";
    int pos=0;
    for (; *in && pos<outcap-4; in++) {
        if (unreserved(*in)) out[pos++]=*in;
        else {
            out[pos++]='%';
            out[pos++]=h[((uint8_t)*in)>>4];
            out[pos++]=h[((uint8_t)*in)&15];
        }
    }
    out[pos]=0;
}
void url_decode(const char* in, char* out, int outcap) {
    int pos=0;
    for (; *in && pos<outcap-1; in++) {
        if (*in=='%' && in[1] && in[2]) {
            int hi=hexval(in[1]), lo=hexval(in[2]);
            if (hi>=0&&lo>=0) { out[pos++]=(char)((hi<<4)|lo); in+=2; continue; }
        }
        out[pos++]=*in;
    }
    out[pos]=0;
}

// =====================================================================
// quoted-printable
// =====================================================================
void qp_encode(const char* in, char* out, int outcap) {
    static const char* h="0123456789ABCDEF";
    int pos=0;
    for (; *in && pos<outcap-4; in++) {
        uint8_t c=(uint8_t)*in;
        if (c==9 || (c>=32&&c<=126&&c!='=')) out[pos++]=*in;
        else { out[pos++]='='; out[pos++]=h[c>>4]; out[pos++]=h[c&15]; }
    }
    out[pos]=0;
}
void qp_decode(const char* in, char* out, int outcap) {
    int pos=0;
    for (; *in && pos<outcap-1; in++) {
        if (*in=='=' && in[1] && in[2] && in[1]!='\r' && in[1]!='\n') {
            int hi=hexval(in[1]), lo=hexval(in[2]);
            if (hi>=0&&lo>=0) { out[pos++]=(char)((hi<<4)|lo); in+=2; continue; }
        }
        if (*in=='=' && (in[1]=='\r'||in[1]=='\n')) continue;  // 软换行
        out[pos++]=*in;
    }
    out[pos]=0;
}

// =====================================================================
// 自测试
// =====================================================================
int codec_self_test() {
    int fail=0;
    char buf[512]; uint8_t dbuf[256];
    // Base58("Hello World") = JxF12TrwUP45BMd
    {
        base58_encode((const uint8_t*)"Hello World",11,buf,sizeof(buf));
        if (strcmp(buf,"JxF12TrwUP45BMd")!=0) fail++;
        int n=base58_decode(buf,dbuf,sizeof(dbuf));
        if (n!=11 || memcmp(dbuf,"Hello World",11)!=0) fail++;
    }
    // Base32("foobar") 已知向量
    {
        base32_encode((const uint8_t*)"foobar",6,buf,sizeof(buf));
        if (strcmp(buf,"MZXW6YTBOI======")!=0) fail++;
        int n=base32_decode(buf,dbuf,sizeof(dbuf));
        if (n!=6 || memcmp(dbuf,"foobar",6)!=0) fail++;
    }
    // hex 往返
    {
        hex_encode((const uint8_t*)"\x01\x23\xAB",3,buf);
        if (strcmp(buf,"0123ab")!=0) fail++;
        int n=hex_decode(buf,dbuf,sizeof(dbuf));
        if (n!=3 || dbuf[0]!=1 || dbuf[1]!=0x23 || dbuf[2]!=0xAB) fail++;
    }
    // ROT13 自反
    {
        rot13("Hello, World!",buf);
        char back[64]; rot13(buf,back);
        if (strcmp(back,"Hello, World!")!=0) fail++;
    }
    // ROT47 自反
    {
        rot47("Test! 123",buf);
        char back[64]; rot47(buf,back);
        if (strcmp(back,"Test! 123")!=0) fail++;
    }
    // URL 往返
    {
        url_encode("a b&c=?",buf,sizeof(buf));
        char back[64]; url_decode(buf,back,sizeof(back));
        if (strcmp(back,"a b&c=?")!=0) fail++;
    }
    // Base62 往返
    {
        base62_encode((const uint8_t*)"Hello",5,buf,sizeof(buf));
        int n=base62_decode(buf,dbuf,sizeof(dbuf));
        if (n!=5 || memcmp(dbuf,"Hello",5)!=0) fail++;
    }
    // Base91 往返
    {
        const char* msg="nefuOS base91 round trip test 12345";
        base91_encode((const uint8_t*)msg,(int)strlen(msg),buf,sizeof(buf));
        int n=base91_decode(buf,dbuf,sizeof(dbuf));
        if (n!=(int)strlen(msg) || memcmp(dbuf,msg,n)!=0) fail++;
    }
    // Ascii85 往返
    {
        const char* msg="Man is distinguished";
        base85_ascii85_encode((const uint8_t*)msg,(int)strlen(msg),buf,sizeof(buf));
        int n=base85_ascii85_decode(buf,dbuf,sizeof(dbuf));
        if (n!=(int)strlen(msg) || memcmp(dbuf,msg,n)!=0) fail++;
    }
    // QP 往返
    {
        qp_encode("a=b c",buf,sizeof(buf));
        char back[64]; qp_decode(buf,back,sizeof(back));
        if (strcmp(back,"a=b c")!=0) fail++;
    }
    return fail;
}

} // namespace crypto
} // namespace nefu
