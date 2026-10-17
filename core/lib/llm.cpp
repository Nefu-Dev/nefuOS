// nefuLLM -- freestanding int8 GPT-Neo inference engine (Q16.16, no FPU/no libc)
// ---------------------------------------------------------------------------
// Integer-only transformer inference for the NEFL blob produced by
// convert_model.py (GPT-Neo: LayerNorm / gelu_new / causal attention /
// learned positional embedding / tied lm_head, weights int8 per-row quant).
// Activations are Q16.16 (int32, value = x>>16); weights stay int8 in the blob,
// dequantized on the fly with a per-row Q16.16 scale.
#include "llm.h"
#include "../lib/softmath.h"

// --- freestanding memory helpers (no libc) ---
static void lmemset(void* dst, int v, size_t n){ unsigned char* p=(unsigned char*)dst; for(size_t i=0;i<n;i++) p[i]=(unsigned char)v; }
static void lmemcpy(void* dst, const void* src, size_t n){ const unsigned char* s=(const unsigned char*)src; unsigned char* d=(unsigned char*)dst; for(size_t i=0;i<n;i++) d[i]=s[i]; }
static int  lmemcmp(const void* a, const void* b, size_t n){ const unsigned char* x=(const unsigned char*)a,*y=(const unsigned char*)b; for(size_t i=0;i<n;i++){ if(x[i]!=y[i]) return x[i]-y[i]; } return 0; }

namespace nefu {
namespace llm {

using fx::fix;
using fx::fix64;
using fx::FX_ONE;
using fx::FX_HALF;
using fx::itofix;
using fx::fx_mul;
using fx::fx_div;
using fx::fx_sqrt;
using fx::fx_exp;
using fx::fx_ln;

// ------------------------------------------------------------------ blob I/O
struct Reader {
    const uint8_t* d; size_t size; size_t off; bool ok;
    Reader(const uint8_t* b, size_t n) : d(b), size(n), off(0), ok(true) {}
    uint32_t u32() {
        if (off + 4 > size) { ok = false; return 0; }
        uint32_t v = (uint32_t)d[off] | ((uint32_t)d[off+1] << 8) |
                     ((uint32_t)d[off+2] << 16) | ((uint32_t)d[off+3] << 24);
        off += 4; return v;
    }
    int32_t i32() { return (int32_t)u32(); }
    uint16_t u16() {
        if (off + 2 > size) { ok = false; return 0; }
        uint16_t v = (uint16_t)((uint16_t)d[off] | ((uint16_t)d[off+1] << 8));
        off += 2; return v;
    }
    const uint8_t* bytes(size_t n) {
        if (off + n > size) { ok = false; off = size; return d + size; }
        const uint8_t* p = d + off; off += n; return p;
    }
};

struct Bump {
    uint8_t* base; size_t used; size_t cap;
    Bump(void* b, size_t c) : base((uint8_t*)b), used(0), cap(c) {}
    void* alloc(size_t n, size_t align) {
        size_t u = (used + align - 1) & ~(align - 1);
        if (u + n > cap) { return 0; }
        used = u + n; return base + u;
    }
};

struct MatRef { const int8_t* q; uint32_t R, C; const fix* s; };
struct VecRef { const fix* v; uint32_t R; };

struct Model {
    const uint8_t* d; size_t size;
    uint32_t vocab, embd, nlayer, nhead, ninner, npos;
    uint32_t bos, eos; int32_t eps_fix;
    uint32_t ntens, nbyte, nvocab, nmerges;
    uint32_t maxT;
    fix temp;               // generation temperature (Q16.16), default 0.80
    fix rep;                // repetition penalty   (Q16.16), default 1.30
    // tokenizer
    const uint32_t* byte_enc;         // [256] byte -> codepoint
    const uint8_t* vocab_bytes;       // start of vocab string section
    uint32_t* vocab_off; uint32_t* vocab_len;   // [nvocab]
    uint8_t  byte_to_id[256];
    uint32_t byte_dec[512];           // codepoint -> byte (0xFF absent)
    // merges hash: key=(id1<<16)|id2
    uint32_t mhash_mask;
    uint32_t* mcap;                   // [cap] key
    uint32_t* mhash_rank;             // [cap]
    uint32_t* mhash_merged;           // [cap]
    // weights (per tensor id)
    MatRef* mat; VecRef* vec;
    // for token-string hashing during load
    uint32_t shash_mask;
    uint32_t* shash_key; uint32_t* shash_val;
};

static uint32_t fnv1a(const uint8_t* p, uint32_t l) {
    uint32_t h = 2166136261u;
    for (uint32_t i=0;i<l;i++){ h^=p[i]; h*=16777619u; }
    return h;
}
static uint32_t hash(uint32_t x){ x^=x>>16; x*=0x7FEB352Du; x^=x>>13; return x; }

// ---------------------------------------------------------------- workspace
size_t llm_workspace_needed(const uint8_t* blob, size_t blob_size) {
    Reader r(blob, blob_size);
    if (blob_size < 8) return 0;
    if (blob[0]!='N'||blob[1]!='E'||blob[2]!='F'||blob[3]!='L') return 0;
    r.u32(); r.u32(); r.u32();
    for (int i=0;i<6;i++) r.u32();
    r.u32(); r.u32(); r.i32();
    uint32_t ntens = r.u32(); r.u32(); r.u32();
    uint32_t nbyte=r.u32(), nvocab=r.u32(), nmerges=r.u32();
    (void)nbyte; (void)ntens;
    if (!r.ok) return 0;
    size_t sz = 0;
    sz += (size_t)nvocab*8;                    // vocab_off+len
    // merges hash + string hash
    size_t mcap=1; while(mcap<nmerges*2+8) mcap<<=1;
    sz += 3*mcap*4;
    size_t scap=1; while(scap<nvocab*2+8) scap<<=1;
    sz += 2*scap*4;
    // merges raw pair scratch
    sz += (size_t)nmerges*16 + (size_t)nmerges*8;   // MPair + merged strings
    // weights refs + scales + biases
    sz += (size_t)ntens*(sizeof(MatRef)+sizeof(VecRef));
    sz += (size_t)nvocab*4;                    // wte scale (largest single)
    sz += 32*1024*1024;                        // safe margin for all scales/biases
    return sz + 65536;
}

static fix f2fix(uint32_t fbits) {
    int32_t sign=(int32_t)(fbits>>31);
    int32_t ex=(int32_t)((fbits>>23)&0xFF);
    uint32_t mant=fbits&0x7FFFFF;
    if(ex==0xFF){ return sign?0x80000001:0x7FFFFFFF; }
    uint32_t mval; int32_t sh;
    if(ex==0){ if(mant==0) return 0; mval=mant; sh=-149+16; }
    else { mval=(1u<<23)|mant; sh=(ex-127-23)+16; }   // value=mval*2^(ex-127-23); *65536 -> +16
    if(sh>=0){
        if(sh>=31) return sign?0x80000001:0x7FFFFFFF;
        int64_t q=((int64_t)mval)<<sh;
        if(q>0x7FFFFFFFll) q=0x7FFFFFFFll;
        return sign?(fix)-q:(fix)q;
    } else {
        int rs=-sh; if(rs>31) return 0;
        int64_t q=(((int64_t)mval)+(1ll<<(rs-1)))>>rs;
        return sign?(fix)-q:(fix)q;
    }
}

// ---------------------------------------------------------------- loader
int llm_load(const uint8_t* blob, size_t blob_size, void* ws, size_t ws_size,
             Model** out) {
    if (!blob || blob_size < 8) return -1;
    if (blob[0]!='N'||blob[1]!='E'||blob[2]!='F'||blob[3]!='L') return -2;
    Reader r(blob, blob_size);
    r.u32(); r.u32(); r.u32();
    Bump bp(ws, ws_size);
    Model* m = (Model*)bp.alloc(sizeof(Model), 8);
    if(!m) return -2;
    lmemset(m, 0, sizeof(Model));
    m->d=blob; m->size=blob_size;
    m->vocab=r.u32(); m->embd=r.u32(); m->nlayer=r.u32(); m->nhead=r.u32();
    m->ninner=r.u32(); m->npos=r.u32();
    m->bos=r.u32(); m->eos=r.u32(); m->eps_fix=r.i32();
    m->ntens=r.u32(); r.u32(); r.u32();          // ntens, wte_rows, wpe_rows
    m->nbyte=r.u32(); m->nvocab=r.u32(); m->nmerges=r.u32();
    if (!r.ok) return -3;
    m->maxT = m->npos < 256 ? m->npos : 256;
    m->temp = fx::fxf(80, 100);    // 0.80
    m->rep  = fx::fxf(130, 100);   // 1.30
    m->byte_enc = (const uint32_t*)r.bytes((size_t)m->nbyte*4);
    if (!r.ok) { return -4; }

    m->vocab_bytes = blob;   // base for vocab_off[] (absolute blob offsets)
    m->vocab_off = (uint32_t*)bp.alloc((size_t)m->nvocab*4,4);
    m->vocab_len = (uint32_t*)bp.alloc((size_t)m->nvocab*4,4);
    if (!m->vocab_off || !m->vocab_len) { return -5; }
    for (uint32_t i=0;i<m->nvocab;i++) {
        uint16_t l = r.u16();
        m->vocab_off[i] = (uint32_t)r.off;
        m->vocab_len[i] = l;
        r.bytes(l);
    }
    if (!r.ok) { return -6; }
    for (int i=0;i<512;i++) m->byte_dec[i]=0xFF;
    for (int b=0;b<256;b++){ uint32_t cp=m->byte_enc[b]; if(cp<512) m->byte_dec[cp]=(uint32_t)b; }

    // string->id hash over vocab (used to resolve merged ids + byte_to_id)
    size_t scap=1; while(scap<m->nvocab*2+8) scap<<=1;
    m->shash_mask=(uint32_t)(scap-1);
    m->shash_key=(uint32_t*)bp.alloc(scap*4,4);
    m->shash_val=(uint32_t*)bp.alloc(scap*4,4);
    if (!m->shash_key || !m->shash_val) { return -7; }
    for (size_t i=0;i<scap;i++) m->shash_key[i]=0xFFFFFFFFu;
    for (uint32_t id=0;id<m->nvocab;id++){
        const uint8_t* s=blob+m->vocab_off[id]; uint32_t l=m->vocab_len[id];
        uint32_t h=fnv1a(s,l), k=h;
        for(;;){ uint32_t sl=k&m->shash_mask;
            if(m->shash_key[sl]==0xFFFFFFFFu){ m->shash_key[sl]=h; m->shash_val[sl]=id; break; }
            if(m->shash_key[sl]==h){
                uint32_t oid=m->shash_val[sl];
                if(m->vocab_len[oid]==l && lmemcmp(blob+m->vocab_off[oid],s,l)==0) break;
            }
            k=k+1;
        }
    }
    auto find_id=[&](const uint8_t* s,uint32_t l)->uint32_t{
        uint32_t h=fnv1a(s,l),k=h;
        for(;;){ uint32_t sl=k&m->shash_mask;
            if(m->shash_key[sl]==0xFFFFFFFFu) return 0xFFFFFFFFu;
            if(m->shash_key[sl]==h){
                uint32_t id=m->shash_val[sl];
                if(m->vocab_len[id]==l && lmemcmp(blob+m->vocab_off[id],s,l)==0) return id;
            }
            k=k+1;
        }
    };
    for (int b=0;b<256;b++){
        uint32_t cp=m->byte_enc[b]; uint8_t tmp[4]; uint32_t tl=0;
        if(cp<0x80) tmp[tl++]=(uint8_t)cp;
        else if(cp<0x800){ tmp[tl++]=0xC0|(cp>>6); tmp[tl++]=0x80|(cp&0x3F); }
        else { tmp[tl++]=0xE0|(cp>>12); tmp[tl++]=0x80|((cp>>6)&0x3F); tmp[tl++]=0x80|(cp&0x3F); }
        uint32_t id=find_id(tmp,tl);
        m->byte_to_id[b]=(id<256)?(uint8_t)id:0xFF;
    }

    // merges hash
    size_t mcap=1; while(mcap<m->nmerges*2+8) mcap<<=1;
    m->mhash_mask=(uint32_t)(mcap-1);
    m->mcap=(uint32_t*)bp.alloc(mcap*4,4);
    m->mhash_rank=(uint32_t*)bp.alloc(mcap*4,4);
    m->mhash_merged=(uint32_t*)bp.alloc(mcap*4,4);
    if(!m->mcap||!m->mhash_rank||!m->mhash_merged){ return -8; }
    for(size_t i=0;i<mcap;i++){ m->mhash_rank[i]=0xFFFFFFFFu; m->mcap[i]=0; }
    for (uint32_t i=0;i<m->nmerges;i++){
        uint16_t l1=r.u16(); const uint8_t* s1=r.bytes(l1);
        uint16_t l2=r.u16(); const uint8_t* s2=r.bytes(l2);
        uint32_t id1=find_id(s1,l1), id2=find_id(s2,l2);
        if(id1==0xFFFFFFFFu||id2==0xFFFFFFFFu) continue;
        uint8_t mbuf[64]; uint32_t mlen=l1+l2;
        if(mlen>64) continue;
        lmemcpy(mbuf,s1,l1); lmemcpy(mbuf+l1,s2,l2);
        uint32_t mid=find_id(mbuf,mlen);
        if(mid==0xFFFFFFFFu) continue;
        uint32_t key=(id1<<16)|id2; uint32_t k=hash(key);
        for(;;){ uint32_t sl=k&m->mhash_mask;
            if(m->mhash_rank[sl]==0xFFFFFFFFu){ m->mhash_rank[sl]=i; m->mhash_merged[sl]=mid; m->mcap[sl]=key; break; }
            if(m->mcap[sl]==key) break;
            k=k+1;
        }
    }

    // weights
    m->mat=(MatRef*)bp.alloc((size_t)m->ntens*sizeof(MatRef),4);
    m->vec=(VecRef*)bp.alloc((size_t)m->ntens*sizeof(VecRef),4);
    if(!m->mat||!m->vec){ return -9; }
    for (uint32_t t=0;t<m->ntens;t++){
        uint32_t R=r.u32(), C=r.u32();
        if(C==0xFFFFFFFFu){
            fix* v=(fix*)bp.alloc((size_t)R*4,4);
            if(!v){ return -10; }
            for(uint32_t i=0;i<R;i++) v[i]=f2fix(r.u32());
            m->vec[t].v=v; m->vec[t].R=R;
        } else {
            const int8_t* q=(const int8_t*)r.bytes((size_t)R*C);
            fix* s=(fix*)bp.alloc((size_t)R*4,4);
            if(!q||!s){ return -11; }
            for(uint32_t i=0;i<R;i++) s[i]=f2fix(r.u32());
            m->mat[t].q=q; m->mat[t].R=R; m->mat[t].C=C; m->mat[t].s=s;
        }
    }
    if(!r.ok){ return -12; }
    *out=m;
    return 0;
}

void llm_unload(Model* m){ (void)m; }

// ------------------------------------------------------------------ encode
// GPT-2 byte BPE. Returns token count (or -1 error).
static int encode(Model* m, const char* prompt, uint32_t* out_ids, int max_ids) {
    // byte-level pre-tokenize
    uint32_t toks[1024]; int nt=0;
    const uint8_t* p=(const uint8_t*)prompt;
    while(*p){
        uint8_t b=*p++;
        uint8_t id=m->byte_to_id[b];
        if(id==0xFF){ /* unknown byte -> skip */ continue; }
        if(nt>=1023) return -1;
        toks[nt++]=id;
    }
    if(nt==0) return 0;
    // BPE merge loop
    for(;;){
        uint32_t bestRank=0xFFFFFFFFu; int bestI=-1; uint32_t bestMerged=0;
        for(int i=0;i<nt-1;i++){
            uint32_t key=(toks[i]<<16)|toks[i+1];
            uint32_t k=hash(key);
            for(;;){ uint32_t sl=k&m->mhash_mask;
                if(m->mhash_rank[sl]==0xFFFFFFFFu) break;
                if(m->mcap[sl]==key){ uint32_t r=m->mhash_rank[sl];
                    if(r<bestRank){ bestRank=r; bestI=i; bestMerged=m->mhash_merged[sl]; }
                    break; }
                k=k+1;
            }
        }
        if(bestI<0) break;
        toks[bestI]=bestMerged;
        for(int i=bestI+1;i<nt-1;i++) toks[i]=toks[i+1];
        nt--;
        if(nt>1023) break;
    }
    if(nt>max_ids) nt=max_ids;
    for(int i=0;i<nt;i++) out_ids[i]=toks[i];
    return nt;
}

// ------------------------------------------------------------------ decode
static int detok(Model* m, const uint32_t* ids, int n, char* out, int cap) {
    int o=0;
    for(int i=0;i<n;i++){
        uint32_t id=ids[i];
        if(id>=m->nvocab) continue;
        const uint8_t* s=m->vocab_bytes + m->vocab_off[id];
        uint32_t l=m->vocab_len[id];
        // decode token utf8 to codepoints, map to bytes, append
        for(uint32_t j=0;j<l;){
            uint8_t b0=s[j];
            uint32_t cp; uint32_t cl;
            if(b0<0x80){ cp=b0; cl=1; }
            else if((b0&0xE0)==0xC0){ cp=(b0&0x1F)<<6|(s[j+1]&0x3F); cl=2; }
            else if((b0&0xF0)==0xE0){ cp=(b0&0x0F)<<12|(s[j+1]&0x3F)<<6|(s[j+2]&0x3F); cl=3; }
            else if((b0&0xF8)==0xF0){ cp=(b0&0x07)<<18|(s[j+1]&0x3F)<<12|(s[j+2]&0x3F)<<6|(s[j+3]&0x3F); cl=4; }
            else { cp=b0; cl=1; }
            j+=cl;
            uint32_t by = cp<512 ? m->byte_dec[cp] : 0xFF;
            if(by==0xFF) continue;
            if(o<cap-1) out[o++]= (char)by;
        }
    }
    out[o<cap?o:cap-1]=0;
    return o;
}

// ---------------------------------------------------------------- scratch
size_t llm_scratch_needed(Model* m) {
    uint32_t T=m->maxT, E=m->embd, IN=m->ninner, V=m->vocab;
    size_t s=0;
    s += 5*(size_t)T*E*4;      // x,h,q,k,v  (5 buffers)
    s += (size_t)T*T*4;        // attn
    s += (size_t)T*IN*4;       // fc
    s += 2*(size_t)V*4;        // logits + probs
    s += (size_t)T*4;          // ids
    return s + 4096;
}

// --------------------------------------------------------------- inference
static void matvec(const int8_t* q, uint32_t R, uint32_t C, const fix* sfix,
                   const fix* x, fix* out, const fix* bias) {
    for (uint32_t r=0;r<R;r++){
        const int8_t* row=q+(size_t)r*C;
        fix64 acc=0;
        for(uint32_t c=0;c<C;c++) acc+=(fix64)row[c]*x[c];
        fix64 v=(acc*sfix[r])>>16;
        if(bias) v+=bias[r];
        if(v>0x7FFFFFFFll) v=0x7FFFFFFFll;
        if(v<-0x7FFFFFFFll) v=-0x7FFFFFFFll;
        out[r]=(fix)v;
    }
}

static void layernorm(const fix* x, fix* out, uint32_t T, uint32_t E,
                      const fix* w, const fix* b, fix eps) {
    for (uint32_t t=0;t<T;t++){
        const fix* row=x+(size_t)t*E;
        fix64 sum=0; for(uint32_t c=0;c<E;c++) sum+=row[c];
        fix mean=(fix)(sum/E);
        fix64 vsum=0; for(uint32_t c=0;c<E;c++){ fix64 d=(fix64)row[c]-mean; vsum+=d*d; }
        fix var=(fix)((vsum/E)>>16);
        fix denom=var+eps; if(denom<0) denom=0;
        fix inv=fx_div(FX_ONE,fx_sqrt(denom));
        fix* o=out+(size_t)t*E;
        for(uint32_t c=0;c<E;c++){ fix nc=fx_mul(row[c]-mean,inv); o[c]=fx_mul(nc,w[c])+b[c]; }
    }
}

static fix gelu_new(fix x){
    const fix C=(fix)(0.7978845608f*65536.0f);
    const fix A=(fix)(0.044715f*65536.0f);
    fix x2=fx_mul(x,x), x3=fx_mul(x2,x);
    fix inner=x+fx_mul(A,x3);
    fix t=fx_mul(C,inner);
    fix two=fx_mul(itofix(2),t);
    if(two>itofix(11)) two=itofix(11);
    if(two<-itofix(11)) two=-itofix(11);
    fix e=fx_exp(two);
    fix th=fx_div(e-FX_ONE,e+FX_ONE);
    return fx_mul(FX_HALF,fx_mul(x,FX_ONE+th));
}

// forward(ids,T) -> logits for last token (into logits[V]).
static void forward(Model* m, const uint32_t* ids, uint32_t T,
                     fix* x, fix* h, fix* q, fix* k, fix* v, fix* attn,
                     fix* fc, fix* logits) {
    const uint32_t E=m->embd, H=m->nhead, D=E/H, IN=m->ninner, V=m->vocab;
    MatRef& wte=m->mat[0]; MatRef& wpe=m->mat[1];
    fix sqrtd = fx_sqrt(itofix((int)D));
    for(uint32_t t=0;t<T;t++){
        uint32_t id=ids[t]<m->vocab?ids[t]:0;
        uint32_t pos=t%m->npos;
        const int8_t* we=wte.q+(size_t)id*E;
        const int8_t* pe=wpe.q+(size_t)pos*E;
        fix ws=wte.s[id], ps=wpe.s[pos];
        fix* row=x+(size_t)t*E;
        for(uint32_t c=0;c<E;c++){
            fix a=(fix)((fix64)we[c]*ws); fix b=(fix)((fix64)pe[c]*ps);
            row[c]=a+b;
        }
    }
    VecRef& LFW=m->vec[m->ntens-2]; VecRef& LFB=m->vec[m->ntens-1];
    for(uint32_t l=0;l<m->nlayer;l++){
        uint32_t base=2+13*l;
        MatRef& Q=m->mat[base+2], &K=m->mat[base+3], &Vp=m->mat[base+4], &Out=m->mat[base+5];
        VecRef& L1W=m->vec[base], &L1B=m->vec[base+1], &OB=m->vec[base+6];
        VecRef& L2W=m->vec[base+7], &L2B=m->vec[base+8];
        MatRef& CF=m->mat[base+9], &CP=m->mat[base+11];
        VecRef& CFB=m->vec[base+10], &CPB=m->vec[base+12];
        // layernorm -> h
        layernorm(x,h,T,E,L1W.v,L1B.v,m->eps_fix);
        for(uint32_t t=0;t<T;t++){
            const fix* hr=h+(size_t)t*E;
            matvec(Q.q,E,E,Q.s,hr,q+(size_t)t*E,0);
            matvec(K.q,E,E,K.s,hr,k+(size_t)t*E,0);
            matvec(Vp.q,E,E,Vp.s,hr,v+(size_t)t*E,0);
        }
        // attention
        for(uint32_t hh=0;hh<H;hh++){
            for(uint32_t t=0;t<T;t++){
                const fix* qrow=q+(size_t)t*E+hh*D;
                fix* arow=attn+(size_t)t*T;
                for(uint32_t t2=0;t2<=t;t2++){
                    const fix* krow=k+(size_t)t2*E+hh*D;
                    fix64 acc=0; for(uint32_t c=0;c<D;c++) acc+=(fix64)qrow[c]*krow[c];
                    fix sfix_dot=(fix)(acc>>16);          // Q16.16 dot
                    arow[t2]=fx_div(sfix_dot, sqrtd);     // / sqrt(D)
                }
                fix mx=-0x40000000; for(uint32_t t2=0;t2<=t;t2++) if(arow[t2]>mx)mx=arow[t2];
                fix64 ssum=0;
                for(uint32_t t2=0;t2<=t;t2++){ fix e=fx_exp(arow[t2]-mx); arow[t2]=e; ssum+=e; }
                if(ssum<=0) ssum=1;
                for(uint32_t t2=0;t2<=t;t2++) arow[t2]=(fix)(((fix64)arow[t2]<<16)/ssum);
            }
            for(uint32_t t=0;t<T;t++){
                const fix* arow=attn+(size_t)t*T;
                fix* orow=h+(size_t)t*E+hh*D;   // reuse h
                for(uint32_t c=0;c<D;c++){
                    fix64 acc=0;
                    for(uint32_t t2=0;t2<T;t2++){
                        const fix* vrow=v+(size_t)t2*E+hh*D;
                        acc+=(fix64)arow[t2]*vrow[c];
                    }
                    orow[c]=(fix)(acc>>16);
                }
            }
        }
        // out_proj + bias (write to q scratch, then residual into x)
        for(uint32_t t=0;t<T;t++){
            const fix* hr=h+(size_t)t*E;
            fix* orow=q+(size_t)t*E;
            matvec(Out.q,E,E,Out.s,hr,orow,OB.v);
        }
        for(uint32_t t=0;t<T;t++){
            fix* xr=x+(size_t)t*E; const fix* qr=q+(size_t)t*E;
            for(uint32_t c=0;c<E;c++) xr[c]=xr[c]+qr[c];
        }
        // MLP
        layernorm(x,h,T,E,L2W.v,L2B.v,m->eps_fix);
        for(uint32_t t=0;t<T;t++){
            const fix* hr=h+(size_t)t*E;
            fix* fr=fc+(size_t)t*IN;
            matvec(CF.q,IN,E,CF.s,hr,fr,CFB.v);
            for(uint32_t c=0;c<IN;c++) fr[c]=gelu_new(fr[c]);
            matvec(CP.q,E,IN,CP.s,fr,h+(size_t)t*E,CPB.v);
        }
        for(uint32_t t=0;t<T;t++){
            fix* xr=x+(size_t)t*E; const fix* hr=h+(size_t)t*E;
            for(uint32_t c=0;c<E;c++) xr[c]=xr[c]+hr[c];
        }
    }
    // final layernorm on last token, then head logits
    uint32_t t=T-1;
    const fix* row=x+(size_t)t*E;
    fix64 sum=0; for(uint32_t c=0;c<E;c++) sum+=row[c];
    fix mean=(fix)(sum/E);
    fix64 vsum=0; for(uint32_t c=0;c<E;c++){ fix64 dd=(fix64)row[c]-mean; vsum+=dd*dd; }
    fix var=(fix)((vsum/E)>>16);
    fix denom=var+m->eps_fix; if(denom<0) denom=0;
    fix inv=fx_div(FX_ONE,fx_sqrt(denom));
    fix hf[768];
    for(uint32_t c=0;c<E;c++) hf[c]=fx_mul(fx_mul(row[c]-mean,inv),LFW.v[c])+LFB.v[c];
    matvec(wte.q,V,E,wte.s,hf,logits,0);
}

// ----------------------------------------------------------------- generate
static uint32_t sample_topk(const fix* logits, uint32_t V, int topk, fix temp,
                            const uint32_t* hist, int hist_len, fix rep,
                            uint32_t* cid, fix* cval, uint64_t* rng);

int llm_generate(Model* m, const char* prompt, char* out, int outcap,
                 void* scratch, size_t scratch_size, int max_new) {
    if(!m||!prompt||!out) return -1;
    uint32_t E=m->embd, IN=m->ninner, V=m->vocab, Tmax=m->maxT;
    // carve scratch
    Bump bp(scratch, scratch_size);
    fix* x    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* h    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* q    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* k    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* v    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* attn =(fix*)bp.alloc(4*(size_t)Tmax*Tmax,4);
    fix* fc   =(fix*)bp.alloc(4*(size_t)Tmax*IN,4);
    fix* logits=(fix*)bp.alloc(4*(size_t)V,4);
    uint32_t* ids=(uint32_t*)bp.alloc((size_t)Tmax*4,4);
    if(!x||!h||!q||!k||!v||!attn||!fc||!logits||!ids) return -2;

    uint32_t seq[1024]; int prompt_nt=encode(m,prompt,seq,1023);
    if(prompt_nt<=0) return -3;
    // PRNG seeded from the prompt (repeatable per prompt)
    uint64_t rng=0x9E3779B97F4A7C15ull;
    for(int i=0;i<prompt_nt;i++)
        rng ^= (uint64_t)seq[i]+0x9E3779B97F4A7C15ull+(rng<<6)+(rng>>2);
    if(rng==0) rng=0xDEADBEEFCAFEBABEull;
    const fix TEMP=m->temp;
    const fix REP=m->rep;
    uint32_t cid[50]; fix cval[50];
    int slen=prompt_nt;
    int outlen=0;
    for(int step=0;step<max_new;step++){
        int T=slen;
        if(T>(int)Tmax) T=(int)Tmax;
        forward(m,seq,T,x,h,q,k,v,attn,fc,logits);
        // temperature + top-k + repetition-penalty sampling
        uint32_t picked=sample_topk(logits,V,50,TEMP,seq,slen,REP,cid,cval,&rng);
        seq[slen]=picked;
        slen++;
        if(picked==m->eos) break;
        if(slen>1023) break;
    }
    // decode only the generated portion (after the prompt tokens)
    outlen=detok(m, seq + prompt_nt, slen - prompt_nt, out, outcap);
    return outlen;
}

// Set generation knobs (Q16.16): temperature (default 0.80) and repetition
// penalty (default 1.30). Take effect on the next generation call.
void llm_set_sampling(Model* m, int32_t temp_q16, int32_t rep_q16) {
    if (!m) return;
    m->temp = temp_q16;
    m->rep  = rep_q16;
}

// ------------------------------------------------------------------ sampling
// xorshift64 PRNG (freestanding, no libc).
static uint64_t xs64(uint64_t* s) {
    uint64_t x=*s; x^=x<<13; x^=x>>7; x^=x<<17; *s=x; return x;
}

// Temperature + top-k + repetition-penalty sampling (Q16.16). Picks a token id
// from `logits`. Tokens whose id appears in `hist` (the generated context so
// far) get their logit divided by `rep` before softmax. `cid`/`cval` are
// scratch arrays of at least `topk` entries (local, small).
static uint32_t sample_topk(const fix* logits, uint32_t V, int topk,
                            fix temp, const uint32_t* hist, int hist_len, fix rep,
                            uint32_t* cid, fix* cval, uint64_t* rng) {
    int n=0;
    for(uint32_t i=0;i<V;i++){
        fix v=logits[i];
        if(n<topk){ cid[n]=i; cval[n]=v; n++; }
        else {
            int mi=0; for(int j=1;j<topk;j++) if(cval[j]<cval[mi]) mi=j;
            if(v>cval[mi]){ cval[mi]=v; cid[mi]=i; }
        }
    }
    if(n<=0) return 0;
    // repetition penalty: tokens already in hist get their logit divided by rep
    if(rep>FX_ONE){
        for(int j=0;j<n;j++){
            for(int i=0;i<hist_len;i++){
                if(cid[j]==hist[i]){ cval[j]=fx_div(cval[j],rep); break; }
            }
        }
    }
    // temperature scale + softmax over the top-k candidates
    fix mx=-0x40000000;
    for(int j=0;j<n;j++){ fix s=fx_div(cval[j], temp); cval[j]=s; if(s>mx) mx=s; }
    fix64 ssum=0;
    for(int j=0;j<n;j++){ fix e=fx_exp(cval[j]-mx); cval[j]=e; ssum+=e; }
    if(ssum<=0) ssum=1;
    // sample: uniform r in [0,1), pick by cumulative probability
    fix rfix=(fix)((xs64(rng)>>48)&0xFFFF);
    fix cum=0;
    for(int j=0;j<n;j++){
        fix pj=(fix)(((fix64)cval[j]<<16)/ssum);
        cum+=pj;
        if(cum>=rfix || j==n-1) return cid[j];
    }
    return cid[n-1];
}

// ------------------------------------------------------------------ stepwise
struct Gen {
    Model* m;
    fix *x,*h,*q,*k,*v,*attn,*fc,*logits;
    uint32_t* seq;
    int slen, prompt_nt, max_new, step;
    bool done;
    uint64_t rng;
};

size_t llm_gen_buf_needed(Model* m) {
    (void)m;
    size_t s=(sizeof(Gen)+7u)&~(size_t)7u;
    return s + 1024u*4u + 64u;
}

int llm_gen_begin(Model* m, const char* prompt,
                  void* scratch, size_t scratch_size,
                  void* gen_buf, size_t gen_size, Gen** out) {
    if(!m||!prompt||!scratch||!gen_buf||!out) return -1;
    uint32_t E=m->embd, IN=m->ninner, V=m->vocab, Tmax=m->maxT;
    if(gen_size < llm_gen_buf_needed(m)) return -2;
    Bump bp(scratch, scratch_size);
    fix* x    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* h    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* q    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* k    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* v    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* attn =(fix*)bp.alloc(4*(size_t)Tmax*Tmax,4);
    fix* fc   =(fix*)bp.alloc(4*(size_t)Tmax*IN,4);
    fix* logits=(fix*)bp.alloc(4*(size_t)V,4);
    if(!x||!h||!q||!k||!v||!attn||!fc||!logits) return -3;

    Gen* g=(Gen*)gen_buf;
    lmemset(g,0,sizeof(Gen));
    g->m=m; g->x=x; g->h=h; g->q=q; g->k=k; g->v=v;
    g->attn=attn; g->fc=fc; g->logits=logits;
    uint32_t* seq=(uint32_t*)(((uint8_t*)gen_buf)+((sizeof(Gen)+7u)&~(size_t)7u));
    g->seq=seq;
    int pn=encode(m,prompt,seq,1023);
    if(pn<=0) return -4;
    g->slen=pn; g->prompt_nt=pn; g->max_new=48; g->step=0; g->done=false;
    // seed the PRNG deterministically from the prompt (repeatable per prompt)
    uint64_t seed=0x9E3779B97F4A7C15ull;
    for(int i=0;i<pn;i++) seed^=(uint64_t)seq[i]+0x9E3779B97F4A7C15ull+(seed<<6)+(seed>>2);
    if(seed==0) seed=0xDEADBEEFCAFEBABEull;
    g->rng=seed;
    *out=g;
    return 0;
}

int llm_gen_step(Gen* g, char* tok, int tokcap) {
    if(!g||g->done) return 0;
    Model* m=g->m;
    uint32_t Tmax=m->maxT;
    int T=g->slen; if(T>(int)Tmax) T=(int)Tmax;
    forward(m,g->seq,(uint32_t)T,g->x,g->h,g->q,g->k,g->v,g->attn,g->fc,g->logits);
    // temperature + top-k + repetition-penalty sampling
    uint32_t cid[50]; fix cval[50];
    const fix TEMP=m->temp;
    const fix REP=m->rep;
    uint32_t picked=sample_topk(g->logits,m->vocab,50,TEMP,
                                g->seq,g->slen,REP,cid,cval,&g->rng);
    if(picked==m->eos){ g->done=true; return 0; }
    g->seq[g->slen++]=picked;
    g->step++;
    if(g->step>=g->max_new || g->slen>=1023){ g->done=true; }
    if(tok && tokcap>0){
        detok(m, g->seq+g->slen-1, 1, tok, tokcap);
        return 1;
    }
    return 1;
}

int llm_gen_text(Gen* g, char* out, int outcap) {
    if(!g||!out||outcap<=0) return 0;
    return detok(g->m, g->seq+g->prompt_nt, g->slen-g->prompt_nt, out, outcap);
}

void llm_gen_end(Gen* g) { (void)g; }


// ---- debug: predict top-k tokens for a prompt (matches numpy reference) ----
int llm_debug_topk(Model* m, const char* prompt, uint32_t* topids, int32_t* topvals, int k,
                   void* scratch, size_t scratch_size) {
    uint32_t E=m->embd, IN=m->ninner, V=m->vocab, Tmax=m->maxT;
    Bump bp(scratch, scratch_size);
    fix* x    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* h    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* q    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* kk   =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* v    =(fix*)bp.alloc(4*(size_t)Tmax*E,4);
    fix* attn =(fix*)bp.alloc(4*(size_t)Tmax*Tmax,4);
    fix* fc   =(fix*)bp.alloc(4*(size_t)Tmax*IN,4);
    fix* logits=(fix*)bp.alloc(4*(size_t)V,4);
    if(!x||!h||!q||!kk||!v||!attn||!fc||!logits) return -2;
    uint32_t seq[1024]; int n=encode(m,prompt,seq,1023);
    if(n<=0) return -3;
    forward(m,seq,(uint32_t)n,x,h,q,kk,v,attn,fc,logits);
    // top-k
    for(int i=0;i<k;i++){ topids[i]=0; topvals[i]=-0x7FFFFFFF; }
    for(uint32_t t=0;t<V;t++){
        fix lv=logits[t];
        for(int i=0;i<k;i++){ if(lv>topvals[i]){ for(int j=k-1;j>i;j--){topids[j]=topids[j-1];topvals[j]=topvals[j-1];} topids[i]=t; topvals[i]=lv; break; } }
    }
    return n;
}

} // namespace llm
} // namespace nefu

