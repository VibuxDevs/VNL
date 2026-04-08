/*
 * VNL sh — POSIX-subset shell interpreter with bash extensions
 *
 * Supports:
 *   - Variables: VAR=val, $VAR, ${VAR}, ${VAR:-default}, ${#VAR}
 *   - Special vars: $?, $#, $0..$9, $*, $@, $$, $RANDOM, $LINENO
 *   - Quoting: 'single', "double" (expands $var), \escape
 *   - Command substitution: $(cmd) and `cmd`
 *   - Arithmetic: $((expr))  — +, -, *, /, %, &, |, ^, ~, <<, >>
 *   - Pipelines: cmd1 | cmd2 | cmd3
 *   - Redirection: > file, >> file, < file, 2> file, 2>&1
 *   - Conditionals: if / elif / else / fi
 *   - Loops: while / do / done,  for var in list; do / done
 *   - Case: case $x in pat) ... ;; esac
 *   - Functions: name() { ... }
 *   - Logical: && ||
 *   - Semicolons and newlines as command separators
 *   - Builtins: echo, printf, read, test/[, true, false,
 *               export, unset, shift, return, break, continue,
 *               source/., alias, type, which, env
 *   - All VNL shell commands available as external commands
 */
#include "sh.h"
#include "types.h"
#include "string.h"
#include "heap.h"
#include "printf.h"
#include "vga.h"
#include "keyboard.h"
#include "vfs.h"
#include "timer.h"
#include "sched.h"

/* ================================================================
   Limits
   ================================================================ */
#define SH_MAX_VARS    128
#define SH_VAR_NAMSZ   64
#define SH_VAR_VALSZ   512
#define SH_MAX_ARGS    64
#define SH_MAX_FUNCS   32
#define SH_MAX_ALIASES 32
#define SH_PIPE_BUF    4096
#define SH_LINE_MAX    1024
#define SH_EXPAND_MAX  2048
#define SH_ARITH_MAX   64

int sh_last_status = 0;

/* ================================================================
   Variable store
   ================================================================ */
typedef struct { char name[SH_VAR_NAMSZ]; char val[SH_VAR_VALSZ]; bool exported; } ShVar;
static ShVar sh_vars[SH_MAX_VARS];
static int   sh_nvar = 0;

static ShVar *var_find(const char *name) {
    for (int i = 0; i < sh_nvar; i++)
        if (strcmp(sh_vars[i].name, name) == 0) return &sh_vars[i];
    return NULL;
}

static void var_set(const char *name, const char *val, bool exported) {
    ShVar *v = var_find(name);
    if (!v) {
        if (sh_nvar >= SH_MAX_VARS) return;
        v = &sh_vars[sh_nvar++];
        strncpy(v->name, name, SH_VAR_NAMSZ-1);
        v->exported = exported;
    }
    strncpy(v->val, val ? val : "", SH_VAR_VALSZ-1);
    if (exported) v->exported = true;
}

static const char *var_get(const char *name) {
    ShVar *v = var_find(name);
    return v ? v->val : NULL;
}

static void var_unset(const char *name) {
    for (int i = 0; i < sh_nvar; i++) {
        if (strcmp(sh_vars[i].name, name) == 0) {
            sh_vars[i] = sh_vars[--sh_nvar];
            return;
        }
    }
}

/* ================================================================
   Function store
   ================================================================ */
typedef struct { char name[SH_VAR_NAMSZ]; char *body; } ShFunc;
static ShFunc sh_funcs[SH_MAX_FUNCS];
static int    sh_nfunc = 0;

static ShFunc *func_find(const char *name) {
    for (int i = 0; i < sh_nfunc; i++)
        if (strcmp(sh_funcs[i].name, name) == 0) return &sh_funcs[i];
    return NULL;
}

static void func_set(const char *name, const char *body) {
    ShFunc *f = func_find(name);
    if (!f) {
        if (sh_nfunc >= SH_MAX_FUNCS) return;
        f = &sh_funcs[sh_nfunc++];
        strncpy(f->name, name, SH_VAR_NAMSZ-1);
        f->body = NULL;
    }
    kfree(f->body);
    f->body = strdup(body);
}

/* ================================================================
   Alias store
   ================================================================ */
typedef struct { char name[SH_VAR_NAMSZ]; char val[SH_VAR_VALSZ]; } ShAlias;
static ShAlias sh_aliases[SH_MAX_ALIASES];
static int     sh_nalias = 0;

static const char *alias_get(const char *name) {
    for (int i = 0; i < sh_nalias; i++)
        if (strcmp(sh_aliases[i].name, name) == 0) return sh_aliases[i].val;
    return NULL;
}
static void alias_set(const char *name, const char *val) {
    for (int i = 0; i < sh_nalias; i++) {
        if (strcmp(sh_aliases[i].name, name) == 0) {
            strncpy(sh_aliases[i].val, val, SH_VAR_VALSZ-1); return;
        }
    }
    if (sh_nalias >= SH_MAX_ALIASES) return;
    strncpy(sh_aliases[sh_nalias].name, name, SH_VAR_NAMSZ-1);
    strncpy(sh_aliases[sh_nalias].val,  val,  SH_VAR_VALSZ-1);
    sh_nalias++;
}

/* ================================================================
   Positional parameters ($1..$9, $#, $*, $@)
   ================================================================ */
static const char *sh_argv[SH_MAX_ARGS];
static int         sh_argc = 0;
static const char *sh_script_name = "sh";


/* ================================================================
   Arithmetic evaluator  ($((expr)))
   Handles: + - * / % & | ^ ~ ! << >>  and nested parens
   ================================================================ */
static int64_t arith_eval(const char *expr);

static int64_t arith_primary(const char **p) {
    while (**p == ' ') (*p)++;
    if (**p == '(') {
        (*p)++;
        int64_t v = arith_eval(*p);
        /* skip to matching ) */
        int depth = 1;
        while (**p && depth) { if (**p=='(') depth++; else if (**p==')') depth--; (*p)++; }
        return v;
    }
    if (**p == '-') { (*p)++; return -arith_primary(p); }
    if (**p == '+') { (*p)++; return  arith_primary(p); }
    if (**p == '~') { (*p)++; return ~arith_primary(p); }
    if (**p == '!') { (*p)++; return !arith_primary(p); }
    /* variable reference */
    if (**p == '$') {
        (*p)++;
        char vname[SH_VAR_NAMSZ]; int vi = 0;
        if (**p == '{') { (*p)++; while (**p && **p != '}') vname[vi++] = *(*p)++; if (**p) (*p)++; }
        else while (**p && ((**p>='a'&&**p<='z')||(**p>='A'&&**p<='Z')||(**p>='0'&&**p<='9')||**p=='_')) vname[vi++] = *(*p)++;
        vname[vi] = '\0';
        const char *v = var_get(vname);
        return v ? (int64_t)strtol(v, NULL, 0) : 0;
    }
    /* number */
    char *end;
    int64_t v = strtol(*p, &end, 0);
    *p = end;
    return v;
}

static int64_t arith_mul(const char **p) {
    int64_t v = arith_primary(p);
    while (**p == '*' || **p == '/' || **p == '%') {
        char op = *(*p)++;
        int64_t r = arith_primary(p);
        if (op == '*') v *= r;
        else if (op == '/') v = r ? v/r : 0;
        else v = r ? v%r : 0;
    }
    return v;
}
static int64_t arith_add(const char **p) {
    int64_t v = arith_mul(p);
    while (**p == '+' || **p == '-') { char op = *(*p)++; int64_t r = arith_mul(p); v = (op=='+') ? v+r : v-r; }
    return v;
}
static int64_t arith_shift(const char **p) {
    int64_t v = arith_add(p);
    while ((**p=='<'&&*(*p+1)=='<') || (**p=='>'&&*(*p+1)=='>')) {
        char op = *(*p); (*p)+=2; int64_t r = arith_add(p);
        v = (op=='<') ? v<<r : v>>r;
    }
    return v;
}
static int64_t arith_rel(const char **p) {
    int64_t v = arith_shift(p);
    while (**p == '<' || **p == '>') {
        bool eq = (*p)[1]=='=';
        char op = *(*p)++; if (eq) (*p)++;
        int64_t r = arith_shift(p);
        if (op=='<') v = eq ? v<=r : v<r;
        else         v = eq ? v>=r : v>r;
    }
    return v;
}
static int64_t arith_eq(const char **p) {
    int64_t v = arith_rel(p);
    while ((**p=='='&&*(*p+1)=='=')||(**p=='!'&&*(*p+1)=='=')) {
        bool neq = **p=='!'; (*p)+=2;
        int64_t r = arith_rel(p);
        v = neq ? (v!=r) : (v==r);
    }
    return v;
}
static int64_t arith_band(const char **p) { int64_t v=arith_eq(p); while(**p=='&'&&*(*p+1)!='&'){(*p)++;v&=arith_eq(p);} return v; }
static int64_t arith_bxor(const char **p) { int64_t v=arith_band(p); while(**p=='^'){(*p)++;v^=arith_band(p);} return v; }
static int64_t arith_bor(const char **p)  { int64_t v=arith_bxor(p); while(**p=='|'&&*(*p+1)!='|'){(*p)++;v|=arith_bxor(p);} return v; }
static int64_t arith_land(const char **p) { int64_t v=arith_bor(p); while(**p=='&'&&*(*p+1)=='&'){(*p)+=2;int64_t r=arith_bor(p);v=v&&r;} return v; }
static int64_t arith_lor(const char **p)  { int64_t v=arith_land(p); while(**p=='|'&&*(*p+1)=='|'){(*p)+=2;int64_t r=arith_land(p);v=v||r;} return v; }

static int64_t arith_eval(const char *expr) {
    const char *p = expr;
    return arith_lor(&p);
}

/* ================================================================
   Word expansion
   ================================================================ */
static void expand_word(const char *src, char *dst, size_t dstsz) {
    size_t di = 0;
    #define OUT(c) do { if (di+1 < dstsz) { dst[di++] = (c); dst[di] = '\0'; } } while(0)
    #define OUTS(s) do { const char *_s=(s); while(*_s && di+1<dstsz) { dst[di++]=*_s++; dst[di]='\0'; } } while(0)

    while (*src && di+1 < dstsz) {
        if (*src == '\\') {
            src++;
            if (*src) { OUT(*src++); }
            continue;
        }
        if (*src == '\'') {
            src++;
            while (*src && *src != '\'') OUT(*src++);
            if (*src) src++;
            continue;
        }
        if (*src == '"') {
            src++;
            while (*src && *src != '"') {
                if (*src == '\\' && (src[1]=='"'||src[1]=='\\'||src[1]=='$'||src[1]=='`')) { src++; OUT(*src++); continue; }
                if (*src == '$') goto expand_dollar;
                if (*src == '`') goto expand_backtick;
                OUT(*src++);
                continue;
            expand_dollar:;
            expand_backtick:;
                /* Fall through to general $ / ` handling below */
                goto handle_special;
            }
            if (*src) src++;
            continue;
        }
    handle_special:
        if (*src == '$') {
            src++;
            if (*src == '(') {
                if (src[1] == '(') {
                    /* Arithmetic $((expr)) */
                    src += 2;
                    const char *start = src;
                    int depth = 2;
                    while (*src && depth) { if(*src=='(') depth++; else if(*src==')') depth--; src++; }
                    char expr[SH_ARITH_MAX]; size_t elen = (size_t)(src - start - 2);
                    if (elen >= SH_ARITH_MAX) elen = SH_ARITH_MAX-1;
                    memcpy(expr, start, elen); expr[elen] = '\0';
                    int64_t result = arith_eval(expr);
                    char num[32]; ksprintf(num, sizeof(num), "%lld", result);
                    OUTS(num);
                } else {
                    /* Command substitution $(...) */
                    src++;
                    const char *start = src;
                    int depth = 1;
                    while (*src && depth) { if(*src=='(') depth++; else if(*src==')') depth--; src++; }
                    char cmd[SH_LINE_MAX]; size_t clen = (size_t)(src - start - 1);
                    if (clen >= SH_LINE_MAX) clen = SH_LINE_MAX-1;
                    memcpy(cmd, start, clen); cmd[clen] = '\0';
                    static char outbuf[SH_PIPE_BUF];
                    kout_redirect(outbuf, sizeof(outbuf));
                    sh_exec(cmd);
                    kout_reset();
                    /* Strip trailing newlines */
                    size_t ol = strlen(outbuf);
                    while (ol > 0 && (outbuf[ol-1]=='\n'||outbuf[ol-1]=='\r')) outbuf[--ol] = '\0';
                    OUTS(outbuf);
                }
            } else if (*src == '{') {
                src++;
                char vname[SH_VAR_NAMSZ]; int vi = 0;
                /* read var name */
                while (*src && *src!='}' && *src!=':' && *src!='#' && vi < SH_VAR_NAMSZ-1) vname[vi++] = *src++;
                vname[vi] = '\0';
                if (*src == '#') {
                    /* ${#VAR} — length */
                    src++;
                    const char *v = var_get(vname);
                    char num[16]; ksprintf(num, sizeof(num), "%u", (unsigned)(v ? strlen(v) : 0));
                    OUTS(num);
                    while (*src && *src!='}') src++;
                    if (*src) src++;
                } else if (*src == ':' && src[1] == '-') {
                    /* ${VAR:-default} */
                    src += 2;
                    const char *start = src;
                    while (*src && *src!='}') src++;
                    const char *v = var_get(vname);
                    if (v && *v) { OUTS(v); }
                    else {
                        char def[SH_VAR_VALSZ]; size_t dl = (size_t)(src-start);
                        if (dl >= SH_VAR_VALSZ) dl = SH_VAR_VALSZ-1;
                        memcpy(def, start, dl); def[dl] = '\0';
                        char expdef[SH_VAR_VALSZ];
                        expand_word(def, expdef, sizeof(expdef));
                        OUTS(expdef);
                    }
                    if (*src) src++;
                } else {
                    if (*src == '}') src++;
                    const char *v = var_get(vname);
                    if (v) OUTS(v);
                }
            } else if (*src == '?') {
                src++;
                char num[16]; ksprintf(num, sizeof(num), "%d", sh_last_status);
                OUTS(num);
            } else if (*src == '#') {
                src++;
                char num[16]; ksprintf(num, sizeof(num), "%d", sh_argc);
                OUTS(num);
            } else if (*src == '$') {
                src++;
                char num[16]; ksprintf(num, sizeof(num), "1");  /* PID stub */
                OUTS(num);
            } else if (*src == '*' || *src == '@') {
                src++;
                for (int i = 0; i < sh_argc; i++) {
                    if (i) OUT(' ');
                    OUTS(sh_argv[i]);
                }
            } else if (*src == '0') {
                src++;
                OUTS(sh_script_name);
            } else if (*src >= '1' && *src <= '9') {
                int idx = *src++ - '0';
                if (idx <= sh_argc) OUTS(sh_argv[idx-1]);
            } else if (*src == 'R' && strncmp(src,"RANDOM",6)==0) {
                src += 6;
                uint64_t t = timer_ticks();
                char num[16]; ksprintf(num,sizeof(num),"%llu",t%32768);
                OUTS(num);
            } else {
                /* $VARNAME */
                char vname[SH_VAR_NAMSZ]; int vi = 0;
                while (*src && ((*src>='a'&&*src<='z')||(*src>='A'&&*src<='Z')||(*src>='0'&&*src<='9')||*src=='_') && vi<SH_VAR_NAMSZ-1)
                    vname[vi++] = *src++;
                vname[vi] = '\0';
                const char *v = var_get(vname);
                if (v) OUTS(v);
            }
            continue;
        }
        if (*src == '`') {
            src++;
            const char *start = src;
            while (*src && *src != '`') src++;
            char cmd[SH_LINE_MAX]; size_t clen = (size_t)(src-start);
            if (clen >= SH_LINE_MAX) clen = SH_LINE_MAX-1;
            memcpy(cmd, start, clen); cmd[clen] = '\0';
            if (*src) src++;
            static char outbuf[SH_PIPE_BUF];
            kout_redirect(outbuf, sizeof(outbuf));
            sh_exec(cmd);
            kout_reset();
            size_t ol = strlen(outbuf);
            while (ol>0&&(outbuf[ol-1]=='\n'||outbuf[ol-1]=='\r')) outbuf[--ol]='\0';
            OUTS(outbuf);
            continue;
        }
        OUT(*src++);
    }
    #undef OUT
    #undef OUTS
}

/* ================================================================
   Tokenizer  —  splits a command into argv[], handling quotes,
   $(), `` and brace groups.
   Returns argc.
   ================================================================ */
static int tokenize(const char *line, char **argv, int maxargs, char *strbuf, size_t bufsz) {
    int argc = 0;
    const char *p = line;
    size_t bi = 0;

    while (*p && argc < maxargs-1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') break;   /* comment */
        if (*p == '\n' || *p == ';') { p++; break; }
        /* Collect one token */
        argv[argc++] = strbuf + bi;
        while (*p && *p!=' ' && *p!='\t' && *p!='\n' && *p!=';' && *p!='#') {
            if (*p == '\'') {
                p++;
                while (*p && *p != '\'') { if (bi+1<bufsz) strbuf[bi++]=*p; p++; }
                if (*p) p++;
            } else if (*p == '"') {
                char tmp[SH_EXPAND_MAX];
                const char *qs = p;
                /* find closing " respecting escapes */
                p++;
                const char *qstart = p;
                while (*p) {
                    if (*p == '\\' && p[1]) { p+=2; continue; }
                    if (*p == '"') break;
                    p++;
                }
                /* expand the content */
                size_t qlen = (size_t)(p - qstart) + 2; /* include quotes */
                char quoted[SH_LINE_MAX];
                if (qlen >= SH_LINE_MAX) qlen = SH_LINE_MAX-1;
                memcpy(quoted, qs, qlen);
                quoted[qlen] = '\0';
                expand_word(quoted, tmp, sizeof(tmp));
                size_t tl = strlen(tmp);
                if (bi+tl < bufsz) { memcpy(strbuf+bi, tmp, tl); bi += tl; }
                if (*p == '"') p++;
            } else if (*p == '$' || *p == '`') {
                /* Expand in place */
                const char *tok_start = p;
                /* Collect until whitespace/special */
                const char *scan = p;
                if (*scan == '$') {
                    scan++;
                    if (*scan == '(') {
                        scan++; int d=1;
                        while (*scan&&d){if(*scan=='(')d++;else if(*scan==')')d--;scan++;}
                    } else if (*scan == '{') {
                        scan++; while(*scan&&*scan!='}')scan++; if(*scan)scan++;
                    } else {
                        while(*scan&&((*scan>='a'&&*scan<='z')||(*scan>='A'&&*scan<='Z')||(*scan>='0'&&*scan<='9')||*scan=='_'||*scan=='?'||*scan=='#'||*scan=='*'||*scan=='@'||*scan=='$')) scan++;
                    }
                } else { /* backtick */
                    scan++;
                    while(*scan&&*scan!='`')scan++;
                    if(*scan)scan++;
                }
                char piece[SH_EXPAND_MAX]; size_t plen=(size_t)(scan-tok_start);
                if(plen>=SH_EXPAND_MAX)plen=SH_EXPAND_MAX-1;
                memcpy(piece,tok_start,plen); piece[plen]='\0';
                char expanded[SH_EXPAND_MAX];
                expand_word(piece, expanded, sizeof(expanded));
                size_t el=strlen(expanded);
                if(bi+el<bufsz){memcpy(strbuf+bi,expanded,el);bi+=el;}
                p = scan;
            } else {
                if (bi+1 < bufsz) strbuf[bi++] = *p;
                p++;
            }
        }
        if (bi < bufsz) strbuf[bi++] = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

/* ================================================================
   External command dispatch (calls shell.c built-ins)
   ================================================================ */
/* Declared in shell.c */
extern int shell_exec_builtin(int argc, char **argv);

/* ================================================================
   I/O redirect helper
   ================================================================ */
typedef struct {
    int    stdin_fd;    /* -1 = default */
    char  *stdout_buf;  /* non-NULL = capture stdout */
    size_t stdout_cap;
    char  *stdin_str;   /* non-NULL = read from string */
    size_t stdin_pos;
} ShIO;

/* ================================================================
   Execute a single simple command (no pipes).
   Returns exit status.
   ================================================================ */
static int sh_run_simple(int argc, char **argv) {
    if (argc == 0) return 0;

    /* ---- Shell builtins ---------------------------------------- */
    const char *cmd = argv[0];

    /* true / false */
    if (strcmp(cmd,"true")==0)  return 0;
    if (strcmp(cmd,"false")==0) return 1;
    if (strcmp(cmd,":")==0)     return 0;

    /* echo */
    if (strcmp(cmd,"echo")==0) {
        bool newline = true;
        int start = 1;
        if (argc > 1 && strcmp(argv[1],"-n")==0) { newline = false; start = 2; }
        for (int i = start; i < argc; i++) {
            if (i > start) kprintf(" ");
            /* Handle -e escape sequences */
            const char *s = argv[i];
            while (*s) {
                if (*s == '\\' && s[1]) {
                    s++;
                    switch (*s) {
                        case 'n': kprintf("\n"); break;
                        case 't': kprintf("\t"); break;
                        case 'r': kprintf("\r"); break;
                        case '\\': kprintf("\\"); break;
                        case 'e': case 'E': break; /* escape — ignore in VGA context */
                        default: kprintf("\\"); kprintf("%c", *s); break;
                    }
                    s++;
                } else { kprintf("%c", *s++); }
            }
        }
        if (newline) kprintf("\n");
        return 0;
    }

    /* printf builtin */
    if (strcmp(cmd,"printf")==0) {
        if (argc < 2) return 1;
        const char *fmt = argv[1];
        int ai = 2;
        while (*fmt) {
            if (*fmt == '%' && fmt[1]) {
                fmt++;
                switch (*fmt) {
                    case 's': kprintf("%s", ai<argc?argv[ai++]:""); break;
                    case 'd': kprintf("%d", ai<argc?(int)strtol(argv[ai++],NULL,10):0); break;
                    case 'i': kprintf("%d", ai<argc?(int)strtol(argv[ai++],NULL,0):0); break;
                    case 'x': kprintf("%x", ai<argc?(unsigned)(int)strtol(argv[ai++],NULL,0):0u); break;
                    case 'X': kprintf("%X", ai<argc?(unsigned)(int)strtol(argv[ai++],NULL,0):0u); break;
                    case 'o': kprintf("%o", ai<argc?(unsigned)(int)strtol(argv[ai++],NULL,0):0u); break;
                    case '%': kprintf("%%"); break;
                    default: kprintf("%%%c", *fmt); break;
                }
                fmt++;
            } else if (*fmt == '\\') {
                fmt++;
                switch (*fmt) {
                    case 'n': kprintf("\n"); break;
                    case 't': kprintf("\t"); break;
                    case '\\': kprintf("\\"); break;
                    default: kprintf("\\%c", *fmt); break;
                }
                fmt++;
            } else { kprintf("%c", *fmt++); }
        }
        return 0;
    }

    /* read */
    if (strcmp(cmd,"read")==0) {
        char buf[256]; int bi2 = 0;
        int c;
        while ((c = keyboard_getkey()) != '\n' && c != '\r' && bi2 < 255) {
            if (c == '\b') { if (bi2>0) bi2--; } else { buf[bi2++] = (char)c; }
        }
        buf[bi2] = '\0';
        kprintf("\n");
        if (argc > 1) var_set(argv[1], buf, false);
        return 0;
    }

    /* export */
    if (strcmp(cmd,"export")==0) {
        for (int i = 1; i < argc; i++) {
            char name[SH_VAR_NAMSZ]; char *eq = strchr(argv[i],'=');
            if (eq) {
                size_t nl = (size_t)(eq-argv[i]);
                if (nl >= SH_VAR_NAMSZ) nl = SH_VAR_NAMSZ-1;
                memcpy(name,argv[i],nl); name[nl]='\0';
                var_set(name, eq+1, true);
            } else {
                ShVar *v = var_find(argv[i]);
                if (v) v->exported = true;
                else var_set(argv[i], "", true);
            }
        }
        return 0;
    }

    /* unset */
    if (strcmp(cmd,"unset")==0) {
        for (int i = 1; i < argc; i++) var_unset(argv[i]);
        return 0;
    }

    /* shift */
    if (strcmp(cmd,"shift")==0) {
        int n = (argc>1) ? (int)strtol(argv[1],NULL,10) : 1;
        if (n > sh_argc) n = sh_argc;
        for (int i = 0; i+n < sh_argc; i++) sh_argv[i] = sh_argv[i+n];
        sh_argc -= n;
        return 0;
    }

    /* alias */
    if (strcmp(cmd,"alias")==0) {
        if (argc == 1) {
            for (int i = 0; i < sh_nalias; i++)
                kprintf("alias %s='%s'\n", sh_aliases[i].name, sh_aliases[i].val);
            return 0;
        }
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i],'=');
            if (eq) {
                char name[SH_VAR_NAMSZ]; size_t nl=(size_t)(eq-argv[i]);
                if (nl>=SH_VAR_NAMSZ) nl=SH_VAR_NAMSZ-1;
                memcpy(name,argv[i],nl); name[nl]='\0';
                /* strip surrounding quotes from value */
                char *val = eq+1;
                if ((*val=='\''||*val=='"')&&val[strlen(val)-1]==*val) {
                    val++; val[strlen(val)-1]='\0';
                }
                alias_set(name, val);
            } else {
                const char *v = alias_get(argv[i]);
                if (v) kprintf("alias %s='%s'\n", argv[i], v);
            }
        }
        return 0;
    }

    /* type / which */
    if (strcmp(cmd,"type")==0 || strcmp(cmd,"which")==0) {
        for (int i = 1; i < argc; i++) {
            if (alias_get(argv[i])) kprintf("%s: aliased to %s\n", argv[i], alias_get(argv[i]));
            else if (func_find(argv[i])) kprintf("%s is a shell function\n", argv[i]);
            else kprintf("%s is a shell builtin/command\n", argv[i]);
        }
        return 0;
    }

    /* env */
    if (strcmp(cmd,"env")==0) {
        for (int i = 0; i < sh_nvar; i++)
            if (sh_vars[i].exported)
                kprintf("%s=%s\n", sh_vars[i].name, sh_vars[i].val);
        return 0;
    }

    /* test / [ */
    if (strcmp(cmd,"test")==0 || strcmp(cmd,"[")==0) {
        int end = (strcmp(cmd,"[")==0 && argc>1 && strcmp(argv[argc-1],"]")==0) ? argc-1 : argc;
        if (end == 1) return 1; /* no args = false */
        /* -z string */
        if (end==3 && strcmp(argv[1],"-z")==0) return strlen(argv[2])==0?0:1;
        if (end==3 && strcmp(argv[1],"-n")==0) return strlen(argv[2])!=0?0:1;
        if (end==3 && strcmp(argv[1],"-f")==0) { VFSNodeType t; return vfs_stat(argv[2],&t,NULL)==0&&t==VFS_FILE?0:1; }
        if (end==3 && strcmp(argv[1],"-d")==0) { VFSNodeType t; return vfs_stat(argv[2],&t,NULL)==0&&t==VFS_DIR?0:1; }
        if (end==3 && strcmp(argv[1],"-e")==0) { return vfs_resolve(argv[2])>=0?0:1; }
        /* string comparisons */
        if (end==4 && strcmp(argv[2],"=")==0)  return strcmp(argv[1],argv[3])==0?0:1;
        if (end==4 && strcmp(argv[2],"!=")==0) return strcmp(argv[1],argv[3])!=0?0:1;
        /* numeric comparisons */
        if (end==4) {
            int64_t a=strtol(argv[1],NULL,10), b=strtol(argv[3],NULL,10);
            if (strcmp(argv[2],"-eq")==0) return a==b?0:1;
            if (strcmp(argv[2],"-ne")==0) return a!=b?0:1;
            if (strcmp(argv[2],"-lt")==0) return a<b?0:1;
            if (strcmp(argv[2],"-le")==0) return a<=b?0:1;
            if (strcmp(argv[2],"-gt")==0) return a>b?0:1;
            if (strcmp(argv[2],"-ge")==0) return a>=b?0:1;
        }
        /* bare string = true if non-empty */
        if (end==2) return strlen(argv[1])!=0?0:1;
        return 1;
    }

    /* VAR=val assignment */
    if (strchr(cmd,'=') && cmd[0]!='=') {
        char *eq = strchr(argv[0],'=');
        char name[SH_VAR_NAMSZ]; size_t nl=(size_t)(eq-argv[0]);
        if (nl>=SH_VAR_NAMSZ) nl=SH_VAR_NAMSZ-1;
        memcpy(name,argv[0],nl); name[nl]='\0';
        char expanded[SH_VAR_VALSZ];
        expand_word(eq+1, expanded, sizeof(expanded));
        var_set(name, expanded, false);
        /* If there are more args, run them with this var set temporarily */
        if (argc > 1) return sh_run_simple(argc-1, argv+1);
        return 0;
    }

    /* Shell function call */
    ShFunc *fn = func_find(cmd);
    if (fn && fn->body) {
        const char *saved_argv[SH_MAX_ARGS]; int saved_argc = sh_argc;
        memcpy(saved_argv, sh_argv, sizeof(sh_argv));
        sh_argc = argc - 1;
        for (int i = 0; i < sh_argc; i++) sh_argv[i] = argv[i+1];
        int ret = sh_run_string(fn->body);
        sh_argc = saved_argc;
        memcpy(sh_argv, saved_argv, sizeof(sh_argv));
        return ret;
    }

    /* Check alias */
    const char *aliased = alias_get(cmd);
    if (aliased) {
        char expanded_alias[SH_LINE_MAX];
        expand_word(aliased, expanded_alias, sizeof(expanded_alias));
        /* Append remaining args */
        for (int i = 1; i < argc; i++) {
            strncat(expanded_alias, " ", sizeof(expanded_alias)-strlen(expanded_alias)-1);
            strncat(expanded_alias, argv[i], sizeof(expanded_alias)-strlen(expanded_alias)-1);
        }
        return sh_exec(expanded_alias);
    }

    /* External command (shell built-in or VNL command) */
    return shell_exec_builtin(argc, argv);
}

/* ================================================================
   Pipeline execution
   Handles:  cmd1 | cmd2 | cmd3
   Each stage except last is captured into a buffer; the buffer
   becomes stdin (via $STDIN_CONTENT var hack) for the next stage.
   ================================================================ */
static int sh_run_pipeline(const char *line) {
    /* Split on unquoted | */
    static char stages[9][SH_LINE_MAX];
    int nstages = 0;
    const char *p = line;
    int si = 0;
    int depth_paren = 0;
    bool in_sq = false, in_dq = false;

    while (*p && nstages < 8) {
        if (*p == '\'' && !in_dq) in_sq = !in_sq;
        else if (*p == '"' && !in_sq) in_dq = !in_dq;
        else if (!in_sq && !in_dq) {
            if (*p == '(') depth_paren++;
            else if (*p == ')') depth_paren--;
            else if (*p == '|' && depth_paren == 0 && p[1] != '|') {
                stages[nstages][si] = '\0';
                nstages++;
                si = 0;
                p++;
                continue;
            }
        }
        if (si < SH_LINE_MAX-1) stages[nstages][si++] = *p;
        p++;
    }
    stages[nstages][si] = '\0';
    nstages++;

    if (nstages == 1) {
        /* No pipe — handle redirection and run */
        char cmdline[SH_LINE_MAX];
        strncpy(cmdline, stages[0], SH_LINE_MAX-1);

        /* Parse redirections */
        char *redir_out  = NULL, *redir_in = NULL, *redir_app = NULL;
        char *redir_err  = NULL;
        bool  redir_e2o  = false;

        /* Simple redirect scanner (crude but functional) */
        char stripped[SH_LINE_MAX]; int ssi = 0;
        const char *cp = cmdline;
        while (*cp) {
            if (*cp == '>' && cp[1] == '>') {
                cp += 2; while(*cp==' ')cp++;
                static char app_name[128]; int ani=0;
                while(*cp&&*cp!=' '&&ani<127) app_name[ani++]=*cp++;
                app_name[ani]='\0'; redir_app=app_name;
            } else if (cp[0]=='2'&&cp[1]=='>'&&cp[2]=='&'&&cp[3]=='1') {
                cp+=4; redir_e2o=true;
            } else if (cp[0]=='2'&&cp[1]=='>') {
                cp+=2; while(*cp==' ')cp++;
                static char err_name[128]; int eni=0;
                while(*cp&&*cp!=' '&&eni<127) err_name[eni++]=*cp++;
                err_name[eni]='\0'; redir_err=err_name;
                (void)redir_err; /* stderr goes to VGA always for now */
            } else if (*cp == '>') {
                cp++; while(*cp==' ')cp++;
                static char out_name[128]; int oni=0;
                while(*cp&&*cp!=' '&&oni<127) out_name[oni++]=*cp++;
                out_name[oni]='\0'; redir_out=out_name;
            } else if (*cp == '<') {
                cp++; while(*cp==' ')cp++;
                static char in_name[128]; int ini=0;
                while(*cp&&*cp!=' '&&ini<127) in_name[ini++]=*cp++;
                in_name[ini]='\0'; redir_in=in_name;
            } else {
                if (ssi < SH_LINE_MAX-1) stripped[ssi++] = *cp++;
                continue;
            }
        }
        stripped[ssi] = '\0';
        (void)redir_e2o;

        /* Read stdin from file if < redirect */
        if (redir_in) {
            int fd = vfs_open(redir_in, VFS_O_READ);
            if (fd >= 0) {
                static char ibuf[SH_PIPE_BUF]; int n = vfs_read(fd, ibuf, sizeof(ibuf)-1); if(n<0)n=0;
                ibuf[n]='\0'; vfs_close(fd);
                var_set("STDIN_CONTENT", ibuf, false);
            }
        }

        /* Capture stdout to file */
        int ret;
        static char redir_obuf[SH_PIPE_BUF];
        if (redir_out) {
            kout_redirect(redir_obuf, sizeof(redir_obuf));
            ret = sh_exec(stripped);
            kout_reset();
            int fd = vfs_open(redir_out, VFS_O_WRITE|VFS_O_CREATE|VFS_O_TRUNC);
            if (fd >= 0) { vfs_write(fd, redir_obuf, strlen(redir_obuf)); vfs_close(fd); }
        } else if (redir_app) {
            kout_redirect(redir_obuf, sizeof(redir_obuf));
            ret = sh_exec(stripped);
            kout_reset();
            int fd = vfs_open(redir_app, VFS_O_WRITE|VFS_O_CREATE);
            if (fd >= 0) { vfs_write(fd, redir_obuf, strlen(redir_obuf)); vfs_close(fd); }
        } else {
            ret = sh_exec(stripped);
        }
        return ret;
    }

    /* Multi-stage pipeline */
    static char pipe_buf[SH_PIPE_BUF]; pipe_buf[0] = '\0';
    int ret = 0;

    for (int i = 0; i < nstages; i++) {
        char stage_cmd[SH_LINE_MAX];
        expand_word(stages[i], stage_cmd, sizeof(stage_cmd));

        /* Make previous output available as $STDIN_CONTENT */
        if (i > 0) var_set("STDIN_CONTENT", pipe_buf, false);

        if (i < nstages-1) {
            /* Capture this stage's output */
            kout_redirect(pipe_buf, SH_PIPE_BUF);
            ret = sh_exec(stage_cmd);
            kout_reset();
        } else {
            /* Last stage — output to screen normally */
            ret = sh_exec(stage_cmd);
        }
    }
    return ret;
}

/* ================================================================
   Script executor  —  handles control flow
   ================================================================ */

/* flow control signals */
#define SH_FLOW_NONE     0
#define SH_FLOW_BREAK    1
#define SH_FLOW_CONTINUE 2
#define SH_FLOW_RETURN   3

static int sh_flow = SH_FLOW_NONE;
static int sh_return_val = 0;

/* Forward declaration */
static int sh_exec_block(const char **pp, bool execute);

static int sh_exec_block(const char **pp, bool execute) {
    const char *p = *pp;
    int last_ret = 0;

    while (*p) {
        /* skip blank lines and comments */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n') { p++; continue; }
        if (*p == '#') { while(*p && *p!='\n') p++; continue; }
        if (!*p) break;

        /* Peek at the current line */
        const char *line_start = p;

        /* Collect logical line (handle line continuation \\\n) */
        char logline[SH_LINE_MAX]; int li = 0;
        while (*p && *p != '\n' && *p != ';') {
            if (*p == '\\' && p[1] == '\n') { p += 2; continue; }
            /* stop at unquoted # */
            if (*p == '#') { while(*p && *p!='\n') p++; break; }
            if (li < SH_LINE_MAX-1) logline[li++] = *p;
            p++;
        }
        logline[li] = '\0';
        if (*p == '\n' || *p == ';') p++;

        /* Trim trailing whitespace */
        while (li > 0 && (logline[li-1]==' '||logline[li-1]=='\t')) logline[--li]='\0';
        if (li == 0) continue;

        /* ---- Function definition -------------------------------- */
        /* name() { or function name { */
        {
            const char *fl = logline;
            while (*fl == ' ') fl++;
            bool is_func = false;
            char fname[SH_VAR_NAMSZ]; int fni = 0;
            if (strncmp(fl,"function ",9)==0) {
                fl += 9; while(*fl==' ')fl++;
                while(*fl&&*fl!=' '&&*fl!='('&&fni<SH_VAR_NAMSZ-1) fname[fni++]=*fl++;
                fname[fni]='\0'; is_func = true;
                while(*fl&&*fl!='{') fl++;
            } else {
                /* name() { */
                const char *tp = fl;
                while(*tp&&*tp!='('&&*tp!=' ') { fname[fni++]=*tp++; }
                fname[fni]='\0';
                if (*tp=='(') { tp++; if(*tp==')') { tp++; while(*tp==' ')tp++; if(*tp=='{') is_func=true; } }
            }
            if (is_func) {
                /* collect body until matching } */
                char fbody[SH_LINE_MAX*8]; int fbi = 0;
                while (*p) {
                    while(*p==' '||*p=='\t') p++;
                    if (*p == '}') { p++; while(*p&&*p!='\n')p++; if(*p)p++; break; }
                    while(*p && *p!='\n') { if(fbi<(int)sizeof(fbody)-1) fbody[fbi++]=*p; p++; }
                    if(fbi<(int)sizeof(fbody)-1) fbody[fbi++]='\n';
                    if(*p=='\n') p++;
                }
                fbody[fbi]='\0';
                if (execute) func_set(fname, fbody);
                continue;
            }
        }

        /* ---- if / elif / else / fi ------------------------------ */
        if (strncmp(logline,"if ",3)==0 || strcmp(logline,"if")==0) {
            char cond[SH_LINE_MAX];
            /* extract condition after 'if' and before 'then' */
            const char *kw = logline+3;
            char *then_pos = strstr(logline," then");
            if (then_pos) { size_t cl=(size_t)(then_pos-kw); if(cl>=SH_LINE_MAX)cl=SH_LINE_MAX-1; memcpy(cond,kw,cl); cond[cl]='\0'; }
            else { strncpy(cond, kw, SH_LINE_MAX-1); }

            bool cond_true = false;
            if (execute) { int r = sh_exec(cond); cond_true = (r == 0); }

            /* run the 'then' block */
            last_ret = sh_exec_block(&p, execute && cond_true);

            /* handle elif / else */
            while (*p) {
                while(*p==' '||*p=='\t') p++;
                if (strncmp(p,"elif ",5)==0) {
                    const char *ep = p+5;
                    char elif_cond[SH_LINE_MAX]; int eci=0;
                    while(*ep&&*ep!='\n'&&strncmp(ep," then",5)!=0) { if(eci<SH_LINE_MAX-1) elif_cond[eci++]=*ep++; }
                    elif_cond[eci]='\0';
                    while(*ep&&*ep!='\n') ep++; if(*ep) ep++;
                    p = ep;
                    bool elif_true = false;
                    if (execute && !cond_true) { int r=sh_exec(elif_cond); elif_true=(r==0); }
                    last_ret = sh_exec_block(&p, execute && !cond_true && elif_true);
                    if (elif_true) cond_true = true;
                } else if (strncmp(p,"else",4)==0 && (p[4]=='\n'||p[4]==' '||p[4]==';')) {
                    while(*p&&*p!='\n') p++; if(*p) p++;
                    last_ret = sh_exec_block(&p, execute && !cond_true);
                } else if (strncmp(p,"fi",2)==0 && (p[2]=='\n'||p[2]==' '||p[2]==';'||!p[2])) {
                    while(*p&&*p!='\n') p++; if(*p) p++;
                    break;
                } else break;
            }
            last_ret = 0;
            continue;
        }

        /* Block terminators */
        if (strncmp(logline,"fi",2)==0   && (logline[2]=='\0'||logline[2]==' ')) { *pp = p; return last_ret; }
        if (strncmp(logline,"done",4)==0 && (logline[4]=='\0'||logline[4]==' ')) { *pp = p; return last_ret; }
        if (strncmp(logline,"esac",4)==0 && (logline[4]=='\0'||logline[4]==' ')) { *pp = p; return last_ret; }
        if (logline[0] == '}') { *pp = p; return last_ret; }
        if (strncmp(logline,"then",4)==0 && (logline[4]=='\0'||logline[4]==' ')) continue;
        if (strncmp(logline,"do",2)==0   && (logline[2]=='\0'||logline[2]==' '))  continue;
        if (strncmp(logline,"else",4)==0 && (logline[4]=='\0'||logline[4]==' ')) { *pp = p; return last_ret; }
        if (strncmp(logline,"elif",4)==0)  { *pp = line_start; return last_ret; }

        /* ---- while loop ---------------------------------------- */
        if (strncmp(logline,"while ",6)==0 || strcmp(logline,"while")==0) {
            char cond[SH_LINE_MAX];
            strncpy(cond, logline+6, SH_LINE_MAX-1);
            /* strip trailing 'do' */
            char *do_pos = strstr(cond,"; do"); if(do_pos) *do_pos='\0';
            do_pos = strstr(cond," do"); if(do_pos) *do_pos='\0';

            const char *loop_start = p;
            while (execute) {
                int r = sh_exec(cond);
                if (r != 0) break;
                const char *lp = loop_start;
                last_ret = sh_exec_block(&lp, true);
                p = lp;    /* advance past the block each iteration */
                if (sh_flow == SH_FLOW_BREAK)    { sh_flow = SH_FLOW_NONE; break; }
                if (sh_flow == SH_FLOW_CONTINUE)  { sh_flow = SH_FLOW_NONE; continue; }
                if (sh_flow == SH_FLOW_RETURN)    break;
                /* reset to loop body start */
                p = loop_start;
            }
            if (!execute) { /* skip block */ const char *dummy=p; sh_exec_block(&dummy,false); p=dummy; }
            continue;
        }

        /* ---- for loop ------------------------------------------ */
        if (strncmp(logline,"for ",4)==0) {
            /* for VAR in word1 word2 ...; do */
            char varname[SH_VAR_NAMSZ]; int vni=0;
            const char *fp = logline+4;
            while(*fp&&*fp!=' ') { if(vni<SH_VAR_NAMSZ-1) varname[vni++]=*fp++; }
            varname[vni]='\0';
            while(*fp==' ') fp++;
            if(strncmp(fp,"in ",3)==0) fp+=3;
            /* collect word list */
            char wordlist[SH_LINE_MAX];
            strncpy(wordlist, fp, SH_LINE_MAX-1);
            char *do_pos = strstr(wordlist,"; do"); if(do_pos)*do_pos='\0';
            do_pos = strstr(wordlist," do"); if(do_pos)*do_pos='\0';

            /* Expand wordlist */
            char expanded_wl[SH_EXPAND_MAX];
            expand_word(wordlist, expanded_wl, sizeof(expanded_wl));

            /* Split into words */
            char *words[SH_MAX_ARGS]; int nwords=0;
            char wbuf[SH_LINE_MAX];
            /* reuse tokenize for splitting */
            nwords = tokenize(expanded_wl, words, SH_MAX_ARGS, wbuf, sizeof(wbuf));

            const char *loop_start = p;
            for (int wi = 0; wi < nwords && execute; wi++) {
                var_set(varname, words[wi], false);
                const char *lp = loop_start;
                last_ret = sh_exec_block(&lp, true);
                p = lp;
                if (sh_flow == SH_FLOW_BREAK)    { sh_flow = SH_FLOW_NONE; break; }
                if (sh_flow == SH_FLOW_CONTINUE)  { sh_flow = SH_FLOW_NONE; continue; }
                if (sh_flow == SH_FLOW_RETURN)    break;
                p = loop_start;
            }
            if (!execute) { const char *dummy=p; sh_exec_block(&dummy,false); p=dummy; }
            continue;
        }

        /* ---- case statement ------------------------------------ */
        if (strncmp(logline,"case ",5)==0) {
            char word_src[SH_LINE_MAX]; char expanded_word[SH_LINE_MAX];
            strncpy(word_src, logline+5, SH_LINE_MAX-1);
            char *in_pos = strstr(word_src," in"); if(in_pos)*in_pos='\0';
            expand_word(word_src, expanded_word, sizeof(expanded_word));
            bool matched = false;
            /* parse patterns */
            while (*p) {
                while(*p==' '||*p=='\t'||*p=='\n') p++;
                if (strncmp(p,"esac",4)==0&&(p[4]=='\n'||p[4]==' '||!p[4])) { while(*p&&*p!='\n')p++; if(*p)p++; break; }
                /* read pattern(s) */
                char pat[SH_VAR_NAMSZ*2]; int pi=0;
                while(*p&&*p!=')'&&pi<(int)sizeof(pat)-1) pat[pi++]=*p++;
                pat[pi]='\0'; if(*p==')')p++;
                /* strip whitespace from pat */
                while(pi>0&&pat[pi-1]==' ') pat[--pi]='\0';
                /* simple glob match: check | separated patterns */
                bool this_match = false;
                if (!matched && execute) {
                    char *pat_tok = pat; char *save_ptr;
                    /* manual strtok on | */
                    while (pat_tok) {
                        char *next_pipe = strchr(pat_tok,'|');
                        if(next_pipe) *next_pipe++='\0';
                        /* trim */
                        while(*pat_tok==' ')pat_tok++;
                        char *pe=pat_tok+strlen(pat_tok); while(pe>pat_tok&&pe[-1]==' ')pe--;*pe='\0';
                        /* match: exact or simple glob */
                        if (strcmp(pat_tok,"*")==0 || strcmp(pat_tok,expanded_word)==0) { this_match=true; break; }
                        pat_tok = next_pipe;
                        (void)save_ptr;
                    }
                }
                if (this_match) matched = true;
                /* run body until ;; */
                char body_buf[SH_LINE_MAX*4]; int bbi=0;
                while(*p) {
                    while(*p==' '||*p=='\t') p++;
                    if(strncmp(p,";;",2)==0){p+=2;while(*p&&*p!='\n')p++;if(*p)p++;break;}
                    if(strncmp(p,"esac",4)==0) break;
                    while(*p&&*p!='\n'){if(bbi<(int)sizeof(body_buf)-1)body_buf[bbi++]=*p;p++;}
                    if(bbi<(int)sizeof(body_buf)-1)body_buf[bbi++]='\n';
                    if(*p=='\n')p++;
                }
                body_buf[bbi]='\0';
                if (this_match && execute) { last_ret = sh_run_string(body_buf); }
            }
            continue;
        }

        /* ---- break / continue / return ------------------------- */
        if (strcmp(logline,"break")==0)    { sh_flow=SH_FLOW_BREAK; *pp=p; return last_ret; }
        if (strcmp(logline,"continue")==0) { sh_flow=SH_FLOW_CONTINUE; *pp=p; return last_ret; }
        if (strncmp(logline,"return",6)==0) {
            if (!execute) continue;
            const char *rv = logline+6; while(*rv==' ')rv++;
            sh_return_val = *rv ? (int)strtol(rv,NULL,10) : last_ret;
            sh_flow = SH_FLOW_RETURN;
            *pp = p; return sh_return_val;
        }

        /* ---- && and || --------------------------------------- */
        /* Handle cmd1 && cmd2 / cmd1 || cmd2 on a single line */
        {
            char *and_pos = strstr(logline," && ");
            char *or_pos  = strstr(logline," || ");
            if (and_pos || or_pos) {
                /* find earliest */
                char *op = NULL; bool is_and = false;
                if (and_pos && (!or_pos || and_pos < or_pos)) { op=and_pos; is_and=true; }
                else { op=or_pos; is_and=false; }
                *op = '\0'; const char *rhs = op + 4;
                int r1 = execute ? sh_exec(logline) : 0;
                sh_last_status = r1;
                bool run_rhs = is_and ? (r1==0) : (r1!=0);
                last_ret = (execute && run_rhs) ? sh_exec(rhs) : r1;
                sh_last_status = last_ret;
                continue;
            }
        }

        /* ---- Expand and execute line --------------------------- */
        if (execute) {
            /* Full expand of the line */
            char expanded_line[SH_LINE_MAX];
            expand_word(logline, expanded_line, sizeof(expanded_line));
            last_ret = sh_run_pipeline(expanded_line);
            sh_last_status = last_ret;
        }
    }
    *pp = p;
    return last_ret;
}

/* ================================================================
   Public API
   ================================================================ */
int sh_run_string(const char *script) {
    if (!script) return 1;
    const char *p = script;
    sh_flow = SH_FLOW_NONE;
    return sh_exec_block(&p, true);
}

int sh_run_file(const char *path) {
    int fd = vfs_open(path, VFS_O_READ);
    if (fd < 0) { kprintf("sh: %s: not found\n", path); return 127; }
    char *buf = (char *)kmalloc(SH_LINE_MAX * 32);
    if (!buf) { vfs_close(fd); return 1; }
    int n = vfs_read(fd, buf, SH_LINE_MAX*32-1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    vfs_close(fd);
    /* skip shebang */
    const char *script = buf;
    if (script[0]=='#'&&script[1]=='!') { while(*script&&*script!='\n') script++; if(*script)script++; }
    sh_script_name = path;
    int ret = sh_run_string(script);
    kfree(buf);
    return ret;
}

int sh_exec(const char *cmdline) {
    if (!cmdline) return 0;
    char expanded[SH_LINE_MAX];
    expand_word(cmdline, expanded, sizeof(expanded));
    /* tokenize and run */
    char *argv[SH_MAX_ARGS];
    char strbuf[SH_LINE_MAX*2];
    int argc = tokenize(expanded, argv, SH_MAX_ARGS, strbuf, sizeof(strbuf));
    if (argc == 0) return 0;
    return sh_run_simple(argc, argv);
}

/* ================================================================
   Interactive sh/bash loop
   ================================================================ */
void sh_interactive(bool bash_mode) {
    char line[SH_LINE_MAX];
    bool multiline = false;
    static char mlbuf[SH_LINE_MAX*8]; int mli = 0; mlbuf[0] = '\0';

    /* Built-in default aliases for bash mode */
    if (bash_mode) {
        alias_set("ll",  "ls -l");
        alias_set("la",  "ls");
        alias_set("grep","grep");   /* placeholder */
        var_set("BASH_VERSION", "5.2.0(1)-vnl", false);
        var_set("PS1", "\\u@vnl:\\w\\$ ", false);
    } else {
        var_set("PS1", "$ ", false);
    }
    var_set("SHELL", bash_mode ? "/bin/bash" : "/bin/sh", false);
    var_set("PATH",  "/bin:/usr/bin", false);

    kprintf("%s interactive session. Type 'exit' to return.\n",
            bash_mode ? "bash" : "sh");

    while (1) {
        /* Prompt */
        if (!multiline) {
            const char *ps1 = var_get("PS1");
            if (!ps1) ps1 = bash_mode ? "bash$ " : "$ ";
            /* Expand \w in PS1 */
            char cwd[128]; vfs_getcwd(cwd, sizeof(cwd));
            char ps1_exp[128]; int pi=0;
            for (const char *c=ps1; *c && pi<127; c++) {
                if (*c=='\\' && c[1]=='w') { size_t wl=strlen(cwd); if(pi+wl<127){memcpy(ps1_exp+pi,cwd,wl);pi+=wl;} c++; }
                else if (*c=='\\' && c[1]=='u') { ps1_exp[pi++]='r'; c++; } /* root */
                else if (*c=='\\' && c[1]=='$') { ps1_exp[pi++]='#'; c++; }
                else ps1_exp[pi++]=*c;
            }
            ps1_exp[pi]='\0';
            vga_set_color(VGA_LCYAN, VGA_BLACK); kprintf("%s", ps1_exp);
            vga_set_color(VGA_WHITE, VGA_BLACK);
        } else {
            vga_set_color(VGA_LCYAN, VGA_BLACK); kprintf("> ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        }

        /* Read input */
        int li = 0;
        int k;
        while ((k = keyboard_getkey()) != '\n' && li < SH_LINE_MAX-1) {
            if (k == '\b') { if(li>0){li--;vga_putchar('\b');} }
            else if (k < KEY_UP) { line[li++]=(char)k; vga_putchar((char)k); }
        }
        line[li] = '\0'; vga_putchar('\n');

        if (strcmp(line,"exit")==0 || strcmp(line,"quit")==0) break;
        if (li == 0 && !multiline) continue;

        /* Check for line continuation */
        bool cont = (li > 0 && line[li-1] == '\\');
        if (cont) line[li-1] = '\n';

        if (mli + li + 1 < (int)sizeof(mlbuf)) {
            memcpy(mlbuf+mli, line, (size_t)li);
            mli += li;
            mlbuf[mli++] = '\n';
            mlbuf[mli]   = '\0';
        }

        if (cont) { multiline = true; continue; }
        multiline = false;

        int ret = sh_run_string(mlbuf);
        mli = 0; mlbuf[0] = '\0';
        sh_last_status = ret;
        (void)ret;
    }
}
const char *sh_getvar(const char *name) { return var_get(name); }
