// 完整对比：encode 写的码 vs decode 读的码
#include <cstdio>
#include <cstring>
#include "complib/lzw.h"

// 手动拷贝 encode/decode 逻辑做对比（仅诊断）
struct Entry { int prefix; int ch; };
struct D { Entry e[4096]; int n;
  void clear(){ n=258; for(int i=0;i<256;i++){e[i].prefix=-1;e[i].ch=i;} }
  int find(int p,int c){ for(int i=258;i<n;i++) if(e[i].prefix==p&&e[i].ch==c) return i; return -1; }
  void add(int p,int c){ if(n<4096){ e[n].prefix=p; e[n].ch=c; n++; } }
};

// 简化 BitWriter/BitReader（LSB-first 复刻）
struct BW { unsigned char* b; int cap, pos, bit, cur;
  BW(unsigned char* o,int c):b(o),cap(c),pos(0),bit(0),cur(0){}
  void wb(int v){ if(v) cur|=(1<<bit); bit++; if(bit==8){ if(pos<cap) b[pos]=(unsigned char)cur; pos++; cur=0; bit=0; } }
  void wbits(int v,int n){ for(int i=0;i<n;i++) wb((v>>i)&1); }
  int finish(){ if(bit>0){ if(pos<cap) b[pos]=(unsigned char)cur; pos++; } return pos; }
};
struct BR { const unsigned char* b; int nbytes, pos, bit, curv, cur;
  BR(const unsigned char* x,int n):b(x),nbytes(n),pos(0),bit(0),curv(0),cur(0){}
  int rb(){ if(curv==0){ if(pos>=nbytes) return 0; cur=b[pos]; curv=8; pos++; } int v=(cur>>bit)&1; bit++; curv--; if(curv==0) bit=0; return v; }
  int rbits(int n){ int v=0; for(int i=0;i<n;i++) v|=rb()<<i; return v; }
};

int main() {
    unsigned char in[500];
    for (int i = 0; i < 500; i++) in[i] = (unsigned char)((i * 7 + 1) % 200);
    // encode 追踪
    D ed; ed.clear();
    int encodes[2000], ec = 0, ecb[2000], en[2000];
    BW bw(0,0); // 不真写，仅追踪码
    int prefix = in[0], codebits = 9;
    for (int i = 1; i < 500; i++) {
        int c = in[i];
        int idx = ed.find(prefix, c);
        if (idx >= 0) prefix = idx;
        else {
            encodes[ec] = prefix; ecb[ec] = codebits; en[ec] = ed.n; ec++;
            ed.add(prefix, c);
            int next = ed.n;
            if (next > (1 << codebits) - 1 && codebits < 12) codebits++;
            prefix = c;
            if (ed.n >= 4095) { ed.clear(); codebits = 9; prefix = c; }
        }
    }
    encodes[ec] = prefix; ecb[ec] = codebits; en[ec] = ed.n; ec++;
    printf("encode codes=%d\n", ec);
    // decode 追踪（用真实字节流）
    unsigned char enc[8192];
    int el = nefu::comp::lzw_encode(in, 500, enc, sizeof(enc));
    D dd; dd.clear();
    BR br(enc, el);
    int decodes[2000], dcb[2000], dn[2000], dc = 0;
    int prev = -1, cbits = 9;
    for (;;) {
        if (br.pos >= el && br.curv == 0 && dc > 500) break;
        int code = br.rbits(cbits);
        decodes[dc] = code; dcb[dc] = cbits; dn[dc] = dd.n; dc++;
        if (code == 257) break;
        if (dc > 2000) break;
        if (code == 256) { dd.clear(); cbits = 9; prev = -1; continue; }
        bool special = (code >= dd.n);
        int start = special ? prev : code;
        int st = 0;
        unsigned char stack[4096];
        int walk = start;
        while (walk >= 0 && st < 4096) { stack[st++] = (unsigned char)dd.e[walk].ch; walk = dd.e[walk].prefix; }
        if (special) stack[st++] = stack[st - 1];
        if (prev >= 0 && dd.n < 4096) dd.add(prev, stack[st - 1]);
        prev = code;
        int next = dd.n;
        if (next > (1 << cbits) - 1 && cbits < 12) cbits++;
    }
    printf("decode codes=%d\n", dc);
    // 找第一个不同步
    for (int k = 0; k < ec && k < dc; k++) {
        if (encodes[k] != decodes[k] || ecb[k] != dcb[k]) {
            printf("first diff at code[%d]: enc=%d(bits%d,n%d) dec=%d(bits%d,n%d)\n",
                   k, encodes[k], ecb[k], en[k], decodes[k], dcb[k], dn[k]);
            for (int j = k - 3; j <= k + 2; j++) {
                if (j >= 0) printf("  [%d] enc=%d(b%d,n%d) dec=%d(b%d,n%d)\n",
                    j, j<ec?encodes[j]:-99, j<ec?ecb[j]:-99, j<ec?en[j]:-99,
                    j<dc?decodes[j]:-99, j<dc?dcb[j]:-99, j<dc?dn[j]:-99);
            }
            return 0;
        }
    }
    printf("all match up to %d\n", ec < dc ? ec : dc);
    return 0;
}
