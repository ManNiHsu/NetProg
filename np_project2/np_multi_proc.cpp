#include <iostream>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <array>
#include <sstream>
#include <algorithm>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <arpa/inet.h>
#include <sys/stat.h>

using namespace std;

#define	MAXLINE	15000
#define MAXUSER 31
#define MAXNAMELEN 25
#define MAXADDRLEN 20
#define MAXMSGLEN 1030
#define	SHMKEY	((key_t) 4999) /* base value for shmem key */

struct pipeRec
{
    int line;
    int pipe0;
    int pipe1;
    int err;    
};

struct shm {
    int port[MAXUSER];
    char addr[MAXUSER][MAXADDRLEN];
    int sockfd[MAXUSER];
    char name[MAXUSER][MAXNAMELEN];
    char msg[MAXUSER][MAXMSGLEN];
    int userpipe[MAXUSER][MAXUSER];
    pid_t pid[MAXUSER];
    int waitsig[MAXUSER];
};

struct shm* shmptr;

int numOfWait = 0;
vector<pid_t> childs;
int shmid = -1;

int getMyId(){
    pid_t mypid = getpid();
    for(int i = 1; i<MAXUSER; i++){
        if(shmptr->pid[i] == mypid){
            return(i);
        }
    }
    return(-1);
}

void delMyInfoInShm(){
    int id = getMyId();
    memset(shmptr->addr[id], '\0', MAXADDRLEN);
    strcpy(shmptr->name[id], "(no name)");
    memset(shmptr->msg[id], '\0', MAXMSGLEN);
    shmptr->port[id] = -1;
    shmptr->sockfd[id] = -1;
    shmptr->pid[id] = -1;
    shmptr->waitsig[id] = 0;

    for(int i = 0; i<MAXUSER; i++){
        shmptr->userpipe[id][i] = -1;
        shmptr->userpipe[i][id] = -1;
    }
}

void sendMsgto(int id, char* msg){
    strcpy(shmptr->msg[id], msg);
    shmptr->waitsig[getMyId()] = 1;
    kill(shmptr->pid[id], SIGUSR1);
    while(shmptr->waitsig[getMyId()] == 1);
}

void broacast(char* msg){
    for(int i = 0; i<MAXUSER; i++){
        if(shmptr->pid[i] >= 0){
            sendMsgto(i, msg);
        }
    }
}

void readMsg(int signo){
    int id = getMyId();
    if(id < 0){
        cout << "I am master sock!\n";
        fflush(stdout);
        return;
    }
    cout << shmptr->msg[id];
    memset(shmptr->msg[id], '\0', MAXMSGLEN);
    fflush(stdout);
    for(int i = 0; i<MAXUSER; i++){
        if(shmptr->waitsig[i] == 1){
            kill(shmptr->pid[i], SIGUSR2);
            break;
        }
    }
}

void writeEnd(int signal){
    shmptr->waitsig[getMyId()] = 0;
}

void sig_int(int signal){
    for(int i = 0; i<MAXUSER; i++){
        for(int j = 0; j<MAXUSER; j++){
            if(shmptr->userpipe[i][j] > 0){
                string fifoname = "./user_pipe/" + to_string(i) + "to" + to_string(j);;
                if(remove(fifoname.c_str()) < 0);
                    // perror("remove file");
            }
        }
    }
    if(rmdir("./user_pipe") < 0);
        // perror("remove dir");
    if(shmdt(shmptr) < 0);
        // cerr << "can't detach the shared memory segment\n";
    if(shmctl(shmid, IPC_RMID, NULL) < 0);
        // cerr << "can't remove the shared memory segment\n";
    // cout << "close server\n";
    exit(0);
}

void sig_chld(int signo)
{
    pid_t pid;
    int stat;
    while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0){
        vector<pid_t>::iterator it = find(childs.begin(), childs.end(), pid); // find 10
        if (it != childs.end())
            numOfWait++;
    }
    return;
}


int npshell(){
    int myid = getMyId();
    numOfWait = 0;
    childs.clear();
    int fileRed = 0;
    int savedFd;
    int numPipe = 0;
    vector<pipeRec> numberedPipe;

    int line = 0;
    while(++line){
        int isNumberedPipe = 0;
        string cmd;
        vector<string> input;
        
        vector<int> pipePos;
        pipePos.push_back(0);

        if(fileRed == 1){
            // cerr << savedFd << " recover " << STDOUT_FILENO << endl;
            fileRed = 0;
            dup2(savedFd, STDOUT_FILENO);
            close(savedFd);
        }

        string str;
        cerr << "% ";
        getline(cin, str);
        if(str == ""){
            line--;
            continue;
        }
        stringstream allInput(str);
        pipeRec newNumberedPipe;
        int sendmsg = 0;
        int error = 0;
        int userpipeWFd = -1;
        int userpipeRFd = -1;
        string fifoname = "";
        char recv_msg[MAXMSGLEN] = "";
        char send_msg[MAXMSGLEN] = "";
        while(allInput >> cmd){
            if(cmd == "exit" || cmd == "EOF"){
                return(0);
            }
            else if(cmd == "yell" || cmd == "tell"){
                sendmsg = 1;
            }

            if(fileRed == 1){
                const char* path = cmd.c_str();
                int fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0666);//dup target file to STDOUT_FILENO(1)
                dup2(fd, STDOUT_FILENO);
                close(fd);
                break;
            }
            else if(cmd == ">" && sendmsg == 0){
                fileRed = 1;
                savedFd = dup(STDOUT_FILENO); //save STDOUT_FILENO(1)
            }
            else if(cmd == "|" && sendmsg == 0){
                pipePos.push_back(input.size());
            }
            else if((cmd[0] == '|' || cmd[0] == '!') && sendmsg == 0){
                int nline = 0;
                for(int i = 1; i<cmd.length(); i++)
                    nline = nline*10 + (cmd[i] - '0');
                if(cmd[0] == '|')
                    newNumberedPipe = {line+nline, 0, 0, 0};
                else if(cmd[0] == '!')
                    newNumberedPipe = {line+nline, 0, 0, 1};
                isNumberedPipe = 1;
                break;
            }
            // user pipe
            else if(cmd[0] == '>' && sendmsg == 0){
                int senderId = myid;
                string rec;
                rec.assign(cmd, 1, cmd.size()-1);
                int receiverId = stoi(rec);
                if(receiverId >= MAXUSER || shmptr->pid[receiverId] < 0){
                    cerr << "*** Error: user #" << receiverId << " does not exist yet. ***\n";
                    error = 1;
                    // break;
                    continue;
                }
                else if(shmptr->userpipe[senderId][receiverId] > 0){
                    cerr << "*** Error: the pipe #" << senderId << "->#" << receiverId << " already exists. ***\n";
                    error = 1;
                    // break;
                    continue;
                }
                else{
                    fifoname = "./user_pipe/" + to_string(senderId) + "to" + to_string(receiverId);
                    if ( (userpipeWFd = open(fifoname.c_str(), O_CREAT | O_RDWR | O_TRUNC, S_IREAD | S_IWRITE)) < 0){
                        // perror("can't open read fifo");
                        error = 1;
                        // break;
                        continue;
                    }
                    shmptr->userpipe[senderId][receiverId] = userpipeWFd;
                    string command = str;
                    command.erase(remove(command.begin(), command.end(), '\n'), command.end());
                    command.erase(remove(command.begin(), command.end(), '\r'), command.end());
                    // *** <sender_name> (#<sender_id>) just piped ’<command>’ to <receiver_name> (#<receiver_id>) ***
                    sprintf(send_msg, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", 
                            shmptr->name[senderId], senderId, command.c_str(), shmptr->name[receiverId], receiverId);
                }
            }
            else if(cmd[0] == '<' && sendmsg == 0){
                int receiverId = myid;
                string sen;
                sen.assign(cmd, 1, cmd.size()-1);
                int senderId = stoi(sen);
                if(senderId >= MAXUSER || shmptr->pid[senderId] < 0){
                    cerr << "*** Error: user #" << senderId << " does not exist yet. ***\n";
                    // error = 1;
                    // break;
                    error = 2;
                    continue;
                }
                else if(shmptr->userpipe[senderId][receiverId] < 0){
                    cerr << "*** Error: the pipe #" << senderId << "->#" << receiverId << " does not exist yet. ***\n";
                    // error = 1;
                    // break;
                    error = 2;
                    continue;
                }
                else{
                    fifoname = "./user_pipe/" + to_string(senderId) + "to" + to_string(receiverId);
                    if ( (userpipeRFd = open(fifoname.c_str(), O_RDWR)) < 0){
                        // perror("can't open read fifo");
                        // error = 1;
                        // break;
                        error = 2;
                        continue;
                    }
                    shmptr->userpipe[senderId][receiverId] = -1;
                    string command = str;
                    command.erase(remove(command.begin(), command.end(), '\n'), command.end());
                    command.erase(remove(command.begin(), command.end(), '\r'), command.end());
                    // *** <receiver_name> (#<receiver_id>) just received from <sender_name> (#<sender_id>) by ’<command>’ ***
                    sprintf(recv_msg, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", 
                            shmptr->name[receiverId], receiverId, shmptr->name[senderId], senderId, command.c_str() );
                    
                }
            }
            else
                input.push_back(cmd);
        }
        // if(error == 1){
        //     continue;
        // }
        if(userpipeRFd >= 0){
            // cerr << myid << " broacast recv_msg\n"; 
            broacast(recv_msg);
        }
        if(userpipeWFd >= 0){
            // cerr << myid << " broacast send_msg\n"; 
            broacast(send_msg);
        }
        
        pipePos.push_back(input.size());
        
        if(input[0].compare("setenv") == 0){
            if(input.size() < 3)
                continue;
            if(setenv(input[1].c_str(), input[2].c_str(), 1) < 0) perror("setenv");
            continue;
        }
        else if(input[0].compare("printenv") == 0){
            if(input.size() < 2)
                continue;
            if(getenv(input[1].c_str()))
                cout << getenv(input[1].c_str()) << endl;
                fflush(stdout);
            continue;
        }
        else if(input[0].compare("who") == 0){
            cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
            for(int i = 1; i<MAXUSER; i++){
                if(shmptr->pid[i] > -1){
                    cout << i << "\t" << shmptr->name[i] << "\t" << shmptr->addr[i] << ":" << shmptr->port[i] << "\t";
                    if(i == myid)
                        cout << "<-me";
                    cout << endl;
                }
            }
            continue;
        }
        // yell <message>:
        else if(input[0].compare("yell") == 0){
            if(input.size() < 2){
                continue;
            }
            string msg;
            msg.assign(allInput.str(), input[0].size()+1, (allInput.str()).size()-1);
            // *** <sender’s name> yelled ***: <message>
            // msg = "*** " + shmptr->name[myid] + " yelled ***: " + msg + "\n";
            char broacastMsg[MAXMSGLEN];
            sprintf(broacastMsg, "*** %s yelled ***: %s\n", shmptr->name[myid], msg.c_str());
            broacast(broacastMsg);
            continue;
        }
        // tell <user id> <message>:
        else if(input[0].compare("tell") == 0){
            if(input.size() < 3){
                continue;
            }
            int id = stoi(input[1]);
            // cout << "tarFd =" << tarFd << endl;
            string msg;
            msg.assign(allInput.str(), input[0].size()+input[1].size()+2, (allInput.str()).size()-1);
            // msg = "*** " + to_string(shmptr->name[myid]) + " told you ***: " + msg + "\n";
            char sendMsg[MAXMSGLEN];
            sprintf(sendMsg, "*** %s told you ***: %s\n", shmptr->name[myid], msg.c_str());
            if(shmptr->pid[id] < 0){
                // *** Error: user #<user id> does not exist yet. ***
                cerr << "*** Error: user #" << id << " does not exist yet. ***\n";
            }
            else{
                // *** <sender’s name> told you ***: <message>
                sendMsgto(id, sendMsg);
            }
            continue;
        }
        else if(input[0].compare("name") == 0){
            if(input.size() < 2){
                continue;
            }
            int rep = 0;
            string newName = input[1];
            for(int i = 1; i<MAXUSER; i++){
                if(strcmp(shmptr->name[i], input[1].c_str()) == 0){
                    // *** User ’<new name>’ already exists. ***
                    cout << "*** User '" << newName << "' already exists. ***\n";
                    // fprintf(stdout, "*** User '%s' already exists. ***\n", newName.c_str());
                    rep = 1;
                    break;
                }
            }
            if(rep == 0){
                strcpy(shmptr->name[myid], newName.c_str());
                char msg[MAXMSGLEN];
                sprintf(msg, "*** User from %s:%d is named '%s'. ***\n", shmptr->addr[myid], shmptr->port[myid], shmptr->name[myid]);
                broacast(msg);
            }
            continue;
        }

        int lastPipe[2];
        int newPipe[2];
        int numOfCmd = pipePos.size()-1;
        int numexec = 0;
        int numberedPipe0;
        int numberedPipe1;
        int isnewpipe = 0;

        for(int i = 0; i<numOfCmd; i++){
            if(numOfCmd > 1 || isNumberedPipe){ // number of cmd more than 1(need to pipe)
                if(i != 0){
                    lastPipe[0] = newPipe[0];
                    lastPipe[1] = newPipe[1];
                }
                if((i+1 < numOfCmd) || ((i == (numOfCmd - 1)) && isNumberedPipe)){  // not the last one or is the last one but need to pipe
                    if((i == (numOfCmd - 1)) && isNumberedPipe){
                        int j = 0;
                        for(j = 0; j < numberedPipe.size(); j++){
                            if(numberedPipe[j].line == newNumberedPipe.line){
                                numberedPipe0 = numberedPipe[j].pipe0;
                                numberedPipe1 = numberedPipe[j].pipe1;
                                break;
                            }
                        }
                        if(j == numberedPipe.size()){
                            if (pipe(newPipe) < 0)
                                perror("numberedPipe1 error");
                                // cerr << "can't create pipes\n";
                            newNumberedPipe.pipe0 = newPipe[0];
                            newNumberedPipe.pipe1 = newPipe[1];
                            numberedPipe0 = newNumberedPipe.pipe0;
                            numberedPipe1 = newNumberedPipe.pipe1;
                            isnewpipe = 1;
                            // numberedPipe.push_back(newNumberedPipe);
                        }
                    }
                    else{
                        if (pipe(newPipe) < 0)
                            perror("numberedPipe2 error");
                            // cerr << "can't create pipes\n";
                    }
                }
            }

            pid_t newChild;
            if( (newChild = fork()) == -1) {
                // perror("newChild fork error");
                char buff[1024];
	            int	n;
                if(numOfCmd > 1 || isNumberedPipe){ // number of cmd more than 1(need to pipe)
                    if((i+1 < numOfCmd) || ((i == (numOfCmd - 1)) && isNumberedPipe)){ // not the last one
                        if((i == (numOfCmd - 1)) && isNumberedPipe){ // is the last one but need to pipe
                            // dup2(numberedPipe1, STDOUT_FILENO);
                            // if(newNumberedPipe.err)
                            //     dup2(numberedPipe1, STDERR_FILENO);
                            close(numberedPipe0);
                            close(numberedPipe1);
                        }
                        else{
                            // dup2(newPipe[1], STDOUT_FILENO);
                            close(newPipe[0]);
                            close(newPipe[1]);
                        }
                    }
                    if(i != 0){ // not the first one
                        // dup2(lastPipe[0], STDIN_FILENO);
                        while ( (n = read(lastPipe[0], buff, 1024)) > 0){
                            if (write(STDOUT_FILENO, buff, n) != n)	perror("number pipe data write error");
                        }
                        if (n < 0) fprintf(stderr, "numbered pipe data read error\n");
                        close(lastPipe[0]);
                    }
                }
                // close(newPipe[0]);
                // close(newPipe[1]);
                // while ( (n = read(lastPipe[0], buff, 1024)) > 0){
                //     if (write(STDOUT_FILENO, buff, n) != n)	perror("number pipe data write error");
                // }
                // if (n < 0) fprintf(stderr, "numbered pipe data read error\n");
                // close(lastPipe[0]);

                if(userpipeWFd >= 0){
                    close(userpipeWFd);
                }
                if(userpipeRFd >= 0){
                    if(i == 0){
                        while ( (n = read(userpipeRFd, buff, 1024)) > 0){
                            if (write(STDOUT_FILENO, buff, n) != n)	perror("number pipe data write error");
                        }
                        if (n < 0) fprintf(stderr, "numbered pipe data read error\n");
                    }
                    close(userpipeRFd);
                }
                break;
            } 
            else if (newChild == 0) {
                /* child process */
                if(userpipeWFd >= 0){
                    if(i == (numOfCmd-1))
                        dup2(userpipeWFd, STDOUT_FILENO);
                    close(userpipeWFd);
                }
                if(userpipeRFd >= 0){
                    if(i == 0){
                        dup2(userpipeRFd, STDIN_FILENO);
                    }
                    close(userpipeRFd);
                }
                if(error == 1){
                    if(i == (numOfCmd-1)){
                        int null_fd = open("/dev/null", O_WRONLY);
                        dup2(null_fd, STDOUT_FILENO);
                        close(null_fd);
                    }
                }
                if(error == 2){
                    if(i == 0){
                        int null_fd = open("/dev/null", O_RDONLY);
                        dup2(null_fd, STDIN_FILENO);
                        close(null_fd);
                    }
                }

                for(int j = 0; j < numberedPipe.size(); j++){
                    if(numberedPipe[j].line == line){
                        if(i == 0)
                            dup2(numberedPipe[j].pipe0, STDIN_FILENO);
                        close(numberedPipe[j].pipe0);
                        close(numberedPipe[j].pipe1);
                        break;
                    }
                }

                if(numOfCmd > 1 || isNumberedPipe){ // number of cmd more than 1(need to pipe)
                    if((i+1 < numOfCmd) || ((i == (numOfCmd - 1)) && isNumberedPipe)){ // not the last one
                        if((i == (numOfCmd - 1)) && isNumberedPipe){ // is the last one but need to pipe
                            dup2(numberedPipe1, STDOUT_FILENO);
                            if(newNumberedPipe.err)
                                dup2(numberedPipe1, STDERR_FILENO);
                            close(numberedPipe0);
                            close(numberedPipe1);
                        }
                        else{
                            dup2(newPipe[1], STDOUT_FILENO);
                            close(newPipe[0]);
                            close(newPipe[1]);
                        }
                    }
                    if(i != 0){ // not the first one
                        dup2(lastPipe[0], STDIN_FILENO);
                        close(lastPipe[0]);
                    }
                }
            
                char* arg[pipePos[i+1]-pipePos[i]+1];
                for(int j = pipePos[i], k = 0; j<pipePos[i+1]; j++, k++) {
                    char *c = new char[input[j].length() + 1];
                    strcpy(c, input[j].c_str());
                    arg[k] = c;
                }
                arg[pipePos[i+1]-pipePos[i]] = NULL;
                
                if(execvp(arg[0], arg) < 0){
                    fprintf(stderr, "Unknown command: [%s].\n", arg[0]);
                    exit(0);
                }
                    
            } else {
                /* parent process */
                
                childs.push_back(newChild);
                if(numOfCmd > 1){ // number of cmd more than 1(need to pipe)
                    if(i+1 < numOfCmd){ // not the last one
                        close(newPipe[1]);
                    }
                    if(i != 0){ // not the first one
                        close(lastPipe[0]);
                    }
                }

                signal(SIGCHLD, sig_chld);
                
                if(i == numOfCmd-1){
                    for(int j = 0; j < numberedPipe.size(); j++){
                        if(numberedPipe[j].line == line){
                            close(numberedPipe[j].pipe0);
                            close(numberedPipe[j].pipe1);
                            numberedPipe.erase(numberedPipe.begin()+j, numberedPipe.begin()+j+1);
                        }
                    }
                    // if(isNumberedPipe == 0 && userpipeWFd < 0)
                    if(isNumberedPipe == 0)
                        while (numOfWait < numOfCmd);
                    if(isNumberedPipe){
                        isNumberedPipe = 0;
                    }
                    if(isnewpipe){
                        numberedPipe.push_back(newNumberedPipe);
                        isnewpipe = 0;
                    }
                    numOfWait = 0;
                    childs.clear();

                    if(userpipeWFd >= 0){
                        close(userpipeWFd);
                        userpipeWFd = -1;
                    }
                    if(userpipeRFd >= 0){
                        close(userpipeRFd);
                        userpipeRFd = -1;
                    }
                }                   
                
            }
        }
    }
    fflush(stdout);
    return(0);
}


int main(int argc, char **argv){
    socklen_t clilen;
    struct sockaddr_in	cli_addr, serv_addr;
    int	msock;	/* master server socket */
    int	ssock;	/* slave server socket */
    pid_t childpid;

    int port = atoi(argv[1]);

    signal(SIGCHLD, sig_chld);
    signal(SIGUSR1, readMsg);
    signal(SIGUSR2, writeEnd);
    signal(SIGINT, sig_int);

    mkdir("./user_pipe", 0777);

    // share memory
    if ( (shmid = shmget(SHMKEY, sizeof(struct shm), 0666 | IPC_CREAT)) < 0 ){
        // cerr << "can't get share memory id\n";
        exit(-1);
    }
    if ( (shmptr = (struct shm*)shmat(shmid, (char *)0, 0)) == (struct shm*)-1 ){
        // cerr << "can't attach share memory id\n";
        exit(-1);
    }
    for(int i = 0; i<MAXUSER; i++){
        memset(shmptr->addr[i], '\0', MAXADDRLEN);
        strcpy(shmptr->name[i], "(no name)");
        memset(shmptr->msg[i], '\0', MAXMSGLEN);

        shmptr->port[i] = -1;
        shmptr->sockfd[i] = -1;
        shmptr->pid[i] = -1;
        shmptr->waitsig[i] = 0;

        for(int j = 0; j<MAXUSER; j++){
            shmptr->userpipe[i][j] = -1;
        }
    }
    
    //  Open a TCP socket (an Internet stream socket).
    if ( (msock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror("server: can't open stream socket");
    // set flag SO REUSEADDR
    int opt = 1;
    if (setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
        perror("setsockopt");
    // Bind our local address so that the client can send to us.
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port        = htons(port);
    if (bind(msock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        perror("server: can't bind local address");
    listen(msock, 30); // maximum client
    
    setenv("PATH", "bin:.", 1);

    while(1) {
        clilen = sizeof(cli_addr);
        ssock = accept(msock, (struct sockaddr *) &cli_addr, &clilen);
        if (ssock < 0){
            if (errno == EINTR)
                    continue;
            perror("accept error");
        }
            
        if ( (childpid = fork()) < 0)
            perror("server: fork error");
        else if (childpid == 0) {   // child process
            // close original socket
            (void)close(msock); 	
            // process the request
            dup2(ssock, STDOUT_FILENO);
            dup2(ssock, STDERR_FILENO);
            dup2(ssock, STDIN_FILENO);

            char msg[MAXMSGLEN];
            char cli_addr_str[INET_ADDRSTRLEN];
            inet_ntop( AF_INET, &(cli_addr.sin_addr), cli_addr_str, INET_ADDRSTRLEN );
            for(int i = 1; i<MAXUSER; i++){
                if(shmptr->pid[i]<0){
                    shmptr->pid[i] = getpid();
                    strcpy(shmptr->addr[i], cli_addr_str);
                    shmptr->port[i] = ntohs(cli_addr.sin_port);
                    shmptr->sockfd[i] = ssock;
                    // *** User ’<user name>’ entered from <IP>:<port>. ***
                    sprintf(msg, "*** User '%s' entered from %s:%d. ***\n", shmptr->name[i], shmptr->addr[i], shmptr->port[i]);
                    break;
                }
            }
            string welcome = "****************************************\n** Welcome to the information server. **\n****************************************\n";
            cout << welcome;
            fflush(stdout);
            broacast(msg);
            npshell();
            // *** User ’<user name>’ left. ***
            sprintf(msg, "*** User '%s' left. ***\n", shmptr->name[getMyId()]);
            broacast(msg);
            delMyInfoInShm();
            exit(0);
        }
        (void)close(ssock);  // parent process
    }
}
