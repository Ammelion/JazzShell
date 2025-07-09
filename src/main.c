#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <dirent.h>
#include <termios.h>
#include <limits.h>

typedef struct trienode
{
    struct trienode *children[256];
    bool terminal;
} trienode;

struct termios orig, raw; //autocompletion stuff

trienode *createnode(){ 
    trienode *newnode = malloc(sizeof *newnode);
    for(int i=0;i<256;i++){
        newnode->children[i]=NULL;
    }
    newnode->terminal=false;
    return newnode;
}

bool trieinsert(trienode **root, char *signedtext){
    if (*root==NULL){
        *root=createnode();
    }

    unsigned char *text=(unsigned char *)signedtext;
    trienode *tmp=*root;
    int length=strlen(signedtext);
    for(int i=0;i<length;i++){
        if(tmp->children[text[i]]==NULL){
            tmp->children[text[i]]=createnode();
        }
        tmp=tmp->children[text[i]];
    }
    if(tmp->terminal){
        return false;
    }
    else{
        tmp->terminal = true;
        return true;
    }
}

trienode *cptrie(trienode *root){
    if(!root){return NULL;}

    trienode *newnode=createnode();
    newnode->terminal=root->terminal;

    for(int i=0;i<256;i++){
        if(root->children[i]){
            newnode->children[i]=cptrie(root->children[i]);
        }
    }
    
    return newnode;
}

trienode *find(trienode *root,const char *prefix){
    trienode *node=root;
    for (;*prefix && node;prefix++) {
        node = node->children[(unsigned char)*prefix];
    }
    return node;
}

static int dfs(trienode *node, char *buf, int depth){
    int found = 0;
    if (node->terminal){
        buf[depth]='\0';
        found=1;
    }
    for(int c=0; c<256 && found<2; c++){
        if (node->children[c]){
            buf[depth] = (char)c;
            int n=dfs(node->children[c], buf, depth + 1);
            found += n;
            if (found > 1) break;
        }
    }
    return found;
}

bool usufix(trienode *node, char *outbuf) {
    int count = dfs(node, outbuf, 0);
    return (count == 1);
}

void enable_raw_mode(){
    tcgetattr(STDIN_FILENO, &orig);
    raw = orig;
    raw.c_iflag &= ~(BRKINT|INPCK|ISTRIP|IXON);
    //raw.c_oflag &= ~OPOST; //Fucking Trash flag
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode(){
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&orig);
}

int parser(char *input, char *args[]){  //this is the funny tokenizaton part, checks for all backslashes , quoting and what not
    int count=0,single=0,idouble=0;
    char *read=input,*write=input,*token=NULL;
    while(*read){
        char c=*read;
        if(single){
            if (c=='\''){
                single=0;
            }
            else{
                *write++=c;
            }
            read++;
        }
        else if(idouble){
            if (c=='"'){
                idouble=0;
                read++;
            }
            else if (c=='\\' && (read[1]=='"'||read[1]=='\\'||read[1]=='$'
                                 ||read[1]=='`'||read[1]=='\n')){
                read++;
                if(*read!='\n') *write++=*read;
                read++;
            }
            else {
                *write++=c;
                read++;
            }
        }
        else{
            if(c=='\''){
                single=1;
                if(!token){
                    token=write;
                    args[count++]=token;
                }
                read++;
            }
            else if(c=='"'){
                idouble=1;
                if(!token){
                    token=write;
                    args[count++]=token;
                }
                read++;
            }
            else if(c=='\\' && read[1]){
                if(!token){
                    token=write;
                    args[count++]=token;
                }
                read++;
                *write++=*read++;
            }
            else if(isspace((unsigned char)c)){
                if(token){
                    *write++='\0';
                    token=NULL;
                }
                read++;
            }
            else{
                if(!token){
                    token=write;
                    args[count++]=token;
                }
                *write++=c;
                read++;
            }
        }
    }
    if(token) *write++='\0';
    args[count]=NULL;
    return count;
}

char *matches[256];
int mcount = 0;
void collect(trienode *node, char *b, int depth) {
    if (node->terminal) {
        b[depth] = '\0';
        matches[mcount++] = strdup(b);
    }
    for (int i = 0; i < 256 && mcount < 256; i++) {
        if (node->children[i]) {
            b[depth] = (char)i;
            collect(node->children[i], b, depth + 1);
        }
    }
}

void print_prompt() {
    if (isatty(STDOUT_FILENO))
        write(STDOUT_FILENO, "$ ", 2);
}

static void collect_full(trienode *node, const char *prefix, char *b, int depth) {
    if (node->terminal) {
        b[depth] = '\0';
        size_t plen = strlen(prefix);
        size_t total = plen + depth;
        char *full = malloc(total + 1);
        memcpy(full, prefix, plen);
        memcpy(full + plen, b, depth);
        full[total] = '\0';
        matches[mcount++] = full;
    }
    for (int c = 0; c < 256 && mcount < 256; c++) {
        if (node->children[c]) {
            b[depth] = (char)c;
            collect_full(node->children[c], prefix, b, depth + 1);
        }
    }
}

ssize_t read_line(char *buf, size_t size, trienode *groot) {
    size_t pos = 0;
    static int tab_count = 0;

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) return -1;

        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\r\n", 2);
            break;
        }
        else if (c == '\t') {
            buf[pos] = '\0';
            size_t start = pos;
            while (start > 0 && !isspace((unsigned char)buf[start-1]))
                start--;

            trienode *n = find(groot, buf + start);
            if (!n) {
                write(STDOUT_FILENO, "\a", 1);
                tab_count = 0;
                continue;
            }

            if (tab_count == 0) {
                for (int i = 0; i < mcount; i++) free(matches[i]);
                mcount = 0;

                char prefix[100];
                size_t plen = pos - start;
                memcpy(prefix, buf + start, plen);
                prefix[plen] = '\0';

                char suffix[100];
                collect_full(n, prefix, suffix, 0);

                if (mcount == 1) {
                    char *s = matches[0];
                    int already = plen;
                    int extra   = strlen(s) - already;
                    if (pos + extra + 1 < size) {
                        memcpy(buf + pos, s + already, extra);
                        pos += extra;
                        buf[pos++] = ' ';
                        write(STDOUT_FILENO, s + already, extra);
                        write(STDOUT_FILENO, " ", 1);
                    }
                }
                else if (mcount > 1) {
                    // LCP of full completions
                    int common = strlen(matches[0]);
                    for (int i = 1; i < mcount; i++)
                        common = common < (int)strspn(matches[i], matches[0])
                               ? common
                               : (int)strspn(matches[i], matches[0]);

                    int already = plen;
                    int extra   = common - already;
                    if (extra > 0 && pos + extra < size) {
                        memcpy(buf + pos, matches[0] + already, extra);
                        pos += extra;
                        write(STDOUT_FILENO, matches[0] + already, extra);
                    } else {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                }
                else {
                    write(STDOUT_FILENO, "\a", 1);
                }

                tab_count = 1;
            }
            else {
                write(STDOUT_FILENO, "\r\n", 2);
                for (int i = 0; i < mcount; i++) {
                    write(STDOUT_FILENO, matches[i], strlen(matches[i]));
                    if (i + 1 < mcount)
                        write(STDOUT_FILENO, "  ", 2);
                }
                write(STDOUT_FILENO, "\r\n", 2);
                // re-draw prompt + buffer
                print_prompt();
                write(STDOUT_FILENO, buf, pos);
                fflush(stdout);

                tab_count = 0;
            }
        }
        else {
            if (c == 127 || c == '\b') {
                if (pos > 0) {
                    pos--;
                    write(STDOUT_FILENO, "\b \b", 3);
                }
            }
            else if (isprint((unsigned char)c) && pos + 1 < size) {
                buf[pos++] = c;
                write(STDOUT_FILENO, &c, 1);
            }
            tab_count = 0;
        }
    }

    buf[pos] = '\0';
    return pos;
}

int exit_cmd(char **args, int nargs){
    if (nargs>1){
        char *end;
        errno=0;
        long val=strtol(args[1], &end, 10);
        if (errno==0 && *end=='\0'){
            return (int)val;
        }
        else{
            fprintf(stderr,"exit: invalid numeric argument: %s\n",args[1]);
            return 1;
        }
    }
    return 0;
}

void cd(char **args,int nargs){
    char *target = nargs>1 ? args[1] : getenv("HOME");
    if(!target) target="/";

    if(target[0]=='~'){
        char hcp[300];
        strcpy(hcp,getenv("HOME"));
        strcat(hcp,target+1);
        target=hcp;
        }
    if(chdir(target)!=0)
    fprintf(stderr,"cd: %s: %s\n",target,strerror(errno));
    return;
}
    
void pwd(int nargs,int builtin_saved){
    if(nargs>1){
    printf("pwd: Too many arguments");
    }
    else {
        char cwd[300];
        if(getcwd(cwd,sizeof(cwd))){
            printf("%s",cwd);
            if (builtin_saved == -1)
                write(STDOUT_FILENO, "\r\n", 2);
            else
                write(builtin_saved, "\r\n", 2);
        }
    }
    return;
}

void echo(char **args, int nargs){
    for(int i=1;i<nargs;i++){
        printf("%s",args[i]);
        if(i+1<nargs) printf(" ");
    }
    write(STDOUT_FILENO, "\n", 1);
    return;
}

void type(char **args, int nargs, char **builtins){
    char *t = nargs>1 ? args[1] : NULL;
    int fb=0;
    for(int i=0;i<5;i++){
        if(t&&strcmp(t,builtins[i])==0){
            printf("%s is a shell builtin",t);
            write(STDOUT_FILENO, "\n", 1);
            fb=1;
            break;
        }
    }
    if(!fb){
        char *p2 = getenv("PATH");
        char *cp2 = strdup(p2);
        char *d  = strtok(cp2,":");
        int fe=0;
        while(d){
            char fp[300];
            strcpy(fp,d); strcat(fp,"/"); strcat(fp,t?t:"");
            if(t&&access(fp,X_OK)==0){
                printf("%s is %s",t,fp);
                write(STDOUT_FILENO, "\n", 1);
                fe=1;
                break;
            }
            d=strtok(NULL,":");
        }
        free(cp2);
        if(!fe){
            printf("%s: not found",t?t:"");
            write(STDOUT_FILENO, "\n", 1);
        }
    }
}


int runexec(char **arr, int stream, char *red_op, char *red_file){ //if i "somehow" fail to find a builtin... maybe its in the executables list
    int found=0;
    char command[100];
    strcpy(command,arr[0]);
    char *path=getenv("PATH");
    char pcc[300]; strcpy(pcc,path);
    char *dir=strtok(pcc,":");
    char cp[300];

    while(dir){
        strcpy(cp,dir);
        strcat(cp,"/");
        strcat(cp,command);
        if(access(cp,X_OK)==0){
            strcpy(pcc,cp);
            found=1;
            break;
        }
        dir=strtok(NULL,":");
    }
    if(!found) return 0;

    int pid=fork();
    if(pid==0){
        if(red_file){
            int flags = O_WRONLY|O_CREAT
                      | (strstr(red_op, ">>") ? O_APPEND : O_TRUNC);
            int fd=open(red_file, flags, 0666);
            if(fd<0){
                perror(red_file);
                exit(1);
            }
            dup2(fd,stream);
            if(strcmp(red_op,"&>")==0 || strcmp(red_op,"&>>")==0)
                dup2(fd,2);
            close(fd);
        }
        execv(pcc,arr);
        perror("execv");
        exit(1);
    } else {
        wait(NULL);
        return 1;
    }
}



int main(void){
    setbuf(stdout,NULL);
    char *args[100],len=0;
    trienode *sroot =NULL;
    char *builtins[]={"echo","exit","type","pwd","cd",NULL};
    while(builtins[len]){len++;}

    for(int i=0;i<len;i++){
        trieinsert(&sroot,builtins[i]);
    }
    enable_raw_mode();
    while(1){
        print_prompt();
        //i MUST initiate the stupid autocompletion part ;p
        trienode *groot=cptrie(sroot);
        DIR *d=opendir(".");
        if(d){
            struct dirent *ent;
            while(ent=readdir(d)){
                if(ent->d_name[0]=='.') continue;
                trieinsert(&groot,ent->d_name);
            }
            closedir(d);
        }

        char *path_env = getenv("PATH");
        if(path_env){
            char *path_dup = strdup(path_env);
            char *dir = strtok(path_dup, ":");
            while(dir){
                DIR *pd = opendir(dir);
                if(pd){
                    struct dirent *pent;
                    while((pent = readdir(pd))){
                        if(pent->d_name[0]=='.') continue;
                        char full[300];
                        snprintf(full, sizeof(full), "%s/%s", dir, pent->d_name);
                        if(access(full, X_OK) == 0){
                            trieinsert(&groot, pent->d_name);
                        }
                    }
                    closedir(pd);
                }
                dir = strtok(NULL, ":");
            }
            free(path_dup);
        }

        char input[100];
        read_line(input,sizeof input, groot);
        int nargs = parser(input,args);


        if(nargs==0){
            enable_raw_mode();
            continue;
        }

        char *cmd = args[0];
        int stream=-1, index=-1;
        char *red_file=NULL, *red_op=NULL;
        for(int i=0;args[i];i++){
            if(strcmp(args[i],">")==0 || strcmp(args[i],"1>")==0 || strcmp(args[i],">>")==0 || strcmp(args[i],"1>>")==0)
            {
                stream = 1;
                index = i;
                red_op = args[i];
                red_file = args[i+1];
                break;
            }
            if(strcmp(args[i],"2>")==0  || strcmp(args[i],"2>>")==0 ){
                stream = 2;
                index = i;
                red_op = args[i];
                red_file = args[i+1];
                break;
            }

            if(strcmp(args[i],"&>")==0  || strcmp(args[i],"&>>")==0 ){
                stream = 1;
                index = i;
                red_op = args[i];
                red_file = args[i+1];
                break;
            }
        }

        int builtin_saved=-1;
        if(red_file){
            int flags = O_WRONLY|O_CREAT
                      | (strstr(red_op, ">>") ? O_APPEND : O_TRUNC);
            int fd = open(red_file, flags, 0666);
            if(fd<0){
                perror(red_file);
            } else {
                builtin_saved=dup(stream);
                dup2(fd,stream);
                if(strcmp(red_op,"&>")==0 || strcmp(red_op,"&>>")==0)
                    dup2(fd,2);
                close(fd);
            }
        }

        if(index!=-1){
            args[index]=NULL;
            nargs = index;
        }

        if(strcmp(cmd,"exit")==0){
            disable_raw_mode();
            return exit_cmd(args,nargs);
        }
        else if(strcmp(cmd,"cd")==0){
            cd(args,nargs);
        }
        else if(strcmp(cmd,"echo")==0){
            echo(args,nargs);
        }

        else if(strcmp(cmd,"type")==0){
            type(args,nargs,builtins);
        }
        else {
            if(!runexec(args,stream,red_op,red_file)){
                printf("%s: command not found",cmd);
                write(STDOUT_FILENO, "\n", 1);
            }
        }
        if(builtin_saved!=-1){
            dup2(builtin_saved,stream);
            close(builtin_saved);
        }
        fflush(stdout);
        enable_raw_mode();
    }
    disable_raw_mode();
    return 0;
}
