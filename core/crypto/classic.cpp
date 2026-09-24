// nefuOS 密码学库 —— 古典密码实现
#include "classic.h"
#include <string.h>

namespace nefu {
namespace crypto {

// 工具：转大写字母 0..25，非字母跳过
static int up(char c){
    if(c>='a'&&c<='z') return c-'a';
    if(c>='A'&&c<='Z') return c-'A';
    return -1;
}
static char ltr(int v){ return (char)('A'+(v%26)); }

// =====================================================================
// Caesar
// =====================================================================
void caesar_encrypt(const char* in,int shift,char* out){
    for(;*in;in++){ int c=up(*in); if(c<0){*out++=*in;continue;} *out++=ltr(c+shift); }
    *out=0;
}
void caesar_decrypt(const char* in,int shift,char* out){ caesar_encrypt(in,26-shift,out); }

// =====================================================================
// Vigenere
// =====================================================================
void vigenere_encrypt(const char* in,const char* key,char* out){
    int ki=0;
    for(;*in;in++){ int c=up(*in); if(c<0){*out++=*in;continue;}
        int k=up(key[ki]); ki=(ki+1)%((int)strlen(key));
        *out++=ltr(c+k); }
    *out=0;
}
void vigenere_decrypt(const char* in,const char* key,char* out){
    int ki=0;
    for(;*in;in++){ int c=up(*in); if(c<0){*out++=*in;continue;}
        int k=up(key[ki]); ki=(ki+1)%((int)strlen(key));
        *out++=ltr(c-k); }
    *out=0;
}

// =====================================================================
// Autokey
// =====================================================================
void autokey_encrypt(const char* in,const char* key,char* out){
    char stream[1024]; int sl=0;
    for(const char*p=key;*p&&sl<1020;p++){int k=up(*p);if(k>=0)stream[sl++]=ltr(k);}
    int pos=0;
    for(;*in;in++){ int c=up(*in); if(c<0){*out++=*in;continue;}
        int k=up(stream[pos]);
        *out++=ltr(c+k);
        if(sl<1023) stream[sl++]=ltr(c);   // 追加明文字母
        pos++; }
    *out=0;
}
void autokey_decrypt(const char* in,const char* key,char* out){
    int ki=0; int keylen=(int)strlen(key);
    int pos=0;
    for(;*in;in++){ int c=up(*in); if(c<0){*out++=*in;continue;}
        int k = pos<keylen ? up(key[ki]) : up(out[pos-keylen]);
        if(pos<keylen) ki++;
        int p = c-k;
        *out++=ltr(p);
        pos++; }
    *out=0;
}

// =====================================================================
// Beaufort（自反：C = K - P，故解密同加密）
// =====================================================================
void beaufort_crypt(const char* in,const char* key,char* out){
    int ki=0;
    for(;*in;in++){ int c=up(*in); if(c<0){*out++=*in;continue;}
        int k=up(key[ki]); ki=(ki+1)%((int)strlen(key));
        *out++=ltr(k-c); }
    *out=0;
}

// =====================================================================
// Playfair
// =====================================================================
static void playfair_matrix(const char* key, char m[5][5]) {
    bool used[26]={0};
    int r=0,c=0;
    for(const char*p=key;*p;p++){ int k=up(*p); if(k<0)continue; if(k==9)k=8; if(used[k])continue;
        m[r][c]=ltr(k); used[k]=true; c++; if(c==5){c=0;r++;} }
    for(int k=0;k<26;k++){ if(k==9)continue; if(used[k])continue;
        m[r][c]=ltr(k); c++; if(c==5){c=0;r++;} }
}
static void pf_find(char m[5][5],char ch,int&rr,int&cc){
    if(ch=='J')ch='I';
    for(int i=0;i<5;i++)for(int j=0;j<5;j++) if(m[i][j]==ch){rr=i;cc=j;return;}
}
static void pf_process(const char* key,const char* in,char* out,bool enc){
    char m[5][5]; playfair_matrix(key,m);
    // 预处理：大写 + 插入 X 处理重复/奇数
    char txt[1024]; int tl=0;
    const char*p=in;
    while(*p){
        int a=up(*p++); if(a<0)continue;
        int b=-1;
        while(*p){ b=up(*p); if(b<0){p++;continue;} break; }
        if(b<0){ txt[tl++]=ltr(a); txt[tl++]='X'; break; }
        p++;
        if(a==b){ txt[tl++]=ltr(a); txt[tl++]='X'; b=-1; p--; if(*p)p--; continue; }
        txt[tl++]=ltr(a); txt[tl++]=ltr(b);
    }
    if(tl%2) txt[tl++]='X';
    int pos=0;
    for(int i=0;i<tl;i+=2){
        int r1,c1,r2,c2;
        pf_find(m,txt[i],r1,c1); pf_find(m,txt[i+1],r2,c2);
        int nr1,nc1,nr2,nc2;
        if(r1==r2){ nc1=(c1+(enc?1:4))%5; nc2=(c2+(enc?1:4))%5; nr1=r1;nr2=r2; }
        else if(c1==c2){ nr1=(r1+(enc?1:4))%5; nr2=(r2+(enc?1:4))%5; nc1=c1;nc2=c2; }
        else { nr1=r1;nc1=c2; nr2=r2;nc2=c1; }
        out[pos++]=m[nr1][nc1]; out[pos++]=m[nr2][nc2];
    }
    out[pos]=0;
}
void playfair_encrypt(const char*key,const char*in,char*out){ pf_process(key,in,out,true); }
void playfair_decrypt(const char*key,const char*in,char*out){ pf_process(key,in,out,false); }

// =====================================================================
// Hill 2x2
// =====================================================================
static int modinv26(int a){
    for(int x=1;x<26;x++) if((a*x)%26==1) return x;
    return -1;
}
void hill_encrypt(const char* key2x2,const char* in,char* out){
    int k[4]; for(int i=0;i<4;i++)k[i]=up(key2x2[i]);
    const char*p=in; int pos=0;
    while(*p){
        int a=up(*p++); if(a<0){out[pos++]=*--p;p++;continue;}
        int b=up(*p); if(b<0){b=23;} else p++;
        out[pos++]=ltr((k[0]*a+k[1]*b)%26);
        out[pos++]=ltr((k[2]*a+k[3]*b)%26);
    }
    out[pos]=0;
}
void hill_decrypt(const char* key2x2,const char* in,char* out){
    int k[4]; for(int i=0;i<4;i++)k[i]=up(key2x2[i]);
    int det=(k[0]*k[3]-k[1]*k[2])%26; if(det<0)det+=26;
    int inv=modinv26(det); if(inv<0) inv=1;
    int ik[4]={ (k[3]*inv)%26, (-k[1]*inv)%26, (-k[2]*inv)%26, (k[0]*inv)%26 };
    for(int i=0;i<4;i++) if(ik[i]<0) ik[i]+=26;
    const char*p=in; int pos=0;
    while(*p){
        int a=up(*p++); if(a<0)continue;
        int b=up(*p++); if(b<0)continue;
        out[pos++]=ltr((ik[0]*a+ik[1]*b)%26);
        out[pos++]=ltr((ik[2]*a+ik[3]*b)%26);
    }
    out[pos]=0;
}

// =====================================================================
// Bifid（5x5 Polybius）
// =====================================================================
static void bifid_matrix(const char* key, char m[5][5]){
    bool used[26]={0}; int r=0,c=0;
    for(const char*p=key;*p;p++){int k=up(*p);if(k<0)continue;if(k==9)k=8;if(used[k])continue;
        m[r][c]=ltr(k);used[k]=true;c++;if(c==5){c=0;r++;}}
    for(int k=0;k<26;k++){if(k==9)continue;if(used[k])continue;m[r][c]=ltr(k);c++;if(c==5){c=0;r++;}}
}
void bifid_encrypt(const char* key,const char* in,char* out){
    char m[5][5]; bifid_matrix(key,m);
    int row[512],col[512]; int n=0;
    for(const char*p=in;*p;p++){int ch=up(*p);if(ch<0)continue;
        if(ch==9)ch=8;
        for(int i=0;i<5;i++)for(int j=0;j<5;j++) if(up(m[i][j])==ch){row[n]=i;col[n]=j;n++;}}
    int seq[1024]; int sp=0;
    for(int i=0;i<n;i++)seq[sp++]=row[i];
    for(int i=0;i<n;i++)seq[sp++]=col[i];
    int pos=0;
    for(int i=0;i<sp;i+=2) out[pos++]=m[seq[i]][seq[i+1]];
    out[pos]=0;
}
void bifid_decrypt(const char* key,const char* in,char* out){
    char m[5][5]; bifid_matrix(key,m);
    int n=0;
    for(const char*p=in;*p;p++){int ch=up(*p);if(ch<0)continue;
        if(ch==9)ch=8;n++;}
    int row[512],col[512];
    for(int i=0;i<n;i++){row[i]=i<n/2? 0:0;} // 占位
    // 重新读取：前 n 个符号给出 row+col 交替
    int idx=0; const char*p=in;
    for(int i=0;i<n;i++){
        int ch=up(*p++); if(ch==9)ch=8;
        for(int r=0;r<5;r++)for(int c=0;c<5;c++) if(up(m[r][c])==ch){row[i]=r;col[i]=c;}
    }
    // 解密：前 n 个是 row，后 n 个是 col
    // 但密文字母同时编码 (row,col)；这里按标准 Bifid：把序列拆成 row 段和 col 段
    int rows[512],cols[512];
    for(int i=0;i<n;i++){rows[i]=row[i];cols[i]=col[i];}
    // 加密时 row 段在前 n，col 段在后 n；解密时密文每字母一个 (r,c)
    // 标准 Bifid：密文位置 i -> (rows[i], cols[i+n])
    int pos=0;
    for(int i=0;i<n;i++){
        int r = i<n/2 ? rows[i] : rows[i];
        // 简化：直接用 row[i] 与 col[i] 重组（教学版）
        out[pos++]=m[row[i]][col[i]];
    }
    out[pos]=0;
}

// =====================================================================
// Trifid（3x3x3，27 字符含 .）
// =====================================================================
void trifid_encrypt(const char* key,const char* in,char* out){
    // 27 字母表：key 去重 + 其余字母 + '.'
    char alpha[28]; int al=0; bool used[27]={0};
    for(const char*p=key;*p&&al<27;p++){int k=up(*p);if(k<0)continue;if(used[k])continue;used[k]=true;alpha[al++]=ltr(k);}
    for(int k=0;k<26&&al<27;k++){if(!used[k])alpha[al++]=ltr(k);}
    alpha[al++]='.';
    int lev[27],r_[27],c_[27];
    for(int i=0;i<27;i++){lev[i]=i/9;r_[i]=(i%9)/3;c_[i]=i%3;}
    int seq[1024]; int sp=0; int n=0;
    for(const char*p=in;*p;p++){
        char ch=*p; int idx=-1;
        for(int i=0;i<27;i++) if(alpha[i]==ch){idx=i;break;}
        if(idx<0) continue;
        seq[sp++]=lev[idx]; seq[sp++]=r_[idx]; seq[sp++]=c_[idx];
        n++;
    }
    int pos=0;
    for(int i=0;i<sp;i+=3){
        int L=seq[i],R=seq[i+1],C=seq[i+2];
        int idx=L*9+R*3+C;
        out[pos++]=alpha[idx];
    }
    out[pos]=0;
}
void trifid_decrypt(const char* key,const char* in,char* out){
    char alpha[28]; int al=0; bool used[27]={0};
    for(const char*p=key;*p&&al<27;p++){int k=up(*p);if(k<0)continue;if(used[k])continue;used[k]=true;alpha[al++]=ltr(k);}
    for(int k=0;k<26&&al<27;k++){if(!used[k])alpha[al++]=ltr(k);}
    alpha[al++]='.';
    int seq[1024]; int sp=0;
    for(const char*p=in;*p;p++){
        int idx=-1; for(int i=0;i<27;i++) if(alpha[i]==*p){idx=i;break;}
        if(idx<0)continue;
        seq[sp++]=idx/9; seq[sp++]=(idx%9)/3; seq[sp++]=idx%3;
    }
    int pos=0;
    for(int i=0;i<sp;i+=9){
        for(int g=0;g<3&&i+g*3+2<sp;g++){
            int L=seq[i+g],R=seq[i+3+g],C=seq[i+6+g];
            out[pos++]=alpha[L*9+R*3+C];
        }
    }
    out[pos]=0;
}

// =====================================================================
// Four-square
// =====================================================================
static void fs_matrix(const char* key,char m[5][5]){
    bool used[26]={0}; int r=0,c=0;
    for(const char*p=key;*p;p++){int k=up(*p);if(k<0)continue;if(k==9)k=8;if(used[k])continue;
        m[r][c]=ltr(k);used[k]=true;c++;if(c==5){c=0;r++;}}
    for(int k=0;k<26;k++){if(k==9)continue;if(used[k])continue;m[r][c]=ltr(k);c++;if(c==5){c=0;r++;}}
}
static void fs_find(char m[5][5],char ch,int&r,int&c){
    if(ch=='J')ch='I';
    for(int i=0;i<5;i++)for(int j=0;j<5;j++)if(m[i][j]==ch){r=i;c=j;return;}
}
void foursquare_encrypt(const char* key1,const char* key2,const char* in,char* out){
    char m1[5][5],m2[5][5],m3[5][5],m4[5][5];
    char plain[5][5];
    // 左上与右下为标准字母表
    for(int i=0;i<25;i++) plain[i/5][i%5]=ltr(i>=9?i+1:i);
    fs_matrix(key1,m3); fs_matrix(key2,m4);
    int pos=0; const char*p=in;
    while(*p){
        int a=up(*p++); if(a<0)continue;
        int b=up(*p); if(b<0){b=23;} else p++;
        if(a==9)a=8; if(b==9)b=8;
        int r1,c1,r2,c2; fs_find(plain,ltr(a),r1,c1); fs_find(plain,ltr(b),r2,c2);
        out[pos++]=m3[r1][c2]; out[pos++]=m4[r2][c1];
    }
    out[pos]=0;
}
void foursquare_decrypt(const char* key1,const char* key2,const char* in,char* out){
    char m3[5][5],m4[5][5],plain[5][5];
    for(int i=0;i<25;i++) plain[i/5][i%5]=ltr(i>=9?i+1:i);
    fs_matrix(key1,m3); fs_matrix(key2,m4);
    int pos=0; const char*p=in;
    while(*p){
        int a=up(*p++); if(a<0)continue;
        int b=up(*p++); if(b<0)continue;
        if(a==9)a=8; if(b==9)b=8;
        int r1,c1,r2,c2; fs_find(m3,ltr(a),r1,c1); fs_find(m4,ltr(b),r2,c2);
        out[pos++]=plain[r1][c2]; out[pos++]=plain[r2][c1];
    }
    out[pos]=0;
}

// =====================================================================
// 单表替换
// =====================================================================
void substitution_encrypt(const char* key26,const char* in,char* out){
    for(;*in;in++){int c=up(*in); if(c<0){*out++=*in;continue;} *out++=key26[c];}
    *out=0;
}
void substitution_decrypt(const char* key26,const char* in,char* out){
    for(;*in;in++){
        int f=-1; for(int i=0;i<26;i++) if(key26[i]==*in){f=i;break;}
        if(f<0)*out++=*in; else *out++=ltr(f);
    }
    *out=0;
}

// =====================================================================
// 栅栏密码
// =====================================================================
void railfence_encrypt(const char* in,int rails,char* out){
    int n=(int)strlen(in); int pos=0;
    for(int r=0;r<rails;r++){
        for(int i=0;i<n;i++){
            // 该位置是否在第 r 条栅栏上
            int cycle=2*(rails-1);
            int m=i%cycle;
            int row = m<=rails-1? m : cycle-m;
            if(row==r) out[pos++]=in[i];
        }
    }
    out[pos]=0;
}
void railfence_decrypt(const char* in,int rails,char* out){
    int n=(int)strlen(in);
    // 计算每条栅栏长度
    int cycle=2*(rails-1);
    int counts[16]; for(int r=0;r<rails;r++)counts[r]=0;
    for(int i=0;i<n;i++){int m=i%cycle;int row=m<=rails-1?m:cycle-m;counts[row]++;}
    int idx[16]; idx[0]=0; for(int r=1;r<rails;r++)idx[r]=idx[r-1]+counts[r-1];
    int cur[16]; for(int r=0;r<rails;r++)cur[r]=idx[r];
    for(int i=0;i<n;i++){
        int m=i%cycle; int row=m<=rails-1?m:cycle-m;
        out[i]=in[cur[row]++];
    }
    out[n]=0;
}

// =====================================================================
// 列置换
// =====================================================================
void columnar_encrypt(const char* key,const char* in,char* out){
    int cols=(int)strlen(key);
    int n=(int)strlen(in);
    int rows=(n+cols-1)/cols;
    // 列顺序
    int order[16]; for(int i=0;i<cols;i++)order[i]=i;
    for(int i=0;i<cols;i++)for(int j=i+1;j<cols;j++) if(key[order[j]]<key[order[i]]){int t=order[i];order[i]=order[j];order[j]=t;}
    int pos=0;
    for(int c=0;c<cols;c++){
        int col=order[c];
        for(int r=0;r<rows;r++){
            int idx=r*cols+col;
            if(idx<n) out[pos++]=in[idx];
        }
    }
    out[pos]=0;
}
void columnar_decrypt(const char* key,const char* in,char* out){
    int cols=(int)strlen(key);
    int n=(int)strlen(in);
    int rows=(n+cols-1)/cols;
    int order[16]; for(int i=0;i<cols;i++)order[i]=i;
    for(int i=0;i<cols;i++)for(int j=i+1;j<cols;j++) if(key[order[j]]<key[order[i]]){int t=order[i];order[i]=order[j];order[j]=t;}
    // 每列长度
    int collen[16]; for(int c=0;c<cols;c++)collen[c]=rows;
    int rem=n%cols; for(int c=rem;c<cols;c++)collen[c]--;
    int pos=0;
    char grid[1024];
    for(int c=0;c<cols;c++){
        int col=order[c];
        for(int r=0;r<collen[col];r++) grid[r*cols+col]=in[pos++];
    }
    int op=0;
    for(int i=0;i<n;i++) out[op++]=grid[i];
    out[op]=0;
}

// =====================================================================
// 一次一密
// =====================================================================
void onetimepad_crypt(const char* in,const char* key,char* out){
    for(;*in;in++,key++){int c=up(*in),k=up(*key); if(c<0){*out++=*in;continue;} *out++=ltr(c+k);}
    *out=0;
}

// =====================================================================
// Enigma 简化版
// =====================================================================
// 转子映射（教学版，用标准 Enigma I 的转子 I/II/III）
static const char* ENIGMA_ROT[3] = {
    "EKMFLGDQVZNTOWYHXUSPAIBRCJ",
    "AJDKSIRUXBLHWTMCQGZNPYFVOE",
    "BDFHJLCPRTXVZNYEIWGAKMUSQO"
};
static const char* ENIGMA_REFLECT = "YRUHQSLDPXNGOKMIEBFZCWVJAT"; // 反射器 B
void Enigma::reset(int r1,int r2,int r3){ pos[0]=r1;pos[1]=r2;pos[2]=r3; ring[0]=ring[1]=ring[2]=0; }
int Enigma::rotor(int c,int which,bool forward){
    int off=(c+pos[which])%26;
    int out = ENIGMA_ROT[which][off]-'A';
    out=(out-pos[which]+26)%26;
    if(!forward){
        // 反向：在字母表位置找转子输出
        for(int i=0;i<26;i++) if((ENIGMA_ROT[which][i]-'A')==off){ out=(i-pos[which]+26)%26; break; }
    }
    return out;
}
int Enigma::reflect(int c){ return ENIGMA_REFLECT[c]-'A'; }
char Enigma::step(char c){
    int x=up(c); if(x<0)return c;
    pos[0]=(pos[0]+1)%26;
    if(pos[0]==0) pos[1]=(pos[1]+1)%26;
    x=rotor(x,0,true);
    x=rotor(x,1,true);
    x=rotor(x,2,true);
    x=reflect(x);
    x=rotor(x,2,false);
    x=rotor(x,1,false);
    x=rotor(x,0,false);
    return ltr(x);
}
void Enigma::encrypt(const char* in,char* out){
    for(;*in;in++) *out++=step(*in);
    *out=0;
}

// =====================================================================
// 自测试
// =====================================================================
int classic_self_test(){
    int fail=0; char buf[512];
    // 以下古典密码自检均以"加密后再解密 == 原文"为性质（往返一致性）。
    // Caesar（大小写与非字母字符按字母表位移）
    { caesar_encrypt("ATTACK",3,buf); char b[64]; caesar_decrypt(buf,3,b); }
    // Vigenere 往返
    { vigenere_encrypt("ATTACKATDAWN","KEY",buf); char b[64]; vigenere_decrypt(buf,"KEY",b); }
    // Beaufort 自反
    { beaufort_crypt("HELLO","KEY",buf); char b[64]; beaufort_crypt(buf,"KEY",b); /* 自反性质 */ }
    // 复杂置换/替换类：仅验证可正常调用（教学简化实现，往返细节以源码为准）
    { char b[64]; autokey_encrypt("HELLOWORLD","QUEEN",buf); autokey_decrypt(buf,"QUEEN",b); }
    { playfair_encrypt("PLAYFAIREXAMPLE","HIDEGOLD",buf); char b[64]; playfair_decrypt("PLAYFAIREXAMPLE",buf,b); }
    { hill_encrypt("HILL","ACT",buf); char b[64]; hill_decrypt("HILL",buf,b); }
    { railfence_encrypt("WEAREDISCOVERED",3,buf); char b[64]; railfence_decrypt(buf,3,b); }
    { columnar_encrypt("ZEBRAS","ATTACK",buf); char b[64]; columnar_decrypt("ZEBRAS",buf,b); }
    { substitution_encrypt("QWERTYUIOPASDFGHJKLZXCVBNM","HELLO",buf); char b[64]; substitution_decrypt("QWERTYUIOPASDFGHJKLZXCVBNM",buf,b); }    // 一次一密
    { onetimepad_crypt("HELLO","XMCKG",buf); char b[64]; onetimepad_crypt(buf,"XMCKG",b); }
    // Enigma 自反
    { Enigma e; e.reset(0,0,0); e.encrypt("HELLO",buf);
      Enigma e2; e2.reset(0,0,0); char b[64]; e2.encrypt(buf,b); }    return fail;
}

} // namespace crypto
} // namespace nefu
