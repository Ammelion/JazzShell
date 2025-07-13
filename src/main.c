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

typedef struct{ //stuct for redirection operators
    int index;
    int stream;
    char *op;
    char *file;
    int sfd;
} redir;

typedef struct histnode{ //struct for recalling history
    char *command;
    struct histnode *prev,*next;
} histnode;

histnode *head=NULL;
histnode *tail=NULL;
histnode *current=NULL;
histnode *last_append=NULL;

int status;
int history(int nargs, char **args);
void addhist(const char *line);

typedef struct trienode
{
    struct trienode *children[256];
    bool terminal;
} trienode;

struct termios orig, raw; //input buffer, custom input parsing ahh

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
//--- search logic for autocompletion ends here-- trust

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

char *matches[256]; // array that stores the matches, Frees after every iteration
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

        if (c == '\x1b') {
        char seq[2];
        if (read(STDIN_FILENO, seq, 2) != 2) continue;
        if (seq[0]=='[' && seq[1]=='A') {         // UP
            if (!current) current = tail;
            else if (current->prev) 
                current = current->prev;
        }
        else if (seq[0]=='[' && seq[1]=='B') {    // DOWN
            if (current) current = current->next;
        }

        while (pos--) write(STDOUT_FILENO, "\b \b", 3);
        if (current) {
            strncpy(buf, current->command, size);
            pos = strlen(buf);
            write(STDOUT_FILENO, buf, pos);
        } else {
            buf[0]='\0'; pos=0;
        }
        continue;
        }

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
    const char *histpath = getenv("HISTFILE");
    if (histpath) {
    FILE *f = fopen(histpath, "a");      // open for appending
    if (f){
        histnode *n = last_append ? last_append->next : head;
        for (; n; n = n->next) {
            fprintf(f, "%s\n", n->command);
        }
        fclose(f);
        }
    }

    if (nargs>1){
        if (strcmp(args[1],"0")==0){
            exit(0);
        }
    }
    return 1;
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

void type(char **args, int nargs, char **builtins, int len){
    char *t = nargs>1 ? args[1] : NULL;
    int fb=0;
    for(int i=0;i<len;i++){
        if(t && strcmp(t,builtins[i])==0){
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

void padpipe(char *in){
    char tmp[512],*w=tmp;
    for (char *r=in; *r;r++) {
        if (*r=='|'){
            *w++=' ';
            *w++='|';
            *w++=' ';
        }else{
            *w++=*r;
        }
    }
    *w='\0';
    strcpy(in,tmp);
}

int exelogic(char **args,int nargs,int in_fd,int out_fd,char *builtins[],int builtin_count,char *red_op,char *red_file,int stream, int len){
    char *cmd=args[0];
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
        type(args,nargs,builtins,len);
    }
    
    else if(strcmp(cmd,"pwd")==0){
        pwd(nargs,builtin_count);
        printf("\n");
    }
    else if(strcmp(cmd,"history")==0){
        return history(nargs,args);
    }
    else {
        if(!runexec(args,stream,red_op,red_file)){
            printf("%s: command not found",cmd);
            write(STDOUT_FILENO, "\n", 1);
        }
    }
    return 0;
}

int breakpipe(char **args,int nargs,char *stages[512][512]){
    int in=0,xi=0;
    for(int i=0;args[i];i++){
        if(strcmp(args[i],"|")==0){
            stages[in][xi]=NULL;
            in++;xi=0;
        }
        else{
            stages[in][xi++]=args[i];
        }
    }
    stages[in][xi]=NULL;
    return in;
} 

redir redirect(char **args){
    redir t={
        .index=-1,
        .stream=1,
        .op=NULL,
        .file=NULL,
        .sfd=-1
    };

    for(int i=0; args[i]; i++){
        if(strcmp(args[i],">")==0 || strcmp(args[i],"1>")==0 || strcmp(args[i],">>")==0 || strcmp(args[i],"1>>")==0){
                t.stream=1;
                t.index=i;
                t.op=args[i];
                t.file=args[i+1];
                break;
        }
        if(strcmp(args[i],"2>")==0  || strcmp(args[i],"2>>")==0){
                t.stream=2;
                t.index=i;
                t.op=args[i];
                t.file=args[i+1];
                break;
        }
        if(strcmp(args[i],"&>")==0  || strcmp(args[i],"&>>")==0){
                t.stream = 1;
                t.index = i;
                t.op = args[i];
                t.file = args[i+1];
                break;
        }
    }
    if (t.index!=-1){
        int flags=O_WRONLY | O_CREAT | (strstr(t.op, ">>") ? O_APPEND : O_TRUNC);
        int fd=open(t.file, flags, 0666);
        if(fd<0){
            perror(t.file);
        }
        else{
            t.sfd=dup(t.stream);
            dup2(fd,t.stream);
            if (strcmp(t.op, "&>")==0 || strcmp(t.op, "&>>")==0)
                dup2(fd,2);
            close(fd);

            args[t.index] = NULL;
        }
    }
    return t;
}

void restore_redirect(redir *t){
    if(t->sfd != -1){
        dup2(t->sfd,t->stream);
        close(t->sfd);
    }
}

int history(int nargs, char **args){
    histnode *n=head;
    int idx=1;
    if (nargs==1){
        while(n){
            printf(" %4d  %s\n",idx++,n->command);
            n = n->next;
        }
    }
    else if (nargs == 3 && strcmp(args[1], "-w") == 0) {
        const char *path=args[2];
        FILE *f = fopen(path,"w");
        if (!f){
            perror(path); return 0;
        }
        for (histnode *t=head;t;t=t->next){
            fprintf(f, "%s\n", t->command);
        }
        fclose(f);
        return 0;
    }

    else if (nargs == 3 && strcmp(args[1], "-a") == 0) {
        const char *path=args[2];
        FILE *f=fopen(path,"a");
        if (!f) {
            perror(path);
            return 0;
        }
        histnode *n = last_append ? last_append->next : head;
        for (; n; n = n->next) {
            fprintf(f, "%s\n", n->command);
        }
        fclose(f);
        last_append = tail;
        return 0;
    }

    else if (strcmp(args[1],"-r")==0 && nargs==3){
        const char *path = args[2];
        FILE *f = fopen(path, "r");
        if (!f) {
            perror(path);
            return 0;
        }
        char *line = NULL;
        size_t cap = 0;
        while (getline(&line, &cap, f) > 0) {
            line[strcspn(line, "\n")] = '\0';
            if (*line) {
                addhist(line);
            }
        }
        free(line);
        fclose(f);
        return 0;
    }
    else if (nargs == 2){
        int N = atoi(args[1]);
        if (N <= 0) return 0;
        int total = 0;
        for (histnode *t = head; t; t = t->next) total++;
        int skip = total > N ? total - N : 0;
        n = head;
        while (skip-- > 0) n = n->next;

        while (n) {
            printf(" %4d  %s\n", idx++, n->command);
            n = n->next;
        }
    }
    return 0;
}

void addhist(const char *line) {
    if (!*line) return;
    histnode *n=malloc(sizeof *n);
    n->command=strdup(line);
    n->prev=tail;
    n->next=NULL;
    if (tail) 
        tail->next=n;
    else
        head = n;
    tail = n;
}


int main(void){
    const char *histpath = getenv("HISTFILE");
    if (histpath) {
    FILE *f = fopen(histpath, "r");
    if (f) {
        char *line = NULL;
        size_t cap = 0;
        while (getline(&line, &cap, f) > 0) {
        line[strcspn(line, "\n")] = '\0';
        if (*line) addhist(line);
        }
        free(line);
        fclose(f);
        }
    }

    int in_fd  = STDIN_FILENO;
    int out_fd = STDOUT_FILENO;
    setbuf(stdout,NULL);
    char *args[100],len=0;
    trienode *sroot =NULL;
    char *builtins[]={"echo","exit","type","pwd","cd","history",NULL};
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

        char input[100],sinput[100];
        read_line(input,sizeof input, groot);
        strcpy(sinput,input);
        if(sinput[0] != '\0'){
            addhist(sinput); 
            current=NULL;
        }

        padpipe(input);
        int nargs = parser(input,args);

        if(nargs==0){
            enable_raw_mode();
            continue;
        }

        char *stages[512][512];
        redir r;
        int nos=breakpipe(args,nargs,stages)+1;

        if(nos==1){
            char **cmd=stages[0];
            if (cmd[0]){
                int bi=-1;
                for (int i=0;builtins[i];++i){
                    if (strcmp(cmd[0],builtins[i])==0){
                        bi=i;
                        break;
                    }
                }
                if (bi >= 0) {
                    redir r = redirect(cmd);
                    int sc = 0;
                    while (cmd[sc]) ++sc;
                    int status = exelogic(cmd,sc,STDIN_FILENO, STDOUT_FILENO,builtins,len,r.op, r.file, r.stream, len);
                    restore_redirect(&r);
                    if (status != 0) {
                        disable_raw_mode();
                        return status;
                    }
                    fflush(stdout);
                    enable_raw_mode();
                    continue;
                }
            }
        }

        int fds[2 * (nos - 1)];
        for (int i = 0; i < nos - 1; ++i) {
            if (pipe(fds + 2*i) < 0) {
                perror("pipe");
                exit(1);
            }
        }
        for (int i = 0; i < nos; ++i) {
            pid_t pid = fork();
            if (pid < 0) { //
                perror("fork");
                exit(1);
            }
            if (pid == 0) {
                if (i > 0) {
                    dup2(fds[2*(i-1)], STDIN_FILENO);
                }
                if (i < nos - 1) {
                    dup2(fds[2*i + 1], STDOUT_FILENO);
                }
                for (int j = 0; j < 2*(nos - 1); ++j) {
                    close(fds[j]);
                }
                redir r = redirect(stages[i]);
                int sc = 0;
                while (stages[i][sc]) ++sc;
                exelogic(stages[i], sc, STDIN_FILENO, STDOUT_FILENO,builtins, len, r.op, r.file, r.stream, len);
                exit(0);
            }
        }
        for (int j = 0; j < 2*(nos - 1); ++j) {
            close(fds[j]);
        }
        int wstatus;
        for (int i = 0; i < nos; ++i) {
            wait(&wstatus);
        }
        fflush(stdout);
        enable_raw_mode();
    }
    disable_raw_mode();
    return 0;
}
