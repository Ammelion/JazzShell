#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
int parser(char *input, char *args[]) {
  int countarg=0, i=0;
  int inquote=0;
  int mergeFlags[100] = {0};  /* CHANGED: track which args came from adjacent quotes */

  while(input[i] && input[i]==' '){i++;}

  i=0; inquote=0;
  while(input[i]){
    if(input[i]=='\''){
      inquote=1;
      i++;
      if(input[i]){
        args[countarg]=&input[i];
        countarg++;
      }
      while(inquote && input[i]){
        if(input[i]=='\'') inquote=0;
        i++;
      }
      input[i-1]='\0';
      continue;
    }
    if(input[i]=='"'){
      inquote=1;
      i++;
      if(input[i]){
        /* CHANGED: if this quote is immediately after another quote, mark for merge */
        if(i>0 && input[i-1]=='"') {
          args[countarg]=&input[i];
          mergeFlags[countarg]=1;
          countarg++;
        } else {
          args[countarg]=&input[i];
          countarg++;
        }
      }
      while(inquote && input[i]){
        if(input[i]=='"') inquote=0;
        i++;
      }
      input[i-1]='\0';
      continue;
    }
    if(input[i] && input[i]!=' '){
      args[countarg]=&input[i];
      countarg++;
    }
    while((input[i] && input[i]!=' ') || inquote){
      if(!inquote && input[i]=='\\' && input[i+1]){
        int k=i;
        while(input[k]){
          input[k]=input[k+1];
          k++;
        }
        i++;
        continue;
      }
      i++;
    }
    if(input[i]) {
      input[i++]='\0';
      while(input[i] && input[i]==' ') i++;
    }
  }

  /* CHANGED: only merge when flagged as adjacent-quote */
  for(int j=0; j < countarg-1; j++){
    if( mergeFlags[j+1]
        && args[j+1] == args[j] + strlen(args[j]) + 1 ) {
      strcat(args[j], args[j+1]);
      for(int k=j+1; k < countarg; k++){
        args[k] = args[k+1];
        mergeFlags[k] = mergeFlags[k+1];
      }
      countarg--;
      j--;  
    }
  }

  args[countarg]=NULL;
  return countarg;
}




int runexec(char **arr){
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
  char *args[100];
  while(1){
    printf("$ ");
    char input[100];
    if(!fgets(input,100,stdin)) break;
    input[strcspn(input,"\n")]='\0';
    int nargs=parser(input,args);
    if(nargs==0) continue;
    char *cmd=args[0];

    if(strcmp(cmd,"exit")==0){
      if(nargs>1 && strcmp(args[1],"0")==0) break;
      continue;
    }

    if(strcmp(cmd,"cd")==0){

      char *target=nargs>1 ? args[1] : getenv("HOME");

      if(!target) target="/";
      if(target[0]=='~'){
        char hcp[300]; 
        strcpy(hcp,getenv("HOME")); 
        strcat(hcp,target+1);
        target=hcp;
      }
      if(chdir(target)!=0) fprintf(stderr,"cd: %s: %s\n",target,strerror(errno));
      continue;
    }

    if(strcmp(cmd,"pwd")==0){
      if(nargs>1){ printf("pwd: Too many arguments\n"); continue; }
      char cwd[300];
      if(getcwd(cwd,sizeof(cwd))) printf("%s\n",cwd);
      continue;
    }

    if(strcmp(cmd,"echo")==0){
      for(int i=1;i<nargs;i++){
        printf("%s",args[i]);
        if(i+1<nargs) printf(" ");
      }
      printf("\n");
      continue;
    }

    if(strcmp(cmd,"type")==0){
      char *t=nargs>1?args[1]:NULL;
      char *builtins[]={"echo","exit","type","pwd","cd"};
      int fb=0;
      for(int i=0;i<5;i++){
        if(t&&strcmp(t,builtins[i])==0){
          printf("%s is a shell builtin\n",t);
          fb=1;
          break;
        }
      }
      if(fb) continue;
      char *p2=getenv("PATH");
      char *cp2=strdup(p2);
      char *dir=strtok(cp2,":");
      int fe=0;
      while(dir){
        char fp[300]; strcpy(fp,dir); strcat(fp,"/"); strcat(fp,t?t:"");
        if(t&&access(fp,X_OK)==0){
          printf("%s is %s\n",t,fp);
          fe=1;
          break;
        }
        dir=strtok(NULL,":");
      }
      free(cp2);
      if(!fe) printf("%s: not found\n",t?t:"");
      continue;
    }

    if(!runexec(args)) printf("%s: command not found\n",cmd);
  }
  return 0;
}
