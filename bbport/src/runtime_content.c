/* Offline AppContent provider for the configured base-game profile.
 * Add-on licenses come from BB_ADDCONT (entitlement labels, comma-separated, e.g.
 * SPEXPANSIONDLC03 for The Old Hunters, whose data ships with the game) and from the add-on
 * folders in <user>/addcont (see addon_list). No package/DLC mounting, downloads or entitlement
 * checks. */
#include "runtime.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define INVALID_ID ((int32_t)0x805a1000)
#define NOT_LOADED ((int32_t)0x805a1001)
#define PARAMETER ((int32_t)0x80d90002)
#define BUSY ((int32_t)0x80d90003)
static unsigned refs, initialized, configured;
static uint32_t parameters[5];
static unsigned queries, lists;
/* OrbisAppContentAddcontInfo: entitlement label (16 characters, NUL, padding) and status. */
typedef struct { char label[17]; char padding[3]; uint32_t status; } AddcontInfo;
#define ADDCONT_INSTALLED 4u
#define ADDCONT_MAX 8
static char addcont[ADDCONT_MAX][17];
static unsigned addcont_count;
static void load_addcont(void) {
    addcont_count=0;
    for (const char *p=getenv("BB_ADDCONT"); p && *p && addcont_count<ADDCONT_MAX;) {
        size_t n=strcspn(p,",");
        if (n>16) fprintf(stderr,"WARNING: BB_ADDCONT label longer than 16 characters ignored: %.*s\n",(int)n,p);
        else if (n) { memcpy(addcont[addcont_count],p,n); addcont[addcont_count++][n]=0; }
        p+=n; if (*p==',') ++p;
    }
}
void runtime_content_configure(const uint32_t values[5]) {
    if (refs || initialized || (values[0]!=1 && values[0]!=3)) {
        fputs("ERROR: invalid or late content profile\n",stderr); exit(1);
    }
    memcpy(parameters,values,sizeof(parameters)); configured=1;
}
static void require_provider(void) {
    if (!configured) { fputs("STOP: AppContent needs --content-profile\n",stderr); exit(21); }
}
/* System modules: every library this eboot imports is provided by the host
 * runtime, so loading one only tracks a reference count. AppContent (0xb4)
 * additionally requires the configured content profile. */
static unsigned module_refs[0x10000];
static ABI int32_t module_load(uint16_t id) {
    if (!id) return INVALID_ID;
    if (id==0xb4) require_provider();
    if (module_refs[id]==UINT32_MAX) { fputs("STOP: module reference overflow\n",stderr); exit(21); }
    ++module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; printf("Runtime: AppContent host provider loaded; references=%u\n",refs); }
    else printf("Runtime: system module 0x%x loaded (host implementation)\n",id);
    return 0;
}
static ABI int32_t module_loaded(uint16_t id) {
    if (!id) return INVALID_ID;
    return module_refs[id] ? 0 : NOT_LOADED;
}
static ABI int32_t module_unload(uint16_t id) {
    int32_t result=module_loaded(id);
    if (result) return result;
    --module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; if (!refs) initialized=0; }
    return 0;
}
static void require_loaded(void) {
    require_provider();
    if (!refs) { fputs("STOP: AppContent called before module load\n",stderr); exit(21); }
}
static ABI int32_t content_init(const unsigned char *init,unsigned char *boot) {
    require_loaded();
    if (!init || !boot) return PARAMETER;
    if (initialized) return BUSY;
    /* Only the reserved-zero init profile is implemented. */
    for (unsigned i=0;i<32;++i) if (init[i]) return PARAMETER;
    memset(boot,0,40); initialized=1;
    load_addcont();
    if (!addcont_count) puts("Runtime: AppContent initialized; offline base-game profile, no add-ons");
    else {
        printf("Runtime: AppContent initialized; offline base-game profile, add-on licenses:");
        for (unsigned i=0;i<addcont_count;++i) printf(" %s",addcont[i]);
        putchar('\n');
    }
    return 0;
}
static void require_initialized(void) {
    require_loaded();
    if (!initialized) { fputs("STOP: AppContent query before initialization\n",stderr); exit(21); }
}
static ABI int32_t param_int(uint32_t id,int32_t *out) {
    require_initialized();
    if (id>4 || !out) return PARAMETER;
    memcpy(out,&parameters[id],4); ++queries;
    printf("Runtime: AppContent parameter %u = %d\n",id,*out);
    return 0;
}
/* SceAppContentAddcontInfo: entitlement label (16 chars + NUL), padding, download status. */
#define LABEL_SIZE 17
#define ADDON_INSTALLED 4u
typedef struct { char label[LABEL_SIZE]; char padding[3]; uint32_t status; } AddonInfo;
/* Add-ons are the folders <user>/addcont/<title id>/<entitlement label> (shadPS4's layout),
 * each reported as installed. No add-on data is mounted: Bloodborne: The Old Hunters
 * (SPEXPANSIONDLC03) needs only the entitlement, its data ships with patch 1.09. Without a list
 * the count is returned, with one at most capacity entries are written. Never touch unused list
 * slots. */
static ABI int32_t addon_list(uint32_t service,void *list,uint32_t capacity,uint32_t *hits) {
    require_initialized();
    if (service) { fputs("STOP: nonzero AppContent service label unsupported\n",stderr); exit(21); }
    if ((!capacity || !list) && !hits) return PARAMETER;
    /* BB_ADDCONT labels first, then the add-on folders (not already listed); nothing is mounted. */
    int fill=list && capacity;
    uint32_t count=0;
    char found[ADDCONT_MAX*2][LABEL_SIZE];
    uint32_t nfound=0;
    for (unsigned i=0;i<addcont_count && nfound<ADDCONT_MAX*2;++i) memcpy(found[nfound++],addcont[i],LABEL_SIZE);
    char root[4096];
    snprintf(root,sizeof(root),"%s/addcont",runtime_file_user_dir());
    DIR *titles=opendir(root);
    for (struct dirent *t; titles && (t=readdir(titles));) {
        if (t->d_name[0]=='.') continue;
        char title[4096+256];
        snprintf(title,sizeof(title),"%s/%s",root,t->d_name);
        DIR *labels=opendir(title);
        for (struct dirent *l; labels && (l=readdir(labels));) {
            struct stat entry;
            char path[sizeof(title)+256];
            size_t n=strlen(l->d_name);
            if (l->d_name[0]=='.' || n>=LABEL_SIZE) continue;
            snprintf(path,sizeof(path),"%s/%s",title,l->d_name);
            if (stat(path,&entry) || !S_ISDIR(entry.st_mode)) continue;
            int known=0;
            for (uint32_t i=0;i<nfound;++i) known|=!strcmp(found[i],l->d_name);
            if (known || nfound>=ADDCONT_MAX*2) continue;
            memset(found[nfound],0,LABEL_SIZE); memcpy(found[nfound++],l->d_name,n);
        }
        if (labels) closedir(labels);
    }
    if (titles) closedir(titles);
    for (uint32_t i=0;i<nfound;++i) {
        if (fill && count>=capacity) break;
        if (fill) {
            AddonInfo info;
            memset(&info,'\0',sizeof(info)); memcpy(info.label,found[i],LABEL_SIZE);
            info.status=ADDON_INSTALLED;
            memcpy((unsigned char *)list+(size_t)count*sizeof(info),&info,sizeof(info));
        }
        ++count;
    }
    if (hits) *hits=count;
    if (!lists) printf("Runtime: AppContent add-on list queried; %u reported (BB_ADDCONT, %s)\n",(unsigned)count,root);
    ++lists; return 0;
}
uintptr_t runtime_content_resolve(const char *name) {
    if (!strcmp(name,"g8cM39EUZ6o#M#N")) return (uintptr_t)module_load;
    if (!strcmp(name,"fMP5NHUOaMk#M#N")) return (uintptr_t)module_loaded;
    if (!strcmp(name,"eR2bZFAAU0Q#M#N")) return (uintptr_t)module_unload;
    if (!strcmp(name,"R9lA82OraNs#c#d")) return (uintptr_t)content_init;
    if (!strcmp(name,"99b82IKXpH4#c#d")) return (uintptr_t)param_int;
    if (!strcmp(name,"xnd8BJzAxmk#c#d")) return (uintptr_t)addon_list;
    return 0;
}
void runtime_content_report(void) {
    printf("Runtime: AppContent references=%u, initialized=%u, parameter queries=%u, list queries=%u\n",refs,initialized,queries,lists);
}
