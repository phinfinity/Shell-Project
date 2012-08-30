/*
 * aniSH - Anish SHell 
 * The following is part of the ICS231 Operating Systems
 * Course at IIIT-Hyderabad
 * It is part of the First project.
 * Author : Anish Shankar
 * Roll No: 201001085
 *
 *
 * Some Assumptions Taken:
 * 	1. Some syntax constraints are required to be followed
 * 	   for e.g some statements like ls & | cat
 * 	2. !histn command is assumed to not be stored as such
 * 	   in the history. It is stored after expansion
 * 	3. in general undefined expression like ls | cd will/may 
 * 	   have unexpected/undefined output and are not expected
 * 	   to be handled in any specific way
 * 	4. The ~ is taken as the directory in which the shell
 * 	   is started and not as the home directory .
 * 	5. Currently there are restrictions on command length 
 * 	   maximum number of arguments specified, and some other
 * 	   size restrictions. These restrictions are quite huge
 * 	   and can be easily increased if required to a much larger
 * 	   size by simply modifying the constants declared right
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <signal.h>

//REMOVE/COMMENT Below to remove debug MESSAGES
//#define DEBUG 0


//Constants
#define HOST_NAME_MAX _SC_HOST_NAME_MAX
#define ARGS_LENGTH_MAX 256
#define PLIST_LENGTH_MAX 4096
#define _HIST_LENGTH_MAX 4096
#define _COMMAND_LENGTH_MAX 4096
#define _PATH_LENGTH_MAX 4096
#define MAX_PIPE 4096
#define EXEC_FLAG_BACKGROUND 1
#define EXEC_MORE_PIPES 2
char *white_space=" \t\f\r\v\n";
int PATH_LENGTH_MAX=_PATH_LENGTH_MAX;

//Shell/User Info:
extern char *program_invocation_short_name;
extern char *program_invocation_name;
char *orig_program_invocation_name;
char *homedir;
int homelen;
char hostname[HOST_NAME_MAX];
uid_t curr_user_id;
struct passwd* user_info;
char *user_name;
pid_t mypid;
int shell_in; //Points to STDIN_FILENO
int shell_out; //Points to STDOUT_FILENO

//Process history

typedef struct p_info{
   char *process_name;
   //process name (with path)
   pid_t pid;
   int inp_fd;
   int out_fd;
   pid_t gpid;
}p_info;

int COMMAND_LENGTH_MAX=_COMMAND_LENGTH_MAX;
char *last_command;
p_info active_process_list[PLIST_LENGTH_MAX];
int actv_plist_cnt=0;
p_info process_list[PLIST_LENGTH_MAX];
int plist_end=0,plist_start=0;
//process_list is a queue with elements starting from plist_start warping around till plist_end not including plist_end , max of PLIST_LENGTH_MAX-1 elements

void exec_hist(int hist_val);
int check_hist_str(char *a);
int HIST_LENGTH_MAX=_HIST_LENGTH_MAX;
char **comm_history;
int hist_size=0;
//comm_history is a dynamically resizing list 

//function declarations:
void signal_handler(int signal);
void init();
char *get_formatted_cwd();
void print_prompt();
void input_shell_command();
int parse_input();
pid_t exec_command(char *comm_name,char **args,int flags,pid_t group_id,int infile,int outfile);
void exec_pid(char **args);
pid_t process_single_command(char *comm,int inp_fd,int out_fd,pid_t pgid,int exec_flags);
void process_shell_commad();

void check_child_status(){
   register int i;
   int cpid,status;
   while((cpid=waitpid(WAIT_ANY,&status,WNOHANG|WUNTRACED))>0){
      if(WIFEXITED(status)|WIFSIGNALED(status)){
	 for(i=0;i<actv_plist_cnt;i++){
	    if(active_process_list[i].pid==cpid){
	       /*
	       if(active_process_list[i].inp_fd!=STDIN_FILENO)
		  close(active_process_list[i].inp_fd);
	       if(active_process_list[i].out_fd!=STDOUT_FILENO)
		  close(active_process_list[i].out_fd);
		*/
	       if(WIFEXITED(status)){
		  status=WEXITSTATUS(status);
		  if(status==0)
		     fprintf(stderr,"%s %d exited normally\n",active_process_list[i].process_name,active_process_list[i].pid);
		  else
		     fprintf(stderr,"%s %d exited with %d\n",active_process_list[i].process_name,active_process_list[i].pid,status);
	       }
	       else{
		  status=WTERMSIG(status);
		  fprintf(stderr,"%s %d terminated with signal %d\n",active_process_list[i].process_name,active_process_list[i].pid,status);
	       }
	       actv_plist_cnt--;
	       while(i<actv_plist_cnt){
		  active_process_list[i]=active_process_list[i+1];
		  i++;
	       }
	       break;
	    }
	 }
      }
   }
}

void signal_handler(int signal){
   switch(signal){
      case SIGCHLD:
	 //check_child_status();
	 /* Not doing anything as it is not very pleasing to have text appear
	    halfway between a process or when a command is being typed */
	 break;
      case SIGINT:
#ifdef DEBUG
	 fprintf(stderr,"SIGINT RECIEVED!!\n");
#endif
	 printf("\n");
	 print_prompt();
	 fflush(stdout);
	 break;
      default:
	 fprintf(stderr,"UNHANDLED SIGNAL(%d) RECIEVED\n",signal);
   }
}


void init(){
   orig_program_invocation_name=program_invocation_name;
   program_invocation_name=program_invocation_short_name;
   signal (SIGCHLD,signal_handler);
   signal (SIGINT, signal_handler);
   signal (SIGQUIT, SIG_IGN);
   signal (SIGTSTP, SIG_IGN);
   signal (SIGTTIN, SIG_IGN);
   signal (SIGTTOU, SIG_IGN);
   mypid=getpid();
   setpgid(mypid,mypid);	//process grouping
   shell_in=STDIN_FILENO;
   shell_out=STDOUT_FILENO;
   tcsetpgrp(shell_in,mypid);
   curr_user_id=getuid();
   user_info=getpwuid(curr_user_id);
   user_name=user_info->pw_name;
   //homedir=user_info->pw_dir;
   /* #NOTE Some wierd convention requires the ~ to refer to the 
    * directory the shell was instantiated in instead of the home directory
    */
   homedir=malloc(PATH_LENGTH_MAX);
   while(getcwd(homedir,PATH_LENGTH_MAX)==NULL){
      if(errno==ERANGE){
	 PATH_LENGTH_MAX*=2;
	 homedir=realloc(homedir,PATH_LENGTH_MAX);
      }
      else{
	 perror("getcwd");
	 exit(2);
      }
   }
   homelen=strlen(homedir);
   gethostname(hostname,sizeof(hostname));
   last_command=malloc(COMMAND_LENGTH_MAX);
   comm_history=malloc(HIST_LENGTH_MAX*sizeof(char*));
}

char *get_formatted_cwd(){
   char *cwd=getcwd(NULL,0); 
   if(strncmp(homedir,cwd,homelen)==0){
      int cwdlen=strlen(cwd);
      *cwd='~';
      memmove(cwd+1,cwd+homelen,cwdlen-homelen+1);
   }
   return cwd;
}

void print_prompt(){
   char *cwd=get_formatted_cwd();
   printf("<%s@%s:%s> ",user_name,hostname,cwd);
   free(cwd);
}

void input_shell_command(){
   last_command[0]='\0';
   fgets(last_command,COMMAND_LENGTH_MAX,stdin);
   if(last_command[0]=='\0'){
      puts("");
      exit(0);
   }
}

int parse_input(){
   //should transform !histn to expanded value and should space separat pipes and remove spaces before <,>
   //this will allow strtok to easily split it lateor
   int bufl=COMMAND_LENGTH_MAX;
   char *tmpbuffer=malloc(bufl);
   char *putp=tmpbuffer;
   char *putplim=tmpbuffer+bufl;
   char *getp=last_command;
   //pass1 do blind replacement without any additional space for all !histn entries

   while(*getp){
      while(*getp!='!' && *getp && putp!=putplim) 
	 *putp++ = *getp++;
      if(putp==putplim) return -2;
      else if(*getp=='!'){
	 if(strncmp(getp+1,"hist",4)==0 && isdigit(getp[5])){
	    int histcnt=0;
	    getp+=5;
	    while(isdigit(*getp))
	       histcnt=histcnt*10+*(getp++) -'0';
	    if(histcnt>hist_size)
	       return -1;
	    int histl=strlen(comm_history[histcnt-1]);
	    if(histl+putp>=putplim)return -2;
	    strcat(putp,comm_history[histcnt-1]);
	    putp+=histl;
	 }
	 else return -1;
      }
   }
   *putp++='\0';
   getp=tmpbuffer;
   putp=last_command;
   //pass2 do spacing and syntax checking
   char curc;
   int somecomm=0;
   int nosp=0;
   while(*getp){
      /* specail characters to handle:
       *     < > & | !
       */
      curc=*getp;
      if((curc=='<' || curc=='>' || curc=='|' || curc=='&') && nosp){
	 *putp++=' ';
	 nosp=0;
      }
      if(!isspace(curc)){
	 *putp++=*getp++;
	 nosp=1;
      }
      if(isspace(curc)){
	 while(isspace(*getp))
	    getp++;
      }
      else if(curc=='<' || curc=='>'){
	 if(!somecomm)
	    return -1;
	 //action bring immediate next string right next to it and remove rest till | , & or end
	 //if removed text is not just whitespace syntax error
	 while(isspace(*getp))getp++;
	 while(!isspace(*getp) && *getp!='|' && *getp!='&' && *getp!='<' && *getp!='>' && *getp)
	    *putp++=*getp++;
	 nosp=1;
	 while(*getp!='|' && *getp!='&' && *getp!='<' && *getp!='>' && *getp)
	    if(!isspace(*getp++))
	       return -1;
      }
      else if(curc=='&'){
	 if(!somecomm)
	    return -1;
	 //action remove rest after it till end
	 //if removed text is not just whitespace syntax error
	 while(isspace(*getp))getp++;
	 if(*getp!='\0')
	    return -1;

      }
      else if(curc=='|'){
	 //action ensure space before and after
	 somecomm=0;
      }
      else{
	 somecomm=1;
      }
      if(*getp!='\0' &&(curc=='<' || curc=='|' || curc=='>' || isspace(curc))){
	 *putp++=' ';
	 nosp=0;
      }
      else
	 nosp=1;
   }
   *putp='\0';
   return 0;
}

int check_hist_str(char *a){
   if(a[0]=='h' && a[1]=='i' && a[2]=='s' && a[3]=='t'){
      int ret=0;
      a+=4;
      while(isdigit(*a))
	 ret=ret*10+(*a++)-'0';
      if(*a=='\0' || isspace(*a))
	 return ret;
      else
	 return -1;
   }
   return -1;
}

void exec_hist(int hist_val){
   register int i=hist_size-hist_val,cnt=1;
   if(hist_val==0)
      i=0;
   if(i<0)i=0;
   for(;i<hist_size;i++)
      printf("%d. %s\n",cnt++,comm_history[i]);
}

pid_t exec_command(char *comm_name,char **args,int flags,pid_t group_id,int infile,int outfile){
   pid_t cpid = fork();
   int hist_ret;
   if(cpid==0){
      //child:
      //Reset Signals
      signal (SIGINT ,SIG_DFL);
      signal (SIGQUIT,SIG_DFL);
      signal (SIGTSTP,SIG_DFL);
      signal (SIGTTIN,SIG_DFL);
      signal (SIGTTOU,SIG_DFL);
      signal (SIGCHLD,SIG_DFL);
      cpid=getpid();
      if(group_id==0)group_id=cpid;
      setpgid(cpid,group_id);
      if(infile!=STDIN_FILENO){
	 dup2(infile,STDIN_FILENO);
	 close(infile);
      }
      if(outfile!=STDOUT_FILENO){
	 dup2(outfile,STDOUT_FILENO);
	 close(outfile);
      }
      if(flags&EXEC_FLAG_BACKGROUND){
      }
      else{
	 tcsetpgrp(shell_in,group_id);
      }
      if(!strcmp(comm_name,"pid")){
	 exec_pid(args);
	 exit(0);
      }
      else if((hist_ret=check_hist_str(comm_name))!=-1){
	 //Check for hist command and process hist #TODz
	 exec_hist(hist_ret);
	 exit(0);
      }
      else{
	 int execret=execvp(comm_name,args);
	 if(execret==-1){
	    error(0,0,"%s : %s\n",comm_name,strerror(errno));
	    exit(2);
	 }
      }
   }
   else{
      //parent:
      if(infile!=STDIN_FILENO)
	 close(infile);
      if(outfile!=STDOUT_FILENO)
	 close(outfile);
      process_list[plist_end].pid=cpid;
      process_list[plist_end].process_name=strdup(args[0]);
      plist_end=(plist_end+1)%PLIST_LENGTH_MAX;
      if(plist_end==plist_start)plist_start=(plist_start+1)%PLIST_LENGTH_MAX;
      if(group_id==0)group_id=cpid;
      setpgid(cpid,group_id);
      if(flags&(EXEC_FLAG_BACKGROUND)){
	 //add process to ActiveProcess list and continue
	 active_process_list[actv_plist_cnt].pid=cpid;
	 active_process_list[actv_plist_cnt].process_name=strdup(args[0]);
	 actv_plist_cnt++;
	 if(actv_plist_cnt>=PLIST_LENGTH_MAX){
	    error(0,0,"Cannot manage more than %d Active Processes. Process Killed\n",PLIST_LENGTH_MAX);
	    kill(cpid,SIGKILL);
	    actv_plist_cnt--;
	 }
      }
      else if(flags&(EXEC_MORE_PIPES)){
	 tcsetpgrp(shell_in,group_id);
      }
      else{
	 //wait patiently till process exits
	 tcsetpgrp(shell_in,group_id);
	 waitpid(cpid,NULL,0);
	 tcsetpgrp(shell_in,mypid);
      }
   }
   return cpid;
}

void exec_pid(char **args){
   register int i;
   if(args[1]==(char*)0){
      printf("command name: %s process id: %d\n",orig_program_invocation_name,mypid);
   }
   else if(!strcmp(args[1],"current")){
      printf("List of currently executing processes spawned from this shell:\n");
      for(i=0;i<actv_plist_cnt;i++)
	 printf("command name: %s process id: %d\n",active_process_list[i].process_name,active_process_list[i].pid); 
   }
   else if(!strcmp(args[1],"all")){
      printf("List of all processes spawned from this shell:\n");
      for(i=plist_start;i!=plist_end;i=(i+1)%PLIST_LENGTH_MAX)
	 printf("command name: %s process id: %d\n",process_list[i].process_name,process_list[i].pid); 
   }
   else{
      error(2,0,"Syntax Error. Usage: pid [current|all]\n");
   }
}


pid_t process_single_command(char *comm,int inp_fd,int out_fd,pid_t pgid,int exec_flags){
   char *args[ARGS_LENGTH_MAX];
   int argcnt=0;
   char *saveptr;
   char *token=strtok_r(comm,white_space,&saveptr);
   pid_t ret_pid=0;
   //split arguments on whitespace
   while(token!=NULL){
      //alocate memory and copy argument
      args[argcnt++]=strdup(token);
      if(argcnt>=ARGS_LENGTH_MAX){
	 error(0,0,"Max Number of Arguments Exceeded\n");
	 ret_pid=-1;
	 goto end_cleanup;
      }
      token=strtok_r(NULL,white_space,&saveptr);
   }
   while(argcnt>0 && args[argcnt-1][0]=='&'){
      exec_flags|=EXEC_FLAG_BACKGROUND;
      argcnt--;
   }
   char *infilename=NULL,*outfilename=NULL;
   while(argcnt>0 && (args[argcnt-1][0]=='<' || args[argcnt-1][0]=='>')){
      argcnt--;
      if(args[argcnt][0]=='<'){
	 if(infilename)free(infilename);
	 infilename=strdup(args[argcnt]+1);
      }
      else{
	 if(outfilename)free(outfilename);
	 outfilename=strdup(args[argcnt]+1);
      }
   }
   if(infilename){
      inp_fd=open(infilename,O_RDONLY);
      if(inp_fd<0){
	 perror("Input Redirect Error");
	 ret_pid=-1;
	 goto end_cleanup;
      }
   }
   if(outfilename){
      out_fd=open(outfilename,O_WRONLY | O_TRUNC | O_CREAT,S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
      if(out_fd<0){
	 perror("Output Redirect Error");
	 ret_pid=-1;
	 goto end_cleanup;
      }
   }
   if(argcnt>0 && argcnt<ARGS_LENGTH_MAX-1){
      args[argcnt]=(char*)0;
      if(!strcmp(args[0],"quit") || !strcmp(args[0],"exit"))
	 exit(0);
      else if(!strcmp(args[0],"cd")){
	 if(argcnt>2){
	    error(0,0,"cd Cannot have more than 1 argument");
	    ret_pid=-1;
	    goto end_cleanup;
	 }
	 else{
	    int chdir_ret;
	    if(argcnt>1)
	       chdir_ret=chdir(args[1]);
	    else
	       chdir_ret=chdir(homedir);
	    if(chdir_ret!=0){
	       perror("Error");
	       ret_pid=-1;
	       goto end_cleanup;
	    }
	 }
      }
      else{
	 ret_pid=exec_command(args[0],args,exec_flags,pgid,inp_fd,out_fd);
      }
   }
end_cleanup:
   while(argcnt--)
      free(args[argcnt]);
   return ret_pid;
}

void process_shell_commad(){

   if(last_command[0]=='\0' || (last_command[0]==' ' && last_command[1]=='\0'))
      return;
   //STORE HISTORY
   comm_history[hist_size++]=strdup(last_command);
   if(hist_size==HIST_LENGTH_MAX){
      HIST_LENGTH_MAX*=2;
      comm_history=realloc(comm_history,HIST_LENGTH_MAX);
   }
   //split on pipe;
   char *pipelist[MAX_PIPE];
   int pipcnt=0;
   pipelist[pipcnt]=strtok(last_command,"|");
   while(pipelist[pipcnt])
      pipelist[++pipcnt]=strtok(NULL,"|");

   int fd[2],curin,curout,prevout;
   register int i;
   prevout=STDIN_FILENO;
   pid_t group_id=0,retid;
   for(i=0;i<pipcnt;i++){
      curin=prevout;
      if(i+1<pipcnt){
	 pipe(fd);
	 curout=fd[1];
	 prevout=fd[0];
      }
      else
	 curout=STDOUT_FILENO;
      retid=process_single_command(pipelist[i],curin,curout,group_id,(i+1<pipcnt)?EXEC_MORE_PIPES:0);
      if(group_id==0)
	 group_id=retid;
   }
   tcsetpgrp(shell_in,mypid);
}

int main(int argc,char **argv){
   init();
   while(1){
      check_child_status();
      print_prompt();
      input_shell_command();
      int status=parse_input();
      if(status!=0)
	 error(0,0,(status==-1?"Syntax Error":"Command Too Long"));
      else{
	 process_shell_commad();
      }
   }
   return 0;
}
