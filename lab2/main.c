#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int err_code;

/*
 * here are some function signatures and macros that may be helpful.
 */

void handle_error(char* fullname, char* action);
bool test_file(char* pathandname);
bool is_dir(char* pathandname);
const char* ftype_to_str(mode_t mode);
void list_file(char* pathandname, char* name, bool list_long, bool human_readable);
void list_dir(char *dirname, bool list_long, bool list_all, bool recursive, bool human_readable);

#define PATH_MAX 4096 // Max path length in linux
#define USRNAME_MAX 32
#define GRPNAME_MAX 32
#define DATESTR_MAX 15
#define WIDTHSTR_MAX 32

/*
 * You can use the NOT_YET_IMPLEMENTED macro to error out when you reach parts
 * of the code you have not yet finished implementing.
 */
#define NOT_YET_IMPLEMENTED(msg)                  \
    do {                                          \
        printf("Not yet implemented: " msg "\n"); \
        exit(255);                                \
    } while (0)

/*
 * PRINT_ERROR: This can be used to print the cause of an error returned by a
 * system call. It can help with debugging and reporting error causes to
 * the user. Example usage:
 *     if ( error_condition ) {
 *        PRINT_ERROR();
 *     }
 */
#define PRINT_ERROR(progname, what_happened, pathandname)               \
    do {                                                                \
        printf("%s: %s %s: %s\n", progname, what_happened, pathandname, \
               strerror(errno));                                        \
    } while (0)

/* PRINT_PERM_CHAR:
 *
 * This will be useful for -l permission printing.  It prints the given
 * 'ch' if the permission exists, or "-" otherwise.
 * Example usage:
 *     PRINT_PERM_CHAR(sb.st_mode, S_IRUSR, "r");
 */
#define PRINT_PERM_CHAR(mode, mask, ch) printf("%s", (mode & mask) ? ch : "-");

/*
 * Get username for uid. Return 1 on failure, 0 otherwise.
 */
static int uname_for_uid(uid_t uid, char* buf, size_t buflen) {
    struct passwd* p = getpwuid(uid);
    if (p == NULL) {
        return 1;
    }
    strncpy(buf, p->pw_name, buflen);
    return 0;
}

/*
 * Get group name for gid. Return 1 on failure, 0 otherwise.
 */
static int group_for_gid(gid_t gid, char* buf, size_t buflen) {
    struct group* g = getgrgid(gid);
    if (g == NULL) {
        snprintf(buf,buflen,"%d",gid);
        return 1;
    }
    strncpy(buf, g->gr_name, buflen);
    return 0;
}

/*
 * Format the supplied `struct timespec` in `ts` (e.g., from `stat.st_mtim`) as a
 * string in `char *out`. Returns the length of the formatted string (see, `man
 * 3 strftime`).
 */
static size_t date_string(struct timespec* ts, char* out, size_t len) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    struct tm* t = localtime(&ts->tv_sec);
    if (now.tv_sec < ts->tv_sec) {
        // Future time, treat with care.
        return strftime(out, len, "%b %e %Y", t);
    } else {
        time_t difference = now.tv_sec - ts->tv_sec;
        if (difference < 31556952ull) {
            return strftime(out, len, "%b %e %H:%M", t);
        } else {
            return strftime(out, len, "%b %e %Y", t);
        }
    }
}

/*
 * Print help message and exit.
 */
static void help() {
    /* TODO: add to this */
    printf("ls: List files\n");
    printf("\t--help: Print this help\n");
    exit(0);
}

/*
 * call this when there's been an error.
 * The function should:
 * - print a suitable error message (this is already implemented)
 * - set appropriate bits in err_code
 */
void handle_error(char* what_happened, char* fullname) {
    PRINT_ERROR("ls", what_happened, fullname);

    // TODO: your code here: inspect errno and set err_code accordingly.
    return;
}

/*
 * test_file():
 * test whether stat() returns successfully and if not, handle error.
 * Use this to test for whether a file or dir exists
 */
bool test_file(char* pathandname) {
    struct stat sb;
    if (stat(pathandname, &sb)) {
        handle_error("cannot access", pathandname);
        return false;
    }
    return true;
}

/*
 * is_dir(): tests whether the argument refers to a directory.
 * precondition: test_file() returns true. that is, call this function
 * only if test_file(pathandname) returned true.
 */
bool is_dir(char* pathandname) {
    /* TODO: fillin */
    struct stat sb;
    if(lstat(pathandname,&sb)==-1){
        PRINT_ERROR("is_dir","couldn't open file or directory",pathandname);
        exit(1);
    }
    return S_ISDIR(sb.st_mode);
}

/* convert the mode field in a struct stat to a file type, for -l printing */
const char* ftype_to_str(mode_t mode) {
    /* TODO: fillin */
    return "?";
}

int maxwidth_sz; // 在-l模式下，表示此时文件所在目录下最大的文件的大小所需要的字符串宽度
int maxwidth_usr;
int maxwidth_grp;

void get_sz_str(size_t size, char *sz_str,size_t len ,bool human_readable){
    if(human_readable){
        char units[]={'\0','K','M','G','T','P','E','Z','Y'};
        int j=0;
        double sz=(double) size;
        while(sz>=1024.0){
            sz/=1024;
            j++;

        }
        snprintf(sz_str,len,"%.1f%c",sz, units[j]);
        
    }else{
        snprintf(sz_str,len,"%ld",size);
    }
}

/* list_file():
 * implement the logic for listing a single file.
 * This function takes:
 *   - pathandname: the directory name plus the file name.
 *   - name: just the name "component".
 *   - list_long: a flag indicated whether the printout should be in
 *   long mode.
 *
 *   The reason for this signature is convenience: some of the file-outputting
 *   logic requires the full pathandname (specifically, testing for a directory
 *   so you can print a '/' and outputting in long mode), and some of it
 *   requires only the 'name' part. So we pass in both. An alternative
 *   implementation would pass in pathandname and parse out 'name'.
 */
void list_file(char* pathandname, char* name, bool list_long, bool human_readable) {
    /* TODO: fill in*/
    /*
    * 输出简略信息
    */
    if(!list_long){
        printf("%s\n",name);
        return;
    }
    /*
     * 输出详细信息
     */
    struct stat sb;
    if (lstat(pathandname, &sb) == -1){
        handle_error("couldn't open file or directory", pathandname);
        exit(1);
    }
    //获取权限信息
    char permission[11] = "----------";
    permission[1] = S_IRUSR & sb.st_mode ? 'r' : '-';
    permission[2] = S_IWUSR & sb.st_mode ? 'w' : '-';
    permission[3] = S_IXUSR & sb.st_mode ? 'x' : '-';
    permission[4] = S_IRGRP & sb.st_mode ? 'r' : '-';
    permission[5] = S_IWGRP & sb.st_mode ? 'w' : '-';
    permission[6] = S_IXGRP & sb.st_mode ? 'x' : '-';
    permission[7] = S_IROTH & sb.st_mode ? 'r' : '-';
    permission[8] = S_IWOTH & sb.st_mode ? 'w' : '-';
    permission[9] = S_IXOTH & sb.st_mode ? 'x' : '-';
    //获取文件被链接的数目
    int num_link = sb.st_nlink;
    //获取文件拥有者
    char usrname[USRNAME_MAX];
    uname_for_uid(sb.st_uid, usrname, USRNAME_MAX);
    //获取文件拥有者所在组
    char grpname[GRPNAME_MAX];
    group_for_gid(sb.st_gid, grpname, GRPNAME_MAX);
    //获取创建日期
    char datestr[DATESTR_MAX];
    date_string(&sb.st_ctim, datestr, DATESTR_MAX);
    //获取文件大小
    long size = sb.st_size;
    char *sz_str=malloc(WIDTHSTR_MAX*sizeof(char));
    get_sz_str(size,sz_str, WIDTHSTR_MAX, human_readable);
    char format_str[30];
    snprintf(format_str, 30, "%%s %%d %%%ds %%%ds %%%ds %%s %%s", maxwidth_usr, maxwidth_grp, maxwidth_sz);
    printf(format_str, permission, num_link, usrname, grpname, sz_str, datestr, name);
    if(S_ISLNK(sb.st_mode)){
        char *link_to = malloc(256*sizeof(char));
        int len;
        if((len=readlink(pathandname,link_to,256))==-1){
            handle_error("read link faild",pathandname);
        }
        link_to[len]='\0';
        printf(" -> %s",link_to);
        free(link_to);
    }
    printf("\n");
    free(sz_str);
    return;
}

/* list_dir():
 * implement the logic for listing a directory.
 * This function takes:
 *    - dirname: the name of the directory
 *    - list_long: should the directory be listed in long mode?
 *    - list_all: are we in "-a" mode?
 *    - recursive: are we supposed to list sub-directories?
 */
void list_dir(char* dirname, bool list_long, bool list_all, bool recursive, bool human_readable) {
    /* TODO: fill in
     *   You'll probably want to make use of:
     *       opendir()
     *       readdir()
     *       list_file()
     *       snprintf() [to make the 'pathandname' argument to
     *          list_file(). that requires concatenating 'dirname' and
     *          the 'd_name' portion of the dirents]
     *       closedir()
     *   See the lab description for further hints
     */
    //处理dirname是文件的情况
    if(!is_dir(dirname)){
        list_file(dirname,dirname,list_long,human_readable);
        return;
    }
    //处理dirname是目录的情况
    DIR *dirp;
    if((dirp = opendir(dirname)) == NULL) {
        PRINT_ERROR("list_dir","couldn't open file",dirname);
        return;
    }
    //处理目录尾的slash
    int i=strlen(dirname);
    if(dirname[i-1]=='/'){
        dirname[i-1]='\0';
    }
    struct stat sb;
    struct dirent *dp;
    // 预处理,获取最大的文件大小，输出所需字符串宽度等信息
    char *pathandname = (char *)malloc(PATH_MAX * sizeof(char));
    maxwidth_sz=0;
    maxwidth_grp=0;
    maxwidth_usr=0;
    while((dp=readdir(dirp))!=NULL){
        snprintf(pathandname,PATH_MAX,"%s%s%s",dirname,"/",dp->d_name);
        if (lstat(pathandname, &sb) == -1){
            handle_error("couldn't open file or directory", pathandname);
            exit(1);
        }
        int len;
        char *buf = malloc(WIDTHSTR_MAX * sizeof(char));
        // 获取组名字符串最大宽度
        group_for_gid(sb.st_gid, buf, GRPNAME_MAX);
        len = strlen(buf);
        maxwidth_grp = len > maxwidth_grp ? len : maxwidth_grp;
        // 获取用户名字符串最大宽度
        uname_for_uid(sb.st_uid, buf, USRNAME_MAX);
        len = strlen(buf);
        maxwidth_usr = len > maxwidth_usr ? len : maxwidth_usr;
        // 获取文件大小字符串最大宽度
        get_sz_str(sb.st_size, buf, WIDTHSTR_MAX, human_readable);
        len = strlen(buf);
        maxwidth_sz = len > maxwidth_sz ? len : maxwidth_sz;
        free(buf);
    }
    // -R模式下要先输出目录名
    if(recursive){
        printf("%s:\n",dirname);
    }
    // 非-R模式下的打印输出
    char *fname;
    rewinddir(dirp);
    while((dp=readdir(dirp))!=NULL){
        fname=dp->d_name;
        //对于隐藏文件或目录，如果没有-a选项直接跳过
        if(fname[0]=='.' && !list_all){
            continue;
        }
        snprintf(pathandname,PATH_MAX,"%s%s%s",dirname,"/",fname);
        list_file(pathandname,fname,list_long,human_readable);
    }
    free(pathandname);
    printf("\n");
    // 如果没有-R选项，程序到这里就结束了
    if(!recursive){
        closedir(dirp);
        return;
    }
    // 有-R选项，继续递归处理
    rewinddir(dirp);
    char *nxt_dirname=(char *)malloc(PATH_MAX*sizeof(char));
    while((dp=readdir(dirp))!=NULL){

        fname=dp->d_name;
        
        if(strlen(fname)==2&&fname[0]=='.'&&fname[1]=='.'){
            continue;
        }
        if(strlen(fname)==1&&fname[0]=='.'){
            continue;
        }
        if(fname[0]=='.'&&!list_all){
            continue;
        }

        snprintf(nxt_dirname,PATH_MAX,"%s%s%s",dirname,"/",fname);

        if(!is_dir(nxt_dirname))
            continue;
        
        list_dir(nxt_dirname,list_long,list_all, recursive, human_readable);
    }
    free(nxt_dirname);
    closedir(dirp);
    return;
}

int main(int argc, char* argv[]) {
    // This needs to be int since C does not specify whether char is signed or
    // unsigned.
    int opt;
    err_code = 0;
    bool list_long = false, list_all = false, recursive=false, human_readable=false;
    // We make use of getopt_long for argument parsing, and this
    // (single-element) array is used as input to that function. The `struct
    // option` helps us parse arguments of the form `--FOO`. Refer to `man 3
    // getopt_long` for more information.
    struct option opts[] = {
        {.name = "help", .has_arg = 0, .flag = NULL, .val = '\a'}};

    // This loop is used for argument parsing. Refer to `man 3 getopt_long` to
    // better understand what is going on here.
    while ((opt = getopt_long(argc, argv, "1alRh", opts, NULL)) != -1) {
        switch (opt) {
            case '\a':
                // Handle the case that the user passed in `--help`. (In the
                // long argument array above, we used '\a' to indicate this
                // case.)
                help();
                break;
            case '1':
                // Safe to ignore since this is default behavior for our version
                // of ls.
                break;
            case 'a':
                list_all = true;
                break;
                // TODO: you will need to add items here to handle the
                // cases that the user enters "-l" or "-R"
            case 'l':
                list_long = true;
                break;
            case 'R':
                recursive = true;
                break;
            case 'h':
                human_readable = true;
                break;
            default:
                printf("Unimplemented flag %d\n", opt);
                break;
        }
    }
    // TODO: Replace this.
    //获取当前路经
    char cwd[PATH_MAX];
    if(getcwd(cwd,PATH_MAX)==NULL){
        PRINT_ERROR("main","get work directory faild","");
        exit(1);
    }
    //printf("current directory：%s\n",cwd);
    if (optind < argc) {
        //printf("Optional arguments: \n");
    }
    if(optind == argc){
        list_dir(".", list_long, list_all, recursive, human_readable);
    }
    for (int i = optind; i < argc; i++) {
        list_dir(argv[i], list_long, list_all, recursive, human_readable);
    }
    NOT_YET_IMPLEMENTED("help, handle error");
    exit(err_code);
}
