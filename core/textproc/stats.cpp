// nefuOS 文本处理库 —— 统计 / 向量 / 可读性(实现)
#include "stats.h"
#include "../klib/klib.h"

namespace nefu {
namespace textproc {

static inline int slen(const char* s) { return s ? (int)strlen(s) : 0; }

// 是否为词内字符(字母)
static inline bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static inline char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// 简易自然对数(供 tf-idf 使用)：把 x 规约到 [1,2) 后用级数。
static double tp_log(double x) {
    if (x <= 0) return 0.0;
    int e = 0;
    while (x >= 2.0) { x *= 0.5; e++; }
    while (x < 1.0) { x *= 2.0; e--; }
    // x in [1,2): ln(x) via ln(1+y), y=x-1 in [0,1)
    double y = x - 1.0;
    double term = y, s = 0.0, pow = y;
    for (int k = 1; k <= 30; k++) {
        s += (k % 2 ? 1.0 : -1.0) * pow / k;
        pow *= y;
    }
    return s + e * 0.6931471805599453;
}

// ---------------------------------------------------------------------------
// 本地切词：把 text 切成小写词，写入动态字符串数组(调用方负责释放)
// ---------------------------------------------------------------------------
static char** tp_split_words(const char* text, int* out_count) {
    int cap = 16, n = 0;
    char** list = new char*[cap];
    char buf[128];
    int bl = 0;
    for (const char* p = text; ; p++) {
        char c = *p;
        if (is_word_char(c)) {
            if (bl < 127) buf[bl++] = to_lower(c);
        } else if (bl > 0) {
            buf[bl] = 0;
            if (n >= cap) {
                int nc = cap * 2;
                char** nl = new char*[nc];
                for (int i = 0; i < n; i++) nl[i] = list[i];
                delete[] list;
                list = nl; cap = nc;
            }
            list[n] = new char[bl + 1];
            for (int i = 0; i <= bl; i++) list[n][i] = buf[i];
            n++;
            bl = 0;
        }
        if (c == 0) break;
    }
    *out_count = n;
    return list;
}

static void tp_free_words(char** w, int n) {
    for (int i = 0; i < n; i++) delete[] w[i];
    delete[] w;
}

// =========================================================================
// 词频
// =========================================================================
WordFreq* word_freq(const char* text) {
    int n = 0;
    char** words = tp_split_words(text, &n);
    WordFreq* wf = new WordFreq;
    wf->words = 0;
    wf->counts = 0;
    wf->n = 0;
    if (n == 0) return wf;
    wf->words = new char*[n];
    wf->counts = new int[n];
    for (int i = 0; i < n; i++) {
        // 线性查重(小语料足够)
        int found = -1;
        for (int k = 0; k < wf->n; k++)
            if (strcmp(wf->words[k], words[i]) == 0) { found = k; break; }
        if (found >= 0) wf->counts[found]++;
        else {
            wf->words[wf->n] = new char[slen(words[i]) + 1];
            for (int k = 0; k <= slen(words[i]); k++) wf->words[wf->n][k] = words[i][k];
            wf->counts[wf->n] = 1;
            wf->n++;
        }
    }
    tp_free_words(words, n);
    return wf;
}

void word_freq_free(WordFreq* wf) {
    if (!wf) return;
    for (int i = 0; i < wf->n; i++) delete[] wf->words[i];
    delete[] wf->words;
    delete[] wf->counts;
    delete wf;
}

int word_freq_get(const WordFreq* wf, const char* word) {
    if (!wf) return 0;
    char low[128]; int l = 0;
    for (const char* p = word; *p && l < 127; p++) low[l++] = to_lower(*p);
    low[l] = 0;
    for (int i = 0; i < wf->n; i++) if (strcmp(wf->words[i], low) == 0) return wf->counts[i];
    return 0;
}

char* word_freq_report(const WordFreq* wf) {
    if (!wf) { char* e = new char[1]; e[0] = 0; return e; }
    int cap = wf->n * 24 + 8;
    char* out = new char[cap];
    out[0] = 0;
    for (int i = 0; i < wf->n; i++) {
        char line[160];
        ksprintf(line, sizeof(line), "%s: %d\n", wf->words[i], wf->counts[i]);
        strcat(out, line);
    }
    return out;
}

// =========================================================================
// n-gram
// =========================================================================
char** ngram_words(const char* text, int n, int* out_count) {
    int nw = 0;
    char** words = tp_split_words(text, &nw);
    int gn = (nw >= n && n > 0) ? nw - n + 1 : 0;
    char** grams = new char*[gn > 0 ? gn : 1];
    for (int i = 0; i < gn; i++) {
        int len = 0;
        for (int k = 0; k < n; k++) len += slen(words[i + k]) + 1;
        grams[i] = new char[len + 1];
        grams[i][0] = 0;
        for (int k = 0; k < n; k++) {
            if (k) strcat(grams[i], " ");
            strcat(grams[i], words[i + k]);
        }
    }
    tp_free_words(words, nw);
    *out_count = gn;
    return grams;
}

char** ngram_chars(const char* text, int n, int* out_count) {
    // 先抽取纯小写字母序列
    char seq[1024]; int sl = 0;
    for (const char* p = text; *p && sl < 1023; p++)
        if (is_word_char(*p)) seq[sl++] = to_lower(*p);
    seq[sl] = 0;
    int gn = (sl >= n && n > 0) ? sl - n + 1 : 0;
    char** grams = new char*[gn > 0 ? gn : 1];
    for (int i = 0; i < gn; i++) {
        grams[i] = new char[n + 1];
        for (int k = 0; k < n; k++) grams[i][k] = seq[i + k];
        grams[i][n] = 0;
    }
    *out_count = gn;
    return grams;
}

void ngram_free(char** grams, int count) {
    for (int i = 0; i < count; i++) delete[] grams[i];
    delete[] grams;
}

// =========================================================================
// 余弦相似度 / TF-IDF
// =========================================================================
double cosine_similarity(const char* docA, const char* docB) {
    WordFreq* a = word_freq(docA);
    WordFreq* b = word_freq(docB);
    // 词表并集 = a.words ∪ b.words
    int total = a->n + b->n;
    char** vocab = new char*[total > 0 ? total : 1];
    double* va = new double[total > 0 ? total : 1];
    double* vb = new double[total > 0 ? total : 1];
    for (int zz = 0; zz < (total > 0 ? total : 1); zz++) { va[zz] = 0; vb[zz] = 0; }
    int vn = 0;
    for (int i = 0; i < a->n; i++) vocab[vn++] = a->words[i];
    for (int i = 0; i < b->n; i++) {
        bool dup = false;
        for (int k = 0; k < a->n; k++) if (strcmp(b->words[i], a->words[k]) == 0) { dup = true; break; }
        if (!dup) vocab[vn++] = b->words[i];
    }
    for (int i = 0; i < vn; i++) { va[i] = word_freq_get(a, vocab[i]); vb[i] = word_freq_get(b, vocab[i]); }
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < vn; i++) {
        dot += va[i] * vb[i];
        na += va[i] * va[i];
        nb += vb[i] * vb[i];
    }
    double result = 0.0;
    if (na > 0 && nb > 0) {
        // 牛顿迭代求 sqrt(na*nb)，避免引入 libm
        double prod = na * nb, x = prod;
        for (int it = 0; it < 40; it++) x = (x + prod / x) * 0.5;
        result = dot / x;
    }
    delete[] vocab;
    delete[] va;
    delete[] vb;
    word_freq_free(a);
    word_freq_free(b);
    return result;
}

double tfidf_score(const char* const* docs, int ndoc, int docIdx, const char* term) {
    if (docIdx < 0 || docIdx >= ndoc) return 0.0;
    WordFreq* d = word_freq(docs[docIdx]);
    int totalWords = 0;
    for (int i = 0; i < d->n; i++) totalWords += d->counts[i];
    double tf = totalWords > 0 ? (double)word_freq_get(d, term) / totalWords : 0.0;
    word_freq_free(d);
    // df: 含 term 的文档数
    int df = 0;
    for (int i = 0; i < ndoc; i++) {
        WordFreq* w = word_freq(docs[i]);
        if (word_freq_get(w, term) > 0) df++;
        word_freq_free(w);
    }
    double idf = (df > 0) ? tp_log((double)ndoc / df) : 0.0;
    return tf * idf;
}

// =========================================================================
// 字符频率 / 可读性
// =========================================================================
void char_frequency(const char* text, int* counts) {
    for (int i = 0; i < 256; i++) counts[i] = 0;
    for (const char* p = text; *p; p++) counts[(uint8_t)*p]++;
}

int count_sentences(const char* text) {
    int s = 0;
    for (const char* p = text; *p; p++)
        if (*p == '.' || *p == '!' || *p == '?') s++;
    return s > 0 ? s : 1;     // 至少算一句
}

int count_words_tp(const char* text) {
    int n;
    char** w = tp_split_words(text, &n);
    tp_free_words(w, n);
    return n;
}

double avg_word_length(const char* text) {
    int n;
    char** w = tp_split_words(text, &n);
    if (n == 0) { tp_free_words(w, n); return 0.0; }
    int total = 0;
    for (int i = 0; i < n; i++) total += slen(w[i]);
    tp_free_words(w, n);
    return (double)total / n;
}

double avg_sentence_length(const char* text) {
    return (double)count_words_tp(text) / count_sentences(text);
}

// 音节估算：按元音字母组计数
static int count_syllables(const char* text) {
    int syl = 0; bool inVowel = false;
    for (const char* p = text; *p; p++) {
        char c = to_lower(*p);
        bool v = (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y');
        if (v && !inVowel) syl++;
        inVowel = v;
    }
    if (syl == 0) syl = 1;
    return syl;
}

void flesch(const char* text, double* out_ease, double* out_grade) {
    int words = count_words_tp(text);
    int sents = count_sentences(text);
    int syl = count_syllables(text);
    double asl = (double)words / sents;
    double asw = (double)syl / words;
    if (words == 0) { if (out_ease) *out_ease = 0; if (out_grade) *out_grade = 0; return; }
    if (out_ease)  *out_ease  = 206.835 - 1.015 * asl - 84.6 * asw;
    if (out_grade) *out_grade = 0.39 * asl + 11.8 * asw - 15.59;
}

double automated_readability_index(const char* text) {
    int words = count_words_tp(text);
    if (words == 0) return 0;
    int chars = 0;
    for (const char* p = text; *p; p++)
        if (((*p) >= 'a' && *p <= 'z') || ((*p) >= 'A' && *p <= 'Z')) chars++;
    int sents = count_sentences(text);
    return 4.71 * (double)chars / words + 0.5 * (double)words / sents - 21.43;
}

double gunning_fog(const char* text) {
    int words = count_words_tp(text);
    if (words == 0) return 0;
    int sents = count_sentences(text);
    // 复杂词近似：长度 >= 3 个音节的词；这里用长度 >= 6 作粗近似
    int n;
    char** w = tp_split_words(text, &n);
    int complex = 0;
    for (int i = 0; i < n; i++) if (slen(w[i]) >= 6) complex++;
    tp_free_words(w, n);
    double asl = (double)words / sents;
    return 0.4 * (asl + 100.0 * (double)complex / words);
}

double type_token_ratio(const char* text) {
    int n;
    char** w = tp_split_words(text, &n);
    if (n == 0) { tp_free_words(w, n); return 0; }
    int uniq = 0;
    for (int i = 0; i < n; i++) {
        bool seen = false;
        for (int k = 0; k < i; k++) if (strcmp(w[i], w[k]) == 0) { seen = true; break; }
        if (!seen) uniq++;
    }
    tp_free_words(w, n);
    return (double)uniq / n;
}

// =========================================================================
// 极简分类：统计每个类别关键词命中数，取最高
// =========================================================================
int classify_best(const char* text, const char* const* categories, int ncat) {
    char lower[1024]; int l = 0;
    for (const char* p = text; *p && l < 1023; p++) lower[l++] = to_lower(*p);
    lower[l] = 0;
    int best = -1, bestScore = 0;
    for (int c = 0; c < ncat; c++) {
        int score = 0;
        // categories[c] 形如 "cat,dog,pet"
        const char* p = categories[c];
        char kw[64]; int kl = 0;
        while (true) {
            char ch = *p;
            if (ch == ',' || ch == 0) {
                if (kl > 0) {
                    kw[kl] = 0;
                    if (strstr(lower, kw)) score++;
                    kl = 0;
                }
                if (ch == 0) break;
            } else {
                if (kl < 63) kw[kl++] = to_lower(ch);
            }
            p++;
        }
        if (score > bestScore) { bestScore = score; best = c; }
    }
    return best;
}

// =========================================================================
// 自检
// =========================================================================
int stats_self_test() {
    int f = 0;
    // 词频
    WordFreq* wf = word_freq("the cat sat on the mat the cat");
    f += (word_freq_get(wf, "the") == 3) ? 0 : 1;
    f += (word_freq_get(wf, "cat") == 2) ? 0 : 1;
    f += (word_freq_get(wf, "dog") == 0) ? 0 : 1;
    word_freq_free(wf);
    // n-gram
    {
        int c;
        char** g = ngram_words("the cat sat", 2, &c);
        f += (c == 2) ? 0 : 1;                 // "the cat","cat sat"
        if (c == 2) f += (strcmp(g[0], "the cat") == 0) ? 0 : 1;
        ngram_free(g, c);
    }
    {
        int c;
        char** g = ngram_chars("abcd", 2, &c);
        f += (c == 3) ? 0 : 1;
        if (c == 3) f += (strcmp(g[0], "ab") == 0 && strcmp(g[2], "cd") == 0) ? 0 : 1;
        ngram_free(g, c);
    }
    // 余弦：相同文本 = 1
    double sim = cosine_similarity("the cat sat", "the cat sat");
    f += (sim > 0.999) ? 0 : 1;
    // 完全不同文本明显更低，但仍 > 0
    double sim2 = cosine_similarity("apple banana fruit", "car truck engine");
    f += (sim2 < 0.2) ? 0 : 1;
    // TF-IDF: 在两文档里 term 在第 0 篇出现
    {
        const char* docs[2] = {"apple apple banana", "banana cherry"};
        double s = tfidf_score(docs, 2, 0, "apple");
        f += (s > 0.0) ? 0 : 1;   // apple 只在 doc0 出现，idf>0
    }
    // 字符频率
    {
        int cc[256];
        char_frequency("aaa", cc);
        f += (cc['a'] == 3) ? 0 : 1;
    }
    // 可读性：一句短文本不崩
    double e, gr;
    flesch("The cat sat on the mat.", &e, &gr);
    f += (e > 0) ? 0 : 1;
    f += (count_words_tp("hello world foo") == 3) ? 0 : 1;
    // 新增可读性指标(短句 ARI/Gunning 可能为负，只验证可运行)
    f += (automated_readability_index("The cat sat on the mat.") < 100.0) ? 0 : 1;
    f += (gunning_fog("The cat sat on the mat.") >= 0.0) ? 0 : 1;
    // TTR: "a a a" = 1/3; "a b c" = 1.0
    f += (type_token_ratio("a a a") > 0.3 && type_token_ratio("a a a") < 0.4) ? 0 : 1;
    f += (type_token_ratio("a b c") > 0.999) ? 0 : 1;
    // 分类
    {
        const char* cats[2] = {"dog,puppy,cat", {"car,truck,road"}};
        int r = classify_best("I have a dog and a cat", cats, 2);
        f += (r == 0) ? 0 : 1;
    }
    return f;
}

} // namespace textproc
} // namespace nefu
