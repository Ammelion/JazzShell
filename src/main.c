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

typedef struct trienode //enables awesome lookup time
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

    for(int i=0; i<256; i++){
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

static int dfs(trienode *node, char *buf, int depth) {
    int found = 0;
    if (node->terminal) {
        buf[depth] = '\0';
        found = 1;
    }
    for (int c = 0; c < 256 && found < 2; c++) {
        if (node->children[c]) {
            buf[depth] = (char)c;
            int n = dfs(node->children[c], buf, depth + 1);
            found += n;
            if (found > 1) break;
        }
    }
    return found;
}

bool trie_unique_suffix(trienode *node, char *outbuf) {
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

int runexec(char **arr, int stream, char *red_op, char *red_file){
    int found = 0;
    char command[100];
    strcpy(command, arr[0]);
    char *path = getenv("PATH");
    char pcc[300];  strcpy(pcc, path);
    char *dir = strtok(pcc, ":");
    char cp[300];

    while (dir) {
        strcpy(cp, dir);
        strcat(cp, "/");
        strcat(cp, command);
        if (access(cp, X_OK) == 0) {
            strcpy(pcc, cp);
            found = 1;
            break;
        }
        dir = strtok(NULL, ":");
    }
    if (!found) return 0;

    pid_t pid = fork();
    if (pid == 0) {
        // --- child ---
        if (red_file) {
            int flags = O_WRONLY | O_CREAT
                      | (strstr(red_op, ">>") ? O_APPEND : O_TRUNC);
            int fd = open(red_file, flags, 0666);
            if (fd < 0) {
                perror(red_file);
                exit(1);
            }
            dup2(fd, stream);
            if (strcmp(red_op, "&>") == 0 || strcmp(red_op, "&>>") == 0)
                dup2(fd, 2);
            close(fd);
        }
        execv(pcc, arr);
        perror("execv");
        exit(1);
    } else {
        // --- parent ---
        wait(NULL);

        // Only do our “fix‑up” if:
        //  - stdout was NOT redirected (stream != 1)
        //  - this was exactly “cat filename” (no extra args)
        if (stream != 1
            && strcmp(arr[0], "cat") == 0
            && arr[1] != NULL
            && arr[2] == NULL)
        {
            // open the file and check its last byte
            int f = open(arr[1], O_RDONLY);
            if (f >= 0) {
                off_t last = lseek(f, -1, SEEK_END);
                if (last >= 0) {
                    char ch;
                    if (read(f, &ch, 1) == 1 && ch != '\n') {
                        write(STDOUT_FILENO, "\r\n", 2);
                    }
                }
                close(f);
            } else {
                // if we couldn’t open it, still print a newline
                write(STDOUT_FILENO, "\r\n", 2);
            }
        }

        return 1;
    }
}

ssize_t read_line(char *buf, size_t size, trienode *groot){
    size_t pos=0;
    while(1){
        char c;
        if (read(STDIN_FILENO,&c,1)<=0) return -1;
        if (c=='\n' || c == '\r') {
            write(STDOUT_FILENO, "\r\n", 2);
            break;
        }
        else if(c=='\t'){
            buf[pos] = '\0';
            trienode *n = find(groot, buf);
            if (!n) {
                write(STDOUT_FILENO, "\x07", 1);
                continue;
            }
            char suffix[100];
            if (n && trie_unique_suffix(n, suffix)) {
                int add = strlen(suffix);
                if (pos + add + 1 < size) {
                    memcpy(buf + pos, suffix, add);
                    pos += add;
                    buf[pos++] = ' ';
                    write(STDOUT_FILENO, suffix, add);
                    write(STDOUT_FILENO, " ", 1);
                }
            }
        }
        else if(c==127 || c=='\b'){
            if (pos > 0){
                pos--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
        } else if (isprint((unsigned char)c)){
            if(pos + 1 < size){
                buf[pos++] = c;
                write(STDOUT_FILENO, &c, 1);
            }
        }
    }
    buf[pos] = '\0';
    return pos;
}

void print_prompt(){
    if (isatty(STDOUT_FILENO))
        write(STDOUT_FILENO, "\r$ ", 3);
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
        for(int i=0; args[i]; i++){
            if(strcmp(args[i],">")==0   || strcmp(args[i],"1>")==0  ||
               strcmp(args[i],">>")==0  || strcmp(args[i],"1>>")==0 )
            {
                stream   = 1;
                index    = i;
                red_op   = args[i];
                red_file = args[i+1];
                break;
            }
            if(strcmp(args[i],"2>")==0  || strcmp(args[i],"2>>")==0 ){
                stream   = 2;
                index    = i;
                red_op   = args[i];
                red_file = args[i+1];
                break;
            }

            if(strcmp(args[i],"&>")==0  || strcmp(args[i],"&>>")==0 ){
                stream   = 1;
                index    = i;
                red_op   = args[i];
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
            if(nargs>1 && strcmp(args[1],"0")==0) break;
        }
        else if(strcmp(cmd,"cd")==0){
            char *target = nargs>1 ? args[1] : getenv("HOME");
            if(!target) target="/";
            if(target[0]=='~'){
                char hcp[300];
                strcpy(hcp,getenv("HOME"));
                strcat(hcp,target+1);
                target=hcp;
            }
            if(chdir(target)!=0){
                fprintf(stderr,"cd: %s: %s",target,strerror(errno));
                write(STDERR_FILENO, "\r\n", 2);
            } else {
                if(stream != 1)
                    write(STDOUT_FILENO, "\r\n", 2);
            }
        }  
        else if(strcmp(cmd,"pwd")==0){
            if(nargs>1){
                printf("pwd: Too many arguments");
                if(stream != 1)
                    write(STDOUT_FILENO, "\r\n", 2);
            }
            else {
                char cwd[300];
                if(getcwd(cwd,sizeof(cwd))){
                    printf("%s",cwd);
                    if(stream != 1)
                        write(STDOUT_FILENO, "\r\n", 2);
                }
            }
        }
        else if(strcmp(cmd,"echo")==0){
            for(int i=1;i<nargs;i++){
                printf("%s",args[i]);
                if(i+1<nargs) printf(" ");
            }
            if(stream != 1)
                write(STDOUT_FILENO, "\r\n", 2);
        }

        else if(strcmp(cmd,"type")==0){
            char *t = nargs>1 ? args[1] : NULL;
            int fb=0;
            for(int i=0;i<5;i++){
                if(t&&strcmp(t,builtins[i])==0){
                    printf("%s is a shell builtin",t);
                    if(stream != 1)
                        write(STDOUT_FILENO, "\r\n", 2);
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
                        if(stream != 1)
                            write(STDOUT_FILENO, "\r\n", 2);
                        fe=1;
                        break;
                    }
                    d=strtok(NULL,":");
                }
                free(cp2);
                if(!fe) {
                    printf("%s: not found",t?t:"");
                    if(stream != 1)
                        write(STDOUT_FILENO, "\r\n", 2);
                }
            }
        }
        else {
            if(!runexec(args,stream,red_op,red_file)){
                printf("%s: command not found",cmd);
                if(stream != 1)
                    write(STDOUT_FILENO, "\r\n", 2);
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
