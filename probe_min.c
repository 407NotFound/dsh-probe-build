/* probe_min.c —— 无任何头文件依赖,交叉编译到 arm64-apple-ios */
typedef void *id;
typedef void *SEL;
typedef void *Class;
typedef signed char BOOL;
typedef unsigned short u16;
typedef int i32;
typedef unsigned long usize;
typedef unsigned long long u64;

extern void *objc_msgSend(void);
extern SEL sel_registerName(const char *);
extern Class objc_getClass(const char *);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern void *malloc(usize);
extern void *memset(void *, int, usize);
extern void *memcpy(void *, const void *, usize);
extern int strcmp(const char *, const char *);
extern char *strncpy(char *, const char *, usize);
extern int snprintf(char *, usize, const char *, ...);

typedef u64 dispatch_time_t;
typedef void *dispatch_queue_t;
extern dispatch_time_t dispatch_time(dispatch_time_t, long long);
extern void dispatch_after(dispatch_time_t, dispatch_queue_t, void (^)(void));
extern dispatch_queue_t dispatch_get_main_queue(void);

#define RTLD_NOW 2
#define RTLD_DEFAULT ((void *)-2)
#define NSEC 1000000000ULL

typedef void *(*fn_void_v)(void);
typedef void *(*fn_p_v)(void *);
typedef const void **(*fn_as_v)(void *, usize *);
typedef void *(*fn_p_p)(const void *);
typedef const char *(*fn_c_p)(void *);
typedef void *(*fn_p_cc)(void *, const char *, const char *);
typedef void *(*fn_p_ci)(void *, const char *, int);
typedef void *(*fn_p_i)(void *);
typedef void *(*fn_inv)(void *, void *, void **, void **);

struct str64 { void *klass; void *monitor; i32 length; u16 chars[1]; };
struct arr64 { void *klass; void *monitor; void *bounds; usize max_length; void *vector[1]; };
struct rect { double x, y, w, h; };

static fn_void_v p_domain_get;
static fn_p_v p_thread_attach;
static fn_as_v p_assemblies;
static fn_p_p p_image_of;
static fn_c_p p_image_name;
static fn_p_cc p_class_from_name;
static fn_p_ci p_method_from_name;
static fn_p_i p_class_type;
static fn_p_i p_type_object;
static fn_inv p_invoke;

static int g_done = 0;
static char g_names[512][64];
static int g_counts[512];
static int g_n = 0;

#define SELN(n) sel_registerName(n)
#define SEND0(o, n) ((id (*)(id, SEL))objc_msgSend)((id)(o), SELN(n))

static void add_one(const char *s) {
    int i;
    for (i = 0; i < g_n; i++) {
        if (strcmp(g_names[i], s) == 0) { g_counts[i]++; return; }
    }
    if (g_n < 512) {
        strncpy(g_names[g_n], s, 63);
        g_names[g_n][63] = 0;
        g_counts[g_n] = 1;
        g_n++;
    }
}

static void u16_to_ascii(const u16 *src, int len, char *dst, int cap) {
    int i, o = 0;
    for (i = 0; i < len && o < cap - 2; i++) {
        u16 c = src[i];
        dst[o++] = (c < 128) ? (char)c : '?';
    }
    dst[o] = 0;
}

static void *find_img(void *dom, const char *want) {
    usize n = 0, i;
    const void **as = p_assemblies(dom, &n);
    for (i = 0; i < n; i++) {
        void *img = p_image_of(as[i]);
        const char *nm = p_image_name(img);
        if (nm && strcmp(nm, want) == 0) return img;
    }
    return 0;
}

static void show(const char *text) {
    Class cWin = objc_getClass("UIWindow");
    Class cVC = objc_getClass("UIViewController");
    Class cTV = objc_getClass("UITextView");
    Class cStr = objc_getClass("NSString");
    Class cCol = objc_getClass("UIColor");
    Class cScr = objc_getClass("UIScreen");
    id ns, screen, win, vc, tv;
    struct rect b;
    if (!cWin || !cVC || !cTV || !cStr) return;

    ns = ((id (*)(id, SEL, const char *))objc_msgSend)((id)cStr, SELN("stringWithUTF8String:"), text);
    screen = SEND0(cScr, "mainScreen");
    b = ((struct rect (*)(id, SEL))objc_msgSend)(screen, SELN("bounds"));

    win = ((id (*)(id, SEL, struct rect))objc_msgSend)(SEND0((id)cWin, "alloc"), SELN("initWithFrame:"), b);
    ((void (*)(id, SEL, double))objc_msgSend)(win, SELN("setWindowLevel:"), 2010.0);
    vc = SEND0(SEND0((id)cVC, "alloc"), "init");
    tv = ((id (*)(id, SEL, struct rect))objc_msgSend)(SEND0((id)cTV, "alloc"), SELN("initWithFrame:"), b);
    if (cCol) {
        id bg = ((id (*)(id, SEL, double, double))objc_msgSend)((id)cCol, SELN("colorWithWhite:alpha:"), 0.0, 0.88);
        id fg = ((id (*)(id, SEL, double, double, double, double))objc_msgSend)((id)cCol, SELN("colorWithRed:green:blue:alpha:"), 0.2, 1.0, 0.3, 1.0);
        ((void (*)(id, SEL, id))objc_msgSend)(tv, SELN("setBackgroundColor:"), bg);
        ((void (*)(id, SEL, id))objc_msgSend)(tv, SELN("setTextColor:"), fg);
    }
    ((void (*)(id, SEL, id))objc_msgSend)(tv, SELN("setText:"), ns);
    ((void (*)(id, SEL, BOOL))objc_msgSend)(tv, SELN("setEditable:"), 0);
    ((void (*)(id, SEL, id))objc_msgSend)(vc, SELN("setView:"), tv);
    ((void (*)(id, SEL, id))objc_msgSend)(win, SELN("setRootViewController:"), vc);
    ((void (*)(id, SEL, BOOL))objc_msgSend)(win, SELN("setHidden:"), 0);
}

static void run_probe(void) {
    void *dom, *img, *k_obj, *k_go, *m_find, *m_name, *type_go, *exc = 0;
    void *args[1];
    struct arr64 *arr;
    usize i, len;
    char *out;
    int off, shown, best, k;

    if (g_done) return;
    dom = p_domain_get ? p_domain_get() : 0;
    if (!dom) return;
    p_thread_attach(dom);

    img = find_img(dom, "UnityEngine.CoreModule");
    if (!img) img = find_img(dom, "UnityEngine");
    if (!img) return;

    k_obj = p_class_from_name(img, "UnityEngine", "Object");
    k_go = p_class_from_name(img, "UnityEngine", "GameObject");
    if (!k_obj || !k_go) return;

    m_find = p_method_from_name(k_obj, "FindObjectsOfType", 1);
    m_name = p_method_from_name(k_obj, "get_name", 0);
    if (!m_find || !m_name) return;

    type_go = p_type_object(p_class_type(k_go));
    args[0] = type_go;
    arr = (struct arr64 *)p_invoke(m_find, 0, args, &exc);
    if (!arr || exc) return;

    len = arr->max_length;
    if (len > 20000) len = 20000;
    for (i = 0; i < len; i++) {
        void *o = arr->vector[i], *e2 = 0, *s;
        struct str64 *st;
        char buf[64];
        if (!o) continue;
        s = p_invoke(m_name, o, 0, &e2);
        if (!s) continue;
        st = (struct str64 *)s;
        if (st->length <= 0 || st->length > 60) continue;
        u16_to_ascii(st->chars, st->length, buf, 64);
        add_one(buf);
    }

    out = (char *)malloc(96 * 1024);
    if (!out) { g_done = 1; return; }
    off = snprintf(out, 96 * 1024, "GameObjects=%lu unique=%d\n\n", len, g_n);
    shown = 0;
    while (shown < 60 && shown < g_n) {
        best = -1;
        for (k = 0; k < g_n; k++) {
            if (g_counts[k] < 0) continue;
            if (best < 0 || g_counts[k] > g_counts[best]) best = k;
        }
        if (best < 0) break;
        off += snprintf(out + off, 96 * 1024 - off, "%4d %s\n", g_counts[best], g_names[best]);
        g_counts[best] = -1;
        shown++;
    }
    dispatch_after(0, dispatch_get_main_queue(), ^{ show(out); });
    g_done = 1;
}

__attribute__((constructor))
static void probe_init(void) {
    void *u = dlopen("/usr/lib/libSystem.B.dylib", RTLD_NOW);
    int i;
    (void)u;
    p_domain_get = (fn_void_v)dlsym(RTLD_DEFAULT, "il2cpp_domain_get");
    p_thread_attach = (fn_p_v)dlsym(RTLD_DEFAULT, "il2cpp_thread_attach");
    p_assemblies = (fn_as_v)dlsym(RTLD_DEFAULT, "il2cpp_domain_get_assemblies");
    p_image_of = (fn_p_p)dlsym(RTLD_DEFAULT, "il2cpp_assembly_get_image");
    p_image_name = (fn_c_p)dlsym(RTLD_DEFAULT, "il2cpp_image_get_name");
    p_class_from_name = (fn_p_cc)dlsym(RTLD_DEFAULT, "il2cpp_class_from_name");
    p_method_from_name = (fn_p_ci)dlsym(RTLD_DEFAULT, "il2cpp_class_get_method_from_name");
    p_class_type = (fn_p_i)dlsym(RTLD_DEFAULT, "il2cpp_class_get_type");
    p_type_object = (fn_p_i)dlsym(RTLD_DEFAULT, "il2cpp_type_get_object");
    p_invoke = (fn_inv)dlsym(RTLD_DEFAULT, "il2cpp_runtime_invoke");
    if (!p_domain_get || !p_invoke || !p_type_object) return;

    for (i = 0; i < 30; i++) {
        dispatch_after(dispatch_time(0, 3ULL * NSEC), dispatch_get_main_queue(), ^{ run_probe(); });
    }
}
