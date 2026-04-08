#include "types.h"
#include "printf.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "pmm.h"
#include "string.h"
#include "cpu.h"
#include "heap.h"
#include "vfs.h"
#include "sched.h"
#include "pci.h"
#include "acpi.h"
#include "panic.h"
#include "sh.h"

#define CMD_MAX    256
#define ARG_MAX    16
#define HIST_MAX   32
#define SH_PIPE_BUF 4096

static char history[HIST_MAX][CMD_MAX];
static int  hist_count = 0;

static void push_history(const char *line) {
    if (!line[0]) return;
    if (hist_count > 0 && strcmp(history[(hist_count-1) % HIST_MAX], line) == 0) return;
    strncpy(history[hist_count % HIST_MAX], line, CMD_MAX-1);
    history[hist_count % HIST_MAX][CMD_MAX-1] = '\0';
    hist_count++;
}

static int readline(char *buf, int maxlen) {
    int len = 0, cur = 0;
    int hist_idx = hist_count;
    int start_row = vga_get_row();
    int start_col = vga_get_col();
    int limit = maxlen - 1;
    if (start_col + limit > 79) limit = 79 - start_col;

    while (1) {
        int k = keyboard_getkey();
        if (k == '\n') {
            vga_set_cursor(start_row, start_col + len);
            vga_putchar('\n');
            buf[len] = '\0';
            return len;
        }
        if (k == '\b') {
            if (cur > 0) {
                int old = len;
                memmove(buf+cur-1, buf+cur, (size_t)(len-cur));
                cur--; len--; buf[len] = '\0';
                vga_set_cursor(start_row, start_col);
                for (int i = 0; i < len; i++) vga_putchar(buf[i]);
                for (int i = len; i < old; i++) vga_putchar(' ');
                vga_set_cursor(start_row, start_col + cur);
            }
            continue;
        }
        if (k == KEY_DEL) {
            if (cur < len) {
                int old = len;
                memmove(buf+cur, buf+cur+1, (size_t)(len-cur-1));
                len--; buf[len] = '\0';
                vga_set_cursor(start_row, start_col);
                for (int i = 0; i < len; i++) vga_putchar(buf[i]);
                for (int i = len; i < old; i++) vga_putchar(' ');
                vga_set_cursor(start_row, start_col + cur);
            }
            continue;
        }
        if (k == KEY_LEFT)  { if (cur > 0)   { cur--;  vga_set_cursor(start_row, start_col+cur); } continue; }
        if (k == KEY_RIGHT) { if (cur < len)  { cur++;  vga_set_cursor(start_row, start_col+cur); } continue; }
        if (k == KEY_HOME)  { cur = 0;   vga_set_cursor(start_row, start_col);     continue; }
        if (k == KEY_END)   { cur = len; vga_set_cursor(start_row, start_col+len); continue; }
        if (k == KEY_UP) {
            if (hist_idx > 0) {
                hist_idx--;
                int old = len;
                strncpy(buf, history[hist_idx % HIST_MAX], (size_t)limit);
                buf[limit] = '\0'; len = cur = (int)strlen(buf);
                vga_set_cursor(start_row, start_col);
                for (int i = 0; i < len; i++) vga_putchar(buf[i]);
                for (int i = len; i < old; i++) vga_putchar(' ');
                vga_set_cursor(start_row, start_col+cur);
            }
            continue;
        }
        if (k == KEY_DOWN) {
            int old = len;
            if (hist_idx < hist_count) {
                hist_idx++;
                if (hist_idx == hist_count) { buf[0] = '\0'; len = cur = 0; }
                else { strncpy(buf, history[hist_idx % HIST_MAX], (size_t)limit); buf[limit]='\0'; len=cur=(int)strlen(buf); }
            }
            vga_set_cursor(start_row, start_col);
            for (int i = 0; i < len; i++) vga_putchar(buf[i]);
            for (int i = len; i < old; i++) vga_putchar(' ');
            vga_set_cursor(start_row, start_col+cur);
            continue;
        }
        if ((unsigned)k >= 32 && k < KEY_UP && len < limit) {
            int old = len;
            memmove(buf+cur+1, buf+cur, (size_t)(len-cur));
            buf[cur] = (char)k; cur++; len++; buf[len] = '\0';
            vga_set_cursor(start_row, start_col);
            for (int i = 0; i < len; i++) vga_putchar(buf[i]);
            for (int i = len; i < old; i++) vga_putchar(' ');
            vga_set_cursor(start_row, start_col+cur);
        }
    }
}

static int parse_args(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
        if (argc >= ARG_MAX-1) break;
    }
    argv[argc] = NULL;
    return argc;
}

static int64_t katoi(const char *s) {
    int64_t v = 0; int neg = 0;
    if (*s == '-') { neg=1; s++; }
    while (*s >= '0' && *s <= '9') v = v*10 + (*s++ - '0');
    return neg ? -v : v;
}

static void cmd_grep(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: grep <pattern> [file]\n"); return; }
    const char *pat = argv[1];
    const char *src = NULL;
    char filebuf[SH_PIPE_BUF];
    if (argc > 2) {
        int fd = vfs_open(argv[2], VFS_O_READ);
        if (fd < 0) { kprintf("grep: %s: not found\n", argv[2]); return; }
        int n = vfs_read(fd, filebuf, sizeof(filebuf)-1); if(n<0)n=0;
        filebuf[n] = '\0'; vfs_close(fd); src = filebuf;
    } else {
        /* read from pipe / STDIN_CONTENT */
        /* use sh_getvar() */
        const char *sc = sh_getvar("STDIN_CONTENT");
        src = sc ? sc : "";
    }
    /* Line-by-line search */
    const char *p = src;
    while (*p) {
        const char *line_s = p;
        while (*p && *p != '\n') p++;
        size_t llen = (size_t)(p - line_s);
        char linebuf[256]; if(llen>=256)llen=255;
        memcpy(linebuf, line_s, llen); linebuf[llen]='\0';
        if (strstr(linebuf, pat)) kprintf("%s\n", linebuf);
        if (*p == '\n') p++;
    }
}

static void cmd_wc(int argc, char **argv) {
    const char *src = NULL; char filebuf[SH_PIPE_BUF];
    if (argc > 1) {
        int fd = vfs_open(argv[1], VFS_O_READ);
        if (fd<0){kprintf("wc: %s: not found\n",argv[1]);return;}
        int n=vfs_read(fd,filebuf,sizeof(filebuf)-1);if(n<0)n=0;filebuf[n]='\0';vfs_close(fd);src=filebuf;
    } else { /* use sh_getvar() */ const char *sc=sh_getvar("STDIN_CONTENT"); src=sc?sc:""; }
    int lines=0,words=0,chars=0; bool inw=false;
    for(const char *c=src;*c;c++){chars++;if(*c=='\n')lines++;if(*c==' '||*c=='\t'||*c=='\n'){inw=false;}else if(!inw){words++;inw=true;}}
    kprintf("%d %d %d\n",lines,words,chars);
}

static void cmd_head(int argc, char **argv) {
    int n=10; const char *path=NULL;
    for(int i=1;i<argc;i++){if(strcmp(argv[i],"-n")==0&&i+1<argc){n=(int)strtol(argv[++i],NULL,10);}else path=argv[i];}
    const char *src=NULL; char filebuf[SH_PIPE_BUF];
    if(path){int fd=vfs_open(path,VFS_O_READ);if(fd<0){kprintf("head: not found\n");return;}int r=vfs_read(fd,filebuf,sizeof(filebuf)-1);if(r<0)r=0;filebuf[r]='\0';vfs_close(fd);src=filebuf;}
    else{/* use sh_getvar() */const char *sc=sh_getvar("STDIN_CONTENT");src=sc?sc:"";}
    const char *p=src; int ln=0;
    while(*p&&ln<n){const char *ls=p;while(*p&&*p!='\n')p++;size_t ll=(size_t)(p-ls);char lb[256];if(ll>=256)ll=255;memcpy(lb,ls,ll);lb[ll]='\0';kprintf("%s\n",lb);if(*p=='\n')p++;ln++;}
}

static void cmd_tail(int argc, char **argv) {
    int n=10; const char *path=NULL;
    for(int i=1;i<argc;i++){if(strcmp(argv[i],"-n")==0&&i+1<argc){n=(int)strtol(argv[++i],NULL,10);}else path=argv[i];}
    const char *src=NULL; char filebuf[SH_PIPE_BUF];
    if(path){int fd=vfs_open(path,VFS_O_READ);if(fd<0){kprintf("tail: not found\n");return;}int r=vfs_read(fd,filebuf,sizeof(filebuf)-1);if(r<0)r=0;filebuf[r]='\0';vfs_close(fd);src=filebuf;}
    else{/* use sh_getvar() */const char *sc=sh_getvar("STDIN_CONTENT");src=sc?sc:"";}
    /* count lines */
    int total=0; for(const char *p=src;*p;p++)if(*p=='\n')total++;
    int skip=total>n?total-n:0; const char *p=src; int ln2=0;
    while(*p){if(*p=='\n'){ln2++;if(ln2==skip){p++;break;}}p++;}
    kprintf("%s",p);
}

static void cmd_sort(int c, char **v) {
    (void)c;(void)v;
    /* use sh_getvar() */ const char *sc=sh_getvar("STDIN_CONTENT");
    if(!sc){return;}
    /* collect lines */
    char *lines[256]; int nl=0; char buf[SH_PIPE_BUF];
    strncpy(buf,sc,SH_PIPE_BUF-1);
    char *p=buf;
    while(*p&&nl<256){lines[nl++]=p;while(*p&&*p!='\n')p++;if(*p)*p++='\0';}
    /* bubble sort */
    for(int i=0;i<nl-1;i++)for(int j=i+1;j<nl;j++)if(strcmp(lines[i],lines[j])>0){char*t=lines[i];lines[i]=lines[j];lines[j]=t;}
    for(int i=0;i<nl;i++)kprintf("%s\n",lines[i]);
}

static void cmd_uniq(int c, char **v) {
    (void)c;(void)v;
    /* use sh_getvar() */ const char *sc=sh_getvar("STDIN_CONTENT");
    const char *src=sc?sc:""; const char *prev=""; const char *p=src;
    while(*p){const char *ls=p;while(*p&&*p!='\n')p++;size_t ll=(size_t)(p-ls);char lb[256];if(ll>=256)ll=255;memcpy(lb,ls,ll);lb[ll]='\0';if(strcmp(lb,prev)!=0){kprintf("%s\n",lb);}prev=lb;if(*p=='\n')p++;}
}

static void cmd_tr(int argc, char **argv) {
    if(argc<3){kprintf("Usage: tr <from> <to>\n");return;}
    /* use sh_getvar() */ const char *sc=sh_getvar("STDIN_CONTENT");
    const char *src=sc?sc:""; const char *from=argv[1],*to=argv[2];
    size_t flen=strlen(from),tlen=strlen(to);
    for(const char *c=src;*c;c++){
        char ch=*c; bool mapped=false;
        for(size_t i=0;i<flen;i++){if(ch==(char)from[i]){if(i<tlen)kprintf("%c",(char)to[i]);mapped=true;break;}}
        if(!mapped)kprintf("%c",ch);
    }
}

static void cmd_tee(int argc, char **argv) {
    /* use sh_getvar() */ const char *sc=sh_getvar("STDIN_CONTENT");
    const char *src=sc?sc:"";
    kprintf("%s",src);
    if(argc>1){int fd=vfs_open(argv[1],VFS_O_WRITE|VFS_O_CREATE|VFS_O_TRUNC);if(fd>=0){vfs_write(fd,src,strlen(src));vfs_close(fd);}}
}

static void cmd_test_cmd(int argc, char **argv) {
    /* 'test' without brackets, useful in scripts */
    (void)argc;(void)argv; /* handled by sh.c builtins */
}

static void cmd_help(int c, char **v)   { (void)c;(void)v;
    kprintf("Commands: help neofetch uname mem uptime clear echo color hello\n");
    kprintf("          ls cat write mkdir rm cd pwd\n");
    kprintf("          ps kill sleep lspci poweroff reboot panic halt\n");
    kprintf("          sh bash eval source\n");
    kprintf("          grep wc head tail sort uniq tr tee\n");
    kprintf("Shell: Variables, if/while/for, pipes, redirects, functions\n");
}
static void cmd_uname(int c, char **v)  { (void)c;(void)v; kprintf("VNL 0.2.0 (Vibe Not Linux) #1 SMP x86_64\n"); }
static void cmd_mem(int c, char **v)    { (void)c;(void)v;
    uint64_t fp=pmm_free_pages(), tp=pmm_total_pages(), up=tp-fp;
    kprintf("Total: %llu MiB  Used: %llu MiB  Free: %llu MiB\n",
        (tp*PAGE_SIZE)/(1024*1024),(up*PAGE_SIZE)/(1024*1024),(fp*PAGE_SIZE)/(1024*1024));
}
static void cmd_uptime(int c, char **v) { (void)c;(void)v;
    uint64_t t=timer_ticks(),s=t/1000,ms=t%1000,m=s/60; s%=60; uint64_t h=m/60; m%=60;
    kprintf("Uptime: %llu:%02llu:%02llu.%03llu\n",h,m,s,ms);
}
static void cmd_clear(int c, char **v)  { (void)c;(void)v; vga_clear(); }
static void cmd_echo(int argc, char **argv) {
    for (int i=1; i<argc; i++) { kprintf("%s", argv[i]); if (i<argc-1) kprintf(" "); }
    kprintf("\n");
}
static void cmd_color(int argc, char **argv) {
    if (argc<3) { kprintf("Usage: color <fg> <bg>\n"); return; }
    vga_set_color((VGAColor)(katoi(argv[1])&0xF), (VGAColor)(katoi(argv[2])&0xF));
}
static void cmd_hello(int c, char **v) { (void)c;(void)v;
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  ____   ____  _   _ _     \n");
    kprintf(" / ___| / ___|| | | | |    \n");
    kprintf(" \\___ \\| |    | |_| | |    \n");
    kprintf("  ___) | |___ |  _  | |___ \n");
    kprintf(" |____/ \\____||_| |_|_____|\n");
    kprintf("\n  Vibe Not Linux — It's a vibe.\n\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

/*
 * Neofetch-style info layout; tall sharp slash V (\\\ vs ///), reads cleanly on VGA.
 */
#define NF_LOGO_W 32
typedef enum { NF_V_ARM, NF_V_ZIG, NF_V_DBL, NF_V_TIP } NfVKind;
typedef struct { int lp, bs, gap, fs; NfVKind kind; } NfVLine;

static int nf_v_row_width(const NfVLine *L) {
    switch (L->kind) {
    case NF_V_ARM: return L->lp + L->bs + L->gap + L->fs;
    case NF_V_ZIG: return L->lp + L->bs + 4;
    case NF_V_DBL: return L->lp + L->bs + 2;
    case NF_V_TIP: return L->lp + 2;
    default:       return 0;
    }
}

static void nf_emit_v_row(const NfVLine *L) {
    for (int i = 0; i < L->lp; i++) kprintf(" ");
    for (int i = 0; i < L->bs; i++) kprintf("\\");
    switch (L->kind) {
    case NF_V_ARM:
        for (int i = 0; i < L->gap; i++) kprintf(" ");
        for (int i = 0; i < L->fs; i++) kprintf("/");
        break;
    case NF_V_ZIG:
        kprintf("\\/\\/");
        break;
    case NF_V_DBL:
        kprintf("//");
        break;
    case NF_V_TIP:
        kprintf("\\/");
        break;
    }
}

static void nf_logo_pad_w(int w) {
    while (w < NF_LOGO_W) {
        kprintf(" ");
        w++;
    }
}

static void cmd_neofetch(int argc, char **argv) {
    (void)argc;
    (void)argv;
    /* Narrow + tall: gap −2 per row; 5-wide stroke reads sharper than 6 on 80×25 */
    static const NfVLine nf_v[] = {
        {0, 5, 20, 5, NF_V_ARM},
        {2, 5, 18, 5, NF_V_ARM},
        {4, 5, 16, 5, NF_V_ARM},
        {6, 5, 14, 5, NF_V_ARM},
        {8, 5, 12, 5, NF_V_ARM},
        {10, 5, 10, 5, NF_V_ARM},
        {12, 5, 8, 5, NF_V_ARM},
        {14, 5, 6, 5, NF_V_ARM},
        {16, 5, 4, 5, NF_V_ARM},
        {18, 5, 2, 5, NF_V_ARM},
        {20, 4, 0, 0, NF_V_ZIG},
        {22, 3, 0, 0, NF_V_DBL},
        {24, 2, 0, 0, NF_V_DBL},
        {26, 0, 0, 0, NF_V_TIP},
    };
    const int logo_n = (int)(sizeof(nf_v) / sizeof(nf_v[0]));
    static const VGAColor logo_fg[] = {
        VGA_LCYAN, VGA_LCYAN, VGA_LCYAN, VGA_LBLUE, VGA_LBLUE,
        VGA_LGREEN, VGA_LMAGENTA, VGA_LMAGENTA, VGA_LRED, VGA_YELLOW,
        VGA_YELLOW, VGA_WHITE, VGA_LGREEN, VGA_WHITE,
    };

    uint64_t sec = timer_ticks() / 1000;
    uint64_t d = sec / 86400;
    sec %= 86400;
    uint64_t h = sec / 3600;
    sec %= 3600;
    uint64_t m = sec / 60;

    char uptline[80];
    ksprintf(uptline, sizeof(uptline),
        "%llu day%s, %llu hour%s, %llu mins",
        (unsigned long long)d, (d == 1 ? "" : "s"),
        (unsigned long long)h, (h == 1 ? "" : "s"),
        (unsigned long long)m);

    uint64_t fp = pmm_free_pages(), tp = pmm_total_pages(), up = tp - fp;
    unsigned mem_pct = tp ? (unsigned)((up * 100) / tp) : 0;
    char memline[80];
    ksprintf(memline, sizeof(memline), "%llu MiB / %llu MiB (%u%%)",
        (unsigned long long)((up * PAGE_SIZE) / (1024 * 1024)),
        (unsigned long long)((tp * PAGE_SIZE) / (1024 * 1024)),
        mem_pct);

    const int info_n = 19;
    for (int row = 0; row < info_n; row++) {
        if (row < logo_n) {
            vga_set_color(logo_fg[row], VGA_BLACK);
            nf_emit_v_row(&nf_v[row]);
            vga_set_color(VGA_WHITE, VGA_BLACK);
            nf_logo_pad_w(nf_v_row_width(&nf_v[row]));
        } else {
            kprintf("%*s", NF_LOGO_W, "");
        }
        kprintf("   ");

        switch (row) {
        case 0:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("vnl@vnl\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            break;
        case 1:
            kprintf("-------\n");
            break;
        case 2:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("OS: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("Vibe Not Linux x86_64\n");
            break;
        case 3:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Host: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("VNL PC\n");
            break;
        case 4:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Kernel: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("VNL 0.2.0 #1 SMP x86_64\n");
            break;
        case 5:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Uptime: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("%s\n", uptline);
            break;
        case 6:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Packages: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("N/A\n");
            break;
        case 7:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Shell: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("sh\n");
            break;
        case 8:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Resolution: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("80x25\n");
            break;
        case 9:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("DE: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("N/A\n");
            break;
        case 10:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("WM: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("N/A\n");
            break;
        case 11:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("WM Theme: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("N/A\n");
            break;
        case 12:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Theme: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("N/A\n");
            break;
        case 13:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Icons: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("N/A\n");
            break;
        case 14:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Terminal: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("VGA text\n");
            break;
        case 15:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Terminal Font: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("kernel 8x16\n");
            break;
        case 16:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("CPU: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("x86_64\n");
            break;
        case 17:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("GPU: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("N/A\n");
            break;
        case 18:
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            kprintf("Memory: ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            kprintf("%s\n", memline);
            break;
        default:
            kprintf("\n");
            break;
        }
    }

    /* `info cols` — classic neofetch 16-color block row (first 8 entries) */
    kprintf("%*s   ", NF_LOGO_W, "");
    static const VGAColor palette[] = {
        VGA_BLACK, VGA_RED, VGA_GREEN, VGA_YELLOW,
        VGA_BLUE, VGA_MAGENTA, VGA_CYAN, VGA_WHITE,
    };
    for (size_t i = 0; i < sizeof(palette) / sizeof(palette[0]); i++) {
        vga_set_color(palette[i], palette[i]);
        kprintf("  ");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("\n\n");
}
static void cmd_ls(int argc, char **argv) {
    const char *path = (argc>1) ? argv[1] : ".";
    char names[VFS_MAX_NODES][VFS_NAME_MAX];
    int n = vfs_readdir(path, names, VFS_MAX_NODES);
    if (n < 0) { kprintf("ls: %s: no such directory\n", path); return; }
    for (int i=0; i<n; i++) kprintf("%s\n", names[i]);
}
static void cmd_cat(int argc, char **argv) {
    if (argc<2) { kprintf("Usage: cat <file>\n"); return; }
    int fd = vfs_open(argv[1], VFS_O_READ);
    if (fd<0) { kprintf("cat: %s: not found\n", argv[1]); return; }
    char buf[256]; int n;
    while ((n=vfs_read(fd,buf,sizeof(buf)-1))>0) { buf[n]='\0'; kprintf("%s",buf); }
    vfs_close(fd); kprintf("\n");
}
static void cmd_write(int argc, char **argv) {
    if (argc<3) { kprintf("Usage: write <file> <text...>\n"); return; }
    int fd = vfs_open(argv[1], VFS_O_WRITE|VFS_O_CREATE|VFS_O_TRUNC);
    if (fd<0) { kprintf("write: cannot open %s\n", argv[1]); return; }
    for (int i=2; i<argc; i++) { vfs_write(fd,argv[i],strlen(argv[i])); if (i<argc-1) vfs_write(fd," ",1); }
    vfs_write(fd,"\n",1); vfs_close(fd);
}
static void cmd_mkdir(int argc, char **argv) {
    if (argc<2) { kprintf("Usage: mkdir <dir>\n"); return; }
    if (vfs_mkdir(argv[1])<0) kprintf("mkdir: failed\n");
}
static void cmd_rm(int argc, char **argv) {
    if (argc<2) { kprintf("Usage: rm <path>\n"); return; }
    if (vfs_unlink(argv[1])<0) kprintf("rm: failed\n");
}
static void cmd_cd(int argc, char **argv) {
    const char *path = (argc>1) ? argv[1] : "/";
    int inode = vfs_resolve(path);
    if (inode<0) { kprintf("cd: %s: not found\n", path); return; }
    VFSNodeType t; vfs_stat(path,&t,NULL);
    if (t!=VFS_DIR) { kprintf("cd: not a directory\n"); return; }
    vfs_set_cwd((uint32_t)inode);
}
static void cmd_pwd(int c, char **v) { (void)c;(void)v; char buf[256]; kprintf("%s\n", vfs_getcwd(buf,sizeof(buf))); }
static void cmd_ps(int c, char **v) { (void)c;(void)v;
    static const char *st[]={"RUN","RDY","SLP","DED"};
    kprintf("PID  STATE  NAME\n");
    for (int i=0; i<sched_task_count(); i++) {
        Task *t=sched_get_task(i);
        if (t) kprintf("%-4u %-6s %s\n", t->pid, st[t->state], t->name);
    }
}
static void cmd_kill(int argc, char **argv) {
    if (argc<2) { kprintf("Usage: kill <pid>\n"); return; }
    uint32_t pid=(uint32_t)katoi(argv[1]);
    for (int i=0; i<sched_task_count(); i++) {
        Task *t=sched_get_task(i);
        if (t && t->pid==pid) {
            if (t==sched_current()) { kprintf("kill: cannot kill self\n"); return; }
            t->state=TASK_DEAD;
            kprintf("Killed pid %u\n", pid); return;
        }
    }
    kprintf("kill: pid %u not found\n", pid);
}
static void cmd_sleep(int argc, char **argv) {
    if (argc<2) { kprintf("Usage: sleep <ms>\n"); return; }
    task_sleep((uint64_t)katoi(argv[1]));
}
static void cmd_lspci(int c, char **v) { (void)c;(void)v;
    int n=pci_dev_count();
    if (!n) { kprintf("No PCI devices.\n"); return; }
    for (int i=0; i<n; i++) {
        const PCIDevice *d=pci_get_dev(i);
        kprintf("%02x:%02x.%x %04x:%04x [%02x.%02x] %s\n",
            d->bus,d->dev,d->func,d->vendor,d->device_id,d->class_code,d->subclass,d->desc);
    }
}
static void cmd_poweroff(int c, char **v) { (void)c;(void)v; kprintf("Shutting down...\n"); acpi_shutdown(); }
static void cmd_reboot(int c, char **v) { (void)c;(void)v;
    kprintf("Rebooting...\n");
    uint8_t g=0x02; while (g&0x02) g=inb(0x64); outb(0x64,0xFE); halt_loop();
}
static void cmd_panic(int argc, char **argv) {
    kpanic("%s", argc>1 ? argv[1] : "Manual panic");
}
static void cmd_halt(int c, char **v) { (void)c;(void)v; kprintf("Halting.\n"); halt_loop(); }

static void cmd_sh(int argc, char **argv) {
    if (argc > 1) sh_run_file(argv[1]);
    else          sh_interactive(false);
}
static void cmd_bash(int argc, char **argv) {
    if (argc > 1) sh_run_file(argv[1]);
    else          sh_interactive(true);
}
/* Run a string as sh */
static void cmd_eval(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: eval <script>\n"); return; }
    char script[CMD_MAX*4]; script[0]='\0';
    for (int i=1; i<argc; i++) { strncat(script,argv[i],sizeof(script)-strlen(script)-1); if(i<argc-1) strncat(script," ",sizeof(script)-strlen(script)-1); }
    sh_run_string(script);
}
/* Execute shell command (dot/source) */
static void cmd_source(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: source <file>\n"); return; }
    sh_run_file(argv[1]);
}

typedef struct { const char *name; void (*fn)(int, char**); } Command;
static const Command cmds[] = {
    {"help",cmd_help},{"neofetch",cmd_neofetch},{"uname",cmd_uname},{"mem",cmd_mem},{"uptime",cmd_uptime},
    {"clear",cmd_clear},{"echo",cmd_echo},{"color",cmd_color},{"hello",cmd_hello},
    {"ls",cmd_ls},{"cat",cmd_cat},{"write",cmd_write},{"mkdir",cmd_mkdir},
    {"rm",cmd_rm},{"cd",cmd_cd},{"pwd",cmd_pwd},{"ps",cmd_ps},{"kill",cmd_kill},
    {"sleep",cmd_sleep},{"lspci",cmd_lspci},{"poweroff",cmd_poweroff},
    {"reboot",cmd_reboot},{"panic",cmd_panic},{"halt",cmd_halt},
    {"sh",cmd_sh},{"bash",cmd_bash},{"eval",cmd_eval},{"source",cmd_source},
    {".",cmd_source},{"grep",cmd_grep},{"wc",cmd_wc},{"head",cmd_head},
    {"tail",cmd_tail},{"sort",cmd_sort},{"uniq",cmd_uniq},{"tr",cmd_tr},
    {"tee",cmd_tee},{"test",cmd_test_cmd},{NULL,NULL}
};

/* ---- Public: called by sh.c to dispatch built-in commands -------- */
int shell_exec_builtin(int argc, char **argv) {
    if (argc == 0) return 0;
    for (const Command *c = cmds; c->name; c++) {
        if (strcmp(argv[0], c->name) == 0) {
            c->fn(argc, argv);
            return sh_last_status;
        }
    }
    kprintf("sh: %s: command not found\n", argv[0]);
    return 127;
}

void shell_run(void) {
    char line[CMD_MAX]; char *argv[ARG_MAX];
    vga_set_color(VGA_LGREEN, VGA_BLACK);
    kprintf("VNL v0.2.0 — type 'help' for commands\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    while (1) {
        char cwd[128]; vfs_getcwd(cwd, sizeof(cwd));
        vga_set_color(VGA_LGREEN, VGA_BLACK); kprintf("vnl:%s", cwd);
        vga_set_color(VGA_WHITE, VGA_BLACK);  kprintf("# ");
        if (readline(line, CMD_MAX) == 0) continue;
        push_history(line);
        int argc = parse_args(line, argv);
        if (argc == 0) continue;
        bool found = false;
        for (const Command *c = cmds; c->name; c++) {
            if (strcmp(argv[0], c->name) == 0) { c->fn(argc, argv); found = true; break; }
        }
        if (!found) {
            /* Try as a shell expression (VAR=val, etc.) */
            int r = sh_exec(line);
            if (r == 127) kprintf("vnl: %s: command not found\n", argv[0]);
        }
    }
}
