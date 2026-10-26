#define _GNU_SOURCE
#include "runtime.h"
#include <stdlib.h>

/* The settings menu of the GPU library restarts through probe.c, which tests do not link. */
void runtime_restart(void) { abort(); }
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

typedef int (ABI *Register)(GuestCallback);
typedef int (ABI *RegisterCxa)(void (ABI *)(void *), void *, void *);
typedef int (ABI *Acquire)(uint64_t *);
typedef void (ABI *Guard)(uint64_t *);
typedef int32_t (ABI *AttrInit)(void **);
typedef int32_t (ABI *AttrType)(void **, int);
typedef int32_t (ABI *MutexInit)(void **, void **, const char *);
typedef int32_t (ABI *HandleOp)(void **);
typedef int32_t (ABI *Allocate)(int64_t, int64_t, uint64_t, uint64_t, int, int64_t *);
typedef int32_t (ABI *Map)(void **, uint64_t, int, int, int64_t, uint64_t);
typedef int32_t (ABI *Unmap)(void *, uint64_t);
typedef int32_t (ABI *Release)(uint64_t, uint64_t);
#define GET(type, name) ((type)runtime_resolve(name, 0))
typedef void *(ABI *TlsAddress)(const uint64_t *);
typedef void *(ABI *ThreadSelf)(void);
typedef int32_t (ABI *ThreadGetAttr)(void *,void **);
typedef int32_t (ABI *ThreadAffinity)(void **,uint64_t *);
static void *main_identity;
static void *tls_worker(void *unused) {
    (void)unused;
    const uint64_t index[]={2,0};
    unsigned char *p=GET(TlsAddress,"vNe1w4diLCs#p#J")(index);
    assert(p[0]==42 && p[1]==17 && p[31]==0);
    p[0]=99;
    assert(GET(ThreadSelf,"aI+OeCz8xrQ#p#J")()!=main_identity);
    return NULL;
}
static void libc_support(void) {
    static const unsigned char initial[]={42,17};
    runtime_set_libc_tls(initial,sizeof(initial),32);
    const uint64_t index[]={2,0}, last[]={2,31};
    TlsAddress tls=GET(TlsAddress,"vNe1w4diLCs#p#J");
    unsigned char *p=tls(index);
    assert(p[0]==42 && p[1]==17 && p[2]==0 && p[31]==0);
    assert(tls(last)==p+31 && tls(index)==p);
    p[0]=23;
    main_identity=GET(ThreadSelf,"aI+OeCz8xrQ#p#J")();
    pthread_t worker;
    assert(pthread_create(&worker,NULL,tls_worker,NULL)==0);
    assert(pthread_join(worker,NULL)==0);
    assert(p[0]==23 && initial[0]==42);
    void *attr=NULL;
    AttrInit init=GET(AttrInit,"nsYoNRywwNg#p#J");
    ThreadGetAttr get=GET(ThreadGetAttr,"x1X76arYMxU#p#J");
    ThreadAffinity affinity=GET(ThreadAffinity,"8+s5BzZjxSg#p#J");
    HandleOp destroy=GET(HandleOp,"62KCwEMmzcM#p#J");
    uint64_t mask=0;
    assert((uint32_t)init(NULL)==0x80020016);
    assert(init(&attr)==0 && get(main_identity,&attr)==0);
    assert(affinity(&attr,&mask)==0 && mask==0x7f);
    assert((uint32_t)get(&mask,&attr)==0x80020003);
    assert((uint32_t)affinity(&attr,NULL)==0x80020016);
    assert(destroy(&attr)==0 && !attr);
    assert((uint32_t)affinity(&attr,&mask)==0x80020016);
    uint64_t param[8]={64};
    runtime_set_procparam(param);
    assert(GET(ThreadSelf,"959qrazPIrg#p#J")()==param);
    typedef void (ABI *SetHeap)(void **);
    void *heap[10]={0};
    GET(SetHeap,"p5EcQeEeJAE#p#J")(heap);
    assert(runtime_application_heap_api()==heap);
    GET(SetHeap,"p5EcQeEeJAE#p#J")(NULL);
    assert(runtime_application_heap_api()==NULL);
    runtime_set_procparam(NULL);
}
static int order[512], count;
static Register register_plain;
static ABI void first(void) { order[count++]=1; }
static ABI void second(void) { order[count++]=2; }
static ABI void with_arg(void *p) { order[count++]=(int)(uintptr_t)p; }
static ABI void add_handler(void) { order[count++]=4; assert(register_plain(first)==0); }

static void exit_handlers(void) {
    register_plain=GET(Register,"8G2LB+A3rzg#q#q");
    RegisterCxa cxa=GET(RegisterCxa,"tsvEmnenz48#q#q");
    assert(register_plain && cxa);
    assert(register_plain(first)==0);
    assert(cxa(with_arg,(void *)3,(void *)11)==0);
    assert(register_plain(second)==0);
    runtime_finalize((void *)11);
    assert(count==1 && order[0]==3);
    runtime_finalize((void *)11); assert(count==1);
    runtime_finalize(NULL);
    assert(count==3 && order[1]==2 && order[2]==1);
    runtime_finalize(NULL); assert(count==3);
    assert(register_plain(add_handler)==0);
    runtime_finalize(NULL);
    assert(count==5 && order[3]==4 && order[4]==1);
    /* Exercise reallocating the registry and strict LIFO ordering. */
    count=0;
    for (uintptr_t i=0; i<200; ++i) assert(cxa(with_arg,(void *)i,(void *)12)==0);
    runtime_finalize((void *)12);
    assert(count==200);
    for (int i=0; i<200; ++i) assert(order[i]==199-i);
}
static void guards(void) {
    uint64_t state=0;
    Acquire acquire=GET(Acquire,"3GPpjQdAMTw#q#q");
    Guard release=GET(Guard,"9rAeANT2tyE#q#q"), abort_guard=GET(Guard,"2emaaluWzUw#q#q");
    assert(acquire(&state)==1 && state==(UINT64_C(1)<<32));
    abort_guard(&state); assert(state==0);
    assert(acquire(&state)==1);
    release(&state); assert(state==1);
    assert(acquire(&state)==0);
}
static void mutexes(void) {
    AttrInit attr_init=GET(AttrInit,"F8bUHwAG284#p#J");
    AttrType type=GET(AttrType,"iMp8QpE+XO4#p#J");
    HandleOp attr_destroy=GET(HandleOp,"smWEktiyyG0#p#J");
    MutexInit init=GET(MutexInit,"cmo1RIYva9o#p#J");
    HandleOp lock=GET(HandleOp,"9UK1vLZQft4#p#J"), unlock=GET(HandleOp,"tn3VlD0hG60#p#J");
    HandleOp trylock=GET(HandleOp,"upoVrzMHFeE#p#J"), destroy=GET(HandleOp,"2Of0f+3mhhE#p#J");
    void *attr=NULL, *mutex=NULL;
    assert((uint32_t)attr_init(NULL)==0x80020016);
    assert(attr_init(&attr)==0);
    assert((uint32_t)type(&attr,99)==0x80020016);
    assert(type(&attr,2)==0 && init(&mutex,&attr,"recursive-test")==0);
    assert(attr_destroy(&attr)==0 && attr==NULL);
    assert(lock(&mutex)==0 && trylock(&mutex)==0);
    assert((uint32_t)destroy(&mutex)==0x80020010);
    assert(unlock(&mutex)==0 && unlock(&mutex)==0 && destroy(&mutex)==0);
    assert((uint32_t)lock(&mutex)==0x80020016);
    mutex=NULL;
    assert(lock(&mutex)==0); /* static initialization */
    assert((uint32_t)lock(&mutex)==0x8002000b);
    assert((uint32_t)trylock(&mutex)==0x80020010);
    assert(unlock(&mutex)==0 && destroy(&mutex)==0);
}
static void posix_mutexes(void) {
    AttrInit init=GET(AttrInit,"dQHWEsJtoE4#I#J");
    AttrType type=GET(AttrType,"mDmgMOGVUqg#I#J");
    HandleOp destroy_attr=GET(HandleOp,"HF7lK46xzjY#I#J");
    typedef int32_t (ABI *PosixInit)(void **,void **);
    PosixInit mutex_init=GET(PosixInit,"ttHNfU+qDBU#I#J");
    HandleOp lock=GET(HandleOp,"7H0iTOciTLo#I#J");
    HandleOp unlock=GET(HandleOp,"2Z+PpY6CaJg#I#J");
    HandleOp trylock=GET(HandleOp,"K-jXhbt2gn4#I#J");
    HandleOp destroy=GET(HandleOp,"ltCfaGr2JGE#I#J");
    void *a=NULL,*m=NULL;
    assert(init(NULL)==22 && init(&a)==0);
    assert(type(&a,99)==22 && type(&a,2)==0);
    assert(mutex_init(&m,&a)==0 && destroy_attr(&a)==0);
    assert(lock(&m)==0 && trylock(&m)==0 && destroy(&m)==16);
    assert(unlock(&m)==0 && unlock(&m)==0 && destroy(&m)==0);
    m=NULL;
    assert(lock(&m)==0 && lock(&m)==11 && trylock(&m)==16);
    assert(unlock(&m)==0 && destroy(&m)==0);
    assert(!runtime_resolve("dQHWEsJtoE4#q#q",0));
}
static void wall_time(void) {
    typedef int (ABI *GetTime)(int64_t *,int32_t *);
    GetTime get=GET(GetTime,"n88vx3C5nW8#I#J");
    struct { int64_t value[2]; uint64_t canary; } t={{0,0},0xabcdef};
    struct { int32_t value[2]; uint32_t canary; } z={{0,0},0x123456};
    time_t before=time(NULL);
    assert(get(t.value,z.value)==0);
    time_t after=time(NULL);
    assert(t.value[0]>=before && t.value[0]<=after);
    assert(t.value[1]>=0 && t.value[1]<1000000);
    assert(t.canary==0xabcdef && z.canary==0x123456);
    assert(get(NULL,NULL)==0 && get(NULL,z.value)==0);
    assert(!runtime_resolve("invalidNID00#q#q",0));
}
static void direct_memory(void) {
    Allocate alloc=GET(Allocate,"rTXw65xmLIA#p#J");
    Map map=GET(Map,"L-Q3LEjIbgA#p#J");
    Unmap unmap=GET(Unmap,"cQke9UuBQOk#p#J");
    Release release=GET(Release,"MBuItvba6z8#p#J");
    int64_t a=-1,b=-1,c=-1;
    const uint64_t pool=536870912, length=16384;
    assert((uint32_t)alloc(0,pool,1,0,0,&a)==0x80020016 && a==-1);
    assert((uint32_t)alloc(0,8192,length,0,0,&a)==0x80020023);
    assert(alloc(0,pool,length,2097152,0,&a)==0 && a==0);
    assert(alloc(0,pool,length,2097152,0,&b)==0 && b==2097152);
    assert(alloc(0,pool,length,16384,0,&c)==0 && c==16384);
    void *x=NULL,*y=NULL,*invalid=NULL;
    assert((uint32_t)map(&invalid,length*2,3,0,b,0)==0x80020016);
    assert(map(&x,length,3,0,a,2097152)==0 && !((uintptr_t)x%2097152));
    assert(map(&y,length,3,0,a,0)==0);
    assert(*(uint64_t *)x==0);
    *(uint64_t *)x=UINT64_C(0xabcdef0123456789);
    assert(*(uint64_t *)y==UINT64_C(0xabcdef0123456789)); /* real shared backing */
    assert((uint32_t)unmap((char *)x+4096,length)==0x80020016); /* unaligned */
    assert(unmap(x,length)==0 && unmap(y,length)==0);
    assert(release(a,length)==0 && release(b,length)==0 && release(c,length)==0);
    assert(alloc(0,pool,length,0,0,&a)==0 && a==0);
    x=NULL; assert(map(&x,length,3,0,a,0)==0);
    assert(*(uint64_t *)x==0); /* released allocation does not leak previous data */
    assert(release(a,length)==0); /* release also unmaps owned mapping */
}
static void memory_primitives(void) {
    typedef void *(ABI *Set)(void *,int,size_t);
    typedef void *(ABI *Copy)(void *,const void *,size_t);
    typedef int (ABI *Compare)(const void *,const void *,size_t);
    char a[16],b[16];
    assert(GET(Set,"8zTFvBIAIN8#q#q")(a,0x5a,sizeof(a))==a);
    assert(GET(Copy,"Q3VBxCXhUHs#q#q")(b,a,sizeof(a))==b);
    assert(GET(Compare,"DfivPArhucg#q#q")(a,b,sizeof(a))==0);
    memcpy(a,"abcdef",7);
    GET(Copy,"+P6FRGH4LfA#q#q")(a+1,a,6);
    assert(memcmp(a,"aabcdef",7)==0);
    typedef size_t (ABI *Length)(const char *);
    Length length=GET(Length,"j4ViWNHEgww#q#q");
    assert(length("")==0 && length("abc\0def")==3 && length("\xff\x80")==2);
}

static HandleOp rw_read, rw_write, rw_tryread, rw_trywrite, rw_unlock, rw_destroy;
typedef struct { int64_t seconds, nanoseconds; } TestTime;
typedef int32_t (ABI *TimedLock)(void **, const TestTime *);
static void rw_setup(void) {
    rw_read=GET(HandleOp,"Ox9i0c7L5w0#p#J"); rw_write=GET(HandleOp,"mqdNorrB+gI#p#J");
    rw_tryread=GET(HandleOp,"XD3mDeybCnk#p#J"); rw_trywrite=GET(HandleOp,"bIHoZCTomsI#p#J");
    rw_unlock=GET(HandleOp,"+L98PIbGttk#p#J"); rw_destroy=GET(HandleOp,"BB+kb08Tl9A#p#J");
    assert(rw_read && rw_write && rw_tryread && rw_trywrite && rw_unlock && rw_destroy);
}
static void rw_lifecycle(void) {
    rw_setup();
    MutexInit init=GET(MutexInit,"6ULAa0fq4jA#p#J");
    void *lock;
    assert((uint32_t)init(NULL,NULL,NULL)==0x80020016);
    assert(init(&lock,NULL,"rw-test")==0);
    assert(rw_read(&lock)==0 && rw_read(&lock)==0);
    assert((uint32_t)rw_trywrite(&lock)==0x80020010);
    assert((uint32_t)rw_write(&lock)==0x8002000b);
    assert((uint32_t)rw_destroy(&lock)==0x80020010);
    assert(rw_unlock(&lock)==0 && rw_unlock(&lock)==0);
    assert((uint32_t)rw_unlock(&lock)==0x80020001);
    assert(rw_write(&lock)==0);
    assert((uint32_t)rw_tryread(&lock)==0x80020010);
    assert((uint32_t)rw_read(&lock)==0x8002000b);
    assert(rw_unlock(&lock)==0 && rw_destroy(&lock)==0);
    assert((uintptr_t)lock==1);
    assert((uint32_t)rw_read(&lock)==0x80020016);
    assert((uint32_t)rw_destroy(&lock)==0x80020016);
    lock=NULL;
    assert(rw_destroy(&lock)==0);
    assert(rw_read(&lock)==0 && rw_unlock(&lock)==0 && rw_destroy(&lock)==0);
    lock=(void *)(uintptr_t)0xdead;
    assert((uint32_t)rw_tryread(&lock)==0x80020016);
}
typedef struct {
    void *lock;
    pthread_barrier_t ready, release, writer_ready;
    _Atomic int value;
} RwFixture;
static void barrier(pthread_barrier_t *b) {
    int e=pthread_barrier_wait(b); assert(e==0 || e==PTHREAD_BARRIER_SERIAL_THREAD);
}
static void *reader_worker(void *context) {
    RwFixture *f=context;
    assert(rw_read(&f->lock)==0);
    barrier(&f->ready); barrier(&f->release);
    assert(atomic_load(&f->value)==0);
    assert(rw_unlock(&f->lock)==0); return NULL;
}
static void *writer_worker(void *context) {
    RwFixture *f=context;
    assert((uint32_t)rw_trywrite(&f->lock)==0x80020010);
    barrier(&f->writer_ready);
    assert(rw_write(&f->lock)==0);
    atomic_store(&f->value,1);
    assert(rw_unlock(&f->lock)==0); return NULL;
}
static void *holding_writer(void *context) {
    RwFixture *f=context;
    assert(rw_write(&f->lock)==0);
    barrier(&f->ready); barrier(&f->release);
    assert(rw_unlock(&f->lock)==0); return NULL;
}
static void fixture_init(RwFixture *f) {
    assert(pthread_barrier_init(&f->ready,NULL,2)==0);
    assert(pthread_barrier_init(&f->release,NULL,2)==0);
    assert(pthread_barrier_init(&f->writer_ready,NULL,2)==0);
}
static void fixture_destroy(RwFixture *f) {
    assert(rw_destroy(&f->lock)==0);
    assert(pthread_barrier_destroy(&f->ready)==0);
    assert(pthread_barrier_destroy(&f->release)==0);
    assert(pthread_barrier_destroy(&f->writer_ready)==0);
}
static void rw_concurrency(void) {
    rw_setup();
    RwFixture f={0}; fixture_init(&f);
    pthread_t reader,writer;
    assert(rw_read(&f.lock)==0);
    assert(pthread_create(&reader,NULL,reader_worker,&f)==0);
    barrier(&f.ready); /* two threads now hold read locks at once */
    assert(rw_unlock(&f.lock)==0);
    assert(pthread_create(&writer,NULL,writer_worker,&f)==0);
    barrier(&f.writer_ready);
    assert((uint32_t)rw_destroy(&f.lock)==0x80020010);
    assert(atomic_load(&f.value)==0);
    barrier(&f.release);
    assert(pthread_join(reader,NULL)==0 && pthread_join(writer,NULL)==0);
    assert(atomic_load(&f.value)==1);
    fixture_destroy(&f);
}
static void rw_timeouts(void) {
    rw_setup();
    TimedLock read_timed=GET(TimedLock,"iPtZRWICjrM#p#J"), write_timed=GET(TimedLock,"adh--6nIqTk#p#J");
    RwFixture f={0}; fixture_init(&f);
    pthread_t writer; assert(pthread_create(&writer,NULL,holding_writer,&f)==0);
    barrier(&f.ready);
    TestTime expired={0,0},invalid={0,1000000000};
    assert((uint32_t)read_timed(&f.lock,&expired)==0x8002003c);
    assert((uint32_t)write_timed(&f.lock,&expired)==0x8002003c);
    assert((uint32_t)read_timed(&f.lock,&invalid)==0x80020016);
    assert((uint32_t)rw_unlock(&f.lock)==0x80020001);
    barrier(&f.release); assert(pthread_join(writer,NULL)==0);
    assert(read_timed(&f.lock,&invalid)==0); /* available lock ignores deadline */
    assert(rw_unlock(&f.lock)==0);
    fixture_destroy(&f);
}
static void audit_fixes_and_extensions(void) {
    /* 1. guest_sysctl tests */
    typedef int32_t (ABI *SysctlFn)(const int32_t *, uint32_t, void *, uint64_t *, const void *, uint64_t);
    SysctlFn sysctl = GET(SysctlFn, "DFmMT80xcNI#p#J");
    assert(sysctl);

    int32_t mib_pagesize[] = {6, 7};
    int32_t pagesize = 0;
    uint64_t len = sizeof(pagesize);
    assert(sysctl(mib_pagesize, 2, &pagesize, &len, NULL, 0) == 0);
    assert(pagesize == 16384 && len == 4);

    int32_t mib_ncpu[] = {6, 3};
    int32_t ncpu = 0;
    len = sizeof(ncpu);
    assert(sysctl(mib_ncpu, 2, &ncpu, &len, NULL, 0) == 0);
    assert(ncpu == 7 && len == 4);

    int32_t mib_byteorder[] = {6, 4};
    int32_t byteorder = 0;
    len = sizeof(byteorder);
    assert(sysctl(mib_byteorder, 2, &byteorder, &len, NULL, 0) == 0);
    assert(byteorder == 1234 && len == 4);

    int32_t mib_physmem[] = {6, 5};
    uint64_t physmem = 0;
    len = sizeof(physmem);
    assert(sysctl(mib_physmem, 2, &physmem, &len, NULL, 0) == 0);
    assert(physmem == (UINT64_C(8) * 1024 * 1024 * 1024) && len == 8);

    int32_t mib_ostype[] = {1, 1};
    char ostype[32] = {0};
    len = sizeof(ostype);
    assert(sysctl(mib_ostype, 2, ostype, &len, NULL, 0) == 0);
    assert(strcmp(ostype, "FreeBSD") == 0);

    int32_t mib_machine[] = {6, 1};
    char machine[32] = {0};
    len = sizeof(machine);
    assert(sysctl(mib_machine, 2, machine, &len, NULL, 0) == 0);
    assert(strcmp(machine, "amd64") == 0);

    int32_t mib_model[] = {6, 2};
    char model[32] = {0};
    len = sizeof(model);
    assert(sysctl(mib_model, 2, model, &len, NULL, 0) == 0);
    assert(strcmp(model, "AMD Jaguar") == 0);

    int32_t mib_ncpuonline[] = {6, 9};
    int32_t ncpuonline = 0;
    len = sizeof(ncpuonline);
    assert(sysctl(mib_ncpuonline, 2, &ncpuonline, &len, NULL, 0) == 0);
    assert(ncpuonline == 7 && len == 4);

    int32_t mib_version[] = {1, 7};
    char version[128] = {0};
    len = sizeof(version);
    assert(sysctl(mib_version, 2, version, &len, NULL, 0) == 0);
    assert(strstr(version, "FreeBSD") != NULL);

    int32_t mib_unknown[] = {99, 99};
    int32_t dummy = 0;
    len = sizeof(dummy);
    assert(sysctl(mib_unknown, 2, &dummy, &len, NULL, 0) == -1);
    assert(*runtime_errno() == 2 /* ENOENT */);

    /* 2. system_param fallback tests */
    typedef int32_t (ABI *SysParamFn)(int32_t, int32_t *);
    SysParamFn sys_param = GET(SysParamFn, "fZo48un7LK4#V#W");
    assert(sys_param);
    int32_t val = -1;
    assert(sys_param(1, &val) == 0 && val >= 1);
    val = -1;
    assert(sys_param(6, &val) == 0 && val == 1);
    val = -1;
    assert(sys_param(9999, &val) == 0 && val == 0); /* Unknown id defaults to 0 safely without exit(21) */

    /* 3. safe_area stack safety check */
    typedef int32_t (ABI *SafeAreaFn)(float *);
    SafeAreaFn safe_area = GET(SafeAreaFn, "1n37q1Bvc5Y");
    assert(safe_area);
    struct {
        float ratio;
        uint32_t guard_canary;
    } safe_struct = {0.0f, 0x12345678};
    assert(safe_area(&safe_struct.ratio) == 0);
    assert(safe_struct.ratio == 1.0f);
    assert(safe_struct.guard_canary == 0x12345678); /* No overflow past ratio! */

    /* 4. open_font_set Linux system font resolution check */
    typedef int32_t (ABI *OpenFontSetFn)(void *, uint32_t, uint32_t, const void *, void **);
    typedef int32_t (ABI *CloseFontFn)(void *);
    OpenFontSetFn open_font_set = (OpenFontSetFn)runtime_resolve("cKYtVmeSTcw", 0);
    CloseFontFn close_font = (CloseFontFn)runtime_resolve("6z0F5m2x7wE", 0);
    assert(open_font_set);
    void *font = NULL;
    int32_t font_ret = open_font_set(NULL, 0, 0, NULL, &font);
    assert(font_ret == 0);
    assert(font != NULL);
    if (close_font) close_font(font);

    /* 5. rwlock non-default attribute handling */
    typedef int32_t (ABI *RwInitFn)(void **, void *const *, const char *);
    RwInitFn rw_init_fn = GET(RwInitFn, "6ULAa0fq4jA#p#J");
    HandleOp rw_destroy_fn = GET(HandleOp, "BB+kb08Tl9A#p#J");
    assert(rw_init_fn && rw_destroy_fn);
    void *dummy_attr = (void *)0x1234;
    void *const attr_ptr = dummy_attr;
    void *rw_custom = NULL;
    assert(rw_init_fn(&rw_custom, (void *const *)&attr_ptr, "custom-attr-test") == 0);
    assert(rw_custom != NULL);
    assert(rw_destroy_fn(&rw_custom) == 0);

    /* 6. dialog and message stubs */
    typedef int32_t (ABI *MsgDialogStatusFn)(void);
    MsgDialogStatusFn msg_dialog_status = (MsgDialogStatusFn)runtime_resolve("sceMsgDialogGetStatus", 0);
    assert(msg_dialog_status);
    assert(msg_dialog_status() == 3);
}
int main(int argc,char **argv) {
    runtime_start(0); assert(runtime_resolve("bzQExy189ZI#q#q",0)==0);
    runtime_start(1);
    assert(runtime_resolve("bzQExy189ZI#wrong#module",0)==0);
    assert(runtime_resolve("unknown",0)==0);
    assert(runtime_resolve("8zTFvBIAIN8#q#q",1)==0);
    assert(runtime_resolve("f7uOxY9mM1U#p#J",0)==0);
    uint64_t *canary=(uint64_t *)runtime_resolve("f7uOxY9mM1U#p#J",1);
    assert(canary && *canary && !(*canary & 255));
    if (argc>1 && !strcmp(argv[1],"--guard-recursion")) {
        uint64_t guard=0; Acquire acquire=GET(Acquire,"3GPpjQdAMTw#q#q");
        acquire(&guard); acquire(&guard); return 99;
    }
    if (argc>1 && !strcmp(argv[1],"--bad-tls")) {
        static const char data[4]={0};
        runtime_set_libc_tls(data,4,8);
        const uint64_t index[]={2,8};
        GET(TlsAddress,"vNe1w4diLCs#p#J")(index); return 99;
    }
    if (argc>1 && !strcmp(argv[1],"--stack-failure")) {
        GET(GuestCallback,"Ou3iL1abvng#p#J")(); return 99;
    }
    if (argc>1 && !strcmp(argv[1],"--rwlock-lifecycle")) { rw_lifecycle(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--rwlock-concurrency")) { rw_concurrency(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--rwlock-timeouts")) { rw_timeouts(); return 0; }
    libc_support(); posix_mutexes(); wall_time(); exit_handlers(); guards(); mutexes(); direct_memory(); memory_primitives();
    rw_lifecycle(); rw_concurrency(); rw_timeouts();
    audit_fixes_and_extensions();
    puts("PASS: callback lifecycle, guard ABI, mutex errors, shared direct memory, memory primitives, resolver scope");
    return 0;
}
