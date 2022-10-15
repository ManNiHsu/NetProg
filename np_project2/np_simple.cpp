#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
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

using namespace std;

#define	MAXLINE	15000

int numOfWait = 0;
vector<pid_t> childs;

struct pipeRec
{
    // vector<string> cmd;
    int line;
    int pipe0;
    int pipe1;
    int err;    
};

void sig_pipe(int signo){
    cerr << "pipe error\n";
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

int npshell(int sockfd){
    numOfWait = 0;
    childs.clear();
    int fileRed = 0;
    int savedFd;
    int numPipe = 0;
    vector<pipeRec> numberedPipe;

    int stdout_backup = dup(STDOUT_FILENO);
    int stdin_backup = dup(STDIN_FILENO);
    int stderr_backup = dup(STDERR_FILENO);

    dup2(sockfd, STDOUT_FILENO);
    dup2(sockfd, STDERR_FILENO);
    dup2(sockfd, STDIN_FILENO);
    setenv("PATH", "bin:.", 1);

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
        while(allInput >> cmd){
            if(cmd == "exit" || cmd == "EOF"){
                dup2(stdout_backup, STDOUT_FILENO);
                dup2(stdin_backup, STDIN_FILENO);
                dup2(stderr_backup, STDERR_FILENO);
                close(stdout_backup);
                close(stdin_backup);
                close(stderr_backup);
                return(0);
            }

            if(fileRed == 1){
                const char* path = cmd.c_str();
                int fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0666);//dup target file to STDOUT_FILENO(1)
                dup2(fd, STDOUT_FILENO);
                close(fd);
                break;
            }
            else if(cmd == ">"){
                fileRed = 1;
                savedFd = dup(STDOUT_FILENO); //save STDOUT_FILENO(1)
            }
            else if(cmd == "|"){
                pipePos.push_back(input.size());
            }
            else if(cmd[0] == '|' || cmd[0] == '!'){
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
            else
                input.push_back(cmd);
        }
        pipePos.push_back(input.size());
        
        if(input[0].compare("setenv") == 0){
            if(input.size() < 3)
                continue;
            if(setenv(input[1].c_str(), input[2].c_str(), 1) < 0) perror("setenv");
            continue;
        }
        if(input[0].compare("printenv") == 0){
            if(input.size() < 2)
                continue;
            if(getenv(input[1].c_str()))
                cout << getenv(input[1].c_str()) << endl;
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
                                cerr << "can't create pipes\n";
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
                            cerr << "can't create pipes\n";
                    }
                }
            }

            pid_t newChild;
            if( (newChild = fork()) == -1) {
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
                break;
            } 
            else if (newChild == 0) {
                /* child process */
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
                }                   
                
            }
        }
    }
    return(0);
}


int main(int argc, char **argv){
    int    sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in	cli_addr, serv_addr;

    int port = atoi(argv[1]);

    signal(SIGPIPE, sig_pipe);
    
    //  Open a TCP socket (an Internet stream socket).
     
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror("server: can't open stream socket");

    // set flag SO REUSEADDR
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt");
    }
    
    // Bind our local address so that the client can send to us.
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr *) &serv_addr,
        sizeof(serv_addr)) < 0)
        perror("server: can't bind local address");

    listen(sockfd, 1); // maximum client
    for ( ; ; ) {
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, 
            (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0)  
            perror("server: accept error");
        npshell(newsockfd);
        close(newsockfd);  // parent process
    }
}
