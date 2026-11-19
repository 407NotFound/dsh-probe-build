// 场景探测:纯 C,objc_msgSend 直连 UIKit,列出所有 GameObject 名字与数量
#include <objc/message.h>
#include <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*fn_domain_get)(void);
typedef void *(*fn_thread_attach)(void *domain);
typedef const void **(*fn_domain_get_assemblies)(void *domain, size_t *size);
typedef void *(*fn_assembly_get_image)(const void *assembly);
typedef const char *(*fn_image_get_name)(void *image);
typedef void *(*fn_class_from_name)(void *image, const char *ns, const char *name);
typedef void *(*fn_method_from_name)(void *klass, const char *name, int argc);
typedef void *(*fn_class_get_type)(void *klass);
typedef void *(*fn_type_get_object)(void *type);
typedef void *(*fn_runtime_invoke)(void *method, void *obj, void **params, void **exc);

struct Il2CppString { void *klass; void *monitor; int32_t length; uint16_t chars[1]; };
struct Il2CppArray { void *klass; void *monitor; void *bounds; uintptr_t max_length; void *vector[1]; };

static fn_domain_get p_domain_get;
static fn_thread_attach p_thread_attach;
static fn_domain_get_assemblies p_assemblies;
static fn_assembly_get_image p_image_of;
static fn_image_get_name p_image_name;
static fn_class_from_name p_class_from_name;
static fn_method_from_name p_method_from_name;
static fn_class_get_type p_class_type;
static fn_type_get_object p_type_object;
static fn_runtime_invoke p_invoke;

static int g_done = 0;

#define S(n) sel_registerName(n)
#define SEND0(o, n) ((id (*)(id, SEL))objc_msgSend)((id)(o), S(n))

struct entry { char name[96]; int count; };
static struct entry g_entries[6000];
static int g_entry_n = 0;

static void add_entry(const char *name) {
    for (int i = 0; i < g_entry_n; i++) {
        if (strcmp(g_entries[i].name, name) == 0) { g_entries[i].count++; return; }
    }
    if (g_entry_n < 6000) {
        strncpy(g_entries[g_entry_n].name, name, sizeof(g_entries[0].name) - 1);
        g_entries[g_entry_n].count = 1;
        g_entry_n++;
    }
}

static void utf16_to_utf8(const uint16_t *src, int len, char *dst, int dstsz) {
    int o = 0;
    for (int i = 0; i < len && o < dstsz - 4; i++) {
        uint16_t c = src[i];
        if (c < 0x80) dst[o++] = (char)c;
        else if (c < 0x800) {
            dst[o++] = (char)(0xC0 | (c >> 6));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            dst[o++] = (char)(0xE0 | (c >> 12));
            dst[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    dst[o] = 0;
}

static void *find_image(void *domain, const char *wanted) {
    size_t n = 0;
    const void **as = p_assemblies(domain, &n);
    for (size_t i = 0; i < n; i++) {
        void *img = p_image_of(as[i]);
        const char *nm = p_image_name(img);
        if (nm && strcmp(nm, wanted) == 0) return img;
    }
    return NULL;
}

static void show_text(const char *text) {
    Class cWin = objc_getClass("UIWindow");
    Class cVC = objc_getClass("UIViewController");
    Class cTV = objc_getClass("UITextView");
    Class cStr = objc_getClass("NSString");
    Class cColor = objc_getClass("UIColor");
    Class cScreen = objc_getClass("UIScreen");
    if (!cWin || !cVC || !cTV || !cStr) return;

    id ns = ((id (*)(id, SEL, const char *))objc_msgSend)((id)cStr, S("stringWithUTF8String:"), text);
    id screen = SEND0(cScreen, "mainScreen");
    struct { double x, y, w, h; } bounds;
    bounds = ((struct { double x, y, w, h; } (*)(id, SEL))objc_msgSend)(screen, S("bounds"));

    id win = ((id (*)(id, SEL, struct { double x, y, w, h; }))objc_msgSend)(
        SEND0((id)cWin, "alloc"), S("initWithFrame:"), bounds);
    ((void (*)(id, SEL, double))objc_msgSend)(win, S("setWindowLevel:"), 2010.0);
    id vc = SEND0(SEND0((id)cVC, "alloc"), "init");
    id tv = ((id (*)(id, SEL, struct { double x, y, w, h; }))objc_msgSend)(
        SEND0((id)cTV, "alloc"), S("initWithFrame:"), bounds);
    if (cColor) {
        id bg = ((id (*)(id, SEL, double, double))objc_msgSend)((id)cColor, S("colorWithWhite:alpha:"), 0.0, 0.88);
        ((void (*)(id, SEL, id))objc_msgSend)(tv, S("setBackgroundColor:"), bg);
        id fg = ((id (*)(id, SEL, double, double, double, double))objc_msgSend)((id)cColor,
                 S("colorWithRed:green:blue:alpha:"), 0.2, 1.0, 0.3, 1.0);
        ((void (*)(id, SEL, id))objc_msgSend)(tv, S("setTextColor:"), fg);
    }
    ((void (*)(id, SEL, id))objc_msgSend)(tv, S("setText:"), ns);
    ((void (*)(id, SEL, signed char))objc_msgSend)(tv, S("setEditable:"), 0);
    ((void (*)(id, SEL, id))objc_msgSend)(vc, S("setView:"), tv);
    ((void (*)(id, SEL, id))objc_msgSend)(win, S("setRootViewController:"), vc);
    ((void (*)(id, SEL, signed char))objc_msgSend)(win, S("setHidden:"), 0);
}

static void probe_run(void) {
    if (g_done) return;
    void *domain = p_domain_get ? p_domain_get() : NULL;
    if (!domain) return;
    p_thread_attach(domain);

    void *img = find_image(domain, "UnityEngine.CoreModule");
    if (!img) img = find_image(domain, "UnityEngine");
    if (!img) return;

    void *kObject = p_class_from_name(img, "UnityEngine", "Object");
    void *kGO = p_class_from_name(img, "UnityEngine", "GameObject");
    if (!kObject || !kGO) return;

    void *mFind = p_method_from_name(kObject, "FindObjectsOfType", 1);
    void *mName = p_method_from_name(kObject, "get_name", 0);
    if (!mFind || !mName) return;

    void *typeGO = p_type_object(p_class_type(kGO));
    void *args[1] = { typeGO };
    void *exc = NULL;
    struct Il2CppArray *arr = (struct Il2CppArray *)p_invoke(mFind, NULL, args, &exc);
    if (!arr || exc) return;

    uintptr_t len = arr->max_length;
    if (len > 30000) len = 30000;
    static char nmbuf[256];
    for (uintptr_t i = 0; i < len; i++) {
        void *obj = arr->vector[i];
        if (!obj) continue;
        void *e2 = NULL;
        void *nmObj = p_invoke(mName, obj, NULL, &e2);
        if (!nmObj) continue;
        struct Il2CppString *s = (struct Il2CppString *)nmObj;
        if (s->length <= 0 || s->length > 200) continue;
        utf16_to_utf8(s->chars, s->length, nmbuf, sizeof(nmbuf));
        add_entry(nmbuf);
    }

    char *out = malloc(600 * 1024);
    if (!out) { g_done = 1; return; }
    int off = snprintf(out, 600 * 1024, "GameObjects=%lu  unique=%d\n\n", (unsigned long)len, g_entry_n);
    int shown = 0;
    char used[6000];
    memset(used, 0, sizeof(used));
    while (shown < 160 && shown < g_entry_n) {
        int best = -1;
        for (int i = 0; i < g_entry_n; i++) {
            if (used[i]) continue;
            if (best < 0 || g_entries[i].count > g_entries[best].count) best = i;
        }
        if (best < 0) break;
        used[best] = 1;
        off += snprintf(out + off, 600 * 1024 - off, "%5d  %s\n", g_entries[best].count, g_entries[best].name);
        if (off > 600 * 1024 - 512) break;
        shown++;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        show_text(out);
    });
    g_done = 1;
}

__attribute__((constructor))
static void probe_init(void) {
    void *u = dlopen("@rpath/UnityFramework.framework/UnityFramework", RTLD_NOW);
    if (!u) u = RTLD_DEFAULT;
    p_domain_get = (fn_domain_get)dlsym(u, "il2cpp_domain_get");
    p_thread_attach = (fn_thread_attach)dlsym(u, "il2cpp_thread_attach");
    p_assemblies = (fn_domain_get_assemblies)dlsym(u, "il2cpp_domain_get_assemblies");
    p_image_of = (fn_assembly_get_image)dlsym(u, "il2cpp_assembly_get_image");
    p_image_name = (fn_image_get_name)dlsym(u, "il2cpp_image_get_name");
    p_class_from_name = (fn_class_from_name)dlsym(u, "il2cpp_class_from_name");
    p_method_from_name = (fn_method_from_name)dlsym(u, "il2cpp_class_get_method_from_name");
    p_class_type = (fn_class_get_type)dlsym(u, "il2cpp_class_get_type");
    p_type_object = (fn_type_get_object)dlsym(u, "il2cpp_type_get_object");
    p_invoke = (fn_runtime_invoke)dlsym(u, "il2cpp_runtime_invoke");
    if (!p_domain_get || !p_invoke || !p_type_object) return;

    for (int i = 0; i < 40; i++) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3ull * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{ probe_run(); });
    }
}
